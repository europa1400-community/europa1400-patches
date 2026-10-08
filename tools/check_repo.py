"""Repository rules that a machine can check (CI and before every commit).

- no game files or recordings: no tracked binaries (.dll/.exe/.e1rec/...), no file above 1 MB outside vendor/
- no large byte arrays in sources (a hint for copied game data); see docs/legal.md
- VERSION is semantic; on a tag build the tag must be v<VERSION>
- build tables: complete [build] section, 64 hex digit sha256, well-formed symbols
- patch manifests: id = folder, module = <id>.dll, api within the loader's version, known targets, every listed build
  exists, every declared symbol exists in every listed build of its target, requires/conflicts name known patches

    uv run tools/check_repo.py
"""

from __future__ import annotations

import configparser
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BINARY_SUFFIXES = {".dll", ".exe", ".asi", ".e1rec", ".bin", ".dat", ".sav", ".pdb", ".lib", ".obj", ".zip", ".7z", ".rar"}
TARGETS = {"server", "game"}
BYTE_ARRAY = re.compile(r"(0x[0-9a-fA-F]{2}\s*,\s*){256,}")


def ini(path: Path) -> configparser.ConfigParser:
    parser = configparser.ConfigParser(interpolation=None, inline_comment_prefixes=(";",), strict=False)
    parser.optionxform = str
    parser.read(path, encoding="utf-8")
    return parser


def tracked_files() -> list[Path]:
    try:
        out = subprocess.run(["git", "ls-files"], cwd=ROOT, capture_output=True, text=True, check=True).stdout
        return [ROOT / line for line in out.splitlines() if line]
    except (OSError, subprocess.CalledProcessError):
        return [p for p in ROOT.rglob("*") if p.is_file() and ".git" not in p.parts]


def api_version() -> int:
    text = (ROOT / "include" / "e1400patch" / "patch_api.h").read_text(encoding="utf-8")
    return int(re.search(r"#define E1400_PATCH_API_VERSION (\d+)", text).group(1))


def main() -> int:
    errors: list[str] = []

    for path in tracked_files():
        relative = path.relative_to(ROOT).as_posix()
        if relative.startswith("vendor/") or not path.exists():
            continue
        if path.suffix.lower() in BINARY_SUFFIXES:
            errors.append(f"{relative}: binary/game file type must not be committed")
        if path.stat().st_size > 1_000_000:
            errors.append(f"{relative}: larger than 1 MB")
        if path.suffix.lower() in {".c", ".h", ".cpp", ".inc"}:
            if BYTE_ARRAY.search(path.read_text(encoding="utf-8", errors="replace")):
                errors.append(f"{relative}: contains a large byte array (game data? see docs/legal.md)")

    version = (ROOT / "VERSION").read_text().strip()
    if not re.fullmatch(r"\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?", version):
        errors.append(f"VERSION {version!r} is not semantic")
    ref = os.environ.get("GITHUB_REF", "")
    if ref.startswith("refs/tags/") and ref != f"refs/tags/v{version}":
        errors.append(f"tag {ref} does not match VERSION {version}")

    builds: dict[str, tuple[str, set[str]]] = {}
    for path in sorted((ROOT / "builds").glob("*.ini")):
        table = ini(path)
        if not table.has_section("build"):
            errors.append(f"builds/{path.name}: no [build] section")
            continue
        build = table["build"]
        build_id = build.get("id", "")
        if path.stem != build_id:
            errors.append(f"builds/{path.name}: id {build_id!r} differs from the file name")
        if build.get("target") not in TARGETS:
            errors.append(f"builds/{path.name}: unknown target {build.get('target')!r}")
        if not re.fullmatch(r"[0-9a-f]{64}", build.get("sha256", "")):
            errors.append(f"builds/{path.name}: sha256 must be 64 lowercase hex digits")
        symbols = set()
        for name, value in (table["symbols"].items() if table.has_section("symbols") else []):
            if not re.fullmatch(r"0x[0-9a-f]{8},0x[0-9a-f]{8}", value):
                errors.append(f"builds/{path.name}: symbol {name} = {value!r} (expected 0xRVA,0xSIGNATURE)")
            symbols.add(name)
        builds[build_id] = (build.get("target", ""), symbols)

    api = api_version()
    manifests: dict[str, configparser.ConfigParser] = {}
    for path in sorted(ROOT.glob("patches/*/patch.ini")):
        manifests[path.parent.name] = ini(path)
    for folder, manifest in manifests.items():
        where = f"patches/{folder}/patch.ini"
        if not manifest.has_section("patch"):
            errors.append(f"{where}: no [patch] section")
            continue
        patch = manifest["patch"]
        if patch.get("id") != folder:
            errors.append(f"{where}: id must be the folder name {folder!r}")
        if patch.get("module") != f"{folder}.dll":
            errors.append(f"{where}: module must be {folder}.dll")
        if not re.fullmatch(r"\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?", patch.get("version", "")):
            errors.append(f"{where}: version must be semantic")
        if not patch.get("api", "").isdigit() or not 1 <= int(patch.get("api")) <= api:
            errors.append(f"{where}: api must be 1..{api}")
        targets = {t.strip() for t in patch.get("targets", "").split(",") if t.strip()}
        if not targets or targets - TARGETS:
            errors.append(f"{where}: targets {sorted(targets)} (allowed: {sorted(TARGETS)})")
        if not (ROOT / "patches" / folder / "CMakeLists.txt").exists():
            errors.append(f"patches/{folder}: CMakeLists.txt missing")
        for other in [r.strip() for key in ("requires", "conflicts") for r in patch.get(key, "").split(",") if r.strip()]:
            if other not in manifests and other != "*":
                errors.append(f"{where}: unknown patch {other!r} in requires/conflicts")
        for target in targets:
            listed = [b.strip() for b in manifest.get("builds", target, fallback="").split(",") if b.strip()]
            wanted = [s.strip() for s in manifest.get("symbols", target, fallback="").split(",") if s.strip()]
            if not listed:
                errors.append(f"{where}: [builds] {target}= is empty")
            for build_id in listed:
                if build_id == "*":
                    if wanted:
                        errors.append(f"{where}: '*' builds cannot be combined with symbols")
                    continue
                if build_id not in builds:
                    errors.append(f"{where}: unknown build {build_id!r}")
                    continue
                build_target, symbols = builds[build_id]
                if build_target != target:
                    errors.append(f"{where}: build {build_id} is a {build_target} build, not {target}")
                for name in wanted:
                    if name not in symbols:
                        errors.append(f"{where}: symbol {name} missing in builds/{build_id}.ini")

    for error in errors:
        print(f"error: {error}")
    print(f"{len(builds)} build table(s), {len(manifests)} patch(es): {'FAIL' if errors else 'OK'}")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
