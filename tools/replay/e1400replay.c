/* Offline replay of a server session recording (.e1rec) against the patch loader.
 *
 * Origin: europa1400-decompilation src/tools/replay.c (own code of the project, no game code).
 *
 * Every Winsock/time import of the server module is redirected to a fake that hands out the next
 * recorded result of the server thread, strictly in order. No real network traffic happens.
 * Every send/sendto is compared byte for byte with the recording; the first divergence (wrong
 * API order, different socket or different bytes) fails the replay. Sleep() is skipped. */

#include <winsock2.h>
#include <windows.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "e1400core.h"

enum { REC_SOCKET = 1, REC_BIND, REC_LISTEN, REC_ACCEPT, REC_RECV, REC_SEND, REC_SENDTO, REC_CLOSESOCKET,
       REC_SHUTDOWN, REC_IOCTLSOCKET, REC_SETSOCKOPT, REC_WSAGETLASTERROR, REC_WSASTARTUP, REC_WSACLEANUP,
       REC_TIMEGETTIME, REC_GETTICKCOUNT, REC_INIT_PARAMS, REC_EXIT };
static const char *const API[] = {"?", "socket", "bind", "listen", "accept", "recv", "send", "sendto",
                                  "closesocket", "shutdown", "ioctlsocket", "setsockopt", "WSAGetLastError",
                                  "WSAStartup", "WSACleanup", "timeGetTime", "GetTickCount", "INIT", "EXIT"};

#pragma pack(push, 1)
typedef struct RecHeader {
    uint32_t seq;
    uint16_t api, thread_tag;
    uint32_t wall_ms;
    int32_t ret;
    uint32_t arg0, arg1, len;
} RecHeader;
#pragma pack(pop)

typedef struct Ev {
    const RecHeader *h;
    const uint8_t *data;
} Ev;

static Ev *g_ev;              /* server-thread events */
static size_t g_count, g_pos;
static const uint8_t *g_init_params;
static volatile LONG g_done;  /* 1 = stream consumed or diverged */
static int g_failed;
static size_t g_sends_ok;
static DWORD g_last_time;
static char g_fail_msg[512];

static const char *api_name(unsigned a) { return a < sizeof(API) / sizeof(API[0]) ? API[a] : "?"; }

static void fail(const char *fmt, ...)
{
    va_list ap;
    if (g_done)
        return;
    va_start(ap, fmt);
    vsnprintf(g_fail_msg, sizeof(g_fail_msg), fmt, ap);
    va_end(ap);
    g_failed = 1;
    InterlockedExchange(&g_done, 1);
}

/* Next recorded event; NULL once the stream is exhausted or diverged (fakes then "would block"). */
static const Ev *next(unsigned api)
{
    const Ev *e;
    if (g_done)
        return NULL;
    if (g_pos >= g_count) {
        InterlockedExchange(&g_done, 1);
        return NULL;
    }
    e = &g_ev[g_pos];
    if (e->h->api != api) {
        fail("event %u (seq %u, %u ms): server called %s, recording has %s", (unsigned)g_pos, e->h->seq,
             e->h->wall_ms, api_name(api), api_name(e->h->api));
        return NULL;
    }
    g_pos++;
    if ((g_pos & 0xffff) == 0)
        printf("  ... %u / %u events\n", (unsigned)g_pos, (unsigned)g_count);
    return e;
}

static int would_block(void)
{
    WSASetLastError(WSAEWOULDBLOCK);
    return SOCKET_ERROR;
}

/* ---- fakes -------------------------------------------------------------------------------- */

static SOCKET WSAAPI f_socket(int af, int type, int proto)
{
    const Ev *e = next(REC_SOCKET);
    (void)af, (void)type, (void)proto;
    return e ? (SOCKET)(uint32_t)e->h->ret : (SOCKET)0x7ff0;
}

static int WSAAPI f_bind(SOCKET s, const struct sockaddr *a, int alen)
{
    const Ev *e = next(REC_BIND);
    (void)s, (void)a, (void)alen;
    return e ? e->h->ret : 0;
}

