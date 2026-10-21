"""Locate the built native targets without hardcoding a host triple or machine path."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
suffix = ".exe" if os.name == "nt" else ""
binaries = {}
for name in ["meditor", "meditor-test-tool"]:
    candidates = list((root / "target").glob(f"**/bin/{name}{suffix}"))
    candidates = [p for p in candidates if "dist" not in p.parts]
    if not candidates:
        raise SystemExit(f"Missing built target: {name}")
    binaries[name] = max(candidates, key=lambda p: p.stat().st_mtime)
subprocess.run([sys.executable, str(root / "scripts/test_cli.py"), "--binary", str(binaries["meditor"]), "--fixture", str(binaries["meditor-test-tool"])], cwd=root, check=True, timeout=120)
