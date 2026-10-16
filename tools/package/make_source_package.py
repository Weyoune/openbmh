#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the audited public source and the dependency sources used by its build."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

COMPONENTS = {
    'SDL2': ('release-2.32.10', 'https://github.com/libsdl-org/SDL', ['include/SDL.h', 'src/SDL.c', 'LICENSE.txt']),
    'miniz': ('3.0.2', 'https://github.com/richgel999/miniz', ['miniz.c', 'miniz.h', 'LICENSE']),
    'lzma_sdk': ('24.09', 'https://github.com/ip7z/7zip', ['C/7zDec.c', 'C/7z.h', 'DOC/License.txt']),
    'nfd': ('v1.2.1', 'https://github.com/btzy/nativefiledialog-extended', ['CMakeLists.txt', 'src/nfd_cocoa.m', 'LICENSE']),
}
# Verified against the archive SHA256 pinned in CMakeLists.txt. These are free
# dependency source files, not game data. Refuse modified archive-cache inputs.
MINIZ_FILES = {
    'miniz.c': '0fcdc9888cb3a29ca8f176bac087e5fe6c7258a6ab06b1c271c1e109a11d3740',
    'miniz.h': '295d1a0041aea09609598c0f1f35c1977ca05ad662acbadcfdaac44c140af37b',
    'LICENSE': '0115478d567121238cf6cc1c0c361926cf07a49d9e4c9e66da97fac6a01646b3',
    'ChangeLog.md': '261c5cfb87942b5c47735b98ae2fa87ba5f3058d3c755838756b6117f3b0c0dd',
    'readme.md': 'b4ea367be28a36e5386f65118b73962bc659a67936d248be68b7a7c03b8d359b',
}


def git(directory, *arguments):
    return subprocess.check_output(['git', '-C', str(directory), *arguments])


def copy_files(source, destination, files, required=False):
    manifest = {}
    for relative in sorted(set(files)):
        origin = source / relative
        if not origin.is_file():
            if required:
                raise RuntimeError('Missing dependency source file: ' + relative.as_posix())
            continue
        if not origin.resolve().is_relative_to(source.resolve()):
            raise RuntimeError('Source symlink leaves its component')
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(origin, target)
        manifest[relative.as_posix()] = hashlib.sha256(target.read_bytes()).hexdigest()
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--build', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    source = args.source.resolve()
    subprocess.run([sys.executable, str(source / 'tools/public_release/check_public_tree.py'),
                    '--root', str(source)], check=True)
    cache = (args.build / 'CMakeCache.txt').read_text()
    import re
    version = re.search(r'^CMAKE_PROJECT_VERSION:STATIC=([^\n]+)$', cache, re.M).group(1)
    if not re.fullmatch(r'\d+\.\d+\.\d+', version):
        raise RuntimeError('Unexpected source version')
    dependencies = json.loads((args.build / 'package-dependencies.json').read_text())
    args.out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='openbmh-source-', dir=args.out) as temporary:
        stage = Path(temporary)
        project_files = [Path(p.decode('utf-8')) for p in git(source, 'ls-files', '-z', '--cached', '--others', '--exclude-standard').split(b'\0') if p]
        manifest = {'version': version, 'project_commit': git(source, 'rev-parse', 'HEAD').decode().strip(),
                    'project_files': copy_files(source, stage / 'project', project_files), 'dependencies': {}}
        initial_cache = []
        for name, (reference, url, markers) in COMPONENTS.items():
            directory = Path(dependencies.get(name, ''))
            if not dependencies.get(name) or not all((directory / marker).is_file() for marker in markers):
                raise RuntimeError('Actual dependency source unavailable: ' + name)
            commit = None
            if (directory / '.git').exists():
                commit = git(directory, 'rev-parse', 'HEAD').decode().strip()
                tag = git(directory, 'rev-parse', reference + '^{commit}').decode().strip()
                if commit != tag or git(directory, 'status', '--porcelain', '--untracked-files=no').strip():
                    raise RuntimeError('Dependency differs from its pinned source: ' + name)
                if git(directory, 'ls-files', '--stage').find(b'160000 ') != -1:
                    raise RuntimeError('Dependency submodules require explicit packaging: ' + name)
            if commit:
                files = [Path(p.decode('utf-8')) for p in git(directory, 'ls-files', '-z').split(b'\0') if p]
                if name == 'lzma_sdk':
                    files = [p for p in files if p.parts[0] == 'C' or p == Path('DOC/License.txt')]
            else:
                # The pinned miniz amalgamation is an archive rather than a Git checkout.
                if name != 'miniz':
                    raise RuntimeError('Dependency revision cannot be verified: ' + name)
                files = [Path(p) for p in MINIZ_FILES]
                for filename, expected in MINIZ_FILES.items():
                    if hashlib.sha256((directory / filename).read_bytes()).hexdigest() != expected:
                        raise RuntimeError('Dependency differs from its pinned archive: ' + name)
            hashes = copy_files(directory, stage / 'dependencies' / name, files, required=True)
            manifest['dependencies'][name] = {'upstream': url, 'reference': reference,
                                              'commit': commit, 'files': hashes}
            initial_cache.append('set(FETCHCONTENT_SOURCE_DIR_' + name.upper() +
                                 ' "${CMAKE_CURRENT_LIST_DIR}/dependencies/' + name + '" CACHE PATH "" FORCE)')
        (stage / 'dependencies.cmake').write_text('\n'.join(initial_cache) + '\n')
        (stage / 'SOURCE_MANIFEST.json').write_text(json.dumps(manifest, indent=2) + '\n')
        (stage / 'BUILD_SOURCE.md').write_text(
            '# Corresponding source\n\nThe project contains its patches, build scripts and vendored Nuked OPL3. '
            'The dependency folders contain the sources used by this build; the LZMA folder contains the C decoder tree and upstream license. '
            'File hashes and upstream revisions are in SOURCE_MANIFEST.json. No game data or test references are included.\n\n'
            'With the native compiler, CMake and platform development libraries described in project/README.md:\n\n'
            '```sh\ncmake -S project -B build -C dependencies.cmake -DCMAKE_BUILD_TYPE=Release -DBMH_DATA=\n'
            'cmake --build build --config Release --parallel 2\nctest --test-dir build -C Release --output-on-failure\n```\n\n'
            'Modify the library/application sources and rebuild with these scripts to produce a relinked program. '
            'The source package does not supply the compiler, operating-system SDK or system libraries.\n')
        destination = args.out / ('OpenBMH-' + version + '-source.zip')
        with zipfile.ZipFile(destination, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
            for file in sorted(stage.rglob('*')):
                if file.is_file():
                    archive.write(file, Path(destination.stem) / file.relative_to(stage))
        print(destination)


if __name__ == '__main__':
    main()
