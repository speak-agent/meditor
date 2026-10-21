# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Sunrisepeak & mcpp-community
"""Refuse artifacts containing this build machine's private path or host identity."""
import argparse
import mmap
import re
from pathlib import Path
import shutil
import socket

PERSONAL_PATH = re.compile(
    rb"(?<![A-Za-z0-9_./])(?:file://)?(?:/(?:home|Users)/|[A-Za-z]:[\\/](?:Users|Documents and Settings)[\\/])([^\x00/\\\r\n]{1,128})[\\/]",
    re.IGNORECASE,
)
WIDE_PATH_START = re.compile(
    b"|".join(re.escape(prefix.encode("utf-16-le")) for prefix in [
        "/home/", "/Users/", "\\Users\\", "/Documents and Settings/", "\\Documents and Settings\\",
    ]),
    re.IGNORECASE,
)


def private_needles():
    paths = [Path.home(), Path(__file__).resolve().parents[1]]
    executable = shutil.which("mcpp")
    if executable:
        tool_root = Path(executable).resolve().parent.parent
        # Standard system prefixes are public runtime paths, not machine identities.
        if tool_root.as_posix() not in {"/usr", "/usr/local", "/"}:
            paths.append(tool_root)
    values = set()
    for path in paths:
        for value in [str(path), path.as_posix()]:
            if len(value) > 5:
                values.add(value)
    host = socket.gethostname()
    if len(host) >= 8:
        values.add(host)
    return [(value.encode(encoding), "host identity" if value == host else "build path")
            for value in sorted(values) for encoding in ["utf-8", "utf-16-le"]]


def personal_build_path(data):
    placeholders = {b"runner", b"runneradmin", b"actions", b"build", b"buildd", b"vsts", b"user", b"username", b"example", b"yourname"}
    for match in PERSONAL_PATH.finditer(data):
        name = match[1].lower()
        if name not in placeholders and not (name.startswith(b"<") and name.endswith(b">")) and not name.startswith(b"$"):
            return True
    return False


def wide_personal_build_path(data):
    # Decode bounded windows around path markers, rather than allocating a text
    # copy of each large binary. Keep preceding characters for URL boundaries.
    for match in WIDE_PATH_START.finditer(data):
        start = max(match.start() - 32, match.start() % 2)
        end = min(len(data), match.end() + 1024)
        end -= (end - start) % 2
        window = data[start:end].decode("utf-16-le", errors="replace").encode("utf-8")
        if personal_build_path(window):
            return True
    return False


def scan(directory):
    directory = directory.resolve()
    needles = private_needles()
    failures = []
    count = 0
    for path in sorted(directory.rglob("*")):
        if path.is_symlink():
            if not path.resolve().is_relative_to(directory):
                failures.append((path.relative_to(directory).as_posix(), "external symlink"))
            continue
        if not path.is_file():
            continue
        count += 1
        if path.stat().st_size == 0:
            continue
        with path.open("rb") as file, mmap.mmap(file.fileno(), 0, access=mmap.ACCESS_READ) as data:
            for needle, kind in needles:
                if data.find(needle) >= 0:
                    failures.append((path.relative_to(directory).as_posix(), kind))
                    break
            else:
                if personal_build_path(data) or wide_personal_build_path(data):
                    failures.append((path.relative_to(directory).as_posix(), "personal build path"))
    return count, failures


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--directory", type=Path, required=True)
    args = parser.parse_args()
    directory = args.directory.resolve()
    if not directory.is_dir():
        raise SystemExit("Artifact directory does not exist")
    count, failures = scan(directory)
    if failures:
        for name, kind in failures[:20]:
            print(f"Private {kind} remains in artifact: {name!r}")
        raise SystemExit(f"Artifact privacy failed: {len(failures)} files require correction; nothing was uploaded")
    print(f"Artifact build-identity privacy check passed ({count} files)")


if __name__ == "__main__":
    main()
