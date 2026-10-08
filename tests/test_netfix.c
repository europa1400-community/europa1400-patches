/* netfix test over real loopback TCP connections, without game files.
 *
 *   test_netfix <e1400patch.dll> <fixture_reader.dll> <netfix.dll> <work dir>
 *
 * The fixture reader stands in for server.dll (same recv call pattern). Without the patch a message sent in pieces is
 * read in pieces (the situation that breaks the original server). With netfix loaded through the loader:
 * - thousands of messages, cut into random pieces, arrive complete, in order, and every read is all or nothing;
 * - accepted connections get TCP_NODELAY;
 * - invalid lengths (too long, too short) abort the connection;
 * - a peer closing in the middle of a message is reported as closed, complete messages before that are delivered;
 * - detach logs the session statistics. */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int(__cdecl *ReaderStepFn)(SOCKET, unsigned char *, int *);
typedef SOCKET(__cdecl *ReaderAcceptFn)(SOCKET);
typedef int(__cdecl *AttachFn)(int target, HMODULE module, const char *path);
typedef void(__cdecl *DetachFn)(int target);

#define MESSAGES 3000
#define MAX_LENGTH 306

static int g_failures, g_checks;
static ReaderStepFn g_step;
static ReaderAcceptFn g_accept;

#define CHECK(condition)                                                                                                    \
    do {                                                                                                                    \
        g_checks++;                                                                                                         \
        if (!(condition)) {                                                                                                 \
            g_failures++;                                                                                                   \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                                     \
        }                                                                                                                   \
    } while (0)

static unsigned g_random = 12345;
static unsigned next_random(void)
{
    g_random = g_random * 1103515245u + 12345u;
    return (g_random >> 8) & 0xffffff;
}

static void write_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    if (file) {
        fputs(text, file);
        fclose(file);
    }
}

static int log_contains(const char *path, const char *text)
{
    FILE *file = fopen(path, "r");
    char line[1024];
    int found = 0;
    if (!file) return 0;
    while (!found && fgets(line, sizeof(line), file)) found = strstr(line, text) != NULL;
    fclose(file);
    return found;
}

/* A connected pair: *client (blocking, TCP_NODELAY so every send leaves at once) and the accepted, non-blocking end. */
static SOCKET connect_pair(SOCKET listener, SOCKET *client)
{
    struct sockaddr_in address;
    int length = sizeof(address);
    u_long on = 1;
    BOOL nodelay = TRUE;
    SOCKET accepted;
    getsockname(listener, (struct sockaddr *)&address, &length);
    *client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    setsockopt(*client, IPPROTO_TCP, TCP_NODELAY, (const char *)&nodelay, sizeof(nodelay));
    if (connect(*client, (struct sockaddr *)&address, sizeof(address))) return INVALID_SOCKET;
    accepted = g_accept(listener);
    if (accepted != INVALID_SOCKET) ioctlsocket(accepted, FIONBIO, &on);
    return accepted;
}

static int make_message(unsigned char *out, int length, int index)
{
    out[0] = (unsigned char)(0x10 + index % 0x50);
    out[1] = (unsigned char)(length & 0xff);
    out[2] = (unsigned char)(length >> 8);
    for (int i = 3; i < length; i++) out[i] = (unsigned char)(index * 31 + i * 7);
    return length;
}

/* Calls the reader until it reports something other than "nothing to read" or the timeout passes. */
static int step_until(SOCKET socket, unsigned char *message, int *position, DWORD timeout)
{
    DWORD start = GetTickCount();
    for (;;) {
        int status = g_step(socket, message, position);
        if (status != 0 || GetTickCount() - start > timeout) return status;
        Sleep(1);
    }
}

