# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Sunrisepeak & mcpp-community
"""Locate the built native targets without hardcoding a host triple or machine path."""
from pathlib import Path
import os
import argparse
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument("--native-tools", action="store_true")
args = parser.parse_args()

root = Path(__file__).resolve().parents[1]
suffix = ".exe" if os.name == "nt" else ""
binaries = {}
for name in (["meditor"] if args.native_tools else ["meditor", "meditor-test-tool"]):
    candidates = list((root / "target").glob(f"**/bin/{name}{suffix}"))
    candidates = [p for p in candidates if "dist" not in p.parts and "candidates" not in p.parts]
    if not candidates:
        raise SystemExit(f"Missing built target: {name}")
    binaries[name] = max(candidates, key=lambda p: p.stat().st_mtime)
for license_name in ["LICENSE", "LICENSING.md"]:
    installed = binaries["meditor"].parent / "licenses/meditor" / license_name
    if not installed.is_file() or installed.read_bytes() != (root / license_name).read_bytes():
        raise SystemExit("Native CLI project license deployment is missing or changed: " + license_name)
for name in ["HuxerUI-LICENSE.txt", "nlohmann-json-LICENSE.txt", "manifest.json"]:
    installed = binaries["meditor"].parent / "licenses/dependencies" / name
    if not installed.is_file() or installed.read_bytes() != (root / "resources/licenses" / name).read_bytes():
        raise SystemExit("Native CLI direct dependency notice deployment is missing or changed: " + name)
command = [sys.executable, str(root / ("scripts/test_native_tools.py" if args.native_tools else "scripts/test_cli.py")), "--binary", str(binaries["meditor"])]
if not args.native_tools:
    command += ["--fixture", str(binaries["meditor-test-tool"])]
subprocess.run(command, cwd=root, check=True, timeout=1800 if args.native_tools else 120)
