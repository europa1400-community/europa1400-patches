# Contributing

Thanks for helping! Please read [docs/legal.md](docs/legal.md) first: this repository accepts **own code only**. Code
copied or translated from a decompilation, game files and session recordings are rejected.

1. Set up as described in [docs/development.md](docs/development.md).
2. One topic per pull request. New patches start from `./scripts/new-patch.ps1`.
3. Before pushing: `uv run tools/check_repo.py` and `./scripts/build.ps1 -Test`.
4. Describe what the original does wrong (behaviour) and how you tested the change (replays, game tests). For patches
   that should not change behaviour, a byte-identical replay is expected.
5. Commit messages and pull request titles follow [Conventional Commits](https://www.conventionalcommits.org/)
   (`fix: ...`, `feat: ...`, `feat!: ...` for breaking changes, `docs:`/`ci:`/`chore:`/`test:` otherwise): they decide
   the next version and become the release notes. Pull requests are squash-merged with their title.
6. Raise `version=` in the manifest of a changed patch.

Bug reports: game version (language, GOG/Steam/CD, patch level), `e1400patch.exe status` output, the loader log
(`e1400patch/logs/e1400patch.log`) and what happened. Do not attach recordings or save games publicly.