static void control_test(SOCKET listener)
{
    static unsigned char message[0x10000], sent[64];
    SOCKET client, server = connect_pair(listener, &client);
    int position = 0, status;
    CHECK(server != INVALID_SOCKET);
    make_message(sent, 40, 1);
    send(client, (const char *)sent, 10, 0);
    Sleep(50);
    status = g_step(server, message, &position);
    CHECK(status == 0 && position == 10); /* without the patch: half a message read */
    send(client, (const char *)sent + 10, 30, 0);
    status = step_until(server, message, &position, 2000);
    CHECK(status == 1 && position == 0 && !memcmp(message, sent, 40));
    closesocket(client);
    closesocket(server);
}

typedef struct Sender {
    SOCKET socket;
    unsigned char *stream;
    int size;
} Sender;

static DWORD WINAPI sender_thread(void *parameter)
{
    Sender *sender = parameter;
    int offset = 0;
    unsigned random = 777;
    while (offset < sender->size) {
        int chunk;
        random = random * 1103515245u + 12345u;
        chunk = 1 + (int)((random >> 8) % 64);
        if (chunk > sender->size - offset) chunk = sender->size - offset;
        if (send(sender->socket, (const char *)sender->stream + offset, chunk, 0) != chunk) return 1;
        offset += chunk;
        if ((random >> 20) % 64 == 0) Sleep(1); /* let pieces arrive separately now and then */
    }
    return 0;
}

static void stream_test(SOCKET listener)
{
    static unsigned char stream[MESSAGES * MAX_LENGTH], message[0x10000], expected[MAX_LENGTH];
    SOCKET client, server = connect_pair(listener, &client);
    Sender sender;
    HANDLE thread;
    BOOL nodelay = FALSE;
    int option_length = sizeof(nodelay), size = 0, received = 0, position = 0, all_or_nothing = 1, in_order = 1;
    DWORD start;

    CHECK(server != INVALID_SOCKET);
    CHECK(!getsockopt(server, IPPROTO_TCP, TCP_NODELAY, (char *)&nodelay, &option_length) && nodelay);
    for (int i = 0; i < MESSAGES; i++) size += make_message(stream + size, 4 + (int)(next_random() % (MAX_LENGTH - 3)), i);
    sender.socket = client;
    sender.stream = stream;
    sender.size = size;
    thread = CreateThread(NULL, 0, sender_thread, &sender, 0, NULL);

    g_random = 12345; /* replay the lengths to rebuild the expected messages */
    start = GetTickCount();
    while (received < MESSAGES && GetTickCount() - start < 60000) {
        int status = g_step(server, message, &position);
        if (position != 0) all_or_nothing = 0;
        if (status == 1) {
            int length = make_message(expected, 4 + (int)(next_random() % (MAX_LENGTH - 3)), received);
            if (memcmp(message, expected, (size_t)length)) in_order = 0;
            received++;
        } else if (status < 0) {
            printf("reader status %d after %d messages\n", status, received);
            break;
        }
    }
    WaitForSingleObject(thread, 10000);
    CloseHandle(thread);
    printf("stream: %d of %d messages (%d bytes) in %lu ms\n", received, MESSAGES, size, GetTickCount() - start);
    CHECK(received == MESSAGES);
    CHECK(all_or_nothing);
    CHECK(in_order);
    closesocket(client);
    closesocket(server);
}

static void invalid_length_test(SOCKET listener, int length)
{
    static unsigned char message[0x10000];
    unsigned char header[8] = {0x22, (unsigned char)(length & 0xff), (unsigned char)(length >> 8), 1, 2, 3, 4, 5};
    SOCKET client, server = connect_pair(listener, &client);
    int position = 0;
    send(client, (const char *)header, sizeof(header), 0);
    CHECK(step_until(server, message, &position, 2000) == -WSAECONNABORTED);
    CHECK(position == 0);
    closesocket(client);
    closesocket(server);
}

static void close_test(SOCKET listener)
{
    static unsigned char message[0x10000], sent[100];
    SOCKET client, server = connect_pair(listener, &client);
    int position = 0;
    make_message(sent, 50, 5);
    make_message(sent + 50, 40, 6);
    send(client, (const char *)sent, 70, 0); /* one message and half of the next */
    closesocket(client);
    CHECK(step_until(server, message, &position, 2000) == 1 && !memcmp(message, sent, 50));
    CHECK(step_until(server, message, &position, 2000) == -1); /* closed, the half message is not handed out */
    CHECK(position == 0);
    closesocket(server);
}

