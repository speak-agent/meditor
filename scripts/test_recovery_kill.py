#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Sunrisepeak & mcpp-community
"""Kill-loop recovery evidence (R11): repeated SIGKILL of the real GUI process
with an unsaved buffer, verifying crash recovery on every relaunch.

Usage: python3 scripts/test_recovery_kill.py [iterations]
Requires a graphical session (the GUI runs for real)."""

from __future__ import annotations
import json
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BIN = ROOT / "target" / "dist" / "dist" / "bin" / "meditor"


class ServeSession:
    """One serve --stdio child per round: attaches to the GUI workspace session (R04)."""

    def __init__(self, binary: Path, workspace: Path):
        self.process = subprocess.Popen(
            [str(binary), "--cli", "serve", "--stdio", "--workspace", str(workspace), "--trust"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            text=True, bufsize=1)
        self.sequence = 0

    def __call__(self, command: str, params: dict | None = None) -> dict:
        self.sequence += 1
        assert self.process.stdin and self.process.stdout
        self.process.stdin.write(json.dumps(
            {"id": self.sequence, "command": command, "params": params or {}}) + "\n")
        self.process.stdin.flush()
        reply_line = self.process.stdout.readline()
        if not reply_line:
            raise RuntimeError(f"serve closed after {command}")
        reply = json.loads(reply_line)
        if not reply.get("ok"):
            print(f"serve {command} failed: {json.dumps(reply)[:220]}")
        return reply

    def close(self) -> None:
        try:
            if self.process.stdin:
                self.process.stdin.close()
            self.process.wait(timeout=10)
        except Exception:
            self.process.kill()


def main() -> int:
    iterations = int(sys.argv[1]) if len(sys.argv) > 1 else 20
    if not BIN.is_file():
        print(f"missing packaged binary: {BIN}")
        return 1
    failures = 0
    with tempfile.TemporaryDirectory(prefix="meditor-kill-") as outer:
        # The packaged build is portable: its data root must stay inside the installation, so the
        # loop works on one package copy and wipes its data/ between rounds.
        package = Path(outer) / "package"
        shutil.copytree(BIN.parent.parent, package)
        binary = package / "bin" / "meditor"
        for round_index in range(iterations):
            root = Path(outer) / f"round-{round_index}"
            workspace = root / "project"
            workspace.mkdir(parents=True)
            document = workspace / "main.cppm"
            document.write_text("export module sample;\n")
            shutil.rmtree(package / "data", ignore_errors=True)
            marker = f"KILL-LOOP-{round_index}"
            gui = subprocess.Popen(
                [str(binary), "--workspace", str(workspace), "--open", "main.cppm"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            session: ServeSession | None = None
            try:
                deadline = time.monotonic() + 20
                opened = None
                while time.monotonic() < deadline:
                    try:
                        if session is None:
                            session = ServeSession(binary, workspace)
                        opened = session("document.open", {"path": "main.cppm"})
                        if opened.get("ok"):
                            break
                    except Exception:
                        if session is not None:
                            session.close()
                            session = None
                        time.sleep(0.4)
                if opened is None or not opened.get("ok"):
                    print(f"round {round_index}: attach/open failed: {opened}")
                    failures += 1
                    continue
                document_id = opened["data"]["document"]
                revision = opened["data"]["revision"]
                edited = session("document.edit", {
                    "document": document_id, "revision": revision,
                    "edits": [{"start": 0, "end": 0, "text": marker}]})
                if not edited.get("ok"):
                    print(f"round {round_index}: edit failed: {edited}")
                    failures += 1
                    continue
            finally:
                if session is not None:
                    session.close()
                gui.send_signal(signal.SIGKILL)
                gui.wait(timeout=10)
            # Relaunch and verify the unsaved marker came back through recovery.
            relaunched = subprocess.Popen(
                [str(binary), "--workspace", str(workspace), "--open", "main.cppm"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            try:
                recovered = None
                deadline = time.monotonic() + 20
                verify: ServeSession | None = None
                while time.monotonic() < deadline:
                    try:
                        if verify is None:
                            verify = ServeSession(binary, workspace)
                        text = verify("document.read", {"document": document_id})
                        if text.get("ok") and marker in text["data"]["text"]:
                            recovered = {"recovered": [document_id]}
                            break
                        time.sleep(0.4)
                    except Exception:
                        if verify is not None:
                            verify.close()
                            verify = None
                        time.sleep(0.4)
                if verify is not None:
                    verify.close()
                if not recovered:
                    print(f"round {round_index}: recovery did not restore {marker}")
                    failures += 1
            finally:
                relaunched.terminate()
                try:
                    relaunched.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    relaunched.kill()
                    relaunched.wait(timeout=10)
            print(f"round {round_index}: kill+recover ok" if not failures else f"round {round_index}: see above")
    if failures:
        print(f"kill-loop recovery: {failures}/{iterations} rounds FAILED")
        return 1
    print(f"kill-loop recovery: {iterations}/{iterations} rounds passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
