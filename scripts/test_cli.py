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


def portable_contract(binary, tools, project, root):
    package = root / "portable package"
    directory = package / "bin"; directory.mkdir(parents=True)
    executable = directory / binary.name
    shutil.copy2(binary, executable)
    for library in binary.parent.glob("*.dll"):
        shutil.copy2(library, directory / library.name)
    shutil.copytree(tools, directory / "tools")
    (package / "portable.json").write_text(json.dumps({"schemaVersion": 1, "dataDirectory": "state"}), encoding="utf-8")

    def command(name, params=None, trust=False):
        environment = {**os.environ, "MEDITOR_FIXTURE_ENV_LOG": str(package / "state/receipts")}
        arguments = [str(executable), name, "--workspace", str(project), "--format", "json", "--params", json.dumps(params or {})]
        if trust:
            arguments.append("--trust")
        result = subprocess.run(arguments, env=environment, capture_output=True, text=True, encoding="utf-8", timeout=20)
        assert result.returncode == 0, "Portable configuration command failed"
        reply = json.loads(result.stdout); assert reply["ok"]
        return reply["data"]

    assert command("doctor")["runtime"] == {"mode": "portable", "managedToolHomes": True}
    assert not (package / "state").exists()
    command("settings.update", {"revision": 0, "changes": {"theme": "light"}})
    assert json.loads((package / "state/settings.json").read_text(encoding="utf-8"))["values"]["theme"] == "light"
    command("build", trust=True)
    receipt = json.loads((package / "state/receipts/task.json").read_text(encoding="utf-8"))
    assert Path(receipt["MCPP_HOME"]).resolve() == (package / "state/mcpp").resolve()
    assert Path(receipt["MCPPLS_CACHE_DIR"]).resolve() == (package / "state/cache/mcppls").resolve()

    previous = package
    package.rename(root / "relocated package"); package = root / "relocated package"
    executable = package / "bin" / binary.name
    assert command("settings.get")["values"]["theme"] == "light"
    command("build", trust=True)
    receipt = json.loads((package / "state/receipts/task.json").read_text(encoding="utf-8"))
    assert Path(receipt["MCPP_HOME"]).resolve() == (package / "state/mcpp").resolve()
    assert not previous.exists()
    (package / "portable.json").write_text(json.dumps({"schemaVersion": 1, "dataDirectory": "../escape"}), encoding="utf-8")
    failed = subprocess.run([str(executable), "doctor", "--workspace", str(project)], capture_output=True, timeout=15)
    assert failed.returncode != 0 and not (root / "escape").exists()


