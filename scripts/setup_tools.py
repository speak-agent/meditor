"""Install the pinned upstream mcpp payload after verifying its published SHA-256."""
import argparse
import hashlib
import os
from pathlib import Path
import platform
import re
import shutil
import tarfile
import tempfile
import time
import urllib.request
import zipfile

VERSION = "2026.10.21.1"


def fetch(url, target):
    for attempt in range(4):
        try:
            with urllib.request.urlopen(url, timeout=90) as source, open(target, "wb") as output:
                shutil.copyfileobj(source, output)
            return
        except Exception:
            if attempt == 3:
                raise RuntimeError("Pinned tool download failed") from None
            time.sleep(2 ** attempt)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--directory", type=Path, required=True)
    args = parser.parse_args()
    system = {"Linux": "linux", "Darwin": "macosx", "Windows": "windows"}[platform.system()]
    arch = {"x86_64": "x86_64", "AMD64": "x86_64", "aarch64": "aarch64", "arm64": "arm64"}[platform.machine()]
    extension = "zip" if system == "windows" else "tar.gz"
    name = f"mcpp-{VERSION}-{system}-{arch}.{extension}"
    url = f"https://github.com/mcpp-community/mcpp/releases/download/v{VERSION}/{name}"
    destination = args.directory.resolve()
    destination.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="meditor-tool-download-") as folder:
        archive, checksum = Path(folder) / name, Path(folder) / "checksum"
        fetch(url, archive)
        fetch(url + ".sha256", checksum)
        matches = re.findall(r"\b[0-9a-fA-F]{64}\b", checksum.read_text())
        if not matches or hashlib.file_digest(archive.open("rb"), "sha256").hexdigest() != matches[0].lower():
            raise RuntimeError("Pinned tool checksum mismatch")
        if extension == "zip":
            with zipfile.ZipFile(archive) as package:
                for entry in package.infolist():
                    path = (destination / entry.filename).resolve()
                    if not path.is_relative_to(destination):
                        raise RuntimeError("Unsafe archive member")
                package.extractall(destination)
        else:
            with tarfile.open(archive) as package:
                package.extractall(destination, filter="data")
    executable = "mcpp.exe" if system == "windows" else "mcpp"
    candidates = sorted((p for p in destination.rglob(executable) if p.is_file() and p.parent.name == "bin"), key=lambda p: len(p.parts))
    if not candidates:
        raise RuntimeError("Pinned payload has no compiler driver")
    binary = candidates[0]
    if system != "windows":
        for file in binary.parent.iterdir():
            if file.is_file():
                file.chmod(file.stat().st_mode | 0o111)
    if os.environ.get("GITHUB_PATH"):
        with open(os.environ["GITHUB_PATH"], "a", encoding="utf-8") as output:
            output.write(str(binary.parent) + "\n")
    print(f"Verified mcpp {VERSION} for {system}/{arch}")
    print("Payload installed; the CI PATH is configured." if os.environ.get("GITHUB_PATH") else "Add the payload bin directory to PATH.")


if __name__ == "__main__":
    main()
