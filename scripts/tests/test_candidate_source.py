# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Sunrisepeak & mcpp-community
"""Exercise committed-source isolation and payload verification without network access."""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import tomllib
import unittest
from unittest.mock import patch

SCRIPTS = Path(__file__).resolve().parents[1]


def module(name):
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / (name + ".py"))
    value = importlib.util.module_from_spec(spec)
    with patch.object(sys, "path", [str(SCRIPTS), *sys.path]):
        spec.loader.exec_module(value)
    return value


candidates = module("prepare_candidate")
payloads = module("prepare_payloads")


class CandidateSource(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="meditor-candidate-fixture-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.target = "x86_64-linux-gnu"
        self.specification = {"targets": {self.target: {"mcpp": {
            "url": "https://example.org/public-tool.tar.gz", "sha256": "0" * 64,
        }}}}
        (self.root / "packaging").mkdir()
        (self.root / "packaging/tools.json").write_text(json.dumps(self.specification))
        (self.root / "mcpp.toml").write_text('[package]\nname = "candidate-fixture"\n[workspace]\nmembers = []\n')
        (self.root / "mcpp.lock").write_text("version = 2\n")
        (self.root / "README.md").write_text("committed public fixture\n")
        self.git("init", "-q")
        self.git("add", ".")
        self.git("-c", "user.name=meditor fixture", "-c", "user.email=fixture@example.org", "-c", "commit.gpgsign=false", "commit", "-qm", "Synthetic source fixture")
        self.commit = self.git("rev-parse", "HEAD").strip()
        self.payload = self.root / "target/payloads" / self.target
        tool = self.payload / "tools/mcpp/bin/mcpp"
        tool.parent.mkdir(parents=True)
        tool.write_bytes(b"synthetic pinned tool\n")
        receipt = {"schemaVersion": 1, "target": self.target,
                   "sources": self.specification["targets"][self.target],
                   "files": payloads.inventory(self.payload)}
        (self.payload / "inventory.json").write_text(json.dumps(receipt))
        self.destination = self.root / "target/candidates/review"

    def git(self, *args):
        return subprocess.check_output(["git", *args], cwd=self.root, text=True, stderr=subprocess.PIPE)

    def prepare(self):
        with patch.object(candidates, "ROOT", self.root), \
             patch.object(candidates.shutil, "which", return_value="/opt/meditor-tools/bin/mcpp"), \
             patch.dict(sys.modules, {"prepare_payloads": payloads}), \
             patch.object(sys, "argv", ["prepare_candidate", "--target", self.target, "--directory", str(self.destination)]), \
             contextlib.redirect_stdout(io.StringIO()):
            candidates.main()

    def test_export_excludes_worktree_secrets_and_records_generated_inputs(self):
        (self.root / "README.md").write_text("uncommitted local changes\n")
        (self.root / ".env").write_text("SYNTHETIC_FIXTURE_SECRET=excluded\n")
        self.prepare()
        source = self.destination / "source"
        self.assertEqual((source / "README.md").read_text(), "committed public fixture\n")
        self.assertFalse((source / ".env").exists())
        self.assertFalse((source / ".git").exists())
        manifest = (source / "mcpp.toml").read_bytes()
        flags = tomllib.loads(manifest.decode())["workspace"]["build"]["dialect_cxxflags"]
        self.assertTrue(flags)
        receipt = json.loads((self.destination / "candidate.json").read_text())
        self.assertEqual(receipt["commit"], self.commit)
        self.assertEqual(receipt["generatedManifestSha256"], hashlib.sha256(manifest).hexdigest())
        self.assertNotIn(str(self.root), json.dumps(receipt))
        payloads.verify(source / "target/payloads" / self.target, self.specification, self.target)
        original_receipt = (self.destination / "candidate.json").read_bytes()
        with self.assertRaises(SystemExit):
            self.prepare()
        self.assertEqual((self.destination / "candidate.json").read_bytes(), original_receipt)

    def test_changed_payload_is_rejected_before_export(self):
        (self.payload / "tools/mcpp/bin/mcpp").write_bytes(b"tampered fixture\n")
        with self.assertRaisesRegex(RuntimeError, "inventory is incomplete or changed"):
            self.prepare()
        self.assertFalse(self.destination.exists())


if __name__ == "__main__":
    unittest.main()
