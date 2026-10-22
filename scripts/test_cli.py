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
import queue
import threading
import time
from test_async_language import exercise as async_language_contract


def language_failure_contract(binary, tools, root):
    for fault in ["normal", "oversize", "exit-after-hover", "flood-and-stall"]:
        workspace = root / fault
        workspace.mkdir()
        source = workspace / "main.cppm"
        source.write_bytes(b'"' * (9 * 1024 * 1024) if fault == "oversize" else b"original\n")
        process = subprocess.Popen([str(binary), "serve", "--stdio", "--workspace", str(workspace), "--tool-root", str(tools), "--data-root", str(workspace / "runtime")],
                                   env={**os.environ, "MEDITOR_FIXTURE_LSP_MODE": fault}, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8")
        replies = queue.Queue()
        def receive():
            try:
                for line in process.stdout:
                    replies.put(json.loads(line))
            finally:
                replies.put(None)
        listener = threading.Thread(target=receive, daemon=True)
        listener.start()
        sequence = 0
        def call(command, params=None, *, ok=True):
            nonlocal sequence
            sequence += 1
            process.stdin.write(json.dumps({"id": sequence, "command": command, "params": params or {}}) + "\n")
            process.stdin.flush()
            reply = replies.get(timeout=15)
            assert reply is not None and reply["requestId"] == sequence and reply["ok"] is ok, (fault, command, reply)
            return reply
        try:
            document = call("document.open", {"path": "main.cppm"})["data"]["document"]
            call("workspace.trust", {"allow": True})
            if fault == "oversize":
                rejected = call("language.completion", {"document": document, "position": {"line": 0, "character": 0}}, ok=False)
                assert rejected["error"]["code"] == "language-message-too-large"
                call("document.edit", {"document": document, "revision": 1, "edits": [{"start": 0, "end": 0, "text": "x"}]})
                saved = call("document.save", {"document": document, "revision": 2})
                assert not saved["data"]["dirty"] and saved["data"]["languageSaveNotification"]["state"] == "not-open"
                assert source.read_bytes() == b"x" + b'"' * (9 * 1024 * 1024)
                call("language.stop")
                continue
            call("language.completion", {"document": document, "position": {"line": 0, "character": 0}})
            if fault == "normal":
                call("document.edit", {"document": document, "revision": 1, "edits": [{"start": 0, "end": 9, "text": "saved current version\n"}]})
                saved = call("document.save", {"document": document, "revision": 2})
                assert saved["data"]["languageSaveNotification"]["state"] == "queued"
                observed = call("language.hover", {"document": document, "position": {"line": 0, "character": 0}})["data"]["fixture"]["lastSaved"]
                assert observed == {"version": 2, "text": "saved current version\n"}, "Save notification preceded the current buffer change"
                call("language.stop")
                continue
            if fault == "exit-after-hover":
                call("language.hover", {"document": document, "position": {"line": 0, "character": 0}})
            deadline = time.monotonic() + 10
            while True:
                state = call("workspace.status")["data"]["language"]
                assert state["queuedMessages"] <= 256 and state["queuedBytes"] <= 64 * 1024 * 1024
                if state["state"] == "stopped":
                    break
                assert time.monotonic() < deadline, "Faulted language service did not reach a terminal state"
                time.sleep(.02)
            call("document.edit", {"document": document, "revision": 1, "edits": [{"start": 0, "end": 9, "text": "saved despite language failure\n"}]})
            start = time.monotonic()
            saved = call("document.save", {"document": document, "revision": 2})
            assert time.monotonic() - start < 3, "Saving waited for the unavailable language server"
            assert saved["effects"] == ["file-write"] and not saved["data"]["dirty"]
            assert saved["data"]["languageSaveNotification"]["state"] == "unavailable"
            assert source.read_bytes() == b"saved despite language failure\n"
            if fault == "exit-after-hover":
                assert call("language.completion", {"document": document, "position": {"line": 0, "character": 0}})["data"]["items"][0]["label"] == "module_value"
                assert call("language.diagnostics")["data"][0]["version"] == 2
            start = time.monotonic()
            call("language.stop")
            assert time.monotonic() - start < 5, "Stopping a blocked language transport did not finish"
        finally:
            process.stdin.close()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill(); process.wait(timeout=5)
            listener.join(timeout=1)
            assert not listener.is_alive() and process.returncode == 0, "Fault-case CLI did not exit cleanly"
            assert not process.stderr.read(), "Fault-case CLI emitted unexpected errors"
            process.stdout.close(); process.stderr.close()
    print("Language save ordering, frame limits, exit/backpressure and bounded stop/reconnect: passed")


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
        bom_original = "\ufeffexport module 你好;\r\nmodule;\n"
        (project / "bom.cppm").write_bytes(bom_original.encode())
        syntax_samples = {
            "README.md": ("# 标题 🙂\n```cpp\nexport module example;\n```\n", "markdown", "heading"),
            "example.json": ('{"键": "🙂", "active": true}', "json", "key"),
            "example.jsonc": ('/* comment */ {"key": true}', "jsonc", "comment"),
            "mcpp.toml": ('[package]\nname = "example"\n', "toml", "key"),
        }
        for filename, (text, _, _) in syntax_samples.items():
            (project / filename).write_bytes(text.encode())
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
            assert capabilities["languageTransport"] == {"notificationDelivery": "queued", "maxMessageBytes": 16 * 1024 * 1024, "maxQueuedBytes": 64 * 1024 * 1024, "maxQueuedMessages": 256, "writeTimeoutMs": 5000}
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
            bom = request("document.open", {"path": "bom.cppm"})
            assert bom["format"]["bom"] and bom["format"]["mixedLineEndings"] and bom["format"]["lineEnding"] == "crlf"
            bom_id = bom["document"]
            assert request("document.highlight", {"document": bom_id})["tokens"][0] == {"line": 0, "start": 3, "end": 9, "role": "keyword"}
            assert request("document.read", {"document": bom_id})["text"] == bom_original
            # CLI offsets and edit bytes stay raw; only GUI presentation normalizes.
            request("document.edit", {"document": bom_id, "revision": 1, "edits": [{"start": 3, "end": 3, "text": "// "}]})
            assert request("document.read", {"document": bom_id})["text"].startswith("\ufeff// export")
            request("document.undo", {"document": bom_id, "revision": 2})
            request("document.save", {"document": bom_id, "revision": 3})
            found = request("document.find", {"document": bom_id, "revision": 3, "query": "你好", "coordinateSpace": "display"})
            assert found["total"] == 1 and found["matches"][0]["rawStart"] == 17
            changed = request("document.replace", {"document": bom_id, "revision": 3, "query": "你好", "replacement": "世界", "coordinateSpace": "display", "all": True})
            assert changed["revision"] == 4 and changed["replacements"] == 1
            assert request("document.find", {"document": bom_id, "revision": 3, "query": "你好"}, ok=False)["code"] == "revision-conflict"
            request("document.undo", {"document": bom_id, "revision": 4})
            request("document.save", {"document": bom_id, "revision": 5})
            assert (project / "bom.cppm").read_bytes() == bom_original.encode()
            document = request("document.open", {"path": "hello.cppm"})
            doc = document["document"]
            assert request("document.read", {"document": doc})["text"] == original
            for filename, (_, language, role) in syntax_samples.items():
                sample_id = request("document.open", {"path": filename})["document"]
                result = request("document.highlight", {"document": sample_id})
                assert result["language"] == language and result["tokens"][0]["role"] == role
                assert result["revision"] == 1 and not result["limited"]
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
            readonly = project / "readonly.cppm"
            readonly.write_bytes(b"original")
            readonly_doc = request("document.open", {"path": "readonly.cppm"})["document"]
            request("document.edit", {"document": readonly_doc, "revision": 1, "edits": [{"start": 0, "end": 8, "text": "pending"}]})
            readonly.chmod(0o444)
            try:
                assert request("document.save", {"document": readonly_doc, "revision": 2}, ok=False)["code"] == "file-read-only"
                assert readonly.read_bytes() == b"original"
                assert request("document.read", {"document": readonly_doc})["dirty"]
            finally:
                readonly.chmod(0o644)
            request("document.save", {"document": readonly_doc, "revision": 2})
            assert readonly.read_bytes() == b"pending"
            request("workspace.trust", {"allow": True})
            switched_project = root / "second 工作区"
            switched_project.mkdir()
            (switched_project / "module.cppm").write_text("export module second;", encoding="utf-8")
            pending = request("build", {"target": "meditor-fixture-long-running"})
            guard = request("workspace.canLeave")
            assert not guard["canLeave"] and guard["runningTasks"] == [pending["task"]]
            switch_params = {"path": str(switched_project), "generation": guard["generation"]}
            assert request("workspace.switch", switch_params, ok=False)["code"] == "task-busy"
            request("task.cancel", {"task": pending["task"]})
            assert request("task.wait", {"task": pending["task"]})["state"] == "cancelled"
            assert request("workspace.canLeave")["canLeave"]
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
            # Switching also stops the live LSP and retains runtime homes and preferences.
            switched = request("workspace.switch", switch_params)
            assert switched["changed"] and switched["generation"] == 2
            assert request("workspace.status")["language"] is None
            assert not request("workspace.status")["trusted"]
            assert request("workspace.status")["documents"] == []
            assert request("document.read", {"document": doc}, ok=False)["code"] == "not-found"
            assert request("task.status", {"task": task["task"]}, ok=False)["code"] == "not-found"
            assert request("workspace.switch", switch_params, ok=False)["code"] == "workspace-conflict"
            new_doc = request("document.open", {"path": "module.cppm"})["document"]
            assert new_doc != doc
            assert request("build", ok=False)["code"] == "workspace-untrusted"
            assert request("settings.get")["revision"] == 3
            request("workspace.trust", {"allow": True})
            new_task = request("build")
            assert new_task["task"] != task["task"]
            assert request("task.wait", {"task": new_task["task"]})["state"] == "succeeded"
            assert json.loads((receipts / "task.json").read_text(encoding="utf-8")) == task_environment
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
        language_failure_contract(binary, tools, root)
        async_language_contract(binary, tools, root)
        localization_contract(binary, project, settings_file)
        portable_contract(binary, tools, project, root)
    print("CLI documents, disk conflicts, task execution and LSP framing: passed")


if __name__ == "__main__":
    main()
