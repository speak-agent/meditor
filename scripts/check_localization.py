# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Sunrisepeak & mcpp-community
"""Validate built-in catalogs and literal UI/CLI references without a toolchain."""
import json
from pathlib import Path
import re
import string

ROOT = Path(__file__).resolve().parents[1]


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("Duplicate translation key")
        result[key] = value
    return result


def arguments(pattern):
    result = set()
    for _, name, specification, conversion in string.Formatter().parse(pattern):
        if name is None:
            continue
        if not re.fullmatch(r"[A-Za-z0-9_]+", name) or specification or conversion:
            raise ValueError("Unsupported translation argument")
        result.add(name)
    return result


def main():
    catalogs = {}
    for language in ["en", "zh"]:
        catalog = json.loads((ROOT / f"resources/i18n/{language}.json").read_text(encoding="utf-8"), object_pairs_hook=unique_object)
        if not isinstance(catalog, dict) or not catalog:
            raise ValueError("Translation catalog must be a nonempty object")
        for key, value in catalog.items():
            if not re.fullmatch(r"[A-Za-z0-9_.-]+", key) or not isinstance(value, str) or not value:
                raise ValueError("Invalid translation key or value")
            if any(ord(character) < 32 and character not in "\n\t\r" for character in value):
                raise ValueError("Unsupported control character in translation")
        catalogs[language] = catalog
    if catalogs["en"].keys() != catalogs["zh"].keys():
        raise ValueError("Translation catalogs have different keys")
    for key in catalogs["en"]:
        if arguments(catalogs["en"][key]) != arguments(catalogs["zh"][key]):
            raise ValueError("Different translation arguments: " + key)
    referenced = set()
    for source in [*ROOT.glob("src/*.cpp"), *ROOT.glob("modules/ui/*.cpp")]:
        referenced.update(re.findall(r'\b(?:t|text|notice)\("([A-Za-z0-9_.-]+)"', source.read_text(encoding="utf-8")))
    for source in ROOT.glob("modules/application/src/*.cpp"):
        referenced.update("error." + code for code in re.findall(r'(?:CommandError|SettingsError)\("([A-Za-z0-9_-]+)"', source.read_text(encoding="utf-8")))
    missing = referenced - catalogs["en"].keys()
    if missing:
        raise ValueError("Missing referenced translations: " + ", ".join(sorted(missing)))
    print(f"Localization catalogs passed ({len(catalogs['en'])} messages, en/zh arguments and literal references)")


if __name__ == "__main__":
    try:
        main()
    except ValueError as error:
        raise SystemExit("Localization validation failed: " + str(error))
    except OSError:
        raise SystemExit("Localization validation failed; check catalog keys, arguments and literal references")
