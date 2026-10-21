# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Sunrisepeak & mcpp-community
"""Validate acceptance evidence; --release refuses every unproven required platform criterion."""
import argparse
import json
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--release", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
report = json.loads((root / "tests/acceptance.json").read_text())
platforms = {"linux", "windows", "android", "macos"}
assert report["schemaVersion"] == 1 and set(report["requiredPlatforms"]) == platforms
assert {row["id"] for row in report["criteria"]} == {f"A{i:02}" for i in range(1, 14)}
assert len(report["criteria"]) == 13
pending = []
for row in report["criteria"]:
    assert set(row["platforms"]) == platforms
    for platform, entry in row["platforms"].items():
        assert entry["status"] in {"passed", "failed", "not-tested", "blocked"}
        assert isinstance(entry["evidence"], list)
        if entry["status"] == "passed":
            assert entry["evidence"], (row["id"], platform, "Missing evidence")
            for evidence in entry["evidence"]:
                for field in ["commit", "artifactSha256", "environment", "procedure", "result", "reference"]:
                    assert evidence.get(field), (row["id"], platform, field)
                assert len(evidence["artifactSha256"]) == 64
        else:
            pending.append(f'{platform}:{row["id"]}')
print(f"Acceptance contract validated: {52 - len(pending)}/52 platform criteria passed.")
if args.release and pending:
    raise SystemExit("Release blocked: required product acceptance remains unproven (" + ", ".join(pending) + ").")
