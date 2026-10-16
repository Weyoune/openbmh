#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the public command catalogue and relative Markdown links."""

import argparse
import csv
from pathlib import Path
import re
import sys
from urllib.parse import unquote, urlsplit


def check(root):
    documents = root / "docs" / "CLI_CONCEPT"
    errors = []
    with (documents / "commands.csv").open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter=";")
        expected = ["window", "command", "parameters", "effect", "errors", "engine", "c4", "status", "source"]
        if reader.fieldnames != expected:
            return ["commands.csv: unexpected schema"], 0
        rows = list(reader)
    seen = set()
    for line, row in enumerate(rows, 2):
        if None in row or any(value is None for value in row.values()):
            errors.append(f"commands.csv:{line}: incorrect column count")
            continue
        if row["status"] not in {"ready", "engine", "partial", "gap", "cli"}:
            errors.append(f"commands.csv:{line}: invalid status")
        if not row["command"] or not row["window"]:
            errors.append(f"commands.csv:{line}: missing window or command")
        for window in row["window"].split("|"):
            identity = (window, re.sub(r"[._]", "", row["command"]).lower())
            if identity in seen:
                errors.append(f"commands.csv:{line}: duplicate window/command")
            seen.add(identity)
        source = row["source"].split(":", 1)[0]
        if source and not (documents / source).is_file():
            errors.append(f"commands.csv:{line}: source document missing")
    for path in root.rglob("*.md"):
        if any(part.startswith("build") or part in {".git", "_deps"} for part in path.relative_to(root).parts):
            continue
        for target in re.findall(r"\]\(([^)]+)\)", path.read_text(encoding="utf-8")):
            target = target.strip().strip("<>")
            parsed = urlsplit(target)
            if parsed.scheme or parsed.netloc or not parsed.path:
                continue
            if not (path.parent / unquote(parsed.path)).exists():
                errors.append(f"{path.relative_to(root)}: broken relative link {target}")
    return errors, len(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    errors, commands = check(args.root.resolve())
    for error in errors:
        print(error)
    print(f"Command catalogue: {commands} entries, {len(errors)} errors")
    return int(bool(errors))


if __name__ == "__main__":
    sys.exit(main())