int main(int argc, char **argv)
{
    char root[MAX_PATH], patches[MAX_PATH], dir[MAX_PATH], file[MAX_PATH], variable[MAX_PATH + 32], log[MAX_PATH];
    HMODULE loader, reader;
    AttachFn attach;
    DetachFn detach;
    WSADATA data;
    SOCKET listener;
    struct sockaddr_in address = {0};

    if (argc != 5) {
        puts("usage: test_netfix <e1400patch.dll> <fixture_reader.dll> <netfix.dll> <work dir>");
        return 2;
    }
    WSAStartup(MAKEWORD(2, 2), &data);
    reader = LoadLibraryA(argv[2]);
    CHECK(reader != NULL);
    if (!reader) return 1;
    g_step = (ReaderStepFn)GetProcAddress(reader, "reader_step");
    g_accept = (ReaderAcceptFn)GetProcAddress(reader, "reader_accept");

    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(!bind(listener, (struct sockaddr *)&address, sizeof(address)) && !listen(listener, 5));

    control_test(listener);

    /* game directory with the netfix module; "*" because the fixture is no known server build */
    CreateDirectoryA(argv[4], NULL);
    snprintf(root, sizeof(root), "%s\\netfix-game", argv[4]);
    snprintf(patches, sizeof(patches), "%s\\patches", root);
    snprintf(dir, sizeof(dir), "%s\\netfix", patches);
    snprintf(log, sizeof(log), "%s\\e1400patch.log", root);
    CreateDirectoryA(root, NULL);
    CreateDirectoryA(patches, NULL);
    CreateDirectoryA(dir, NULL);
    DeleteFileA(log);
    snprintf(file, sizeof(file), "%s\\patch.ini", dir);
    write_file(file, "[patch]\nid=netfix\napi=1\nmodule=netfix.dll\ntargets=server\n[builds]\nserver=*\n"
                     "[settings]\ntcp_nodelay=1\nmax_message_length=306\n");
    snprintf(file, sizeof(file), "%s\\netfix.dll", dir);
    CopyFileA(argv[3], file, FALSE);
#define SETENV(name, value)                                                                                                 \
    snprintf(variable, sizeof(variable), "%s=%s", name, value);                                                             \
    _putenv(variable)
    snprintf(file, sizeof(file), "%s\\e1400patch.ini", root);
    SETENV("E1400PATCH_CONFIG", file);
    SETENV("E1400PATCH_PATCHES_DIR", patches);
    snprintf(file, sizeof(file), "%s\\mods", root);
    SETENV("E1400PATCH_MODS_DIR", file);
    snprintf(file, sizeof(file), "%s\\builds", root);
    SETENV("E1400PATCH_BUILDS_DIR", file);
    SETENV("E1400PATCH_LOG", log);
    SETENV("E1400PATCH_GAME_DIR", root);

    loader = LoadLibraryA(argv[1]);
    CHECK(loader != NULL);
    if (!loader) return 1;
    attach = (AttachFn)GetProcAddress(loader, "e1400_attach");
    detach = (DetachFn)GetProcAddress(loader, "e1400_detach");
    CHECK(attach && detach && attach(0, reader, argv[2]) == 0);
    CHECK(log_contains(log, "netfix: applied to server"));

    stream_test(listener);
    invalid_length_test(listener, 0x200); /* longer than the server's buffer */
    invalid_length_test(listener, 3);     /* no body: the original cannot complete it */
    close_test(listener);

    detach(0);
    CHECK(log_contains(log, "netfix: session:"));
    CHECK(log_contains(log, "invalid lengths"));
    closesocket(listener);
    WSACleanup();
    printf("%d checks, %d failures: %s\n", g_checks, g_failures, g_failures ? "FAIL" : "PASS");
    return g_failures ? 1 : 0;
}
