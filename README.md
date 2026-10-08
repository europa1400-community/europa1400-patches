# europa1400-patches

Patch loader and patches for **Europa 1400: The Guild Gold** (*Die Gilde Gold*).

- **Non-destructive:** no game file is replaced; the loader hooks into the running game and can be removed at any time.
- **Modular:** every fix or extension is a patch you can switch on or off; patches declare which game builds they
  support, and the loader refuses what does not fit your game.
- **Host only (server patches):** only the player who hosts a multiplayer game needs the loader. Clients need nothing.

Status: the loader for `server.dll` is ready (0.1.0); the first patch (network fixes) is in development.
Supported build: German Gold 2.06 (GOG and Steam). Other versions: see [docs/builds.md](docs/builds.md).

## Install

The recommended way is the [europa1400-manager](https://github.com/europa1400-community/europa1400-manager).
Manually (see `INSTALL.txt` in the package):

1. Extract `e1400patch.zip` into the game directory, then run `e1400patch\e1400patch.exe install`.
2. Extract patch archives (e.g. `netfix.zip`) into the game directory.
3. `e1400patch\e1400patch.exe status` shows what is active and whether it fits your game version.

Remove: `e1400patch.exe uninstall`, then delete `e1400patch\` and `patches\`.

## For developers

- [docs/development.md](docs/development.md): setup with VS Code/Cursor, build, tasks, releases
- [docs/architecture.md](docs/architecture.md): loader, shims, targets, patch life cycle, plans for game patches and mods
- [docs/patch-authoring.md](docs/patch-authoring.md): writing a patch
- [docs/builds.md](docs/builds.md): game builds, build tables, supporting a new game version
- [docs/testing.md](docs/testing.md): tests without the game, session replays, game tests
- [docs/legal.md](docs/legal.md): **what may be in this repository** (own code only)

Patches are developed with the help of a community-only decompilation of the game; that decompilation is not part of
this repository, and no code from it is copied here.

## License

MIT, see [LICENSE](LICENSE). MinHook (vendor/minhook) is BSD-2-Clause. The game itself is not included and remains
the property of its rights holders.
