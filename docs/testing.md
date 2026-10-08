# Testing

## Without the game (CI)

`./scripts/build.ps1 -Test` (CTest):

- `core`: unit tests of the core and an end-to-end run of the loader against a test DLL: build identification, hooks by
  symbol and import, settings, unsupported build, disabled patch, newer API, a failing patch whose hooks are removed,
  detach.
- `cli`: install/uninstall/enable/disable against a scratch game directory.

`uv run tools/check_repo.py` checks the repository rules (no game files, manifests, build tables, symbols).

## With the game: session replays

A recording (`.e1rec`) logs every network and time input of the server and every byte it sent during a real
multiplayer session. `e1400replay` feeds the inputs back and compares every send:

```powershell
./scripts/replay.ps1             # loader without patches: must be byte-identical to the original
./scripts/replay.ps1 -Patches    # with the staged patches (netfix diverges by design, see patches/netfix)
./scripts/replay.ps1 -Original   # baseline: the original alone
```

Recordings and noise masks live outside this repository (default: `DECOMP_DIR\bin\recordings`,
`DECOMP_DIR\tests\server`), because recordings contain save games and IP addresses. A noise mask lists the bytes in which
the original differs from its own recording (it sends uninitialized stack bytes in command `0x20`); learn one with
`e1400replay <rec> <original> --original <original> --learn <mask>`.

Patches that change behaviour on purpose (fixes) cannot be byte-identical to old recordings; they need fault injection
in the replayer or new recordings with the fix active, documented with the patch.

## In the game

`./scripts/install-dev.ps1` installs the build into `GAME_DIR` (non-destructive) and shows the status; the VS Code task
"Run game" starts it, "Show loader log" shows the log. `./scripts/install-dev.ps1 -Uninstall` removes it again.
