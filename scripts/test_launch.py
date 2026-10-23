# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Sunrisepeak & mcpp-community
"""Exercise CLI startup of the same executable without a graphical session."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    args = parser.parse_args()
    environment = os.environ.copy()
    environment.pop("DISPLAY", None)
    environment.pop("WAYLAND_DISPLAY", None)
    with tempfile.TemporaryDirectory(prefix="meditor-launch-cli-") as folder:
        root = Path(folder)

        def invoke(*arguments):
            return subprocess.run([str(args.binary.resolve()), *arguments], cwd=root, env=environment,
                                  capture_output=True, text=True, encoding="utf-8", timeout=20)

        version = invoke("--version")
        assert version.returncode == 0 and version.stdout.strip() == "meditor 0.0.1"
        help_result = invoke("--help")
        assert help_result.returncode == 0 and "--cli" in help_result.stdout and "--gui" in help_result.stdout
        capabilities = invoke("--cli", "capabilities", "--format", "json")
        assert capabilities.returncode == 0 and "document.open" in json.loads(capabilities.stdout)["data"]["commands"]
        for command in [("workspace", "status"), ("workspace.status",)]:
            result = invoke("--cli", *command, "--format", "json")
            assert result.returncode == 0 and json.loads(result.stdout)["ok"]
        unicode_workspace = root / "workspace 世界"
        unicode_workspace.mkdir()
        result = invoke("--cli", "workspace", "status", "--workspace", str(unicode_workspace), "--format", "json")
        assert result.returncode == 0 and json.loads(result.stdout)["data"]["name"] == unicode_workspace.name, "Native argument decoding must preserve Unicode paths"
        for arguments in [("--gui", "--cli"), ("--gui", "--workspace"), ("--cli", "serve"),
                          ("--cli", "workspace", "status", "extra"), ("--cli", "build", "run")]:
            result = invoke(*arguments)
            assert result.returncode == 2 and result.stderr, "Ambiguous launch must fail before creating a window or tool task"
    print("Unified executable: no-display help/version/JSON, grouped commands, redirected streams and invalid-mode rejection passed")


if __name__ == "__main__":
    main()
