/* Gfxfix: proxy d3d8.dll for The Guild Gold.
 *
 * The game picks its Direct3D 8 adapter by comparing only the device GUID stored in
 * HKCU\Software\Ahead Entertainment\d8_vesa with every adapter and keeps the LAST match. Two monitors on one graphics
 * card share one GUID, so the game always ends up on the second monitor, whatever the start dialog was told.
 *
 * This proxy sits in the game folder (the game loads d3d8.dll from there first), loads the real d3d8.dll and shows the game
 * exactly ONE adapter: the one chosen in gfxfix.ini. The game then cannot pick a wrong one. Every call that carries an
 * adapter number is shifted to the real adapter. monitor=0 (or no ini) leaves everything untouched.
 *
 * gfxfix.ini (next to d3d8.dll):
 *     [gfxfix]
 *     monitor=2        ; 1 = first monitor (the Windows main display), 2 = second, ...; 0 = off
 * gfxfix.log lists the adapters on every start, which tells which number is which screen. */
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct IDirect3D8Real IDirect3D8Real;
typedef struct Wrapper Wrapper;

/* only what we need of d3d8.h: vtable slots are called by index */
typedef struct {
    char Driver[512];
    char Description[512];
    LARGE_INTEGER DriverVersion;
    DWORD VendorId, DeviceId, SubSysId, Revision;
    GUID DeviceIdentifier;
    DWORD WHQLLevel;
} AdapterIdentifier8;

struct IDirect3D8Real {
    const void *const *vtbl;
};

enum { ST_QUERY = 0, ST_ADDREF, ST_RELEASE, ST_REGISTERSW, ST_GETCOUNT, ST_GETIDENT, ST_MODECOUNT, ST_ENUMMODES, ST_DISPLAYMODE,
       ST_CHECKTYPE, ST_CHECKFORMAT, ST_CHECKMS, ST_CHECKDS, ST_GETCAPS, ST_GETMONITOR, ST_CREATEDEVICE };

struct Wrapper {
    const void *const *vtbl;
    IDirect3D8Real *real;
    UINT first; /* real adapter that is shown as adapter 0 */
    BOOL single; /* show exactly one adapter */
    BOOL pow2; /* report power-of-two texture caps */
};

static HMODULE g_real_module;
static char g_dir[MAX_PATH];

static void note(const char *format, ...)
{
    char path[MAX_PATH], line[512];
    FILE *file;
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    snprintf(path, sizeof(path), "%sgfxfix.log", g_dir);
    file = fopen(path, "a");
    if (file) {
        fprintf(file, "%s\n", line);
        fclose(file);
    }
    OutputDebugStringA(line);
}

#define CALL(real, slot, type, ...) ((type)((real)->vtbl[slot]))(real, __VA_ARGS__)

typedef HRESULT(WINAPI *Fn_Self)(void *);
typedef ULONG(WINAPI *Fn_Ulong)(void *);
typedef UINT(WINAPI *Fn_Uint)(void *);
typedef HRESULT(WINAPI *Fn_Query)(void *, const GUID *, void **);
typedef HRESULT(WINAPI *Fn_Ident)(void *, UINT, DWORD, AdapterIdentifier8 *);
typedef UINT(WINAPI *Fn_ModeCount)(void *, UINT);
typedef HRESULT(WINAPI *Fn_Enum)(void *, UINT, UINT, void *);
typedef HRESULT(WINAPI *Fn_Mode)(void *, UINT, void *);
typedef HRESULT(WINAPI *Fn_Type)(void *, UINT, int, int, int, BOOL);
typedef HRESULT(WINAPI *Fn_Format)(void *, UINT, int, int, DWORD, int, int);
typedef HRESULT(WINAPI *Fn_Ms)(void *, UINT, int, int, BOOL, int);
typedef HRESULT(WINAPI *Fn_Ds)(void *, UINT, int, int, int, int);
typedef HRESULT(WINAPI *Fn_Caps)(void *, UINT, int, void *);
typedef HMONITOR(WINAPI *Fn_Monitor)(void *, UINT);
typedef HRESULT(WINAPI *Fn_Create)(void *, UINT, int, HWND, DWORD, void *, void **);

