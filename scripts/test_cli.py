"""Exercise the real CLI, document transactions and framed LSP against an isolated fixture tool."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
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
        process = subprocess.Popen([str(binary), "serve", "--stdio", "--workspace", str(project), "--tool-root", str(tools)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8")
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
            assert request("modules.graph", {"document": doc})["modules"][0]["name"] == "fixture"
            request("language.stop")
            assert request("unknown.command", ok=False)["code"] == "unknown-command"
            process.stdin.write("not JSON\n"); process.stdin.flush()
            assert json.loads(process.stdout.readline())["error"]["code"] == "invalid-json"
        finally:
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
