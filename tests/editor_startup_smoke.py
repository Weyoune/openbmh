#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Native graphics startup, including Windows Server/WARP and missing assets.

Use an actual display (Xvfb on Linux). Never reads a user profile or game data.
Optionally opens the actual launcher UI too. Windows CI runs on Server 2022.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('editor', type=Path)
    parser.add_argument('--launcher', type=Path)
    args = parser.parse_args()
    # Non-ASCII + spaces exercise UTF-8 native loading and RmlUI URL paths.
    with tempfile.TemporaryDirectory(prefix='OpenBMH Pr\u00fcfung ') as temporary:
        root = Path(temporary)
        executable = args.editor.resolve()
        env = {**os.environ, 'OPENBMH_RML_DRIVER': 'warp', 'OPENBMH_NO_ERROR_DIALOG': '1',
               'LIBGL_ALWAYS_SOFTWARE': '1', 'SDL_AUDIODRIVER': 'dummy'}
        if os.name == 'nt':
            env['SDL_VIDEODRIVER'] = 'windows'
        # Copy the GUI and its own assets, rather than finding files via cwd.
        installed = root / 'Programme'; installed.mkdir()
        exe = installed / executable.name
        shutil.copy2(executable, exe)
        shutil.copytree(executable.parent / 'assets/editor', installed / 'assets/editor')
        for dll in executable.parent.glob('*.dll'):
            shutil.copy2(dll, installed / dll.name)
        settings = root / 'settings.json'
        settings.write_text(json.dumps({'language': 'de', 'theme': 'midnight', 'mcp_enabled': False}))
        for name, options in [('first-start', []), ('demo', ['--demo', '--page', 'regulations'])]:
            screenshot = root / (name + '.png')
            result = subprocess.run([str(exe), '--settings', str(settings), '--quit-after-ms', '1800',
                                     '--screenshot', str(screenshot), *options],
                                    cwd=root, env=env, capture_output=True, timeout=35)
            assert result.returncode == 0, (name, result.returncode, result.stderr.decode(errors='replace'))
            image = screenshot.read_bytes()
            assert image[:8] == b'\x89PNG\r\n\x1a\n' and len(image) > 15000, name
            width, height = struct.unpack_from('>II', image, 16)
            assert width >= 1080 and height >= 720, (width, height)
            if os.name == 'nt':
                assert b'Direct3D 11 (WARP software)' in result.stderr, result.stderr
        result = subprocess.run([str(exe), '--settings', str(settings), '--assets', str(root / 'missing-assets')],
                                cwd=root, env=env, capture_output=True, timeout=20)
        assert result.returncode == 1 and b'ui_assets_missing' in result.stderr, result.stderr
        # Also render and close the real launcher, without needing installed mods.
        if args.launcher:
            original = args.launcher.resolve()
            launcher = installed / original.name
            shutil.copy2(original, launcher)
            shutil.copytree(original.parent / 'assets/launcher', installed / 'assets/launcher')
            for dll in original.parent.glob('*.dll'):
                shutil.copy2(dll, installed / dll.name)
            result = subprocess.run([str(launcher)], cwd=root, env=env, capture_output=True, timeout=25)
            assert result.returncode == 0, result.stderr
            assert b'Native launcher rendered' in result.stdout, result.stdout
            if os.name == 'nt':
                assert b'Direct3D 11 (WARP software)' in result.stderr, result.stderr
        print('Native first start, demo, Unicode asset paths, failure reporting and launcher startup passed')


if __name__ == '__main__':
    main()