static HRESULT WINAPI w_query(Wrapper *w, const GUID *iid, void **out) { return ((Fn_Query)w->real->vtbl[ST_QUERY])(w->real, iid, out); }
static ULONG WINAPI w_addref(Wrapper *w) { return ((Fn_Ulong)w->real->vtbl[ST_ADDREF])(w->real); }
static ULONG WINAPI w_release(Wrapper *w)
{
    ULONG left = ((Fn_Ulong)w->real->vtbl[ST_RELEASE])(w->real);
    if (left == 0) HeapFree(GetProcessHeap(), 0, w);
    return left;
}
static HRESULT WINAPI w_registersw(Wrapper *w, void *fn)
{
    typedef HRESULT(WINAPI * F)(void *, void *);
    return ((F)w->real->vtbl[ST_REGISTERSW])(w->real, fn);
}
static UINT WINAPI w_getcount(Wrapper *w)
{
    if (!w->single) return ((Fn_Uint)w->real->vtbl[ST_GETCOUNT])(w->real);
    return 1;
}
static HRESULT WINAPI w_getident(Wrapper *w, UINT a, DWORD flags, AdapterIdentifier8 *id)
{
    return ((Fn_Ident)w->real->vtbl[ST_GETIDENT])(w->real, a + w->first, flags, id);
}
static UINT WINAPI w_modecount(Wrapper *w, UINT a) { return ((Fn_ModeCount)w->real->vtbl[ST_MODECOUNT])(w->real, a + w->first); }
static HRESULT WINAPI w_enummodes(Wrapper *w, UINT a, UINT mode, void *out) { return ((Fn_Enum)w->real->vtbl[ST_ENUMMODES])(w->real, a + w->first, mode, out); }
static HRESULT WINAPI w_displaymode(Wrapper *w, UINT a, void *out) { return ((Fn_Mode)w->real->vtbl[ST_DISPLAYMODE])(w->real, a + w->first, out); }
static HRESULT WINAPI w_checktype(Wrapper *w, UINT a, int t, int d, int b, BOOL win) { return ((Fn_Type)w->real->vtbl[ST_CHECKTYPE])(w->real, a + w->first, t, d, b, win); }
static HRESULT WINAPI w_checkformat(Wrapper *w, UINT a, int t, int af, DWORD u, int rt, int cf) { return ((Fn_Format)w->real->vtbl[ST_CHECKFORMAT])(w->real, a + w->first, t, af, u, rt, cf); }
static HRESULT WINAPI w_checkms(Wrapper *w, UINT a, int t, int sf, BOOL win, int ms) { return ((Fn_Ms)w->real->vtbl[ST_CHECKMS])(w->real, a + w->first, t, sf, win, ms); }
static HRESULT WINAPI w_checkds(Wrapper *w, UINT a, int t, int af, int rf, int df) { return ((Fn_Ds)w->real->vtbl[ST_CHECKDS])(w->real, a + w->first, t, af, rf, df); }
static HRESULT WINAPI w_getcaps(Wrapper *w, UINT a, int t, void *caps)
{
    HRESULT hr = ((Fn_Caps)w->real->vtbl[ST_GETCAPS])(w->real, a + w->first, t, caps);
    /* D3DCAPS8.TextureCaps (offset 60): modern cards report arbitrary texture sizes, which the game cannot handle
       (cut-off interface graphics); claim POW2 and NONPOW2CONDITIONAL so that it pads textures. */
    if (hr == 0 && w->pow2) *(DWORD *)((BYTE *)caps + 60) |= 0x2 | 0x100;
    return hr;
}
static HMONITOR WINAPI w_getmonitor(Wrapper *w, UINT a) { return ((Fn_Monitor)w->real->vtbl[ST_GETMONITOR])(w->real, a + w->first); }
static HRESULT WINAPI w_createdevice(Wrapper *w, UINT a, int t, HWND focus, DWORD flags, void *params, void **device)
{
    note("CreateDevice: adapter %u (real %u)", a, a + w->first);
    return ((Fn_Create)w->real->vtbl[ST_CREATEDEVICE])(w->real, a + w->first, t, focus, flags, params, device);
}

static const void *const g_wrapper_vtbl[16] = {
    w_query, w_addref, w_release, w_registersw, w_getcount, w_getident, w_modecount, w_enummodes,
    w_displaymode, w_checktype, w_checkformat, w_checkms, w_checkds, w_getcaps, w_getmonitor, w_createdevice};

