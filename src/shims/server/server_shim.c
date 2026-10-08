/* server.dll shim: the game's [Network] Server= entry points here (e1400patch\server.dll). The shim loads the untouched
 * original server.dll and the loader (e1400patch.dll), lets the loader apply the enabled patches to the original, and then
 * forwards Init/Exit. Only the hosting game loads server.dll; clients never need anything.
 *
 * Fail-safe: if the loader is missing or fails, the original runs unpatched. Everything heavy happens in the first Init call
 * (outside the loader lock). FreeLibrary of the shim (the game does that instead of calling Exit) also unloads the original
 * after the loader removed its hooks, so hosting again starts with a fresh original, as without the shim. */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "e1400core.h"

typedef int(__cdecl *InitFn)(void *params);
typedef void(__cdecl *ExitFn)(void);

static HMODULE g_self, g_core, g_original;
static InitFn g_init;
static ExitFn g_exit;
static E1400DetachFn g_detach;
static int g_attached;

/* Minimal log for the case that the loader itself is unavailable. */
static void shim_note(const char *text)
{
    char path[E1400_PATH];
    FILE *file;
    e1400_module_dir(g_self, path, sizeof(path));
    strncat(path, "\\logs\\server_shim.log", sizeof(path) - strlen(path) - 1);
    file = fopen(path, "a");
    if (file) {
        fprintf(file, "%s\n", text);
        fclose(file);
    }
    OutputDebugStringA(text);
}

static int load(void)
{
    char dir[E1400_PATH], core[E1400_PATH], original[E1400_PATH], self[E1400_PATH], note[2 * E1400_PATH];
    E1400OriginalServerFn original_server;
    E1400AttachFn attach;
    if (g_original) return 0;
    e1400_module_dir(g_self, dir, sizeof(dir));
    GetModuleFileNameA(g_self, self, sizeof(self));
    e1400_path_join(core, sizeof(core), dir, "e1400patch.dll");
    g_core = LoadLibraryExA(core, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    original_server = g_core ? (E1400OriginalServerFn)GetProcAddress(g_core, E1400_ORIGINAL_SERVER_EXPORT) : NULL;
    if (!original_server || original_server(original, sizeof(original))) {
        /* without the loader: the default location next to the game */
        E1400Paths paths;
        e1400_paths_init(&paths, dir);
        e1400_resolve_original_server(&paths, original, sizeof(original));
        snprintf(note, sizeof(note), "e1400patch: loader %s unavailable (error %lu), running the original unpatched", core,
                 GetLastError());
        shim_note(note);
    }
    if (!_stricmp(original, self)) {
        shim_note("e1400patch: the configured original server.dll is this shim; check original_server in e1400patch.ini");
        return -1;
    }
    g_original = LoadLibraryExA(original, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    g_init = g_original ? (InitFn)GetProcAddress(g_original, "Init") : NULL;
    g_exit = g_original ? (ExitFn)GetProcAddress(g_original, "Exit") : NULL;
    if (!g_init || !g_exit) {
        snprintf(note, sizeof(note), "e1400patch: cannot load the original server.dll %s (error %lu)", original, GetLastError());
        shim_note(note);
        return -1;
    }
    attach = g_core ? (E1400AttachFn)GetProcAddress(g_core, E1400_ATTACH_EXPORT) : NULL;
    g_detach = g_core ? (E1400DetachFn)GetProcAddress(g_core, E1400_DETACH_EXPORT) : NULL;
    if (attach && attach(E1400_TARGET_SERVER, g_original, original) == 0) g_attached = 1;
    else if (g_core) shim_note("e1400patch: attaching the loader failed, running the original unpatched (see logs)");
    return 0;
}

__declspec(dllexport) int __cdecl Init(void *params)
{
    if (load()) return 0;
    return g_init(params);
}

__declspec(dllexport) void __cdecl Exit(void)
{
    if (g_exit) g_exit();
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = instance;
        DisableThreadLibraryCalls(instance);
    } else if (reason == DLL_PROCESS_DETACH && !reserved && g_original) {
        /* FreeLibrary (not process exit): unload the original like the game would without the shim */
        if (g_attached && g_detach) g_detach(E1400_TARGET_SERVER);
        FreeLibrary(g_original);
        g_original = NULL;
    }
    return TRUE;
}
