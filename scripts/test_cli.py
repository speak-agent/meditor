# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Sunrisepeak & mcpp-community
"""Exercise the real CLI, document transactions and framed LSP against an isolated fixture tool."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--fixture", required=True, type=Path)
    args = parser.parse_args()
    binary, fixture = args.binary.resolve(), args.fixture.resolve()
    assert binary.is_file() and fixture.is_file()
    with tempfile.TemporaryDirectory(prefix="meditor-cli-") as directory:
        root = Path(directory)
        project, tools = root / "project", root / "tools"
        project.mkdir()
        source = project / "hello.cppm"
        original = "export module hello;\nexport int value() { return 42; }\n你好🙂\r\n"
        source.write_bytes(original.encode())
        for name in ["mcpp", "mcppls"]:
            target = tools / name / "bin" / (name + (".exe" if os.name == "nt" else ""))
            target.parent.mkdir(parents=True)
            shutil.copy2(fixture, target)
        settings_file = root / "configuration/settings.json"
        process = subprocess.Popen([str(binary), "serve", "--stdio", "--workspace", str(project), "--tool-root", str(tools), "--settings-file", str(settings_file)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8")
        sequence = 0
        def request(command, params=None, *, ok=True):
            nonlocal sequence
            sequence += 1
            process.stdin.write(json.dumps({"id": sequence, "command": command, "params": params or {}}, ensure_ascii=False) + "\n")
            process.stdin.flush()
            reply = json.loads(process.stdout.readline())
            assert reply["requestId"] == sequence and reply["schemaVersion"] == 1
            assert reply["ok"] is ok, (command, reply)
            return reply.get("data") if ok else reply["error"]
        try:
            assert request("doctor")["tools"]["mcppls"]["available"]
            assert "document.edit" in request("capabilities")["commands"]
            assert request("document.open", {"path": "../outside"}, ok=False)["code"] == "invalid-path"
            assert request("document.open", {"path": "missing"}, ok=False)["code"] == "not-found"
            capabilities = request("capabilities")
            assert set(capabilities["commands"]) == set(capabilities["commandSchemas"])
            settings = request("settings.get")
            assert settings == {"revision": 0, "values": {"theme": "dark", "reducedMotion": False}}
            updated = request("settings.update", {"revision": 0, "changes": {"theme": "light"}})
            assert updated["revision"] == 1 and updated["values"]["theme"] == "light"
            assert request("settings.update", {"revision": 0, "changes": {"theme": "dark"}}, ok=False)["code"] == "settings-conflict"
            assert request("settings.update", {"revision": -1, "changes": {}}, ok=False)["code"] == "invalid-params"
            assert request("settings.update", {"revision": 1, "changes": {"theme": "unknown"}}, ok=False)["code"] == "invalid-settings"
            assert request("settings.update", {"revision": 1, "changes": {"unknown": True}}, ok=False)["code"] == "invalid-settings"
            other = subprocess.run([str(binary), "settings.update", "--workspace", str(project), "--settings-file", str(settings_file), "--format", "json", "--params", json.dumps({"revision": 1, "changes": {"reducedMotion": True}})], capture_output=True, text=True, encoding="utf-8", timeout=15)
            assert other.returncode == 0 and json.loads(other.stdout)["data"]["revision"] == 2
            assert request("settings.get")["values"] == {"theme": "light", "reducedMotion": True}
            racers = [subprocess.Popen([str(binary), "settings.update", "--workspace", str(project), "--settings-file", str(settings_file), "--format", "json", "--params", json.dumps({"revision": 2, "changes": changes})], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8")
                      for changes in [{"theme": "dark"}, {"reducedMotion": False}]]
            race_replies = [json.loads(racer.communicate(timeout=15)[0]) for racer in racers]
            assert sum(reply["ok"] for reply in race_replies) == 1
            assert request("settings.get")["revision"] == 3
            user_config = root / "配置 preferences"
            environment = {**os.environ, "APPDATA": str(user_config), "XDG_CONFIG_HOME": str(user_config), "HOME": str(user_config)}
            default_result = subprocess.run([str(binary), "settings.update", "--workspace", str(project), "--format", "json", "--params", json.dumps({"revision": 0, "changes": {"theme": "light"}})], env=environment, capture_output=True, text=True, encoding="utf-8", timeout=15)
            assert default_result.returncode == 0 and json.loads(default_result.stdout)["data"]["revision"] == 1
            default_file = (user_config / "Library/Application Support" if sys.platform == "darwin" else user_config) / "meditor/settings.json"
            assert json.loads(default_file.read_text(encoding="utf-8"))["values"]["theme"] == "light"
            intact = settings_file.read_bytes()
            settings_file.write_bytes(b"invalid synthetic settings\n")
            assert request("settings.get", ok=False)["code"] == "invalid-settings"
            assert request("settings.update", {"revision": 2, "changes": {"theme": "dark"}}, ok=False)["code"] == "invalid-settings"
            assert settings_file.read_bytes() == b"invalid synthetic settings\n"
            settings_file.write_bytes(intact)
            assert request("document.open", {"path": "hello.cppm", "unexpected": True}, ok=False)["code"] == "invalid-params"
            assert request("workspace.trust", {"allow": "true"}, ok=False)["code"] == "invalid-params"
            document = request("document.open", {"path": "hello.cppm"})
            doc = document["document"]
            assert request("document.read", {"document": doc})["text"] == original
            assert request("build", ok=False)["code"] == "workspace-untrusted"
            edited = request("document.edit", {"document": doc, "revision": 1, "edits": [{"start": 0, "end": 6, "text": "import"}]})
            assert edited["revision"] == 2 and edited["dirty"]
            assert request("document.edit", {"document": doc, "revision": 1, "edits": []}, ok=False)["code"] == "revision-conflict"
            request("document.undo", {"document": doc, "revision": 2})
            assert request("document.read", {"document": doc})["text"] == original
            request("document.redo", {"document": doc, "revision": 3})
            source.write_text("externally changed", encoding="utf-8")
            assert request("document.save", {"document": doc, "revision": 4}, ok=False)["code"] == "disk-conflict"
            source.write_bytes(original.encode())
            request("document.save", {"document": doc, "revision": 4})
            assert source.read_bytes().decode().startswith("import module")
            request("workspace.trust", {"allow": True})
            task = request("build", {"target": "fixture-target"})
            result = request("task.wait", {"task": task["task"]})
            assert result["state"] == "succeeded" and result["exitCode"] == 0
            assert "fixture-target" in result["output"]
            completion = request("language.completion", {"document": doc, "position": {"line": 1, "character": 7}})
            assert completion["items"][0]["label"] == "module_value"
            diagnostics = request("language.diagnostics")
            assert diagnostics[0]["diagnostics"][0]["message"] == "fixture diagnostic"
            assert request("modules.graph", {"document": doc})["modules"][0]["name"] == "fixture"
            request("language.stop")
            assert request("unknown.command", ok=False)["code"] == "unknown-command"
            process.stdin.write("not JSON\n"); process.stdin.flush()
            assert json.loads(process.stdout.readline())["error"]["code"] == "invalid-json"
            process.stdin.write(" " * (8 * 1024 * 1024 + 1) + "\n"); process.stdin.flush()
            assert json.loads(process.stdout.readline())["error"]["code"] == "request-too-large"
            assert request("doctor")["tools"]["mcpp"]["available"]
            boundary = json.dumps({"id": "boundary", "command": "doctor"})
            process.stdin.write(" " * (8 * 1024 * 1024 - len(boundary)) + boundary + "\n"); process.stdin.flush()
            reply = json.loads(process.stdout.readline())
            assert reply["ok"] and reply["requestId"] == "boundary"
            process.stdin.write(json.dumps({"id": "final", "command": "doctor"}))
            process.stdin.close()
            reply = json.loads(process.stdout.readline())
            assert reply["ok"] and reply["requestId"] == "final"
        finally:
            if not process.stdin.closed:
                process.stdin.close()
            process.wait(timeout=20)
            errors = process.stderr.read()
            assert process.returncode == 0 and not errors
        once = subprocess.run([str(binary), "doctor", "--workspace", str(project), "--tool-root", str(tools), "--format", "json"], capture_output=True, text=True, timeout=20)
        assert once.returncode == 0 and json.loads(once.stdout)["ok"]
        assert str(root) not in once.stdout
    print("CLI documents, disk conflicts, task execution and LSP framing: passed")


if __name__ == "__main__":
    main()
