/* e1400patch.dll - the loader runtime shared by all shims.
 *
 * A shim (server.dll now, a game shim later) calls e1400_attach() once its target module is loaded. On the first attach the
 * loader reads its configuration, the build tables and the manifests of patches/ and mods/, and loads the enabled patch
 * modules. Each attach identifies the target build (SHA-256 of the file) and applies every compatible patch for that
 * target. Hooks are created through MinHook (functions) or by writing IAT slots (imports); they are owned per patch and
 * target: a failing patch loses all its hooks, and e1400_detach() removes the hooks of a target before it is unloaded. */
#include "e1400core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MinHook.h"

enum { MAX_HOOKS = 512 };

typedef struct Patch {
    E1400Manifest manifest;
    HMODULE module;
    E1400PatchApplyFn apply;
    E1400PatchReleaseFn release;
    E1400PatchApi api;
    unsigned applied; /* bit per target */
} Patch;

typedef struct Hook {
    Patch *owner;     /* NULL: free entry */
    E1400Target target;
    int import;       /* 1: IAT slot, 0: function (MinHook) */
    void *address;    /* function entry or IAT slot */
    void *detour;
    void *previous;   /* IAT: slot value before the hook */
    int enabled;
} Hook;

typedef struct Target {
    E1400TargetInfo info;
    HMODULE module;
    char path[E1400_PATH];
    const E1400Build *build;
} Target;

static CRITICAL_SECTION g_lock;
static int g_initialized;
static E1400Paths g_paths;
static E1400BuildSet g_builds;
static Patch g_patches[E1400_MAX_PATCHES];
static unsigned g_patch_count;
static Target g_targets[E1400_TARGET_COUNT];
static Hook g_hooks[MAX_HOOKS];
static unsigned g_hook_count;

/* ---- API for patches -------------------------------------------------------------------------------------------------- */

static Patch *patch_of(const E1400PatchApi *api)
{
    return (Patch *)((uint8_t *)api - offsetof(Patch, api));
}

static void api_log(const E1400PatchApi *self, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    e1400_log_v(self->patch_id, format, args);
    va_end(args);
}

static const char *api_setting(const E1400PatchApi *self, const char *key, const char *fallback)
{
    /* returned strings live in a small per-thread ring (valid for the next seven calls) */
    static __declspec(thread) char ring[8][256];
    static __declspec(thread) unsigned next;
    char *out = ring[next++ % 8], user[256];
    Patch *patch = patch_of(self);
    e1400_ini_get(patch->manifest.file, "settings", key, fallback ? fallback : "", out, sizeof(ring[0]));
    e1400_ini_get(g_paths.config, patch->manifest.id, key, "\x01", user, sizeof(user));
    if (user[0] != '\x01') snprintf(out, sizeof(ring[0]), "%s", user);
    return out;
}

static int api_setting_int(const E1400PatchApi *self, const char *key, int fallback)
{
    char text[32], *end;
    const char *value;
    long number;
    snprintf(text, sizeof(text), "%d", fallback);
    value = api_setting(self, key, text);
    number = strtol(value, &end, 0);
    return end == value ? fallback : (int)number;
}

static const E1400TargetInfo *api_target(E1400Target target)
{
    if ((unsigned)target >= E1400_TARGET_COUNT || !g_targets[target].module) return NULL;
    return &g_targets[target].info;
}

static void *api_symbol(E1400Target target, const char *name)
{
    const E1400Symbol *symbol;
    if ((unsigned)target >= E1400_TARGET_COUNT || !g_targets[target].module) return NULL;
    symbol = e1400_build_symbol(g_targets[target].build, name);
    return symbol ? (uint8_t *)g_targets[target].module + symbol->rva : NULL;
}

static Hook *hook_add(Patch *owner, E1400Target target, int import, void *address, void *detour)
{
    for (unsigned i = 0; i < g_hook_count; i++)
        if (g_hooks[i].owner && g_hooks[i].address == address) {
            e1400_log("%s: %p is already hooked by %s (one patch per function)", owner->manifest.id, address,
                      g_hooks[i].owner->manifest.id);
            return NULL;
        }
    if (g_hook_count == MAX_HOOKS) {
        e1400_log("%s: hook table full", owner->manifest.id);
        return NULL;
    }
    g_hooks[g_hook_count] = (Hook){owner, target, import, address, detour, NULL, 0};
    return &g_hooks[g_hook_count++];
}

