/* Test patch: hooks target_add by symbol and the GetTickCount import of the test target. */
#include <windows.h>

#include "e1400patch/patch_api.h"

static int(__cdecl *g_add)(int, int);
static DWORD(WINAPI *g_ticks)(void);
static int g_bonus;

static int __cdecl add_detour(int a, int b)
{
    return g_add(a, b) + g_bonus;
}

static DWORD WINAPI ticks_detour(void)
{
    return 4242;
}

E1400_PATCH_EXPORT int __cdecl e1400_patch_apply(const E1400PatchApi *api, E1400Target target)
{
    if (target != E1400_TARGET_SERVER || api->struct_size < sizeof(E1400PatchApi)) return 1;
    g_bonus = api->setting_int(api, "bonus", 1);
    api->log(api, "applying, bonus %d, build %s", g_bonus, api->target(target)->build_id);
    if (api->hook(api, target, "target_add", (void *)add_detour, (void **)&g_add)) return 2;
    if (api->hook_import(api, target, "KERNEL32.dll", "GetTickCount", (void *)ticks_detour, (void **)&g_ticks)) return 3;
    return 0;
}

E1400_PATCH_EXPORT void __cdecl e1400_patch_release(const E1400PatchApi *api, E1400Target target)
{
    (void)target;
    api->log(api, "released");
}
