# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Sunrisepeak & mcpp-community
"""Check public sources and documentation without publishing machine-specific diagnostics."""
from pathlib import Path
import os
import re
import subprocess

root = Path(__file__).resolve().parents[1]
paths = subprocess.check_output(["git", "ls-files", "-z"], cwd=root).decode().split("\0")
checked = 0
for name in filter(None, paths):
    path = root / name
    if not path.is_file() or path.suffix.lower() in {".png", ".ico", ".jpg", ".zip", ".gz"}:
        continue
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeError:
        continue
    for private in filter(None, [os.environ.get("HOME"), os.environ.get("USERPROFILE")]):
        if len(private) > 4 and private in text:
            raise SystemExit(f"Private machine path in tracked file: {name}")
    if re.search(r"\b(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,})", text):
        raise SystemExit(f"Credential-shaped value in tracked file: {name}")
    if path.suffix == ".md" and "/skills/" not in name:
        if sum(line.startswith("```") for line in text.splitlines()) % 2:
            raise SystemExit(f"Unclosed code fence: {name}")
        for link in re.findall(r"\]\(([^)]+)\)", text):
            if link.startswith(("http://", "https://", "#")):
                continue
            if not (path.parent / link.split("#", 1)[0]).exists():
                raise SystemExit(f"Broken local link: {name}")
    checked += 1
print(f"Public source and document checks passed ({checked} text files).")
