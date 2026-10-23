#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the standalone editor, its native assets and corresponding source.

This deliberately includes no imported world or original game artwork.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import zipfile
from make_source_package import COMPONENTS, MINIZ_FILES, git, copy_files

EXTRA = {
    'editor_json': ('v3.12.0', 'https://github.com/nlohmann/json'),
    'editor_http': ('v0.63.0', 'https://github.com/yhirose/cpp-httplib'),
    'editor_images': ('release-2.8.8', 'https://github.com/libsdl-org/SDL_image'),
    'editor_svg': ('v3.5.0', 'https://github.com/sammycage/lunasvg'),
    'editor_xml': ('v1.15', 'https://github.com/zeux/pugixml'),
}


def zip_tree(root, target):
    with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for file in sorted(root.rglob('*')):
            if file.is_file():
                info = zipfile.ZipInfo.from_file(file, Path(root.name) / file.relative_to(root))
                archive.writestr(info, file.read_bytes(), compress_type=zipfile.ZIP_DEFLATED)


def copy_dependency(directory, target, name, reference):
    if name == 'miniz' and not (directory / '.git').exists():
        for file, expected in MINIZ_FILES.items():
            if hashlib.sha256((directory / file).read_bytes()).hexdigest() != expected:
                raise RuntimeError('Unexpected miniz source')
        copy_files(directory, target, list(map(Path, MINIZ_FILES)), required=True)
        return {'archive': reference}
    commit = git(directory, 'rev-parse', 'HEAD').decode().strip()
    expected = git(directory, 'rev-parse', reference + '^{commit}').decode().strip()
    if commit != expected or git(directory, 'diff', '--name-only', '--ignore-submodules=all').strip():
        raise RuntimeError('Modified dependency: ' + name)
    files, modules = [], {}
    for entry in git(directory, 'ls-files', '--stage', '-z').split(b'\0'):
        if not entry:
            continue
        meta, file = entry.split(b'\t', 1); file = Path(file.decode())
        if meta.startswith(b'160000 '):
            # Only this codec is enabled. Freetype's dlg is unused by CMake.
            required = name == 'editor_images' and file.as_posix() == 'external/libwebp'
            if required:
                module_commit = meta.split()[1].decode()
                modules[str(file)] = copy_dependency(directory / file, target / file, name + '/libwebp', module_commit)
            else:
                modules[str(file)] = {'unbuilt': True, 'commit': meta.split()[1].decode()}
        else:
            files.append(file)
    if name == 'lzma_sdk':
        files = [f for f in files if f.parts[0] == 'C' or f == Path('DOC/License.txt')]
    hashes = copy_files(directory, target, files, required=True)
    return {'commit': commit, 'files': hashes, 'submodules': modules}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--platform', required=True, choices=['linux-x64', 'windows-x64', 'macos-arm64'])
    parser.add_argument('--source-package', action='store_true')
    args = parser.parse_args()
    source, build = args.source.resolve(), args.build.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    commit = git(source, 'rev-parse', 'HEAD').decode().strip()
    version = json.loads((build / 'editor-version.json').read_text())['version']
    if not re.fullmatch(r'\d+\.\d+\.\d+', version):
        raise RuntimeError('Invalid editor version')
    name = 'OpenBMHEditor-' + version + '-' + args.platform
    with tempfile.TemporaryDirectory(prefix='editor-package-') as td:
        stage = Path(td) / name; stage.mkdir()
        exe = 'OpenBMHEditor.exe' if args.platform.startswith('windows') else 'OpenBMHEditor'
        subprocess.run(['cmake', '--install', str(build), '--component', 'Editor', '--config', 'Release',
                        '--prefix', str(stage)], check=True)
        shutil.copytree(source / 'docs/licenses', stage / 'licenses')
        for file in ('LICENSE', 'THIRD_PARTY_NOTICES.md'):
            shutil.copy2(source / file, stage / file)
        shutil.copy2(source / 'docs/EDITOR.md', stage / 'EDITOR.md')
        shutil.copy2(source / 'docs/release/editor-0.5/RELEASE_NOTES.md', stage / 'RELEASE_NOTES.md')
        if args.platform.startswith('windows'):
            # Uses the same app-local CRT/UCRT installation as the main game.
            # A successful runner launch alone does not prove deployability.
            for dll in ('vcruntime140.dll', 'msvcp140.dll'):
                if not (stage / dll).is_file():
                    raise RuntimeError('Required Windows runtime missing: ' + dll)
        (stage / 'BUILD.json').write_text(json.dumps({'commit': commit, 'platform': args.platform, 'version': version,
            'editor_profile': 'editor-world-v1', 'game_data_included': False}, indent=2) + '\n')
        zip_tree(stage, args.out / (name + '.zip'))
        print(args.out / (name + '.zip'))
    if args.source_package:
        deps = {**json.loads((build / 'package-dependencies.json').read_text()),
                **json.loads((build / 'editor-dependencies.json').read_text())}
        components = {**{n: (v[0], v[1]) for n, v in COMPONENTS.items()}, **EXTRA}
        with tempfile.TemporaryDirectory(prefix='editor-source-') as td:
            source_name = 'OpenBMHEditor-' + version + '-source'
            stage = Path(td) / source_name; stage.mkdir()
            project = [Path(f.decode()) for f in git(source, 'ls-files', '-z').split(b'\0') if f]
            manifest = {'project_commit': commit, 'editor_version': version, 'project_files': copy_files(source, stage / 'project', project, required=True), 'dependencies': {}}
            cache = ['set(OPENBMH_BUILD_EDITOR ON CACHE BOOL "" FORCE)']
            for name, (ref, url) in components.items():
                result = copy_dependency(Path(deps[name]), stage / 'dependencies' / name, name, ref)
                manifest['dependencies'][name] = {'reference': ref, 'upstream': url, **result}
                cache.append('set(FETCHCONTENT_SOURCE_DIR_' + name.upper() + ' "${CMAKE_CURRENT_LIST_DIR}/dependencies/' + name + '" CACHE PATH "" FORCE)')
            (stage / 'dependencies.cmake').write_text('\n'.join(cache) + '\n')
            (stage / 'SOURCE_MANIFEST.json').write_text(json.dumps(manifest, indent=2) + '\n')
            (stage / 'BUILD_SOURCE.txt').write_text('cmake -S project -B build -C dependencies.cmake -G Ninja -DCMAKE_BUILD_TYPE=Release\n'
                'cmake --build build --target openbmh_editor editor_core_tests editor_comparison_tests\n'
                'ctest --test-dir build -R "^editor_(core_tests|comparison_tests|version)$" --output-on-failure\n'
                'Native compiler, CMake and OS development libraries: see project/docs/EDITOR.md.\n')
            zip_tree(stage, args.out / (source_name + '.zip'))
            print(args.out / (source_name + '.zip'))


if __name__ == '__main__':
    main()
