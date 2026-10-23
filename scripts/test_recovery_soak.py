#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Sunrisepeak & mcpp-community
"""Sustained-session soak (R11): keeps the portable GUI alive for a duration
with periodic edits, then reports RSS, file descriptors and children so leaks
must be explained or fixed.

Usage: python3 scripts/test_recovery_soak.py [minutes] (default 120)"""

from __future__ import annotations
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BIN = ROOT / "target" / "dist" / "dist" / "bin" / "meditor"


def process_stats(pid: int) -> dict:
    def read(path: Path) -> str:
        try:
            return path.read_text().strip()
        except OSError:
            return "?"
    status = Path(f"/proc/{pid}/status")
    children = subprocess.run(["pgrep", "-P", str(pid)], capture_output=True, text=True).stdout.split()
    return {
        "rssKb": next((line.split()[1] for line in read(status).splitlines() if line.startswith("VmRSS")), "?"),
        "threads": next((line.split()[1] for line in read(status).splitlines() if line.startswith("Threads")), "?"),
        "fds": len(os.listdir(f"/proc/{pid}/fd")) if Path(f"/proc/{pid}/fd").exists() else "?",
        "children": len(children),
    }


def main() -> int:
    minutes = int(sys.argv[1]) if len(sys.argv) > 1 else 120
    if not BIN.is_file():
        print(f"missing packaged binary: {BIN}")
        return 1
    with tempfile.TemporaryDirectory(prefix="meditor-soak-") as outer:
        package = Path(outer) / "package"
        shutil.copytree(BIN.parent.parent, package)
        binary = package / "bin" / "meditor"
        workspace = Path(outer) / "project"
        workspace.mkdir()
        (workspace / "main.cppm").write_text("export module sample;\nexport int value() { return 42; }\n")
        gui = subprocess.Popen([str(binary), "--workspace", str(workspace), "--open", "main.cppm"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            session = subprocess.Popen(
                [str(binary), "--cli", "serve", "--stdio", "--workspace", str(workspace), "--trust"],
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, bufsize=1)
            sequence = 0

            def call(command: str, params: dict | None = None) -> dict:
                nonlocal sequence
                sequence += 1
                assert session.stdin and session.stdout
                session.stdin.write(json.dumps({"id": sequence, "command": command, "params": params or {}}) + "\n")
                session.stdin.flush()
                line = session.stdout.readline()
                return json.loads(line) if line else {"ok": False}

            opened = call("document.open", {"path": "main.cppm"})
            if not opened.get("ok"):
                print(f"soak: open failed {opened}")
                return 1
            document = opened["data"]["document"]
            revision = opened["data"]["revision"]
            samples = []
            started = time.monotonic()
            tick = 0
            while time.monotonic() - started < minutes * 60:
                time.sleep(30)
                tick += 1
                edited = call("document.edit", {
                    "document": document, "revision": revision,
                    "edits": [{"start": 0, "end": 0, "text": f"SOAK-{tick} "}]})
                if not edited.get("ok"):
                    print(f"soak: edit {tick} failed {edited}")
                    return 1
                revision = edited["data"]["revision"]
                if tick % 4 == 0:  # snapshots every two minutes
                    stats = process_stats(gui.pid)
                    stats["minutes"] = round((time.monotonic() - started) / 60, 1)
                    samples.append(stats)
                    print(f"soak sample: {json.dumps(stats)}", flush=True)
            final = process_stats(gui.pid)
            final["minutes"] = round((time.monotonic() - started) / 60, 1)
            print(f"soak final: {json.dumps(final)}")
            print(f"soak history: {json.dumps(samples)}")
            print("soak: session survived the full window" if gui.poll() is None else "soak: GUI exited early")
            return 0 if gui.poll() is None else 1
        finally:
            gui.terminate()
            try:
                gui.wait(timeout=10)
            except subprocess.TimeoutExpired:
                gui.kill()
                gui.wait(timeout=10)


if __name__ == "__main__":
    raise SystemExit(main())
