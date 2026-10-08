/* e1400patch - public patch module API.
 *
 * A patch module is a DLL in <game>/patches/<id>/ (or <game>/mods/<id>/) next to its manifest patch.ini. The loader calls
 * e1400_patch_apply() once for every target the manifest lists, as soon as that target module is loaded and identified.
 * Everything a patch needs from the game goes through this API: addresses come from verified build tables (symbols), hooks
 * are created and owned by the loader (and removed again if the patch fails or the target is unloaded).
 *
 * Compatibility: E1400_PATCH_API_VERSION grows with new functions appended to E1400PatchApi; struct_size tells a patch which
 * functions exist. Existing members never change meaning. */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define E1400_PATCH_API_VERSION 1

/* Target modules a patch can change. Several targets per patch are allowed (e.g. a network fix touching server and game). */
typedef enum E1400Target {
    E1400_TARGET_SERVER = 0, /* server.dll (loaded by the hosting game only) */
    E1400_TARGET_GAME = 1,   /* the game executable (reserved: needs the game shim, see docs/architecture.md) */
    E1400_TARGET_COUNT
} E1400Target;

typedef struct E1400TargetInfo {
    const char *name;     /* "server", "game" */
    void *base;           /* loaded module base */
    const char *path;     /* file the module was loaded from */
    const char *build_id; /* identified build, e.g. "server-de-2.06"; NULL for an unknown build */
} E1400TargetInfo;

typedef struct E1400PatchApi E1400PatchApi;
struct E1400PatchApi {
    uint32_t api_version; /* E1400_PATCH_API_VERSION of the loader */
    uint32_t struct_size; /* sizeof(E1400PatchApi) of the loader */
    const char *patch_id; /* id of the patch this instance belongs to */
    const char *patch_dir;

    /* Writes a line to the loader log, prefixed with the patch id. */
    void (*log)(const E1400PatchApi *self, const char *format, ...);
    /* Patch setting: user override from e1400patch.ini [<patch id>], else [settings] of the manifest, else fallback. */
    const char *(*setting)(const E1400PatchApi *self, const char *key, const char *fallback);
    int (*setting_int)(const E1400PatchApi *self, const char *key, int fallback);

    /* The target module, NULL while it is not loaded. */
    const E1400TargetInfo *(*target)(E1400Target target);
    /* Address of a named symbol of the target's build table, verified against the module (NULL: unknown symbol, unknown build
     * or the code at that address is not what the table expects). */
    void *(*symbol)(E1400Target target, const char *name);
    /* Redirects a function (by symbol) to detour; *original receives a callable trampoline. The hook belongs to the patch. */
    int (*hook)(const E1400PatchApi *self, E1400Target target, const char *symbol, void *detour, void **original);
    /* Same for a raw address inside a target module (use only with addresses derived from verified symbols). */
    int (*hook_address)(const E1400PatchApi *self, void *address, void *detour, void **original);
    /* Redirects an import of a target module (its IAT slot), e.g. ("WS2_32.dll", "#16") or ("KERNEL32.dll", "Sleep"). */
    int (*hook_import)(const E1400PatchApi *self, E1400Target target, const char *dll, const char *function, void *detour,
                       void **original);
};

/* Exported by every patch module (extern "C", __cdecl). Return 0 when the patch is applied to this target. A non-zero result
 * makes the loader remove every hook the patch created for the target. */
typedef int(__cdecl *E1400PatchApplyFn)(const E1400PatchApi *api, E1400Target target);
#define E1400_PATCH_APPLY_EXPORT "e1400_patch_apply"

/* Optional: called before a target is unloaded (after the loader removed the patch's hooks for it). */
typedef void(__cdecl *E1400PatchReleaseFn)(const E1400PatchApi *api, E1400Target target);
#define E1400_PATCH_RELEASE_EXPORT "e1400_patch_release"

#define E1400_PATCH_EXPORT __declspec(dllexport)

#ifdef __cplusplus
}
#endif