static E1400Target target_of_address(const void *address)
{
    HMODULE module = NULL;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)address,
                            &module))
        return E1400_TARGET_COUNT;
    for (unsigned t = 0; t < E1400_TARGET_COUNT; t++)
        if (g_targets[t].module && g_targets[t].module == module) return (E1400Target)t;
    return E1400_TARGET_COUNT;
}

static int api_hook_address(const E1400PatchApi *self, void *address, void *detour, void **original)
{
    Patch *patch = patch_of(self);
    E1400Target target = address ? target_of_address(address) : E1400_TARGET_COUNT;
    MH_STATUS status;
    Hook *hook;
    if (!detour || target == E1400_TARGET_COUNT) {
        e1400_log("%s: hook at %p rejected (not inside a loaded target)", patch->manifest.id, address);
        return -1;
    }
    if (!(hook = hook_add(patch, target, 0, address, detour))) return -1;
    status = MH_CreateHook(address, detour, original);
    if (status != MH_OK) {
        hook->owner = NULL;
        e1400_log("%s: hook at %p failed: %s", patch->manifest.id, address, MH_StatusToString(status));
        return -1;
    }
    return 0;
}

static int api_hook(const E1400PatchApi *self, E1400Target target, const char *symbol, void *detour, void **original)
{
    void *address = api_symbol(target, symbol);
    if (!address) {
        e1400_log("%s: symbol %s is not available for %s", self->patch_id, symbol, e1400_target_name(target));
        return -1;
    }
    return api_hook_address(self, address, detour, original);
}

static int api_hook_import(const E1400PatchApi *self, E1400Target target, const char *dll, const char *function, void *detour,
                           void **original)
{
    Patch *patch = patch_of(self);
    void **slot;
    Hook *hook;
    if ((unsigned)target >= E1400_TARGET_COUNT || !g_targets[target].module || !detour) return -1;
    if (!(slot = e1400_iat_slot(g_targets[target].module, dll, function))) {
        e1400_log("%s: import %s!%s not found in %s", patch->manifest.id, dll, function, e1400_target_name(target));
        return -1;
    }
    if (!(hook = hook_add(patch, target, 1, slot, detour))) return -1;
    hook->previous = *slot;
    if (original) *original = *slot;
    return 0;
}

/* ---- hook ownership --------------------------------------------------------------------------------------------------- */

static void hooks_enable(Patch *owner, E1400Target target)
{
    for (unsigned i = 0; i < g_hook_count; i++) {
        Hook *hook = &g_hooks[i];
        if (hook->owner != owner || hook->target != target || hook->enabled) continue;
        if (hook->import) e1400_iat_write((void **)hook->address, hook->detour);
        else MH_QueueEnableHook(hook->address);
        hook->enabled = 1;
    }
    MH_ApplyQueued();
}

/* Removes the hooks of a patch (owner) or of every patch (owner NULL) for a target. */
static void hooks_remove(Patch *owner, E1400Target target)
{
    for (unsigned i = 0; i < g_hook_count; i++) {
        Hook *hook = &g_hooks[i];
        if (!hook->owner || hook->target != target || (owner && hook->owner != owner)) continue;
        if (hook->import) {
            if (hook->enabled) e1400_iat_write((void **)hook->address, hook->previous);
        } else {
            MH_RemoveHook(hook->address); /* disables first if enabled */
        }
        hook->owner = NULL;
    }
    while (g_hook_count && !g_hooks[g_hook_count - 1].owner) g_hook_count--;
}

/* ---- initialization --------------------------------------------------------------------------------------------------- */

static Patch *patch_by_id(const char *id)
{
    for (unsigned i = 0; i < g_patch_count; i++)
        if (!_stricmp(g_patches[i].manifest.id, id)) return &g_patches[i];
    return NULL;
}