static int WSAAPI f_listen(SOCKET s, int backlog)
{
    const Ev *e = next(REC_LISTEN);
    (void)s, (void)backlog;
    return e ? e->h->ret : 0;
}

static SOCKET WSAAPI f_accept(SOCKET s, struct sockaddr *a, int *alen)
{
    const Ev *e = next(REC_ACCEPT);
    (void)s;
    if (!e)
        return (SOCKET)would_block();
    if (a && alen && e->h->len) {
        memcpy(a, e->data, e->h->len);
        *alen = (int)e->h->arg1;
    }
    return (SOCKET)(uint32_t)e->h->ret;
}

static int WSAAPI f_recv(SOCKET s, char *buf, int len, int flags)
{
    const Ev *e = next(REC_RECV);
    (void)flags;
    if (!e)
        return would_block();
    if ((uint32_t)s != e->h->arg0 || len != (int)e->h->arg1)
        fail("recv seq %u: socket %#x/%#x len %d/%u differ", e->h->seq, (unsigned)s, e->h->arg0, len, e->h->arg1);
    if (e->h->len)
        memcpy(buf, e->data, e->h->len);
    return e->h->ret;
}

/* Noise mask: byte offsets per message kind where the ORIGINAL differs from its own recording
 * (uninitialised stack bytes it sends, see docs/testing.md). Kind = first byte
 * (command id) for send, NOISE_SENDTO for broadcasts. Learned on a baseline run, then allowed. */
#define NOISE_KINDS 257
#define NOISE_SENDTO 256
#define NOISE_MAX 4096
static uint8_t g_noise_seen[NOISE_KINDS][NOISE_MAX / 8];  /* differences observed in this run */
static uint8_t g_noise_ok[NOISE_KINDS][NOISE_MAX / 8];    /* allowed (loaded mask) */
static int g_have_mask;
static size_t g_sends_noisy;

#define BIT_GET(a, i) ((a)[(i) >> 3] & (1u << ((i)&7)))
#define BIT_SET(a, i) ((a)[(i) >> 3] |= (uint8_t)(1u << ((i)&7)))

static int compare_out(const Ev *e, SOCKET s, const char *buf, int len, const char *what)
{
    unsigned kind;
    int noisy = 0;

    if (!e)
        return len; /* after the end: pretend success, nothing to compare */
    if ((uint32_t)s != e->h->arg0) {
        fail("%s seq %u (%u ms): socket %#x, recorded %#x", what, e->h->seq, e->h->wall_ms, (unsigned)s, e->h->arg0);
        return e->h->ret;
    }
    if ((uint32_t)len != e->h->len) {
        fail("%s seq %u (%u ms): %d bytes vs recorded %u", what, e->h->seq, e->h->wall_ms, len, e->h->len);
        return e->h->ret;
    }
    kind = what[4] == 't' ? NOISE_SENDTO : (len ? (uint8_t)buf[0] : 0); /* "sendto" vs "send" */
    for (uint32_t i = 0; i < (uint32_t)len; i++) {
        if ((uint8_t)buf[i] == e->data[i])
            continue;
        if (i >= NOISE_MAX || (g_have_mask && !BIT_GET(g_noise_ok[kind], i))) {
            fail("%s seq %u (%u ms), kind %#x: byte %u differs (%02x vs recorded %02x) outside the noise mask", what,
                 e->h->seq, e->h->wall_ms, kind, i, (uint8_t)buf[i], e->data[i]);
            return e->h->ret;
        }
        BIT_SET(g_noise_seen[kind], i);
        noisy = 1;
    }
    if (noisy)
        g_sends_noisy++;
    else
        g_sends_ok++;
    return e->h->ret;
}

/* Mask file: one line per kind, "kind: off off off ..." (decimal). */
static int mask_load(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[16384];
    if (!f)
        return -1;
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        unsigned kind = (unsigned)strtoul(p, &p, 10);
        if (*p != ':' || kind >= NOISE_KINDS)
            continue;
        p++;
        for (char *tok = strtok(p, " \r\n"); tok; tok = strtok(NULL, " \r\n")) {
            unsigned off = (unsigned)atoi(tok);
            if (off < NOISE_MAX)
                BIT_SET(g_noise_ok[kind], off);
        }
    }
    fclose(f);
    g_have_mask = 1;
    return 0;
}

