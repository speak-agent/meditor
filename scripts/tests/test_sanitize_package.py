# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Sunrisepeak & mcpp-community
"""Sanitizer tests use synthetic artifacts and assert length preservation and scanner acceptance."""
import importlib.util
from pathlib import Path
import random
import sys
import tempfile
import unittest

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))

def module(name):
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / (name + ".py"))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


privacy = module("check_artifact_privacy")
sanitizer = module("sanitize_package")


def synthetical_binary(seed: int = 7, size: int = 8192) -> bytes:
    rng = random.Random(seed)
    return bytes(rng.randrange(256) for _ in range(size))


class SanitizePackage(unittest.TestCase):
    def test_personal_utf8_path_is_anonymized_and_scanner_clean(self):
        data = synthetical_binary() + b"log /home/zhangsan/build/gtk/a.c\0tail" + synthetical_binary(seed=8)
        out, count = sanitizer.sanitize_bytes(data, [])
        self.assertEqual(count, 1)
        self.assertEqual(len(out), len(data))
        cleaned = bytes(out)
        self.assertIn(b"/home/<@@@@@@>/", cleaned)
        self.assertFalse(privacy.personal_build_path(cleaned))

    def test_placeholder_and_service_names_are_untouched(self):
        allowed = b"/home/runner/work/x/y.c /home/build/z /home/<@@@@>/d/x.c /home/$USER/x"
        out, count = sanitizer.sanitize_bytes(allowed, [])
        self.assertEqual(count, 0)
        self.assertEqual(bytes(out), allowed)

    def test_wide_personal_path_is_anonymized_and_scanner_clean(self):
        payload = "pref /home/wangwu/d/build.c end".encode("utf-16-le") + synthetical_binary(seed=9, size=64)
        out, count = sanitizer.sanitize_bytes(payload, [])
        self.assertEqual(count, 1)
        self.assertEqual(len(out), len(payload))
        cleaned = bytes(out)
        self.assertIn("/home/<@@@@>/".encode("utf-16-le"), cleaned)
        self.assertFalse(privacy.wide_personal_build_path(cleaned))

    def test_machine_needles_are_erased_without_resizing(self):
        data = b"prefix " + str(Path.home()).encode() + b"/registry/x\0" + synthetical_binary(seed=11, size=32)
        out, count = sanitizer.sanitize_bytes(data, [(str(Path.home()).encode(), "build path")])
        self.assertGreaterEqual(count, 1)
        cleaned = bytes(out)
        self.assertEqual(len(cleaned), len(data))
        self.assertNotIn(str(Path.home()).encode(), cleaned)

    def test_unrelated_binary_round_trips_identically(self):
        data = synthetical_binary(seed=13, size=100_000)
        out, count = sanitizer.sanitize_bytes(data, [])
        self.assertEqual(count, 0)
        self.assertEqual(bytes(out), data)

    def test_sanitize_tree_passes_the_privacy_gate(self):
        with tempfile.TemporaryDirectory(prefix="meditor-hygiene-fixture-") as folder:
            root = Path(folder)
            (root / "plain.bin").write_bytes(b"note\0/home/lichao/build/gtk/l.c\0" + synthetical_binary(seed=15, size=64))
            (root / "wide.bin").write_bytes("x /home/zhaoqi/build/b.h y".encode("utf-16-le"))
            (root / "clean.bin").write_bytes(b"ordinary content")
            _, failures = privacy.scan(root)
            self.assertEqual({name for name, _kind in failures}, {"plain.bin", "wide.bin"})
            changed, replacements = sanitizer.sanitize_tree(root, dry_run=False)
            self.assertEqual(changed, 2)
            self.assertGreaterEqual(replacements, 2)
            count, failures = privacy.scan(root)
            self.assertEqual(count, 3)
            self.assertFalse(failures)

    def test_dry_run_reports_without_modifying(self):
        with tempfile.TemporaryDirectory(prefix="meditor-hygiene-dry-") as folder:
            root = Path(folder)
            original = b"keep\0/home/mochen/build/x.c\0"
            (root / "a.bin").write_bytes(original)
            changed, replacements = sanitizer.sanitize_tree(root, dry_run=True)
            self.assertEqual((changed, replacements), (1, 1))
            self.assertEqual((root / "a.bin").read_bytes(), original)

    def test_second_pass_is_idempotent(self):
        with tempfile.TemporaryDirectory(prefix="meditor-hygiene-idem-") as folder:
            root = Path(folder)
            (root / "a.bin").write_bytes(b"\0/home/sunli/build/y.c\0")
            sanitizer.sanitize_tree(root, dry_run=False)
            changed, replacements = sanitizer.sanitize_tree(root, dry_run=False)
            self.assertEqual((changed, replacements), (0, 0))


if __name__ == "__main__":
    unittest.main()
