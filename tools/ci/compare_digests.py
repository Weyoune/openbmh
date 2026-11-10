#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare two directories of season_sim digest files.

season_sim writes, with SIM_DIGEST=<file>, one line per desk:

    turn <n> day <date> digest <16 hex digits>

The digest is a fingerprint of the whole game state and of both random generators
(src/game/digest.hpp). A refactoring that does not change behaviour must reproduce every
line of every reference career. This script compares the *.digest files of a reference
directory with those of a candidate directory and reports, per career, the first desk
that differs.

Usage: compare_digests.py <reference dir> <candidate dir>
Exit code: 0 identical, 1 differences or missing files, 2 usage error.
"""
import pathlib
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    reference, candidate = (pathlib.Path(a) for a in sys.argv[1:])
    names = sorted(p.name for p in reference.glob("*.digest"))
    if not names:
        print(f"no *.digest files in {reference}")
        return 2
    bad = 0
    for name in names:
        ref_lines = (reference / name).read_text().splitlines()
        other = candidate / name
        if not other.exists():
            print(f"{name}: MISSING in the candidate directory")
            bad += 1
            continue
        new_lines = other.read_text().splitlines()
        first = next((i for i, (a, b) in enumerate(zip(ref_lines, new_lines)) if a != b), None)
        if first is None and len(ref_lines) == len(new_lines):
            print(f"{name}: identical ({len(ref_lines)} desks)")
            continue
        bad += 1
        if first is None:
            print(f"{name}: DIFFERENT length, reference {len(ref_lines)} desks, candidate {len(new_lines)}")
        else:
            print(f"{name}: DIFFERENT from line {first + 1}")
            print(f"    reference: {ref_lines[first]}")
            print(f"    candidate: {new_lines[first]}")
    # a career that exists only in the candidate is not an error of the refactoring, but worth a note
    for extra in sorted(p.name for p in candidate.glob("*.digest") if p.name not in names):
        print(f"{extra}: only in the candidate directory (not compared)")
    print("RESULT:", "identical" if bad == 0 else f"{bad} of {len(names)} careers differ")
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
