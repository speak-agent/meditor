# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Sunrisepeak & mcpp-community
"""Export committed sources and generate private-path remapping for a candidate build."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import shutil
import os
import shlex
import subprocess
import tarfile
import tomllib
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def privacy_flags(source):
    # The dialect channel also reaches the std BMI and every dependency TU.
    flags = []
    compiler_driver = shutil.which("mcpp")
    if not compiler_driver:
        raise RuntimeError("mcpp is required to resolve candidate build inputs")
    tool_root = Path(compiler_driver).resolve().parent.parent
    for path, replacement in [(Path.home(), "@build-home@"), (tool_root, "@build-tools@"), (ROOT, "."), (source, ".")]:
        for prefix in dict.fromkeys([str(path), path.as_posix()]):
            flag = "-ffile-prefix-map=" + prefix + "=" + replacement
            quoted = subprocess.list2cmdline([flag]) if os.name == "nt" else shlex.quote(flag)
            if quoted not in flags:
                flags.append(quoted)
    return flags


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--target", required=True)
    parser.add_argument("--commit", default="HEAD")
    parser.add_argument("--directory", type=Path)
    args = parser.parse_args()
    commit = subprocess.check_output(["git", "rev-parse", "--verify", "--end-of-options", args.commit + "^{commit}"], cwd=ROOT, text=True).strip()
    specification = json.loads(subprocess.check_output(["git", "show", commit + ":packaging/tools.json"], cwd=ROOT))
    if args.target not in specification["targets"]:
        raise SystemExit("This target has no verified native payload strategy for a release candidate")
    payload = ROOT / "target/payloads" / args.target
    from prepare_payloads import verify
    verify(payload, specification, args.target)
    destination = (args.directory or ROOT / "target/candidates" / commit[:12] / args.target).resolve()
    if destination.exists():
        raise SystemExit("Candidate directory already exists; retain it for review or select a new directory")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".meditor-candidate-", dir=destination.parent) as folder:
        temporary = Path(folder)
        source = temporary / "source"; source.mkdir()
        archive = subprocess.check_output(["git", "archive", "--format=tar", commit], cwd=ROOT)
        with tarfile.open(fileobj=io.BytesIO(archive)) as package:
            package.extractall(source, filter="data")
        manifest = source / "mcpp.toml"
        original = manifest.read_text(encoding="utf-8")
        configuration = tomllib.loads(original)
        if "build" in configuration.get("workspace", {}):
            raise RuntimeError("The candidate workspace already declares build policy; merge its flags before preparing a candidate")
        flags = privacy_flags(destination / "source")
        generated = original.rstrip() + "\n\n[workspace.build]\ndialect_cxxflags = " + json.dumps(flags) + "\n"
        tomllib.loads(generated)
        manifest.write_text(generated, encoding="utf-8")
        shutil.copytree(payload, source / "target/payloads" / args.target, symlinks=True)
        metadata = {
            "schemaVersion": 1, "commit": commit, "target": args.target,
            "lockfileSha256": hashlib.sha256((source / "mcpp.lock").read_bytes()).hexdigest(),
            "toolsManifestSha256": hashlib.sha256((source / "packaging/tools.json").read_bytes()).hexdigest(),
            "generatedManifestSha256": hashlib.sha256(manifest.read_bytes()).hexdigest(),
            "privacyRemapping": True,
        }
        (temporary / "candidate.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
        temporary.rename(destination)
    print("Candidate source prepared from commit " + commit[:12] + " with graph-wide private-path remapping")


if __name__ == "__main__":
    main()