static void mask_write(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    fprintf(f, "# noise mask: byte offsets per message kind (first byte; 256 = sendto) where the original\n"
               "# server.dll differs from its own recording. Generated by e1400replay --learn.\n");
    for (unsigned k = 0; k < NOISE_KINDS; k++) {
        int any = 0;
        for (unsigned i = 0; i < NOISE_MAX; i++) {
            if (!BIT_GET(g_noise_seen[k], i))
                continue;
            if (!any)
                fprintf(f, "%u:", k);
            fprintf(f, " %u", i);
            any = 1;
        }
        if (any)
            fprintf(f, "\n");
    }
    fclose(f);
}

static int WSAAPI f_send(SOCKET s, const char *buf, int len, int flags)
{
    (void)flags;
    return compare_out(next(REC_SEND), s, buf, len, "send");
}

static int WSAAPI f_sendto(SOCKET s, const char *buf, int len, int flags, const struct sockaddr *to, int tolen)
{
    (void)flags, (void)to, (void)tolen;
    return compare_out(next(REC_SENDTO), s, buf, len, "sendto");
}

static int WSAAPI f_closesocket(SOCKET s)
{
    const Ev *e = next(REC_CLOSESOCKET);
    (void)s;
    return e ? e->h->ret : 0;
}

static int WSAAPI f_shutdown(SOCKET s, int how)
{
    const Ev *e = next(REC_SHUTDOWN);
    (void)s, (void)how;
    return e ? e->h->ret : 0;
}

static int WSAAPI f_ioctlsocket(SOCKET s, long cmd, u_long *argp)
{
    const Ev *e = next(REC_IOCTLSOCKET);
    (void)s, (void)cmd;
    if (e && argp && e->h->len == sizeof(*argp))
        memcpy(argp, e->data, sizeof(*argp));
    return e ? e->h->ret : 0;
}

static int WSAAPI f_setsockopt(SOCKET s, int level, int opt, const char *val, int vlen)
{
    const Ev *e = next(REC_SETSOCKOPT);
    (void)s, (void)level, (void)opt, (void)val, (void)vlen;
    return e ? e->h->ret : 0;
}

static int WSAAPI f_WSAGetLastError(void)
{
    const Ev *e = next(REC_WSAGETLASTERROR);
    return e ? e->h->ret : WSAEWOULDBLOCK;
}

static int WSAAPI f_WSAStartup(WORD ver, LPWSADATA data)
{
    const Ev *e = next(REC_WSASTARTUP);
    (void)ver;
    if (e && data && e->h->len == sizeof(*data))
        memcpy(data, e->data, sizeof(*data));
    return e ? e->h->ret : 0;
}

static int WSAAPI f_WSACleanup(void)
{
    const Ev *e = next(REC_WSACLEANUP);
    return e ? e->h->ret : 0;
}

static DWORD fake_time(unsigned api)
{
    const Ev *e = next(api);
    g_last_time = e ? (DWORD)e->h->ret : g_last_time + 15;
    return g_last_time;
}

static DWORD WINAPI f_timeGetTime(void) { return fake_time(REC_TIMEGETTIME); }
static DWORD WINAPI f_GetTickCount(void) { return fake_time(REC_GETTICKCOUNT); }
static void WINAPI f_Sleep(DWORD ms)
{
    (void)ms; /* time comes from the recording; just yield */
    SwitchToThread();
}

/* ---- setup -------------------------------------------------------------------------------- */

