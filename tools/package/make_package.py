#!/usr/bin/env python3
"""Install the Runtime component and make an allowlisted release ZIP."""
# SPDX-License-Identifier: GPL-3.0-only
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
import zipfile


def validate(directory, windows):
    products = {'openbmh_play.exe', 'openbmh_cli.exe'} if windows else {'openbmh_play', 'openbmh_cli'}
    documents = {'README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'ACKNOWLEDGEMENTS.md', 'CONTRIBUTING.md'}
    licenses = {'README.md', 'LGPL-2.1.txt', 'Nuked-OPL3-notices.txt', 'LZMA-SDK.txt', 'SDL2-HIDAPI-BSD.txt',
                'SDL2-HIDAPI-options.txt', 'SDL2-HIDAPI-original.txt', 'SDL2-yuv2rgb.txt',
                'SDL2.txt', 'miniz-source-notices.txt', 'miniz.txt', 'nativefiledialog-extended.txt'}
    present = set()
    for file in directory.rglob('*'):
        if not file.is_file():
            continue
        relative = file.relative_to(directory)
        present.add(relative.as_posix())
        allowed = (len(relative.parts) == 1 and file.name in products | documents)
        allowed |= len(relative.parts) == 3 and relative.parts[:2] == ('docs', 'licenses') and file.name in licenses
        allowed |= (len(relative.parts) >= 2 and relative.parts[0] == 'docs' and
                    'licenses' not in relative.parts and file.suffix in {'.md', '.txt', '.csv'} and
                    all(part not in {'analyses', 'agents', 'manuals', 'validation'} for part in relative.parts))
        allowed |= (windows and len(relative.parts) == 1 and
                    bool(re.fullmatch(r'(?:vcruntime\d+(?:_\d+)?|msvcp\d+(?:_\d+|_atomic_wait|_codecvt_ids)?|'
                                      r'concrt\d+|ucrtbase|api-ms-win-crt-[a-z0-9-]+)\.dll', file.name, re.I)))
        if not allowed:
            raise RuntimeError('Unexpected package entry: ' + relative.as_posix())
    required = products | documents | {'docs/licenses/' + name for name in licenses}
    required.add('docs/release/0.1/RELEASE_NOTES.md')
    if required - present:
        raise RuntimeError('Missing package entries: ' + ', '.join(sorted(required - present)))
    if windows and not {'vcruntime140.dll', 'msvcp140.dll'} <= present:
        raise RuntimeError('App-local Microsoft runtime libraries missing')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--platform', required=True, choices=['windows-x64', 'linux-x64', 'macos-arm64'])
    parser.add_argument('--config', default='Release')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='openbmh-package-', dir=args.out) as temp:
        stage = Path(temp) / 'runtime'
        subprocess.run(['cmake', '--install', str(args.build), '--config', args.config,
                        '--component', 'Runtime', '--prefix', str(stage)], check=True)
        windows = args.platform == 'windows-x64'
        validate(stage, windows)
        cli = stage / ('openbmh_cli.exe' if windows else 'openbmh_cli')
        version = subprocess.check_output([str(cli.resolve()), '--version'], text=True).strip().split()[-1]
        if not re.fullmatch(r'\d+\.\d+\.\d+(?:[-+][A-Za-z0-9.-]+)?', version):
            raise RuntimeError('Invalid product version')
        name = 'OpenBMH-' + version + '-' + args.platform
        destination = args.out / (name + '.zip')
        with zipfile.ZipFile(destination, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
            for file in sorted(stage.rglob('*')):
                if file.is_file():
                    archive.write(file, Path(name) / file.relative_to(stage))
        print(destination)


if __name__ == '__main__':
    main()
