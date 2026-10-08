/* @NAME@ (@ID@)
 *
 * Own code only: no code transcribed from the decompilation (docs/legal.md). Describe here what the original does wrong
 * (behaviour, not code) and what this patch changes. */
#include <windows.h>

#include "e1400patch/patch_api.h"

E1400_PATCH_EXPORT int __cdecl e1400_patch_apply(const E1400PatchApi *api, E1400Target target)
{
    if (target != E1400_TARGET_SERVER) return 1;
    api->log(api, "applying to build %s", api->target(target)->build_id);
    /* example: if (api->hook(api, target, "srv_SomeFunction", (void *)detour, (void **)&original)) return 2; */
    return 0;
}

E1400_PATCH_EXPORT void __cdecl e1400_patch_release(const E1400PatchApi *api, E1400Target target)
{
    (void)target;
    api->log(api, "released");
}
