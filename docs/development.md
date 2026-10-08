# Development setup

## Requirements

- Windows (the game and the loader are 32-bit Windows code)
- Visual Studio 2022 or newer, or the Build Tools, with "Desktop development with C++" (x86 compilers)
- [uv](https://docs.astral.sh/uv/) for the Python tools, git
- VS Code or Cursor (recommended extensions are listed in `.vscode/extensions.json`)
- For game tests: the game (German Gold 2.06 for the current build tables)
- Optional, community only: the private decompilation repository (symbols for build tables, recordings for replays)

## First steps

```powershell
git clone --recursive https://github.com/europa1400-community/europa1400-patches.git
cd europa1400-patches
./scripts/setup.ps1 -GameDir "C:\Path\To\Game"     # submodules, .env, toolchain check
./scripts/build.ps1 -Test
```

In VS Code: open the folder, select the configure preset `MSVC x86`, then use the tasks (Terminal > Run Task): Build, Test,
Check repository, Install into game, Run game, Replay session, Show loader log, Package release, New patch.

`.env` (never committed) holds `GAME_DIR`, `GAME_EXE` and `DECOMP_DIR`. With access to the decompilation:

```powershell
./scripts/setup.ps1 -DecompUrl <url of europa1400-decompilation>
```

clones it next to this repository and sets `DECOMP_DIR`. Building never needs it; replays and build tables do.

## Repository layout

```
include/e1400patch/   public patch API
src/core/             shared core (static library)
src/loader/           e1400patch.dll
src/shims/server/     server.dll shim
src/cli/              e1400patch.exe
patches/<id>/         patches (manifest, sources, CMakeLists.txt)
builds/               build tables and the symbol lists they are generated from
templates/patch/      template for new patches
tools/                replayer (C), build tables, packaging, repository checks (Python)
tests/                CTest suites without game files
scripts/              PowerShell entry points for builds, installs and replays
package/              files added to the loader package
vendor/minhook/       MinHook (submodule)
```

## Releases

No secrets or personal tokens are needed.

1. Merge the pull requests (squash, Conventional Commits title). Raise `version=` in the manifests of changed patches.
2. GitHub: **Actions → Release → Run workflow** on `main`. semantic-release derives the loader version from the
   commits since the last release (`fix:` → patch, `feat:` → minor, `feat!:`/`BREAKING CHANGE:` → major; without such
   commits nothing is released). CI builds, tests and packages with that version; only then the tag `v<version>` and
   the release with generated notes are created and `e1400patch.zip`, one zip per patch, `release.json` and
   `SHA256SUMS.txt` are attached.

Local builds take their version from `git describe` (e.g. `0.1.0-3-gabc1234` three commits after v0.1.0).
3. The unversioned asset names are stable download links for europa1400-database.
