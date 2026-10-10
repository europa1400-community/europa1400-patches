# 🩹 europa1400-patches

Fixes for **Europa 1400: The Guild Gold** (*Die Gilde Gold*) that change no game file and can be removed at any time.

| Patch | For whom | What it fixes |
|---|---|---|
| [Netfix](patches/netfix/README.md) | the player who **hosts** a multiplayer game | lost connections and "out of sync" over VPNs (Radmin, Hamachi, ZeroTier) and the Internet |
| [Monitorfix](package/MONITORFIX.txt) | everyone with several monitors | the game always starts on the wrong (last) monitor; choose the monitor in `monitorfix.ini` (a `d3d8.dll` in the game folder, not a loader patch) |

Works with Gold 2.06, German (GOG, Steam). Other versions follow.

## Install

**Easiest:** with the [Europa 1400 Manager](https://europa1400-community.github.io/europa1400-manager/):
`patch install netfix` or the *Patches* tab. Everything needed comes along.

**By hand:**

1. Download [`e1400patch.zip`](https://github.com/europa1400-community/europa1400-patches/releases/latest/download/e1400patch.zip)
   and [`netfix.zip`](https://github.com/europa1400-community/europa1400-patches/releases/latest/download/netfix.zip).
2. Extract both into the game folder (the one with `game.ini`).
3. Run `e1400patch\e1400patch.exe install` from a command prompt in the game folder.

Check with `e1400patch\e1400patch.exe status`. Remove with `e1400patch\e1400patch.exe uninstall`, then delete the folders
`e1400patch\` and `patches\`. Problems: [Discord](https://discord.gg/jB9HYY8DpT) or
[issues](https://github.com/europa1400-community/europa1400-patches/issues), with the log
`e1400patch\logs\e1400patch.log`.

---

## How it works

The patch loader (`e1400patch`) puts itself between the game and the game's own server component: the game's
`game.ini` is pointed to the loader's `server.dll`, which loads the original and lets patches hook into it while the
game runs. Patches declare which game builds they support; the loader refuses what does not fit your game. Every patch
can be switched on and off. Mods and patches for the game executable will use the same mechanism later.

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
