"""Security-boundary tests use synthetic artifacts and never publish the matched bytes."""
import importlib.util
from pathlib import Path
import shlex
import tempfile
import unittest
from unittest.mock import patch

SCRIPTS = Path(__file__).resolve().parents[1]


def module(name):
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / (name + ".py"))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


privacy = module("check_artifact_privacy")
candidates = module("prepare_candidate")


class ArtifactPrivacy(unittest.TestCase):
    def test_plain_and_wide_private_paths_are_rejected(self):
        with tempfile.TemporaryDirectory(prefix="meditor-private-fixture-") as folder:
            root = Path(folder)
            (root / "plain.bin").write_bytes(b"prefix\0" + str(Path.home()).encode() + b"/example\0")
            (root / "wide.bin").write_bytes((str(Path.home()) + "/example").encode("utf-16-le"))
            count, failures = privacy.scan(root)
            self.assertEqual(count, 2)
            self.assertEqual({name for name, kind in failures}, {"plain.bin", "wide.bin"})

    def test_public_content_and_empty_files_are_accepted(self):
        with tempfile.TemporaryDirectory(prefix="meditor-public-fixture-") as folder:
            root = Path(folder)
            (root / "public.bin").write_bytes(b"meditor\0@build-home@/toolchain/include\0https://example.org/en/users/documentation/WoWLAN\0")
            (root / "empty.bin").touch()
            count, failures = privacy.scan(root)
            self.assertEqual(count, 2)
            self.assertFalse(failures)

    def test_other_build_machine_paths_are_rejected(self):
        with tempfile.TemporaryDirectory(prefix="meditor-foreign-fixture-") as folder:
            root = Path(folder)
            (root / "foreign.bin").write_bytes(b"/home/private-developer/build/library\0")
            count, failures = privacy.scan(root)
            self.assertEqual(failures, [("foreign.bin", "personal build path")])

    def test_foreign_path_formats_and_url_boundaries(self):
        private = [
            "/home/private-developer/build/library",
            "/Users/private-developer/build/library",
            "C:\\Users\\private-developer\\build\\library",
            "C:\\Documents and Settings\\private-developer\\build\\library",
            "file:///home/private-developer/build/library",
        ]
        for value in private:
            for encoding in ["utf-8", "utf-16-le"]:
                with self.subTest(encoding=encoding, format=private.index(value)):
                    data = b"\0" + value.encode(encoding) + b"\0\0"
                    self.assertTrue(privacy.personal_build_path(data) or privacy.wide_personal_build_path(data))
        for value in ["https://example.org/en/users/documentation/WoWLAN", "/usr/share/locale", "@build-home@/library"]:
            for encoding in ["utf-8", "utf-16-le"]:
                data = value.encode(encoding)
                self.assertFalse(privacy.personal_build_path(data) or privacy.wide_personal_build_path(data))

    def test_system_tool_prefix_is_not_a_private_identity(self):
        with patch.object(privacy.shutil, "which", return_value="/usr/bin/mcpp"):
            self.assertNotIn(b"/usr", {needle for needle, _ in privacy.private_needles()})

    def test_external_symlink_is_rejected(self):
        with tempfile.TemporaryDirectory(prefix="meditor-link-fixture-") as folder:
            temporary = Path(folder)
            root = temporary / "bundle"; root.mkdir()
            external = temporary / "outside"; external.write_text("public fixture")
            (root / "escape").symlink_to(external)
            count, failures = privacy.scan(root)
            self.assertEqual(failures, [("escape", "external symlink")])

    def test_candidate_flags_keep_special_paths_in_one_argument(self):
        with tempfile.TemporaryDirectory(prefix="meditor-flag-fixture-") as folder:
            source = Path(folder) / "space ; $literal"
            with patch.object(candidates.shutil, "which", return_value="/opt/meditor-tools/bin/mcpp"):
                flags = candidates.privacy_flags(source)
            for flag in flags:
                self.assertEqual(len(shlex.split(flag)), 1)
            expected = "-ffile-prefix-map=" + str(source) + "=."
            self.assertTrue(any(shlex.split(flag) == [expected] for flag in flags), "Source map must preserve its exact argument")


if __name__ == "__main__":
    unittest.main()
