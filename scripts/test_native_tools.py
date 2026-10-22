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
            for operation in ["symbols", "modules.graph", "diagnostics"]:
                query = request("language.query", {"document": document, "revision": 1, "operation": operation, "timeoutMs": 60000})["query"]
                deadline = time.monotonic() + 90
                while True:
                    response = request("language.result", {"query": query})
                    if response["state"] != "running":
                        break
                    assert time.monotonic() < deadline, "Native asynchronous language query exceeded its deadline"
                    time.sleep(.05)
                assert response["state"] == "succeeded" and not response["stale"], (operation, response.get("error"))
                if operation == "modules.graph":
                    assert "sample.core" in json.dumps(response["result"])
                elif operation == "diagnostics":
                    assert isinstance(response["result"]["diagnostics"], list)
            symbols = request("language.symbols", {"document": document})
            assert isinstance(symbols, list)
            def navigable_answer(items):
                return any(isinstance(item, dict) and
                    ((item.get("name", "").startswith("answer") and "location" not in item and "selectionRange" in item) or
                     navigable_answer(item.get("children", []))) for item in items)
            deadline = time.monotonic() + 30
            while not navigable_answer(symbols) and time.monotonic() < deadline:
                time.sleep(.1)
                symbols = request("language.symbols", {"document": document})
                assert isinstance(symbols, list)
            assert navigable_answer(symbols), "Native language service did not return navigable hierarchical function symbols"
            for operation in ["definition", "references"]:
                deadline = time.monotonic() + 60
                while True:
                    query = request("language.query", {"document": document, "revision": 1, "operation": operation, "position": {"line": 1, "character": 13}, "timeoutMs": 30000})["query"]
                    while True:
                        locations = request("language.result", {"query": query})
                        if locations["state"] != "running":
                            break
                        assert time.monotonic() < deadline, "Native navigation query exceeded its deadline"
                        time.sleep(.05)
                    assert locations["state"] == "succeeded", (operation, locations.get("error"))
                    available = [i for i, item in enumerate(locations["locations"]) if item["available"]]
                    if available or time.monotonic() >= deadline:
                        break
                    time.sleep(.1)
                assert available, "Native language service returned no navigable workspace " + operation
                navigated = request("language.navigate", {"query": query, "location": available[0]})
                assert "answer" in request("document.read", {"document": navigated["target"]["document"]})["text"]
                assert not request("language.result", {"query": query, "includeResult": False})["stale"]
            graph = request("modules.graph", {"document": document})
            assert isinstance(graph, dict)
            assert "sample.core" in json.dumps(graph)
            diagnostics = request("language.diagnostics")
            assert isinstance(diagnostics, list)
            request("language.stop")
            original = request("document.read", {"document": document})["text"]
            request("document.edit", {"document": document, "revision": 1, "edits": [{"start": len(original.encode()), "end": len(original.encode()), "text": "export int broken = ;\n"}]})
            def has_errors(revision, expected):
                deadline = time.monotonic() + 60
                while True:
                    query = request("language.query", {"document": document, "revision": revision, "operation": "diagnostics", "timeoutMs": 30000})["query"]
                    while True:
                        result = request("language.result", {"query": query})
                        if result["state"] != "running":
                            break
                        assert time.monotonic() < deadline, "Native diagnostics query did not finish"
                        time.sleep(.05)
                    assert result["state"] == "succeeded" and not result["stale"], result.get("error")
                    errors = [item for publication in result["result"]["diagnostics"] for item in publication.get("diagnostics", []) if item.get("severity", 1) == 1]
                    if bool(errors) == expected:
                        return
                    assert time.monotonic() < deadline, "Native diagnostics did not reflect the buffer revision"
                    time.sleep(.1)
            has_errors(2, True)
            assert (root / "src/core.cppm").read_bytes() == original.encode(), "Language queries unexpectedly saved the buffer"
            request("document.undo", {"document": document, "revision": 2})
            has_errors(3, False)
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
