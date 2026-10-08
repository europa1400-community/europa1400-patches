"""Create or refresh a build table (builds/<id>.ini) for a game file.

The table names functions of one exact build: symbol -> RVA plus a signature (FNV-1a of the first 16 file bytes, so
no game bytes end up in the repository). Names and addresses come from the private decompilation repository
(symbols/<PROGRAM>/functions.csv); only the symbols listed in builds/symbols/<target>.txt are exported.

    uv run tools/build_table.py --target server --id server-de-2.06 \
        --file "%GAME_DIR%/Server/server.dll" --program SERVER \
        --description "Die Gilde Gold 2.06 DE (GOG, Steam): server.dll"

DECOMP_DIR and GAME_DIR are read from .env when not given.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import os
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SIGNATURE_BYTES = 16


def load_env() -> dict[str, str]:
    values: dict[str, str] = {}
    env = ROOT / ".env"
    if env.exists():
        for line in env.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if line and not line.startswith("#") and "=" in line:
                key, value = line.split("=", 1)
                values[key.strip()] = value.strip().strip('"')
    values.update({k: v for k, v in os.environ.items() if k in ("GAME_DIR", "DECOMP_DIR")})
    return values


def fnv1a(data: bytes) -> int:
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


class PeFile:
    def __init__(self, data: bytes) -> None:
        self.data = data
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        if data[:2] != b"MZ" or data[pe : pe + 4] != b"PE\0\0":
            raise ValueError("not a PE file")
        machine, sections, _, _, _, optional_size, _ = struct.unpack_from("<HHIIIHH", data, pe + 4)
        if machine != 0x14C:
            raise ValueError("not a 32-bit x86 PE file")
        optional = pe + 24
        self.image_base = struct.unpack_from("<I", data, optional + 28)[0]
        self.sections = []
        table = optional + optional_size
        for i in range(sections):
            entry = table + 40 * i
            virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from("<IIII", data, entry + 8)
            self.sections.append((virtual_address, raw_size, raw_pointer))
        # bytes of relocated absolute addresses (HIGHLOW): masked in signatures, they differ between builds
        self.relocated: set[int] = set()
        reloc_rva, reloc_size = struct.unpack_from("<II", data, optional + 96 + 5 * 8)
        block = self.at(reloc_rva, reloc_size) if reloc_rva else None
        position = 0
        while block and position + 8 <= len(block):
            page, size = struct.unpack_from("<II", block, position)
            if size < 8:
                break
            for i in range((size - 8) // 2):
                entry = struct.unpack_from("<H", block, position + 8 + 2 * i)[0]
                if entry >> 12 == 3:
                    self.relocated.update(range(page + (entry & 0xFFF), page + (entry & 0xFFF) + 4))
            position += size

    def signature(self, rva: int) -> int | None:
        window = self.at(rva, SIGNATURE_BYTES)
        if window is None:
            return None
        return fnv1a(bytes(0 if rva + i in self.relocated else b for i, b in enumerate(window)))

    def at(self, rva: int, size: int) -> bytes | None:
        for virtual_address, raw_size, raw_pointer in self.sections:
            if virtual_address <= rva and rva - virtual_address + size <= raw_size:
                offset = raw_pointer + rva - virtual_address
                return self.data[offset : offset + size]
        return None


def main() -> int:
    env = load_env()
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--target", required=True, choices=["server", "game"])
    parser.add_argument("--id", required=True, help="build id, e.g. server-de-2.06")
    parser.add_argument("--file", required=True, help="the game file of this build")
    parser.add_argument("--program", required=True, help="program key in the decompilation repo, e.g. SERVER")
    parser.add_argument("--description", default="")
    parser.add_argument("--decomp", default=env.get("DECOMP_DIR"), help="decompilation repository (default: DECOMP_DIR)")
    parser.add_argument("--symbols", help="symbol list (default: builds/symbols/<target>.txt)")
    args = parser.parse_args()

    if not args.decomp:
        parser.error("--decomp or DECOMP_DIR (.env) is required")
    path = Path(os.path.expandvars(args.file.replace("%GAME_DIR%", env.get("GAME_DIR", ""))))
    data = path.read_bytes()
    image = PeFile(data)
    wanted = [
        line.split("#", 1)[0].strip()
        for line in Path(args.symbols or ROOT / "builds" / "symbols" / f"{args.target}.txt").read_text().splitlines()
    ]
    wanted = [name for name in wanted if name]

    functions: dict[str, int] = {}
    with open(Path(args.decomp) / "symbols" / args.program / "functions.csv", newline="", encoding="utf-8") as file:
        for row in csv.DictReader(file):
            functions.setdefault(row["name"], int(row["address"], 16))

    lines = [
        "; Generated by tools/build_table.py from the decompilation symbols. Names, RVAs and signatures only.",
        "[build]",
        f"id={args.id}",
        f"target={args.target}",
        f"file={path.name}",
        f"sha256={hashlib.sha256(data).hexdigest()}",
        f"description={args.description}",
        "[symbols]",
    ]
    missing = []
    for name in wanted:
        if name not in functions:
            missing.append(name)
            continue
        rva = functions[name] - image.image_base
        signature = image.signature(rva)
        if signature is None:
            missing.append(name)
            continue
        lines.append(f"{name}=0x{rva:08x},0x{signature:08x}")
    out = ROOT / "builds" / f"{args.id}.ini"
    out.write_text("\n".join(lines) + "\n", encoding="ascii", newline="\r\n")
    print(f"{out}: {len(wanted) - len(missing)} symbol(s)")
    for name in missing:
        print(f"  not found: {name}", file=sys.stderr)
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