static void collect(const char *dir, const char *kind)
{
    E1400Manifest found[E1400_MAX_PATCHES];
    unsigned count = e1400_manifests_scan(found, E1400_MAX_PATCHES, dir);
    for (unsigned i = 0; i < count && g_patch_count < E1400_MAX_PATCHES; i++) {
        E1400Manifest *manifest = &found[i];
        if (_stricmp(manifest->kind, kind)) {
            e1400_log("skip %s: kind %s in %s", manifest->id, manifest->kind, dir);
            continue;
        }
        if (patch_by_id(manifest->id)) {
            e1400_log("skip %s in %s: id already used", manifest->id, dir);
            continue;
        }
        if (!e1400_manifest_enabled(manifest, g_paths.config)) {
            e1400_log("%s %s %s: disabled", kind, manifest->id, manifest->version);
            continue;
        }
        if (manifest->api == 0 || manifest->api > E1400_PATCH_API_VERSION) {
            e1400_log("skip %s: needs patch API %u, loader provides %u", manifest->id, manifest->api, E1400_PATCH_API_VERSION);
            continue;
        }
        g_patches[g_patch_count++].manifest = *manifest;
    }
}

/* Drops patches whose requirements are missing or that conflict with an earlier one (repeated until stable). */
static void resolve_dependencies(void)
{
    int changed = 1;
    while (changed) {
        changed = 0;
        for (unsigned i = 0; i < g_patch_count; i++) {
            E1400Manifest *manifest = &g_patches[i].manifest;
            const char *problem = NULL;
            char item[E1400_NAME] = "";
            for (const char *p = manifest->requires; *p && !problem;) {
                size_t length;
                while (*p == ',' || *p == ' ') p++;
                if (!(length = strcspn(p, ", "))) break;
                snprintf(item, sizeof(item), "%.*s", (int)length, p);
                if (!patch_by_id(item)) problem = "requires";
                p += length;
            }
            for (unsigned j = 0; j < i && !problem; j++)
                if (e1400_list_contains(manifest->conflicts, g_patches[j].manifest.id) ||
                    e1400_list_contains(g_patches[j].manifest.conflicts, manifest->id)) {
                    problem = "conflicts with";
                    snprintf(item, sizeof(item), "%s", g_patches[j].manifest.id);
                }
            if (problem) {
                e1400_log("skip %s: %s %s", manifest->id, problem, item);
                memmove(&g_patches[i], &g_patches[i + 1], (g_patch_count - i - 1) * sizeof(Patch));
                g_patch_count--;
                changed = 1;
                break;
            }
        }
    }
}

static void load_modules(void)
{
    for (unsigned i = 0; i < g_patch_count;) {
        Patch *patch = &g_patches[i];
        char path[E1400_PATH];
        e1400_path_join(path, sizeof(path), patch->manifest.dir, patch->manifest.module);
        patch->module = LoadLibraryExA(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        patch->apply = patch->module ? (E1400PatchApplyFn)GetProcAddress(patch->module, E1400_PATCH_APPLY_EXPORT) : NULL;
        patch->release = patch->module ? (E1400PatchReleaseFn)GetProcAddress(patch->module, E1400_PATCH_RELEASE_EXPORT) : NULL;
        if (!patch->apply) {
            e1400_log("skip %s: cannot load %s (error %lu)", patch->manifest.id, path, GetLastError());
            if (patch->module) FreeLibrary(patch->module);
            memmove(patch, patch + 1, (g_patch_count - i - 1) * sizeof(Patch));
            g_patch_count--;
            continue;
        }
        i++;
    }
    /* the API structs point into g_patches, which no longer moves */
    for (unsigned i = 0; i < g_patch_count; i++) {
        Patch *patch = &g_patches[i];
        patch->api = (E1400PatchApi){E1400_PATCH_API_VERSION, sizeof(E1400PatchApi), patch->manifest.id, patch->manifest.dir,
                                     api_log, api_setting, api_setting_int, api_target, api_symbol, api_hook,
                                     api_hook_address, api_hook_import};
        e1400_log("%s %s %s loaded (%s)", patch->manifest.kind, patch->manifest.id, patch->manifest.version, patch->manifest.dir);
    }
}

static int initialize(void)
{
    char loader_dir[E1400_PATH];
    HMODULE self = NULL;
    MH_STATUS status;
    if (g_initialized) return 0;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)initialize,
                       &self);
    e1400_module_dir(self, loader_dir, sizeof(loader_dir));
    e1400_paths_init(&g_paths, loader_dir);
    e1400_log_open(g_paths.log);
    e1400_log("e1400patch %s (patch API %u), game %s", E1400PATCH_VERSION, E1400_PATCH_API_VERSION, g_paths.game);
    if ((status = MH_Initialize()) != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        e1400_log("MinHook: %s", MH_StatusToString(status));
        return -1;
    }
    e1400_builds_load(&g_builds, g_paths.builds);
    e1400_log("%u build table(s) in %s", g_builds.count, g_paths.builds);
    collect(g_paths.patches, "patch");
    collect(g_paths.mods, "mod");
    resolve_dependencies();
    load_modules();
    g_initialized = 1;
    return 0;
}

