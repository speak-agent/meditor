# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Sunrisepeak & mcpp-community
"""Length-preserving anonymization of machine-identity build paths in packaged artifacts.

Overwrites personal build paths and this machine's needles byte-for-byte so
release artifacts stop carrying foreign filesystem identities. File sizes, ELF
layout and load behavior stay unchanged: replacements never cross a NUL or
path separator and keep the exact original byte length in UTF-8 and UTF-16LE.

Run after `mcpp pack` and before `check_artifact_privacy.py`. The scanner stays
the gate; this tool only removes what would fail it.
"""
import argparse
import re
import sys
from pathlib import Path

from check_artifact_privacy import (
    PLACEHOLDER_NAMES,
    WIDE_PATH_START,
    private_needles,
)

PERSONAL_PATH_TEXT = re.compile(
    r"(?<![A-Za-z0-9_./])(?:file://)?(?:/(?:home|Users)/|[A-Za-z]:[\\/](?:Users|Documents and Settings)[\\/])([^\x00/\\\r\n]{1,128})[\\/]",
    re.IGNORECASE,
)
MAX_MACHINE_NEEDLE = 4096


def filler(char_length: int) -> str:
    # `<@@@>`-style placeholder: the privacy scanner deliberately accepts names
    # shaped like <...> because they carry no machine identity.
    if char_length >= 2:
        return "<" + "@" * (char_length - 2) + ">"
    return "@" * char_length


def _is_placeholder(raw: bytes, name: bytes) -> bool:
    return (name in PLACEHOLDER_NAMES
            or (raw.startswith(b"<") and raw.endswith(b">"))
            or raw.startswith(b"$"))


def _replace_utf8_layer(buffer: bytearray) -> int:
    """Rewrite personal names in the UTF-8 text layer, in place.

    surrogateescape round-trips arbitrary bytes exactly, and the ASCII filler
    keeps the byte length of every replaced name.
    """
    text = bytes(buffer).decode("utf-8", errors="surrogateescape")
    replacements = []
    for match in PERSONAL_PATH_TEXT.finditer(text):
        raw = match.group(1).encode("utf-8", errors="surrogateescape")
        if _is_placeholder(raw, raw.lower()):
            continue
        replacements.append((match.start(1), match.end(1), len(raw)))
    for start, end, byte_length in reversed(replacements):
        text = text[:start] + filler(byte_length) + text[end:]
    if replacements:
        rewritten = text.encode("utf-8", errors="surrogateescape")
        if len(rewritten) != len(buffer):
            raise RuntimeError("Length changed while anonymizing utf-8")
        buffer[:] = rewritten
    return len(replacements)


def _sanitize_wide_windows(buffer: bytearray) -> int:
    """Rewrite personal names inside UTF-16LE windows, length-preserving.

    Arbitrary binary cannot round-trip through the utf-16 codec, so each
    16-bit unit maps to exactly one char (chr of the unit) and back; lone
    surrogates survive via surrogatepass. Replacement chars are ASCII, so
    the byte length is always preserved.
    """
    total = 0
    for match in WIDE_PATH_START.finditer(bytes(buffer)):
        start = max(match.start() - 32, match.start() % 2)
        end = min(len(buffer), match.end() + 1024)
        end -= (end - start) % 2
        window = bytes(memoryview(buffer)[start:end])
        text = "".join(chr(unit) for unit in memoryview(window).cast("H"))
        replacements = []
        for found in PERSONAL_PATH_TEXT.finditer(text):
            raw = found.group(1).encode("utf-16-le", errors="surrogatepass")
            if _is_placeholder(raw, raw.lower()):
                continue
            replacements.append((found.start(1), found.end(1), len(raw) // 2))
        for s, e, char_length in reversed(replacements):
            text = text[:s] + filler(char_length) + text[e:]
        if replacements:
            buffer[start:end] = text.encode("utf-16-le", errors="surrogatepass")
            total += len(replacements)
    return total


def sanitize_bytes(data: bytes, needles) -> tuple[bytearray, int]:
    """Return the anonymized copy of `data` and the number of replacements."""
    buffer = bytearray(data)
    count = 0
    for needle, _label in needles:
        if not needle or len(needle) > MAX_MACHINE_NEEDLE:
            continue
        occurrences = buffer.count(needle)
        if occurrences:
            buffer = bytearray(bytes(buffer).replace(needle, b"@" * len(needle)))
            count += occurrences
    count += _replace_utf8_layer(buffer)
    count += _sanitize_wide_windows(buffer)
    return buffer, count


def sanitize_tree(directory: Path, dry_run: bool) -> tuple[int, int]:
    needles = private_needles()
    files_changed = 0
    replacements = 0
    for path in sorted(p for p in directory.rglob("*") if p.is_file() and not p.is_symlink()):
        buffer, count = sanitize_bytes(path.read_bytes(), needles)
        if count:
            files_changed += 1
            replacements += count
            if not dry_run:
                path.write_bytes(bytes(buffer))
            print(f"anonymized {count} path(s) in {path.relative_to(directory)}")
    return files_changed, replacements


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    files_changed, replacements = sanitize_tree(args.directory, args.dry_run)
    mode = "reported" if args.dry_run else "anonymized"
    print(f"Package hygiene: {mode} {replacements} machine path(s) across {files_changed} file(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
