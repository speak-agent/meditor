# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Sunrisepeak & mcpp-community
"""Exercise revision-bound background queries through the real CLI and LSP fixtures."""
import json
import os
import queue
import subprocess
import threading
import time


class Session:
    def __init__(self, binary, tools, workspace, fault):
        workspace.mkdir()
        self.source = workspace / "main.cppm"
        self.source.write_bytes(b"original\n")
        self.process = subprocess.Popen([str(binary), "serve", "--stdio", "--workspace", str(workspace), "--tool-root", str(tools), "--data-root", str(workspace / "runtime")],
                                        env={**os.environ, "MEDITOR_FIXTURE_LSP_MODE": fault}, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8")
        self.replies = queue.Queue()
        self.sequence = 0
        def receive():
            try:
                for line in self.process.stdout:
                    self.replies.put(json.loads(line))
            finally:
                self.replies.put(None)
        self.listener = threading.Thread(target=receive, daemon=True)
        self.listener.start()

    def call(self, command, params=None, *, ok=True):
        self.sequence += 1
        self.process.stdin.write(json.dumps({"id": self.sequence, "command": command, "params": params or {}}) + "\n")
        self.process.stdin.flush()
        reply = self.replies.get(timeout=15)
        assert reply is not None and reply["requestId"] == self.sequence and reply["ok"] is ok, (command, reply)
        if command in ["language.result", "workspace.status"]:
            assert reply["effects"] == []
        return reply["data"] if ok else reply["error"]

    def close(self):
        self.process.stdin.close()
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill(); self.process.wait(timeout=5)
        self.listener.join(timeout=1)
        assert not self.listener.is_alive() and self.process.returncode == 0, "Async language session did not exit cleanly"
        assert not self.process.stderr.read()
        self.process.stdout.close(); self.process.stderr.close()

    def finish(self, query):
        deadline = time.monotonic() + 10
        while True:
            value = self.call("language.result", {"query": query})
            if value["state"] != "running":
                return value
            assert time.monotonic() < deadline, "Language query did not finish"
            time.sleep(.02)


def navigation_contract(binary, tools, root):
    session = Session(binary, tools, root / "async-navigation", "nav-locations")
    try:
        target = session.source.parent / "target.cppm"
        original = b"target original\n"
        target.write_bytes(original)
        source = session.call("document.open", {"path": "main.cppm"})["document"]
        session.call("workspace.trust", {"allow": True})
        params = {"document": source, "revision": 1, "operation": "definition", "position": {"line": 0, "character": 0}}
        query = session.call("language.query", params)["query"]
        result = session.finish(query)
        assert result["state"] == "succeeded" and result["locations"][0]["available"]
        assert result["locations"][0]["path"] == "target.cppm" and "target original" not in json.dumps(result)
        status = session.call("language.result", {"query": query, "includeResult": False})
        assert status["state"] == "succeeded" and "result" not in status and "locations" not in status
        assert session.call("language.navigate", {"query": query, "location": -1}, ok=False)["code"] == "invalid-params"
        stamp = target.stat()
        target.write_bytes(b"TARGET original\n")
        os.utime(target, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))
        assert session.call("language.navigate", {"query": query, "location": 0}, ok=False)["code"] == "revision-conflict"
        assert len(session.call("workspace.status")["documents"]) == 1
        target.write_bytes(original)
        opened = session.call("language.navigate", {"query": query, "location": 0})
        document = opened["target"]["document"]
        assert opened["opened"] and document != source and not opened["target"]["dirty"]
        assert not session.call("language.result", {"query": query, "includeResult": False})["stale"]
        assert not session.call("language.navigate", {"query": query, "location": 0})["opened"]
        session.call("document.edit", {"document": document, "revision": 1, "edits": [{"start": 0, "end": 0, "text": "!"}]})
        assert session.call("language.navigate", {"query": query, "location": 0}, ok=False)["code"] == "revision-conflict"
        query = session.call("language.query", {**params, "operation": "references"})["query"]
        result = session.finish(query)
        assert result["state"] == "succeeded" and len(result["locations"]) == 3
        assert not result["locations"][2]["available"] and "path" not in result["locations"][2]
        assert session.call("language.navigate", {"query": query, "location": 0})["target"]["dirty"]
        assert session.call("language.navigate", {"query": query, "location": 1})["target"]["document"] == source
        assert session.call("language.navigate", {"query": query, "location": 2}, ok=False)["code"] == "capability-unavailable"
        assert session.call("document.read", {"document": document})["text"] == "!target original\n"
        assert target.read_bytes() == original and session.source.read_bytes() == b"original\n"
        assert session.call("language.release", {"query": query})["released"]
        assert session.call("language.navigate", {"query": query, "location": 0}, ok=False)["code"] == "not-found"
        session.call("language.stop")
    finally:
        session.close()
    print("Language navigation captures exact targets, rejects changed files and preserves unsaved buffers: passed")


