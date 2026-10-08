# Writing a patch

Read [legal.md](legal.md) first: patches contain own code only.

## Create

```powershell
./scripts/new-patch.ps1 mypatch -Name "My patch"     # or the VS Code task "New patch"
```

This creates `patches/mypatch/` with `patch.ini`, `mypatch.c` and `CMakeLists.txt`; the build picks it up automatically
and stages it as `build/stage/patches/mypatch/`.

## Manifest (`patch.ini`)

```ini
[patch]
id=mypatch                 ; = folder name, lowercase
name=My patch
version=0.1.0              ; semantic version of this patch
kind=patch                 ; patch | mod
api=1                      ; E1400_PATCH_API_VERSION the module was built against
module=mypatch.dll
targets=server             ; server, game (several allowed)
enabled=1                  ; default; players change it in e1400patch.ini [patches]
order=100                  ; application order
requires=                  ; other patch ids that must be enabled
conflicts=                 ; patch ids that must not be enabled together with this one
description=One line for the manager

[builds]
server=server-de-2.06      ; supported builds (ids of builds/*.ini); "*" only for patches without symbols

[symbols]
server=srv_RecvFromClient  ; every symbol the patch uses; missing in the build table = patch not applied

[settings]
some_option=1              ; defaults, overridable in e1400patch.ini [mypatch]
```

## Module

```c
#include "e1400patch/patch_api.h"

static int (__cdecl *g_original)(void *);

static int __cdecl detour(void *slot)
{
    /* own code before/after the original */
    return g_original(slot);
}

E1400_PATCH_EXPORT int __cdecl e1400_patch_apply(const E1400PatchApi *api, E1400Target target)
{
    if (api->hook(api, target, "srv_RecvFromClient", (void *)detour, (void **)&g_original)) return 1;
    return 0;
}
```

API (see `include/e1400patch/patch_api.h`):

| Function | Use |
|---|---|
| `log` | line in `e1400patch/logs/e1400patch.log`, prefixed with the patch id |
| `setting`, `setting_int` | manifest `[settings]`, overridden by the player |
| `target` | base, path and build id of a target |
| `symbol` | address of a named function of the identified build |
| `hook` | redirect a function by symbol; `original` receives a callable trampoline |
| `hook_address` | redirect at an address inside a target (derive it from a symbol) |
| `hook_import` | redirect an import (IAT slot) of a target, e.g. `("WS2_32.dll", "#16")` for `recv` |

Rules:

- Return non-zero from `e1400_patch_apply` when anything fails; the loader then removes all your hooks for that target.
- Keep state per target; `e1400_patch_release` is called before the target is unloaded (the server is unloaded after
  every hosted game).
- Hooks are switched on only after `apply` returned. Do not call hooked functions from `apply`.
- One function, one patch. If you need a function another patch hooks, extend that patch or move the shared part into a
  common patch both require.
- Declare every symbol in `[symbols]` and add it to `builds/symbols/<target>.txt` (then refresh the build tables, see
  [builds.md](builds.md)).
- Server patches run on the host only. A fix that also needs the clients needs a `game` part (later).

## Test

- `./scripts/build.ps1 -Test`, `uv run tools/check_repo.py`
- `./scripts/replay.ps1 -Patches` replays a recorded session with your patch enabled; a patch that is meant to keep the
  original behaviour must stay byte-identical. Fixes that change behaviour need their own recordings or fault injection
  (see [testing.md](testing.md)).
- `./scripts/install-dev.ps1` installs the build into `GAME_DIR`; then play.
