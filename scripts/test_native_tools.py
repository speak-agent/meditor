# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Sunrisepeak & mcpp-community
"""Validate the shared CLI against the pinned, complete upstream tool payloads."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--payload", type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    extra = ["--tool-root", str(args.payload.resolve() / "tools")] if args.payload else []
    with tempfile.TemporaryDirectory(prefix="meditor-native-tools-") as folder:
        root = Path(folder)
        (root / "src").mkdir(); (root / "tests").mkdir()
        (root / "mcpp.toml").write_text('''[package]
name = "sample"
version = "0.0.1"
standard = "c++23"
[toolchain]
default = "llvm@23.1.3"
[targets.sample]
kind = "bin"
main = "src/main.cpp"
[build]
sources = ["src/*.cpp", "src/*.cppm"]
''', encoding="utf-8")
        (root / "src/main.cpp").write_text('import std;\nimport sample.core;\nint main() { std::println("answer={}", answer()); }\n', encoding="utf-8")
        (root / "src/core.cppm").write_text('export module sample.core;\nexport int answer();\n', encoding="utf-8")
        (root / "src/core.cpp").write_text('module sample.core;\nint answer() { return 42; }\n', encoding="utf-8")
        (root / "tests/core.cpp").write_text('import sample.core;\nint main() { return answer() == 42 ? 0 : 1; }\n', encoding="utf-8")
        process = subprocess.Popen([str(binary), "serve", "--stdio", "--workspace", str(root), "--trust", *extra], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8")
        sequence = 0

        def request(command, params=None):
            nonlocal sequence
            sequence += 1
            process.stdin.write(json.dumps({"id": sequence, "command": command, "params": params or {}}) + "\n")
            process.stdin.flush()
            reply = json.loads(process.stdout.readline())
            assert reply["requestId"] == sequence
            if command == "task.wait" and not reply["ok"] and reply.get("error", {}).get("code") == "timeout":
                return None
            assert reply["requestId"] == sequence and reply["ok"], (command, reply.get("error", {}).get("code"))
            return reply["data"]

        try:
            assert all(entry["available"] for entry in request("doctor")["tools"].values())
            about = request("about")
            if os.environ.get("MEDITOR_SOURCE_COMMIT"):
                assert about["product"]["sourceCommit"] == os.environ["MEDITOR_SOURCE_COMMIT"]
            assert all(tool["state"] == "recorded" and tool["version"] == tool["expectedVersion"] for tool in about["tools"])
            assert all(tool["noticeStatus"] == "present" and tool["licenseText"] for tool in about["tools"])
            for command in ["build", "run", "test"]:
                task = request(command, {"timeoutMs": 600000})
                deadline = time.monotonic() + 660
                result = None
                while result is None:
                    assert time.monotonic() < deadline, "Native tool task exceeded its bounded test deadline"
                    result = request("task.wait", {"task": task["task"], "timeoutMs": 120000})
                # The application already redacts exported task output. Keep a
                # bounded tail so CI failures expose their compiler/tool cause.
                assert result["state"] == "succeeded" and result["exitCode"] == 0, (command, result["state"], result["exitCode"], result.get("output", "")[-16000:])
                if command == "run":
                    assert "answer=42" in result["output"]
            document = request("document.open", {"path": "src/core.cppm"})["document"]
            symbols = request("language.symbols", {"document": document})
            assert isinstance(symbols, list)
            graph = request("modules.graph", {"document": document})
            assert isinstance(graph, dict)
            assert "sample.core" in json.dumps(graph)
            diagnostics = request("language.diagnostics")
            assert isinstance(diagnostics, list)
            request("language.stop")
        finally:
            process.stdin.close()
            try:
                process.wait(timeout=20)
            except subprocess.TimeoutExpired:
                process.kill(); process.wait()
                raise RuntimeError("Native tool session did not terminate") from None
            assert process.returncode == 0
            process.stderr.close()
    print("Pinned native tools: Modules build, run, test, LSP symbols/diagnostics and graph passed")


if __name__ == "__main__":
    main()
