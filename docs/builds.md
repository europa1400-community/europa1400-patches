# Game builds

The game exists in several languages, distributions and patch levels. Patches address functions by name; a build table
maps the names of one exact build (identified by SHA-256) to addresses.

## Known builds

| Build id | Target | File | Game | Source of the names |
|---|---|---|---|---|
| `server-de-2.06` | server | `Server\server.dll` | Die Gilde Gold 2.06, German (GOG and Steam are identical) | decompilation `symbols/SERVER` |

The catalog of game editions (language, distribution, version, DRM) is maintained in europa1400-database; build ids here
should be referenced there once it lists file hashes.

## Build table format

```ini
[build]
id=server-de-2.06
target=server
file=server.dll
sha256=<64 hex digits of the whole file>
description=...
[symbols]
srv_RecvFromClient=0x00009ac0,0x1a2b3c4d   ; RVA, signature
```

The signature is the FNV-1a hash of 16 file bytes at the RVA, with bytes of relocated absolute addresses counted as zero.
It identifies code without storing it.

## Refreshing a table (needs the private decompilation)

```powershell
uv run tools/build_table.py --target server --id server-de-2.06 --program SERVER `
    --file "%GAME_DIR%/Server/server.dll" --description "Die Gilde Gold 2.06 DE (GOG, Steam): server.dll"
```

Names come from `DECOMP_DIR/symbols/<PROGRAM>/functions.csv`; only names listed in `builds/symbols/<target>.txt` are
exported.

## A new game version shows up

1. `e1400patch.exe check <file>` (or `build/stage/e1400patch/e1400patch.exe check <file>`): identifies a known build or
   searches every known symbol by signature and reports which patches would find all their symbols.
2. If everything a patch needs is found uniquely, `check <file> --write <new build id>` writes a candidate table. Review
   it (the decompilation of that build, or at least a disassembler) before adding the build id to a patch's `[builds]`.
3. If symbols are missing, that build differs in code: it needs its own analysis (Ghidra project of that build, symbols
   ported by function hashes in the decompilation repository) and possibly a different patch implementation.

Example: the English GOG `server.dll` (a newer build) currently matches 9 of 26 server symbols by signature; supporting it
needs a port of the names in the decompilation repository first.

## Several decompiled builds

The decompilation repository rebuilds one canonical build per target (German 2.06). Further builds get symbol sets
(`symbols/<PROGRAM>_<variant>`) ported by function hashes; a separate rebuild is only needed where their code really
differs. Patches stay build-independent as long as the functions they use keep their meaning.
