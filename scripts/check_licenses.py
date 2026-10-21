# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Sunrisepeak & mcpp-community
"""Check the project's confirmed license without relabeling third-party inputs."""
import hashlib
from pathlib import Path
import subprocess
import tomllib

ROOT = Path(__file__).resolve().parents[1]
SPDX = "SPDX-License-Identifier: GPL-3.0-only"
CANONICAL_GPL_SHA256 = "3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986"


def main():
    if hashlib.sha256((ROOT / "LICENSE").read_bytes()).hexdigest() != CANONICAL_GPL_SHA256:
        raise SystemExit("LICENSE must preserve the canonical GNU GPL version 3 text")
    policy = (ROOT / "LICENSING.md").read_text(encoding="utf-8")
    for value in ["GPL-3.0-only", "Sunrisepeak & mcpp-community", "speakshen@163.com"]:
        if value not in policy:
            raise SystemExit("Project licensing policy is incomplete")
    files = subprocess.check_output(["git", "ls-files", "-z"], cwd=ROOT).decode().split("\0")
    source_count = package_count = 0
    for name in filter(None, files):
        if name.startswith(".agents/skills/"):
            continue
        path = ROOT / name
        if path.name == "mcpp.toml":
            package = tomllib.loads(path.read_text(encoding="utf-8")).get("package", {})
            if package.get("license") != "GPL-3.0-only":
                raise SystemExit(f"Project package license mismatch: {name}")
            package_count += 1
        if path.suffix in {".cpp", ".cppm", ".mcpp", ".py"}:
            if not any(SPDX in line for line in path.read_text(encoding="utf-8").splitlines()[:4]):
                raise SystemExit(f"Missing project source license identifier: {name}")
            source_count += 1
    print(f"GPL-3.0-only project declarations passed ({package_count} packages, {source_count} sources); third-party inputs retain their licenses.")


if __name__ == "__main__":
    main()