static int load(const char *path)
{
    FILE *f = fopen(path, "rb");
    long size;
    uint8_t *buf;
    size_t off = 6;
    uint16_t server_tag = 0;

    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc(size);
    fread(buf, 1, size, f);
    fclose(f);
    if (size < 6 || memcmp(buf, "E1REC\x01", 6) != 0)
        return -1;
    g_ev = malloc(sizeof(Ev) * (size / sizeof(RecHeader) + 1));
    while (off + sizeof(RecHeader) <= (size_t)size) {
        const RecHeader *h = (const RecHeader *)(buf + off);
        const uint8_t *d = buf + off + sizeof(RecHeader);
        off += sizeof(RecHeader) + h->len;
        if (h->api == REC_INIT_PARAMS) {
            g_init_params = d;
            continue;
        }
        if (h->api == REC_EXIT)
            continue;
        if (!server_tag)
            server_tag = h->thread_tag; /* first event after Init comes from the server thread */
        if (h->thread_tag == server_tag)
            g_ev[g_count++] = (Ev){h, d};
    }
    return g_init_params ? 0 : -1;
}

/* Crash diagnostics: where in the recording were we, and what faulted? */
static LONG WINAPI on_crash(EXCEPTION_POINTERS *x)
{
    const EXCEPTION_RECORD *r = x->ExceptionRecord;
    const CONTEXT *c = x->ContextRecord;
    if (r->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;
    printf("CRASH at %p (%s %p) after event %u (seq %u, %u ms)\n", r->ExceptionAddress,
           r->ExceptionInformation[0] ? "write" : "read", (void *)r->ExceptionInformation[1], (unsigned)g_pos,
           g_pos ? g_ev[g_pos - 1].h->seq : 0, g_pos ? g_ev[g_pos - 1].h->wall_ms : 0);
    if (g_failed)
        printf("  (follow-up crash) DIVERGENCE: %s\n", g_fail_msg);
    printf("  eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx ebp=%08lx esp=%08lx\n", c->Eax, c->Ebx,
           c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    for (size_t i = g_pos > 6 ? g_pos - 6 : 0; i < g_pos; i++)
        printf("  ev %u: %s ret=%d a0=%#x a1=%#x len=%u\n", (unsigned)i, api_name(g_ev[i].h->api), g_ev[i].h->ret,
               g_ev[i].h->arg0, g_ev[i].h->arg1, g_ev[i].h->len);
    fflush(stdout);
    return EXCEPTION_CONTINUE_SEARCH;
}

typedef struct IatFake {
    const char *dll, *function;
    void *impl;
} IatFake;

static const IatFake FAKES[] = {
    {"WS2_32.dll", "#1", (void *)f_accept},          {"WS2_32.dll", "#2", (void *)f_bind},
    {"WS2_32.dll", "#3", (void *)f_closesocket},     {"WS2_32.dll", "#10", (void *)f_ioctlsocket},
    {"WS2_32.dll", "#13", (void *)f_listen},         {"WS2_32.dll", "#16", (void *)f_recv},
    {"WS2_32.dll", "#19", (void *)f_send},           {"WS2_32.dll", "#20", (void *)f_sendto},
    {"WS2_32.dll", "#21", (void *)f_setsockopt},     {"WS2_32.dll", "#22", (void *)f_shutdown},
    {"WS2_32.dll", "#23", (void *)f_socket},         {"WS2_32.dll", "#111", (void *)f_WSAGetLastError},
    {"WS2_32.dll", "#115", (void *)f_WSAStartup},    {"WS2_32.dll", "#116", (void *)f_WSACleanup},
    {"WINMM.dll", "timeGetTime", (void *)f_timeGetTime}, {"KERNEL32.dll", "GetTickCount", (void *)f_GetTickCount},
    {"KERNEL32.dll", "Sleep", (void *)f_Sleep},
};

static void usage(void)
{
    puts("usage: e1400replay <recording.e1rec> <server.dll to test> --original <original server.dll> [--mask <file>] [--learn <file>]\n"
         "  The original is loaded first and its Winsock/time imports are replaced by the recording; the tested DLL (the\n"
         "  loader's server.dll shim, or the original itself) is then started with the recorded Init parameters. Every send\n"
         "  must match the recording byte for byte (outside the noise mask).");
}

int main(int argc, char **argv)
{
    const char *recording = NULL, *tested = NULL, *original = NULL, *mask = NULL, *learn = NULL;
    HMODULE server, dll;
    int(__cdecl * init)(void *);
    void(__cdecl * exit_fn)(void);
    DWORD t0;
    int stalled = 0;
    char variable[E1400_PATH + 32];

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--original") && i + 1 < argc) original = argv[++i];
        else if (!strcmp(argv[i], "--mask") && i + 1 < argc) mask = argv[++i];
        else if (!strcmp(argv[i], "--learn") && i + 1 < argc) learn = argv[++i];
        else if (!recording) recording = argv[i];
        else if (!tested) tested = argv[i];
        else {
            usage();
            return 2;
        }
    }
    if (!recording || !tested || !original) {
        usage();
        return 2;
    }
    if (load(recording)) {
        fprintf(stderr, "cannot read recording %s\n", recording);
        return 2;
    }
    printf("replay %s: %u server-thread events\n", recording, (unsigned)g_count);
    if (mask && mask_load(mask)) {
        fprintf(stderr, "cannot read noise mask %s\n", mask);
        return 2;
    }
    /* the shim loads exactly this file as its original (same path = same module) */
    snprintf(variable, sizeof(variable), "E1400PATCH_ORIGINAL_SERVER=%s", original);
    _putenv(variable);
    server = LoadLibraryExA(original, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!server) {
        fprintf(stderr, "LoadLibrary(%s) failed: %lu\n", original, GetLastError());
        return 2;
    }
    for (size_t i = 0; i < sizeof(FAKES) / sizeof(FAKES[0]); i++) {
        void **slot = e1400_iat_slot(server, FAKES[i].dll, FAKES[i].function);
        if (!slot || e1400_iat_write(slot, FAKES[i].impl)) printf("warning: import %s!%s not found\n", FAKES[i].dll, FAKES[i].function);
    }
    dll = LoadLibraryExA(tested, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!dll) {
        fprintf(stderr, "LoadLibrary(%s) failed: %lu\n", tested, GetLastError());
        return 2;
    }
    init = (int(__cdecl *)(void *))GetProcAddress(dll, "Init");
    exit_fn = (void(__cdecl *)(void))GetProcAddress(dll, "Exit");
    if (!init || !exit_fn) {
        fprintf(stderr, "%s lacks Init/Exit\n", tested);
        return 2;
    }
    AddVectoredExceptionHandler(1, on_crash);
    t0 = GetTickCount();
    printf("Init -> %d\n", init((void *)g_init_params));
    {
        /* watchdog: the server thread must keep consuming events */
        size_t last = (size_t)-1;
        DWORD since = GetTickCount();
        while (!g_done && GetTickCount() - t0 < 30 * 60 * 1000) {
            Sleep(20);
            if (g_pos != last) {
                last = g_pos;
                since = GetTickCount();
            } else if (GetTickCount() - since > 5000) {
                stalled = 1;
                if (g_pos < g_count) {
                    const RecHeader *h = g_ev[g_pos].h;
                    fail("stalled: server stopped calling APIs at event %u, recording expects %s (seq %u, %u ms)", (unsigned)g_pos,
                         api_name(h->api), h->seq, h->wall_ms);
                } else {
                    InterlockedExchange(&g_done, 1);
                }
            }
        }
    }
    if (!stalled) exit_fn();
    printf("consumed %u / %u events, %u sends identical, %u differ only in noise bytes, %.1f s\n", (unsigned)g_pos,
           (unsigned)g_count, (unsigned)g_sends_ok, (unsigned)g_sends_noisy, (GetTickCount() - t0) / 1000.0);
    if (learn) {
        mask_write(learn);
        printf("noise mask written to %s\n", learn);
    }
    if (g_failed) {
        printf("DIVERGENCE: %s\nFAIL\n", g_fail_msg);
        return 1;
    }
    printf(g_pos == g_count ? "PASS\n" : "INCOMPLETE (timeout)\n");
    return g_pos == g_count ? 0 : 1;
}
