#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build products and every configured test program, without unused data fixtures."""
import argparse
import os
from pathlib import Path
import re
import subprocess

TEST = re.compile(r'^add_test\((?:\[=*\[.*?\]=*\]|"[^"]*"|[^ )]+)\s+"((?:\\.|[^"\\])*)"')
HOST = re.compile(r'(?:python(?:[0-9.]+)?|cmake|ctest)(?:\.exe)?$', re.I)


def targets_for_tests(build):
    build = build.resolve(strict=True)
    targets = {'openbmh_play', 'openbmh_cli'}
    if os.name == 'nt':
        targets.add('obdeddi')
    tests = 0
    for file in sorted(build.rglob('CTestTestfile.cmake')):
        for line in file.read_text(encoding='utf-8').splitlines():
            if not line.startswith('add_test('):
                continue
            match = TEST.match(line)
            if not match:
                raise RuntimeError('Unrecognized generated test command; refusing to omit a test')
            tests += 1
            command = match.group(1).replace('\\"', '"').replace('\\\\', '\\')
            executable = Path(command).resolve()
            if executable.is_relative_to(build):
                name = executable.name
                if name.lower().endswith('.exe'):
                    name = name[:-4]
                targets.add('openbmh_play' if name == 'OpenBMH' else name)
            elif not HOST.fullmatch(executable.name):
                raise RuntimeError('Unrecognized external test executable; refusing to omit a test')
    if not tests:
        raise RuntimeError('No configured tests found')
    return sorted(targets), tests


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--config', default='Release')
    parser.add_argument('--parallel', type=int, default=2)
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()
    targets, count = targets_for_tests(args.build)
    print(f'{count} configured tests; {len(targets)} product/test build targets', flush=True)
    if args.list:
        print('\n'.join(targets))
    else:
        subprocess.run(['cmake', '--build', str(args.build), '--config', args.config,
                        '--parallel', str(args.parallel), '--target', *targets], check=True)


if __name__ == '__main__':
    main()
