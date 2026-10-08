"""Package the staged build into release archives (dist/).

Every archive is meant to be extracted into the game directory:

    e1400patch.zip            e1400patch/  (loader, shim, tool, build tables, default configuration, licenses)
    <patch id>.zip            patches/<id>/ (manifest, module, data)
    <mod id>.zip              mods/<id>/
    e1400patch-symbols.zip    PDB files for crash analysis (not for players)
    release.json              machine readable list of the packages (for the database/manager)
    SHA256SUMS.txt

Each archive is also written with its version in the name (<name>-<version>.zip); the unversioned names are the stable
"latest" download links.

    uv run tools/package.py [--build build] [--out dist]
"""

from __future__ import annotations

import argparse
import configparser
import hashlib
import json
import shutil
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SKIP_SUFFIXES = {".pdb", ".ilk", ".lib", ".exp"}


def manifest(path: Path) -> configparser.ConfigParser:
    parser = configparser.ConfigParser(interpolation=None, inline_comment_prefixes=(";",))
    parser.read(path, encoding="utf-8")
    return parser


def add_tree(archive: zipfile.ZipFile, source: Path, prefix: str) -> None:
    for file in sorted(source.rglob("*")):
        if file.is_file() and file.suffix.lower() not in SKIP_SUFFIXES:
            archive.write(file, f"{prefix}/{file.relative_to(source).as_posix()}")


def write_zip(out: Path, name: str, version: str, fill) -> list[Path]:
    written = []
    for file_name in (f"{name}.zip", f"{name}-{version}.zip"):
        path = out / file_name
        with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
            fill(archive)
        written.append(path)
    return written


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", default="build")
    parser.add_argument("--out", default="dist")
    args = parser.parse_args()

    stage = ROOT / args.build / "stage"
    out = ROOT / args.out
    loader = stage / "e1400patch"
    if not (loader / "server.dll").exists():
        print(f"no staged loader in {loader}; build first", file=sys.stderr)
        return 1
    if out.resolve() in (ROOT, ROOT / "package"):
        print(f"refusing to write into {out}", file=sys.stderr)
        return 1
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    version = (ROOT / "VERSION").read_text().strip()
    release = {"version": version, "packages": []}
    written: list[Path] = []

    def fill_loader(archive: zipfile.ZipFile) -> None:
        add_tree(archive, loader, "e1400patch")
        archive.write(ROOT / "LICENSE", "e1400patch/LICENSE.txt")
        archive.write(ROOT / "vendor" / "minhook" / "LICENSE.txt", "e1400patch/THIRD-PARTY-MinHook.txt")
        archive.write(ROOT / "package" / "INSTALL.txt", "e1400patch/INSTALL.txt")

    written += write_zip(out, "e1400patch", version, fill_loader)
    release["packages"].append({"id": "e1400patch", "kind": "loader", "version": version, "archive": "e1400patch.zip"})

    for kind_dir in ("patches", "mods"):
        for directory in sorted((stage / kind_dir).glob("*")) if (stage / kind_dir).exists() else []:
            if not (directory / "patch.ini").exists():
                continue
            info = manifest(directory / "patch.ini")
            patch = info["patch"]
            patch_id, patch_version = patch.get("id"), patch.get("version", "0.0.0")
            written += write_zip(
                out, patch_id, patch_version, lambda archive, d=directory, k=kind_dir, i=patch_id: add_tree(archive, d, f"{k}/{i}")
            )
            release["packages"].append(
                {
                    "id": patch_id,
                    "kind": patch.get("kind", "patch"),
                    "name": patch.get("name", patch_id),
                    "version": patch_version,
                    "targets": patch.get("targets", ""),
                    "builds": dict(info["builds"]) if info.has_section("builds") else {},
                    "requires": ["e1400patch"] + [r.strip() for r in patch.get("requires", "").split(",") if r.strip()],
                    "archive": f"{patch_id}.zip",
                }
            )

    symbols = out / f"e1400patch-symbols-{version}.zip"
    with zipfile.ZipFile(symbols, "w", zipfile.ZIP_DEFLATED) as archive:
        for pdb in sorted(stage.rglob("*.pdb")):
            archive.write(pdb, pdb.relative_to(stage).as_posix())
    written.append(symbols)

    (out / "release.json").write_text(json.dumps(release, indent=2) + "\n", encoding="utf-8")
    written.append(out / "release.json")
    sums = [f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}" for path in written]
    (out / "SHA256SUMS.txt").write_text("\n".join(sums) + "\n", encoding="utf-8")
    for path in written:
        print(path.relative_to(ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
