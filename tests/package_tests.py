#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The release allowlist rejects extra game/test files using synthetic payloads."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

source = Path(__file__).resolve().parents[1] / 'tools' / 'package' / 'make_package.py'
spec = importlib.util.spec_from_file_location('make_package', source)
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class PackageAllowlistTests(unittest.TestCase):
    def test_extra_files_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            # Empty authored placeholders test names/structure, not executable behavior.
            for name in ['openbmh_play', 'openbmh_cli', 'README.md', 'LICENSE',
                         'THIRD_PARTY_NOTICES.md', 'ACKNOWLEDGEMENTS.md', 'CONTRIBUTING.md']:
                (root / name).write_text('synthetic')
            notes = root / 'docs/release/0.1/RELEASE_NOTES.md'
            notes.parent.mkdir(parents=True)
            notes.write_text('synthetic release notes')
            licenses = Path(__file__).resolve().parents[1] / 'docs' / 'licenses'
            for source in licenses.iterdir():
                if source.is_file():
                    target = root / 'docs' / 'licenses' / source.name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_text('synthetic license placeholder')
            package.validate(root, False)
            for name in ['game.fbm', 'BMH.EXE', 'unit_tests', 'secret.txt']:
                (root / name).write_text('synthetic refusal fixture')
                with self.assertRaises(RuntimeError):
                    package.validate(root, False)
                (root / name).unlink()
            (root / 'openbmh_cli').unlink()
            with self.assertRaises(RuntimeError):
                package.validate(root, False)


if __name__ == '__main__':
    unittest.main()
