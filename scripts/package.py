#!/usr/bin/env python3
"""Packages a built BeyondMastery.dll into the release archives.

    scripts/package.py [--dll build/release/BeyondMastery.dll] [--out dist] [--tag v1.2.3] [--check-only]

Writes two archives, named after the version in CMakeLists.txt (the single
source of the version):

    BeyondMastery-<version>.zip               rooted at Data/: for Nexus and mod
                                              managers, or to extract into Data/
    BeyondMastery-<version>-SKSE-Plugins.zip  rooted at Data/SKSE/Plugins/: to
                                              extract straight into that folder

Both hold the DLL plus the license and readme in a BeyondMastery/ folder beside
it. No INI: the plugin writes its own on first start, and shipping one would
overwrite the player's settings on every update.

With --tag, the tag must be v<version>, so a release can never carry a DLL that
reports another version. The archives are reproducible: fixed timestamps and
order, so the same DLL always gives byte-identical zips.
"""

import argparse
import pathlib
import re
import sys
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
NAME = "BeyondMastery"
# 1980-01-01, the earliest date a zip entry can carry.
FIXED_TIME = (1980, 1, 1, 0, 0, 0)


def project_version(cmakelists: str) -> str:
    """The VERSION of the project() call in CMakeLists.txt."""
    m = re.search(r"project\s*\([^)]*?\bVERSION\s+(\d+\.\d+\.\d+)", cmakelists, re.S)
    if not m:
        sys.exit("package: no project(... VERSION x.y.z) in CMakeLists.txt")
    return m.group(1)


def check_tag(tag: str, version: str) -> None:
    if tag != f"v{version}":
        sys.exit(f"package: tag {tag} does not match the project version {version} "
                 f"(expected v{version}; bump VERSION in CMakeLists.txt or retag)")


def _add(z: zipfile.ZipFile, arcname: str, data: bytes) -> None:
    info = zipfile.ZipInfo(arcname, FIXED_TIME)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o644 << 16
    z.writestr(info, data)


def build_archives(dll: pathlib.Path, version: str, out: pathlib.Path,
                   root: pathlib.Path = ROOT) -> list[pathlib.Path]:
    if not dll.is_file():
        sys.exit(f"package: {dll} not found; run scripts/build.sh first")
    files = {
        f"{NAME}.dll": dll.read_bytes(),
        f"{NAME}/LICENSE": (root / "LICENSE").read_bytes(),
        f"{NAME}/README.md": (root / "README.md").read_bytes(),
    }
    out.mkdir(parents=True, exist_ok=True)
    archives = []
    for archive, prefix in ((f"{NAME}-{version}.zip", "SKSE/Plugins/"),
                            (f"{NAME}-{version}-SKSE-Plugins.zip", "")):
        path = out / archive
        with zipfile.ZipFile(path, "w") as z:
            for arcname in sorted(files):
                _add(z, prefix + arcname, files[arcname])
        archives.append(path)
    return archives


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dll", type=pathlib.Path, default=ROOT / "build" / "release" / f"{NAME}.dll")
    ap.add_argument("--out", type=pathlib.Path, default=ROOT / "dist")
    ap.add_argument("--tag", help="release tag; must be v<version>")
    ap.add_argument("--check-only", action="store_true",
                    help="only check --tag against the version, before a long build")
    args = ap.parse_args(argv)

    version = project_version((ROOT / "CMakeLists.txt").read_text())
    if args.tag:
        check_tag(args.tag, version)
    if args.check_only:
        print(f"package: version {version}" + (f", tag {args.tag} matches" if args.tag else ""))
        return 0
    for path in build_archives(args.dll, version, args.out):
        print(f"package: {path} ({path.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
