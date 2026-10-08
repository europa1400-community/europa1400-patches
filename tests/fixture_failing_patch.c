/* Test patch that hooks target_mul and then fails: the loader must remove its hook again. */
#include <windows.h>

#include "e1400patch/patch_api.h"

static int(__cdecl *g_mul)(int, int);

static int __cdecl mul_detour(int a, int b)
{
    return -g_mul(a, b);
}

E1400_PATCH_EXPORT int __cdecl e1400_patch_apply(const E1400PatchApi *api, E1400Target target)
{
    if (api->hook(api, target, "target_mul", (void *)mul_detour, (void **)&g_mul)) return 2;
    return 7; /* fail after hooking */
}
