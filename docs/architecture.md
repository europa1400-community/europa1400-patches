# Architecture

## Goals

- One modular mechanism for every kind of change: server fixes now, game executable patches later, and eventually a
  modding framework that can load large extensions.
- Non-destructive: no game file is replaced or modified on disk; removing the loader restores the original state.
- Per game build: a patch states which builds it supports; the loader identifies the build and refuses what does not fit.
- Safe failure: an unknown build, a missing symbol or a failing patch leaves the game running unpatched (or without that
  patch), never half patched.

## Game directory layout

```
<game>/
  game.ini                 [Network] Server=e1400patch\server.dll   (set by install, previous value kept)
  Server/server.dll        the untouched original
  e1400patch/
    server.dll             shim for the server (the game loads it as its server DLL)
    e1400patch.dll         loader runtime, shared by all shims
    e1400patch.exe         install / uninstall / status / enable / disable / check
    e1400patch.ini         configuration (created from e1400patch.ini.default)
    builds/*.ini           build tables
    logs/e1400patch.log
  patches/<id>/            one folder per patch: patch.ini + <id>.dll (+ data)
  mods/<id>/               the same for mods (kind=mod), later the home of the modding framework's content
```

Installing a patch = extracting its archive into the game directory. Removing it = deleting its folder (or disabling it
in `e1400patch.ini`).

## Components

| Component | Role |
|---|---|
| `src/core` | static library: paths and configuration, INI access, log, SHA-256, PE reading, signatures, IAT lookup, build tables, manifests |
| `src/loader` (`e1400patch.dll`) | runtime: loads enabled patch modules, identifies targets, applies patches, owns hooks (MinHook for functions, IAT slots for imports) |
| `src/shims/server` (`server.dll`) | entry for the server: loads the original and the loader, attaches the target `server`, forwards `Init`/`Exit` |
| `src/cli` (`e1400patch.exe`) | installation and inspection without the manager |
| `include/e1400patch/patch_api.h` | the only interface between loader and patches |
| `tools/replay` (`e1400replay.exe`) | development: replays recorded sessions against loader + patches, byte by byte |

## Targets and shims

A *target* is a module patches can change: `server` (server.dll) and `game` (the executable). A *shim* is the small
entry DLL that gets the loader into the process for a target:

- **server**: `e1400patch\server.dll`, referenced by `game.ini`. Only the hosting game loads the server, so server
  patches only ever run on the host; clients are unaffected.
- **game** (planned): an entry DLL the executable loads on every PC (e.g. an ASI loader or a proxy of a system DLL; to be
  decided when the executable is understood well enough). It will use the same loader, and it can also attach the
  `server` target when the game loads its server DLL.

Both shims call the same exports of `e1400patch.dll` (`e1400_attach`, `e1400_detach`). The loader initializes once per
process, so a patch that targets `server` and `game` is one module with one state, applied to each target as soon as it is
available (a network fix that needs both sides is one patch).

## Patch life cycle

1. First attach: read `e1400patch.ini`, `builds/`, the manifests in `patches/` and `mods/`; drop disabled patches, patches
   for a newer API, missing requirements and conflicts; load the remaining modules.
2. Attach of a target: SHA-256 of the target file -> build table. For every patch that targets it: the build must be
   listed in the manifest and every declared symbol must exist in the table; then `e1400_patch_apply(api, target)`.
3. The patch creates hooks through the API. They are only switched on after `apply` returned 0; a non-zero result
   removes all hooks of that patch for the target.
4. Detach (the game unloads the server DLL): all hooks of the target are removed, `e1400_patch_release` is called, the
   original is unloaded. Hosting again starts from a fresh original.

One function can be hooked by one patch only (the second gets an error); patches that need the same function must be
combined or coordinate through a common patch.

## Build identification

Builds are identified by the SHA-256 of the file. A build table (`builds/<id>.ini`) names its functions with RVAs and a
signature (FNV-1a of 16 code bytes, relocated address bytes masked). The signatures let `e1400patch check` find known
functions in an unknown build (other language, distribution or patch level) and estimate which patches could work there;
see [builds.md](builds.md).

## Later: modding framework

The framework will be a large patch (or a set of patches) built on this loader: it hooks game systems and exposes its own,
higher-level API to mods in `mods/`. Mods that need more than that API can still ship their own hooks through the patch API.
Data overlays (replacing or adding game files through the game's file system) will be a loader feature with its own
manifest section when needed.
