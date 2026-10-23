# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Sunrisepeak & mcpp-community
"""Resolve the GUI branch before building metadata that embeds its lock record."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
import tomllib
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
SOURCE = "git+https://github.com/Sunrisepeak/HuxerUI.git#branch=meditor@"


def main():
    manifest = tomllib.loads((ROOT / "mcpp.toml").read_text(encoding="utf-8"))
    dependency = manifest["dependencies"]["huxerui"]["huxerui"]
    if dependency != {"git": "https://github.com/Sunrisepeak/HuxerUI.git", "branch": "meditor"}:
        raise ValueError("Unexpected GUI dependency declaration")
    toolchain = tomllib.loads((ROOT / "mcpp.toml").read_text(encoding="utf-8"))["workspace"]["toolchain"]["default"]
    target = ROOT / "target"
    target.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="gui-dependency-update-", dir=target) as directory:
        work = Path(directory)
        (work / "src").mkdir()
        (work / "src/main.cpp").write_text("import huxerui;\nint main() { return 0; }\n", encoding="utf-8")
        (work / "mcpp.toml").write_text(
            '[package]\nname = "gui-dependency-check"\nversion = "0.0.1"\nstandard = "c++23"\n'
            '[dependencies]\nhuxerui.huxerui = { git = "https://github.com/Sunrisepeak/HuxerUI.git", branch = "meditor" }\n'
            '[toolchain]\ndefault = ' + json.dumps(toolchain) + '\n', encoding="utf-8")
        result = subprocess.run(["mcpp", "build", "--configure-only"], cwd=work, capture_output=True, timeout=300)
        if result.returncode:
            raise ValueError("GUI branch configuration failed; repository metadata was not changed")
        resolved = (work / "mcpp.lock").read_text(encoding="utf-8")
    record = tomllib.loads(resolved)["package"]["huxerui.huxerui"]
    match = re.fullmatch(re.escape(SOURCE) + r"([0-9a-f]{40})", record["source"])
    if not match:
        raise ValueError("Unexpected resolved GUI source")
    revision = match[1]
    with urllib.request.urlopen(f"https://raw.githubusercontent.com/Sunrisepeak/HuxerUI/{revision}/LICENSE", timeout=30) as response:
        license_bytes = response.read(1024 * 1024 + 1)
    inventory_path = ROOT / "resources/licenses/manifest.json"
    inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
    notices = [item for item in inventory["components"] if item["id"] == "HuxerUI"]
    if len(notices) != 1:
        raise ValueError("Expected one GUI license record")
    notice = notices[0]
    if license_bytes != (ROOT / "resources/licenses/HuxerUI-LICENSE.txt").read_bytes() or hashlib.sha256(license_bytes).hexdigest() != notice["sha256"]:
        raise ValueError("Upstream GUI license changed; review its notice before updating")
    notice["revision"] = revision
    pattern = r'\[package\."huxerui\.huxerui"\]\n.*?(?=\n\[|\Z)'
    blocks = re.findall(pattern, resolved, re.S)
    if len(blocks) != 1:
        raise ValueError("Expected one generated GUI lock record")
    lock_path = ROOT / "mcpp.lock"
    current = lock_path.read_text(encoding="utf-8")
    if len(re.findall(pattern, current, re.S)) > 1:
        raise ValueError("Ambiguous current GUI lock record")
    if re.search(pattern, current, re.S):
        updated = re.sub(pattern, blocks[0].rstrip() + "\n", current, flags=re.S)
    else:
        updated = current.rstrip() + "\n\n" + blocks[0].rstrip() + "\n"
    expected = tomllib.loads(current)
    expected["package"]["huxerui.huxerui"] = record
    if tomllib.loads(updated) != expected:
        raise ValueError("GUI update changed unrelated lock records")
    lock_path.write_text(updated, encoding="utf-8")
    inventory_path.write_text(json.dumps(inventory, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("GUI branch resolution and matching license record updated. Build and test before committing.")


if __name__ == "__main__":
    try:
        main()
    except Exception:
        raise SystemExit("GUI dependency update failed. Check branch access, mcpp configuration and license compatibility locally.")
