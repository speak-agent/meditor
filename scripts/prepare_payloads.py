"""Prepare complete, verified upstream tool trees for mcpp deployment rules."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import tarfile
import tempfile
import zipfile

from setup_tools import fetch

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def extract(archive, destination):
    if archive.name.endswith(".zip"):
        with zipfile.ZipFile(archive) as package:
            for member in package.infolist():
                mode = member.external_attr >> 16
                path = PurePosixPath(member.filename)
                if path.is_absolute() or ".." in path.parts or "\\" in member.filename or stat.S_ISLNK(mode):
                    raise RuntimeError("Unsafe upstream archive entry")
                if not (destination / member.filename).resolve().is_relative_to(destination):
                    raise RuntimeError("Upstream archive entry escapes its payload")
            package.extractall(destination)
    else:
        with tarfile.open(archive) as package:
            package.extractall(destination, filter="data")


def payload_root(folder, executable):
    candidates = [path for path in folder.rglob(executable) if path.is_file() and path.parent.name == "bin"]
    if len(candidates) != 1:
        raise RuntimeError("Upstream payload does not have one unambiguous executable")
    return candidates[0].parent.parent


def inventory(directory):
    files = []
    for path in sorted(directory.rglob("*")):
        relative = path.relative_to(directory).as_posix()
        if relative == "inventory.json":
            continue
        if path.is_symlink():
            if not path.resolve().is_relative_to(directory):
                raise RuntimeError("Payload link escapes its tool tree")
            files.append({"path": relative, "link": os.readlink(path)})
        elif path.is_file():
            files.append({"path": relative, "bytes": path.stat().st_size, "sha256": digest(path)})
    return files


def verify(directory, specification, target):
    receipt = json.loads((directory / "inventory.json").read_text(encoding="utf-8"))
    expected = {"schemaVersion": 1, "target": target, "sources": specification["targets"][target]}
    if any(receipt.get(key) != value for key, value in expected.items()):
        raise RuntimeError("Prepared payload does not match its pinned source manifest")
    if receipt.get("files") != inventory(directory):
        raise RuntimeError("Prepared payload inventory is incomplete or changed")
    return receipt


def prepare(destination, specification, target):
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".meditor-payload-", dir=destination.parent) as folder:
        temporary = Path(folder).resolve()
        output = temporary / "prepared"
        output.mkdir()
        for tool, source in specification["targets"][target].items():
            archive = temporary / source["url"].rsplit("/", 1)[1]
            fetch(source["url"], archive)
            if digest(archive) != source["sha256"]:
                raise RuntimeError("Upstream tool checksum does not match the pinned manifest")
            unpacked = temporary / tool
            unpacked.mkdir()
            extract(archive, unpacked)
            executable = tool + (".exe" if "windows" in target else "")
            tree = payload_root(unpacked, executable)
            shutil.copytree(tree, output / "tools" / tool, symlinks=True)
            # The archive may put licenses next to its bin tree rather than inside it.
            for license_file in unpacked.glob("*LICENSE*"):
                if license_file.is_file() and not (output / "tools" / tool / license_file.name).exists():
                    shutil.copy2(license_file, output / "tools" / tool / license_file.name)
        receipt = {"schemaVersion": 1, "target": target, "sources": specification["targets"][target], "files": inventory(output)}
        (output / "inventory.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
        verify(output, specification, target)
        if destination.exists():
            raise RuntimeError("Destination already exists; verify it or select a new preparation directory")
        output.rename(destination)
    return receipt


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--target", required=True)
    parser.add_argument("--directory", type=Path, default=ROOT / "target" / "payloads")
    parser.add_argument("--verify", action="store_true")
    parser.add_argument("--stamp", type=Path)
    args = parser.parse_args()
    specification = json.loads((ROOT / "packaging/tools.json").read_text(encoding="utf-8"))
    if args.target not in specification["targets"]:
        raise SystemExit("No native tool payload for this target; a verified tool-host strategy is required")
    directory = args.directory.resolve() / args.target
    receipt = verify(directory, specification, args.target) if args.verify or directory.exists() else prepare(directory, specification, args.target)
    if args.stamp:
        args.stamp.parent.mkdir(parents=True, exist_ok=True)
        args.stamp.write_text(json.dumps({"schemaVersion": 1, "target": args.target, "inventorySha256": digest(directory / "inventory.json")}) + "\n", encoding="utf-8")
    total = sum(file.get("bytes", 0) for file in receipt["files"])
    print(f"Verified complete {args.target} tool payload: {len(receipt['files'])} entries, {total} bytes")


if __name__ == "__main__":
    main()