static int read_int(const char *key, int fallback)
{
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%sgfxfix.ini", g_dir);
    return (int)GetPrivateProfileIntA("gfxfix", key, fallback, path);
}

typedef void *(WINAPI *CreateFn)(UINT);

static void log_adapters(IDirect3D8Real *real, UINT count)
{
    UINT i;
    for (i = 0; i < count; i++) {
        AdapterIdentifier8 id;
        HMONITOR monitor = ((Fn_Monitor)real->vtbl[ST_GETMONITOR])(real, i);
        MONITORINFOEXA info;
        memset(&info, 0, sizeof(info));
        info.cbSize = sizeof(info);
        if (monitor && GetMonitorInfoA(monitor, (MONITORINFO *)&info) &&
            ((Fn_Ident)real->vtbl[ST_GETIDENT])(real, i, 0, &id) == 0)
            note("  monitor %u: %s on %s at %ld,%ld size %ldx%ld%s", i + 1, id.Description, info.szDevice, info.rcMonitor.left,
                 info.rcMonitor.top, info.rcMonitor.right - info.rcMonitor.left, info.rcMonitor.bottom - info.rcMonitor.top,
                 (info.dwFlags & MONITORINFOF_PRIMARY) ? " (Windows main display)" : "");
    }
}

__declspec(dllexport) void *WINAPI Direct3DCreate8(UINT sdk_version)
{
    CreateFn create;
    IDirect3D8Real *real;
    Wrapper *w;
    UINT count;
    int wanted, pow2;
    if (!g_real_module) return NULL;
    create = (CreateFn)GetProcAddress(g_real_module, "Direct3DCreate8");
    real = create ? (IDirect3D8Real *)create(sdk_version) : NULL;
    if (!real) return NULL;
    count = ((Fn_Uint)real->vtbl[ST_GETCOUNT])(real);
    wanted = read_int("monitor", 0);
    note("Direct3DCreate8: %u adapter(s), monitor=%d", count, wanted);
    log_adapters(real, count);
    pow2 = read_int("pow2caps", 1) != 0;
    if (wanted <= 0 && !pow2) return real; /* nothing to do */
    if ((UINT)wanted > count) {
        note("monitor=%d does not exist, using monitor 1", wanted);
        wanted = 1;
    }
    w = (Wrapper *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*w));
    if (!w) return real;
    w->vtbl = g_wrapper_vtbl;
    w->real = real;
    w->first = wanted > 0 ? (UINT)wanted - 1 : 0;
    w->single = wanted > 0 && count > 1;
    w->pow2 = pow2;
    return w;
}

/* the remaining exports of d3d8.dll, forwarded by name (the game does not use them) */
static FARPROC real_proc(const char *name) { return g_real_module ? GetProcAddress(g_real_module, name) : NULL; }
__declspec(dllexport) HRESULT WINAPI ValidatePixelShader(const void *a, const void *b, BOOL c, void *d)
{
    typedef HRESULT(WINAPI * F)(const void *, const void *, BOOL, void *);
    F f = (F)real_proc("ValidatePixelShader");
    return f ? f(a, b, c, d) : E_FAIL;
}
__declspec(dllexport) HRESULT WINAPI ValidateVertexShader(const void *a, const void *b, const void *c, BOOL d, void *e)
{
    typedef HRESULT(WINAPI * F)(const void *, const void *, const void *, BOOL, void *);
    F f = (F)real_proc("ValidateVertexShader");
    return f ? f(a, b, c, d, e) : E_FAIL;
}
__declspec(dllexport) void WINAPI DebugSetMute(void)
{
    typedef void(WINAPI * F)(void);
    F f = (F)real_proc("DebugSetMute");
    if (f) f();
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        char system[MAX_PATH], *slash;
        DisableThreadLibraryCalls(instance);
        GetModuleFileNameA(instance, g_dir, sizeof(g_dir));
        slash = strrchr(g_dir, '\\');
        if (slash) slash[1] = 0;
        GetSystemDirectoryA(system, sizeof(system));
        strncat(system, "\\d3d8.dll", sizeof(system) - strlen(system) - 1);
        g_real_module = LoadLibraryA(system);
        if (!g_real_module) note("cannot load %s (error %lu)", system, GetLastError());
    }
    return TRUE;
}
