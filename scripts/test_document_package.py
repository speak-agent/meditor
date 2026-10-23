# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Sunrisepeak & mcpp-community
"""Consume the packed document/syntax interface without its implementation sources."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(arguments, directory):
    result = subprocess.run(arguments, cwd=directory, capture_output=True, text=True, timeout=300)
    if result.returncode:
        diagnostics = result.stdout + result.stderr
        for private, replacement in [(str(ROOT), "<project>"), (str(Path.home()), "<home>")]:
            diagnostics = diagnostics.replace(private, replacement).replace(private.replace("\\", "/"), replacement)
        raise RuntimeError("Packed document consumer failed:\n" + diagnostics)
    return result.stdout


def main():
    envelope = json.loads(run(["mcpp", "pack", "-p", "meditor-document", "--dev", "--format", "dir", "--message-format", "json"], ROOT))
    artifacts = envelope["data"]["artifacts"]
    if len(artifacts) != 1 or artifacts[0]["type"] != "directory":
        raise RuntimeError("Expected one document package directory")
    package = Path(artifacts[0]["path"]).resolve()
    package.relative_to((ROOT / "modules/document/target").resolve())
    if {file.name for file in (package / "interface").glob("*.cppm")} != {"document.cppm", "syntax.cppm"}:
        raise RuntimeError("Packed document public interface is incomplete")
    if list(package.rglob("*.cpp")):
        raise RuntimeError("Packed test must link compiled implementations")
    target = ROOT / "target"
    target.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="document-package-consumer-", dir=target) as directory:
        work = Path(directory)
        shutil.copytree(package, work / "package")
        consumer = work / "consumer"
        (consumer / "src").mkdir(parents=True)
        shutil.copy2(ROOT / "mcpp/test-consumers/document/src/main.cpp", consumer / "src/main.cpp")
        manifest = (ROOT / "mcpp/test-consumers/document/mcpp.toml").read_text(encoding="utf-8")
        if manifest.count('../../../modules/document') != 1:
            raise RuntimeError("Unexpected standalone document consumer manifest")
        (consumer / "mcpp.toml").write_text(manifest.replace('../../../modules/document', '../package'), encoding="utf-8")
        output = run(["mcpp", "run"], consumer)
        if "independent document and syntax consumer: passed" not in output:
            raise RuntimeError("Packed document consumer did not complete its checks")
    print("Packed document and syntax interfaces compile and link without implementation sources: passed")


if __name__ == "__main__":
    main()
