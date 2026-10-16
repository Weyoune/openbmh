#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""List intentional compatibility changes documented in source comments."""

import argparse
from pathlib import Path
import re

from check_public_tree import cpp_exclusions

MARKER = "OpenBMH deviates from the original:"


def render(root):
    entries = []
    for path in sorted((root / "src").rglob("*")):
        if path.suffix not in {".cpp", ".hpp", ".h", ".c", ".inc"} or "third_party" in path.parts:
            continue
        text = path.read_text(encoding="utf-8")
        spans = list(cpp_exclusions(text))
        at = 0
        while at < len(spans):
            start, end, kind = spans[at]
            at += 1
            if kind != "comment":
                continue
            while at < len(spans):
                following, finish, next_kind = spans[at]
                between = text[end:following]
                if next_kind != "comment" or between.strip() or between.count("\n") > 1:
                    break
                end = finish
                at += 1
            content = text[start:end]
            if MARKER not in content:
                continue
            relative = path.relative_to(root).as_posix()
            for declaration in content.split(MARKER)[1:]:
                description = " ".join(line.strip().removeprefix("//").lstrip("* ")
                                       for line in declaration.removesuffix("*/").splitlines())
                # The first complete sentence is the declared change. Adjacent
                # comments can also describe implementation details or examples.
                description = re.split(r"(?<=[.!?])(?:\s|$)", description, maxsplit=1)[0]
                if (relative, description) not in entries:
                    entries.append((relative, description))
    lines = ["# Intentional differences from the original", "",
             "Generated from source comment markers by `tools/public_release/collect_differences.py`.", "",
             "See [Compatibility decisions](COMPATIBILITY.md) for the overall policy. These entries",
             "describe deliberate changes; they are not a list of every known compatibility limit.", ""]
    for path, description in entries:
        lines.append(f"- [{path}](../{path}): {description}")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    destination = args.root / "docs" / "DIFFERENCES.md"
    expected = render(args.root)
    if args.check:
        if not destination.is_file() or destination.read_text(encoding="utf-8") != expected:
            parser.exit(1, "Differences catalogue needs regeneration\n")
    else:
        destination.write_text(expected, encoding="utf-8")


if __name__ == "__main__":
    main()
