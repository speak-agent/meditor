# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Sunrisepeak & mcpp-community
"""Check the project's confirmed license without relabeling third-party inputs."""
import hashlib
import json
from pathlib import Path
import subprocess
import tomllib

ROOT = Path(__file__).resolve().parents[1]
SPDX = "SPDX-License-Identifier: GPL-3.0-only"
CANONICAL_GPL_SHA256 = "3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986"


def main():
    notice_root = ROOT / "resources/licenses"
    notices = json.loads((notice_root / "manifest.json").read_text(encoding="utf-8"))
    expected = {"HuxerUI": "https://github.com/Sunrisepeak/HuxerUI", "nlohmann.json": "https://github.com/nlohmann/json"}
    if notices.get("schemaVersion") != 1 or {item["id"] for item in notices["components"]} != set(expected) or len(notices["components"]) != 2:
        raise SystemExit("Invalid direct dependency notice inventory")
    lock = tomllib.loads((ROOT / "mcpp.lock").read_text(encoding="utf-8"))["package"]
    for notice in notices["components"]:
        name = notice["file"]
        if not name or name in {".", ".."} or any(ch in name for ch in "/\\:"):
            raise SystemExit("Unsafe dependency notice filename")
        if notice["repository"] != expected[notice["id"]] or notice["license"] != "MIT":
            raise SystemExit("Unexpected dependency notice source or license")
        if hashlib.sha256((notice_root / name).read_bytes()).hexdigest() != notice["sha256"]:
            raise SystemExit("Dependency license text changed: " + notice["id"])
        if notice["id"] == "HuxerUI":
            if lock["huxerui.huxerui"]["source"] != "git+" + notice["repository"] + ".git#branch=meditor@" + notice["revision"]:
                raise SystemExit("Framework notice revision differs from lockfile")
        elif lock["nlohmann.json"]["version"] != notice["revision"]:
            raise SystemExit("JSON notice version differs from lockfile")
    license_file = (ROOT / "LICENSE").read_bytes()
    prefix, separator, body = license_file.partition(b"\n---\n\n")
    if not separator or hashlib.sha256(body).hexdigest() != CANONICAL_GPL_SHA256:
        raise SystemExit("LICENSE must preserve the canonical GNU GPL version 3 text")
    policy = (ROOT / "LICENSING.md").read_text(encoding="utf-8")
    for value in ["GPL-3.0-only", "Sunrisepeak & mcpp-community", "speakshen@163.com"]:
        if value not in policy or value.encode("utf-8") not in prefix:
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
