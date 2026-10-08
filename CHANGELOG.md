# Changelog

All notable changes. Versions: loader in `VERSION`, patches in their `patch.ini`.

## [Unreleased]

## [0.1.0]

### Added

- Loader `e1400patch.dll` with patch API 1: patch and mod folders, manifests (targets, builds, symbols, settings,
  requires/conflicts, order), build identification by SHA-256, hooks by symbol, address and import with rollback on
  failure and on unload.
- `server.dll` shim: non-destructive installation via `game.ini`, fail-safe fallback to the unpatched original.
- `e1400patch.exe`: install, uninstall, status, enable, disable, check (identify a game file, find known functions in
  unknown builds by signature, write candidate build tables).
- Build table `server-de-2.06` (German Gold 2.06, GOG/Steam) with 26 server functions.
- Patch `netfix` 0.1.0 for the multiplayer host: messages split by the network (VPN, Internet) are read completely
  instead of corrupting the server's read position; invalid message lengths drop the client instead of overrunning its
  receive buffer; `TCP_NODELAY` on client connections.
- Development: session replayer, build table and packaging tools, repository checks, tests, VS Code tasks, CI and
  release workflows.
