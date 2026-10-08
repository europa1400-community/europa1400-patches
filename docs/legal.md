# Legal ground rules

This repository is public. Its contents must be **our own work**, so that publishing and distributing it does not
reproduce or adapt the copyrighted game. These rules reduce the legal risk; they are not a legal opinion. When in doubt,
ask before you commit.

## The two repositories

| | `europa1400-decompilation` (private, community only) | `europa1400-patches` (this repository, public) |
|---|---|---|
| Contains | decompiled and rebuilt game code, symbols, analysis, recordings | loader, patch modules, tools, documentation |
| Purpose | understand the game, find and prove bugs | ship fixes and extensions as own code |
| Game bytes | only locally, never committed | **never** |

Knowledge flows from the private repository into patches here, **code does not**.

## Allowed here

- Own new code: hooks, wrappers around original functions, new logic, new features.
- Calling the original function from a hook (the original stays the user's own copy, loaded at run time).
- Interface knowledge needed to work with the game: function and data names, addresses/RVAs, structure offsets,
  constants, calling conventions, message formats. Build tables contain names, RVAs and a hash of a few code bytes, never
  the bytes themselves.
- Descriptions of what the original does (behaviour), e.g. in comments and documentation.

## Not allowed here

- Code transcribed or translated from the decompilation (C from the private repository, Ghidra output, disassembly).
  If a whole function has to be replaced, write it anew from the behaviour description and the interface knowledge.
- Game files or parts of them: executables, DLLs, data, assets, byte arrays copied from game files, save games.
- Session recordings (`*.e1rec`): they contain save games and IP addresses.
- Copies of other projects without a compatible license and attribution.

`tools/check_repo.py` (also run by the CI) rejects binaries, large files and large byte arrays. It cannot detect
transcribed code: reviewers and authors are responsible for that.

## Distribution

Releases contain only what is built from this repository plus MinHook (BSD-2-Clause, license included). Players need
their own copy of the game; the loader patches it at run time and changes no game file. Game names are used to
describe compatibility only.

## Background (Germany, informal)

- UrhG §69c: reproducing, adapting and distributing a program needs the rights holder's consent; a rebuild from a
  decompilation is likely an adaptation. That is why the rebuild stays private.
- UrhG §69d: adaptations needed for the intended use, including error correction, are allowed for the user.
- UrhG §69e: decompilation is allowed to achieve interoperability of an independently created program; information
  gained may not be used to create a substantially similar program.
- Mods and fixes made of own code that hook into the user's game are common practice; still, there is no guarantee.
  The project may ask the rights holder (THQ Nordic) for permission.