def localization_contract(binary, project, settings_file):
    common = ["--workspace", str(project), "--settings-file", str(settings_file)]

    def invoke(*arguments, language=None, json_output=False, environment=None):
        command = [str(binary), *arguments, *common]
        if language:
            command += ["--language", language]
        if json_output:
            command += ["--format", "json"]
        return subprocess.run(command, env=environment, capture_output=True, text=True, encoding="utf-8", timeout=15)

    current = json.loads(invoke("settings.get", json_output=True).stdout)["data"]
    changed = invoke("settings.update", "--params", json.dumps({"revision": current["revision"], "changes": {"language": "zh"}}), json_output=True)
    assert changed.returncode == 0
    checkpoint = settings_file.read_bytes()
    assert "用法" in invoke("--help").stdout
    assert "Usage:" in invoke("--help", language="en").stdout
    assert "用法" in invoke("--help", language="zh_CN.UTF-8").stdout
    assert "Usage:" in invoke("--help", language="fr-FR").stdout
    assert settings_file.read_bytes() == checkpoint
    errors = [invoke("unknown.command", language=language, json_output=True) for language in ["en", "zh"]]
    replies = [json.loads(result.stdout) for result in errors]
    assert all(result.returncode == 1 for result in errors)
    assert replies[0]["error"] == replies[1]["error"]
    assert replies[0]["error"]["code"] == "unknown-command"
    assert "未知命令" in invoke("unknown.command", language="zh").stderr
    assert "Unknown command" in invoke("unknown.command", language="en").stderr
    summary = invoke("about", language="zh")
    assert summary.returncode == 0 and "轻量级编辑器" in summary.stdout and "Sunrisepeak & mcpp-community" in summary.stdout
    assert "GNU GENERAL PUBLIC LICENSE" not in summary.stdout
    if os.name != "nt":
        environment = {**os.environ, "LC_ALL": "zh_CN.UTF-8"}
        assert "用法" in invoke("--help", language="auto", environment=environment).stdout
    assert settings_file.read_bytes() == checkpoint


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
        data_root = root / "runtime data"
        receipts = root / "environment-receipts"
        child_environment = {**os.environ, "MEDITOR_FIXTURE_ENV_LOG": str(receipts)}
        process = subprocess.Popen([str(binary), "serve", "--stdio", "--workspace", str(project), "--tool-root", str(tools), "--settings-file", str(settings_file), "--data-root", str(data_root)], env=child_environment, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8")
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
            about = request("about")
            assert about["product"]["license"] == "GPL-3.0-only"
            assert about["product"]["contributors"] == "Sunrisepeak & mcpp-community"
            assert about["product"]["contact"] == "speakshen@163.com"
            assert all(tool["state"] == "unverified" and tool["version"] is None for tool in about["tools"])
            declarations = request("licenses")
            assert declarations["components"][0]["licenseText"] == about["product"]["licenseText"]
            assert not declarations["complete"] and not declarations["releaseReady"]
            assert str(root) not in json.dumps(about)
            assert not receipts.exists(), "About/notice queries must not execute tools"
            assert request("document.open", {"path": "../outside"}, ok=False)["code"] == "invalid-path"
            assert request("document.open", {"path": "missing"}, ok=False)["code"] == "not-found"
            capabilities = request("capabilities")
            assert set(capabilities["commands"]) == set(capabilities["commandSchemas"])
            settings = request("settings.get")
            assert settings == {"revision": 0, "values": {"theme": "dark", "reducedMotion": False, "language": "auto"}}
            updated = request("settings.update", {"revision": 0, "changes": {"theme": "light"}})
            assert updated["revision"] == 1 and updated["values"]["theme"] == "light"
            assert request("settings.update", {"revision": 0, "changes": {"theme": "dark"}}, ok=False)["code"] == "settings-conflict"
            assert request("settings.update", {"revision": -1, "changes": {}}, ok=False)["code"] == "invalid-params"
            assert request("settings.update", {"revision": 1, "changes": {"theme": "unknown"}}, ok=False)["code"] == "invalid-settings"
            assert request("settings.update", {"revision": 1, "changes": {"unknown": True}}, ok=False)["code"] == "invalid-settings"
            other = subprocess.run([str(binary), "settings.update", "--workspace", str(project), "--settings-file", str(settings_file), "--format", "json", "--params", json.dumps({"revision": 1, "changes": {"reducedMotion": True}})], capture_output=True, text=True, encoding="utf-8", timeout=15)
            assert other.returncode == 0 and json.loads(other.stdout)["data"]["revision"] == 2
            assert request("settings.get")["values"] == {"theme": "light", "reducedMotion": True, "language": "auto"}
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
            highlighted = request("document.highlight", {"document": doc})
            assert highlighted["revision"] == 1 and highlighted["language"] == "cpp"
            assert highlighted["layer"] == "lexical" and highlighted["offsetEncoding"] == "utf-8-bytes-per-line"
            assert highlighted["tokens"][0] == {"line": 0, "start": 0, "end": 6, "role": "keyword"}
            assert request("document.highlight", {"document": doc, "startLine": -1}, ok=False)["code"] == "invalid-params"
            assert request("document.highlight", {"document": doc, "maxLines": 1001}, ok=False)["code"] == "invalid-params"
            highlight_doc = request("document.open", {"path": "highlight.cppm", "create": True})["document"]
            request("document.edit", {"document": highlight_doc, "revision": 1, "edits": [{"start": 0, "end": 0, "text": "int " * 3000 + "\n" + "int " * 3000}]})
            bounded = request("document.highlight", {"document": highlight_doc})
            assert bounded["truncated"] and not bounded["limited"] and len(bounded["tokens"]) == 4096
            assert bounded["completeLines"] == 1 and bounded["revision"] == 2
            assert len(request("document.highlight", {"document": highlight_doc, "startLine": 1, "maxLines": 1})["tokens"]) == 3000
            request("document.edit", {"document": highlight_doc, "revision": 2, "edits": [{"start": 0, "end": 24001, "text": "x" * 16385}]})
            bounded = request("document.highlight", {"document": highlight_doc})
            assert bounded["limited"] and not bounded["truncated"] and not bounded["tokens"]
            request("document.undo", {"document": highlight_doc, "revision": 3})
            assert not request("document.undo", {"document": highlight_doc, "revision": 4})["dirty"]

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
            task_environment = json.loads((receipts / "task.json").read_text(encoding="utf-8"))
            assert Path(task_environment["MCPP_HOME"]).resolve() == (data_root / "mcpp").resolve()
            assert Path(task_environment["MCPPLS_CACHE_DIR"]).resolve() == (data_root / "cache/mcppls").resolve()
            assert Path(task_environment["TMPDIR"]).resolve() == (data_root / "temp").resolve()
            completion = request("language.completion", {"document": doc, "position": {"line": 1, "character": 7}})
            assert completion["items"][0]["label"] == "module_value"
            assert json.loads((receipts / "language.json").read_text(encoding="utf-8")) == task_environment
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
        localization_contract(binary, project, settings_file)
        portable_contract(binary, tools, project, root)
    print("CLI documents, disk conflicts, task execution and LSP framing: passed")


if __name__ == "__main__":
    main()