/* ---- shim interface --------------------------------------------------------------------------------------------------- */

__declspec(dllexport) int __cdecl e1400_attach(E1400Target target, HMODULE module, const char *path)
{
    Target *slot;
    char sha256[65];
    unsigned applied = 0;
    if ((unsigned)target >= E1400_TARGET_COUNT || !module) return -1;
    EnterCriticalSection(&g_lock);
    if (initialize()) {
        LeaveCriticalSection(&g_lock);
        return -1;
    }
    slot = &g_targets[target];
    if (slot->module == module) { /* attached again (game hosts once more): hooks are still in place */
        LeaveCriticalSection(&g_lock);
        return 0;
    }
    memset(slot, 0, sizeof(*slot));
    slot->module = module;
    snprintf(slot->path, sizeof(slot->path), "%s", path);
    if (e1400_sha256_file(path, sha256)) sha256[0] = 0;
    slot->build = e1400_builds_find(&g_builds, e1400_target_name(target), sha256);
    slot->info = (E1400TargetInfo){e1400_target_name(target), module, slot->path, slot->build ? slot->build->id : NULL};
    e1400_log("attach %s: %s, sha256 %s, build %s", e1400_target_name(target), path, sha256[0] ? sha256 : "?",
              slot->build ? slot->build->id : "unknown");
    for (unsigned i = 0; i < g_patch_count; i++) {
        Patch *patch = &g_patches[i];
        char why[256];
        int result;
        if (!(patch->manifest.targets & (1u << target))) continue;
        if (e1400_manifest_check(&patch->manifest, target, slot->build, why, sizeof(why))) {
            e1400_log("%s: not applied to %s: %s", patch->manifest.id, e1400_target_name(target), why);
            continue;
        }
        result = patch->apply(&patch->api, target);
        if (result) {
            hooks_remove(patch, target);
            e1400_log("%s: apply to %s failed (%d), hooks removed", patch->manifest.id, e1400_target_name(target), result);
            continue;
        }
        hooks_enable(patch, target);
        patch->applied |= 1u << target;
        applied++;
        e1400_log("%s: applied to %s", patch->manifest.id, e1400_target_name(target));
    }
    e1400_log("%s: %u patch(es) applied", e1400_target_name(target), applied);
    LeaveCriticalSection(&g_lock);
    return 0;
}

__declspec(dllexport) void __cdecl e1400_detach(E1400Target target)
{
    if ((unsigned)target >= E1400_TARGET_COUNT) return;
    EnterCriticalSection(&g_lock);
    if (g_targets[target].module) {
        hooks_remove(NULL, target);
        for (unsigned i = 0; i < g_patch_count; i++) {
            Patch *patch = &g_patches[i];
            if (!(patch->applied & (1u << target))) continue;
            if (patch->release) patch->release(&patch->api, target);
            patch->applied &= ~(1u << target);
        }
        e1400_log("detach %s", e1400_target_name(target));
        memset(&g_targets[target], 0, sizeof(g_targets[target]));
    }
    LeaveCriticalSection(&g_lock);
}

__declspec(dllexport) int __cdecl e1400_original_server(char *out, size_t size)
{
    char loader_dir[E1400_PATH];
    E1400Paths paths;
    HMODULE self = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)e1400_original_server, &self);
    e1400_module_dir(self, loader_dir, sizeof(loader_dir));
    e1400_paths_init(&paths, loader_dir);
    return e1400_resolve_original_server(&paths, out, size);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        InitializeCriticalSection(&g_lock);
    }
    return TRUE;
}