def exercise(binary, tools, root):
    navigation_contract(binary, tools, root)
    session = Session(binary, tools, root / "async-startup", "slow-initialize")
    try:
        doc = session.call("document.open", {"path": "main.cppm"})["document"]
        params = {"document": doc, "revision": 1, "operation": "completion", "position": {"line": 0, "character": 0}}
        assert session.call("language.query", params, ok=False)["code"] == "workspace-untrusted"
        session.call("workspace.trust", {"allow": True})
        assert session.call("language.query", {**params, "revision": 0}, ok=False)["code"] == "revision-conflict"
        assert session.call("language.query", {**params, "position": {"line": 100, "character": 0}}, ok=False)["code"] == "invalid-params"
        start = time.monotonic()
        query = session.call("language.query", params)["query"]
        assert session.call("language.release", {"query": query}, ok=False)["code"] == "language-busy"
        session.call("document.edit", {"document": doc, "revision": 1, "edits": [{"start": 0, "end": 9, "text": "saved while starting\n"}]})
        assert not session.call("document.save", {"document": doc, "revision": 2})["dirty"]
        assert session.source.read_bytes() == b"saved while starting\n"
        assert time.monotonic() - start < 2, "Background initialization blocked editing or saving"
        result = session.finish(query)
        assert result["state"] == "succeeded" and result["stale"] and result["revision"] == 1
        query = session.call("language.query", {**params, "revision": 2})["query"]
        result = session.finish(query)
        assert result["state"] == "succeeded" and not result["stale"]
        assert result["result"]["fixture"]["document"]["text"] == "saved while starting\n"
        diagnostics = session.call("language.query", {"document": doc, "revision": 2, "operation": "diagnostics"})["query"]
        result = session.finish(diagnostics)
        assert result["state"] == "succeeded" and result["result"]["versionVerified"]
        graph = session.call("language.query", {"document": doc, "revision": 2, "operation": "modules.graph"})["query"]
        assert session.finish(graph)["result"]["modules"][0]["name"] == "fixture"
        session.call("language.stop")
    finally:
        session.close()

    session = Session(binary, tools, root / "async-cancel", "ignore-hover")
    try:
        doc = session.call("document.open", {"path": "main.cppm"})["document"]
        session.call("workspace.trust", {"allow": True})
        params = {"document": doc, "revision": 1, "operation": "hover", "position": {"line": 0, "character": 0}}
        queries = [session.call("language.query", params)["query"] for _ in range(8)]
        assert session.call("language.query", params, ok=False)["code"] == "language-busy"
        start = time.monotonic()
        for query in queries:
            session.call("language.cancel", {"query": query})
            assert session.finish(query)["state"] == "cancelled"
        assert time.monotonic() - start < 3
        query = session.call("language.query", {**params, "operation": "completion"})["query"]
        assert session.finish(query)["state"] == "succeeded"
        timed = session.call("language.query", {**params, "timeoutMs": 100})["query"]
        assert session.finish(timed)["error"]["code"] == "timeout"
        for _ in range(20):
            newest = session.call("language.query", {"document": doc, "revision": 1, "operation": "symbols"})["query"]
            assert session.finish(newest)["state"] == "succeeded"
        assert session.call("language.result", {"query": query}, ok=False)["code"] == "not-found"
        assert session.call("language.result", {"query": newest})["state"] == "succeeded"
        session.call("language.stop")
        assert session.call("language.result", {"query": query}, ok=False)["code"] == "not-found"
    finally:
        session.close()

    session = Session(binary, tools, root / "async-switch", "slow-initialize")
    try:
        doc = session.call("document.open", {"path": "main.cppm"})["document"]
        session.call("workspace.trust", {"allow": True})
        query = session.call("language.query", {"document": doc, "revision": 1, "operation": "symbols"})["query"]
        destination = root / "next-workspace"; destination.mkdir()
        (destination / "next.cppm").write_text("export module next;\n", encoding="utf-8")
        start = time.monotonic()
        session.call("workspace.switch", {"path": str(destination), "generation": 1})
        assert time.monotonic() - start < 2, "Workspace switching waited for initialization"
        assert session.call("language.result", {"query": query}, ok=False)["code"] == "not-found"
        assert session.call("workspace.status")["language"] is None
        fresh = session.call("document.open", {"path": "next.cppm"})["document"]
        session.call("workspace.trust", {"allow": True})
        submitted = session.call("language.query", {"document": fresh, "revision": 1, "operation": "symbols"})
        assert submitted["query"] != query and submitted["generation"] == 2
        session.call("language.cancel", {"query": submitted["query"]})
        assert session.finish(submitted["query"])["state"] == "cancelled"
        session.call("language.stop")
    finally:
        session.close()
    for fault in ["private-error", "long-private-error"]:
        session = Session(binary, tools, root / fault, fault)
        try:
            doc = session.call("document.open", {"path": "main.cppm"})["document"]
            session.call("workspace.trust", {"allow": True})
            query = session.call("language.query", {"document": doc, "revision": 1, "operation": "hover", "position": {"line": 0, "character": 0}})["query"]
            failed = session.finish(query)
            assert failed["state"] == "failed" and failed["error"]["code"] == "language-error"
            message = failed["error"]["message"]
            assert "synthetic-user" not in message and "synthetic-secret" not in message and "/home/" not in message
            assert len(message.encode()) <= 1027 and failed["result"] is None
            session.call("language.stop")
        finally:
            session.close()
    print("Async language startup, current/stale snapshots, cancellation, limits and workspace shutdown: passed")
