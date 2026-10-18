#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The release allowlist rejects extra game/test files using synthetic payloads."""
import importlib.util
import hashlib
import stat
from pathlib import Path
import tempfile
import unittest

source = Path(__file__).resolve().parents[1] / 'tools' / 'package' / 'make_package.py'
spec = importlib.util.spec_from_file_location('make_package', source)
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)
source_spec = importlib.util.spec_from_file_location('make_source_package', source.with_name('make_source_package.py'))
source_package = importlib.util.module_from_spec(source_spec)
source_spec.loader.exec_module(source_package)


class PackageAllowlistTests(unittest.TestCase):
    def test_extra_files_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            # Empty authored placeholders test names/structure, not executable behavior.
            for name in ['OpenBMH', 'openbmh_cli', 'openbmh_import', 'README.md', 'LICENSE',
                         'THIRD_PARTY_NOTICES.md', 'ACKNOWLEDGEMENTS.md', 'CONTRIBUTING.md']:
                (root / name).write_text('synthetic')
            notes = root / 'docs/release/0.1/RELEASE_NOTES.md'
            notes.parent.mkdir(parents=True)
            notes.write_text('synthetic release notes')
            for name in ['launcher.rml', 'theme.rcss', 'stadium.tga', 'LatoLatin-Regular.ttf', 'LatoLatin-Bold.ttf']:
                asset = root / 'assets' / 'launcher' / name
                asset.parent.mkdir(parents=True, exist_ok=True)
                asset.write_text('synthetic packaged UI asset')
            licenses = Path(__file__).resolve().parents[1] / 'docs' / 'licenses'
            for source in licenses.iterdir():
                if source.is_file():
                    target = root / 'docs' / 'licenses' / source.name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_text('synthetic license placeholder')
            package.validate(root, False)
            missing = root / 'assets/launcher/LatoLatin-Bold.ttf'
            missing.unlink()
            with self.assertRaises(RuntimeError):
                package.validate(root, False)
            missing.write_text('synthetic font')
            schema = root / 'docs/formats/obw/schema/1.0/manifest.json'
            schema.parent.mkdir(parents=True)
            schema.write_text('{"type":"object"}')
            package.validate(root, False)
            unexpected_json = root / 'docs/world.json'
            unexpected_json.write_text('{"synthetic":true}')
            with self.assertRaises(RuntimeError):
                package.validate(root, False)
            unexpected_json.unlink()
            for name in ['game.fbm', 'BMH.EXE', 'unit_tests', 'secret.txt']:
                (root / name).write_text('synthetic refusal fixture')
                with self.assertRaises(RuntimeError):
                    package.validate(root, False)
                (root / name).unlink()
            (root / 'openbmh_cli').unlink()
            with self.assertRaises(RuntimeError):
                package.validate(root, False)


class SourceInventoryTests(unittest.TestCase):
    def test_only_pinned_unused_freetype_logging_submodule_is_omitted(self):
        record = b'160000 72dfcc858c040c54a6a0b88fcb7e70ee186d3167 0\tsubprojects/dlg\0'
        self.assertEqual(source_package.unbuilt_submodules('freetype', record),
                         {'subprojects/dlg': '72dfcc858c040c54a6a0b88fcb7e70ee186d3167'})
        for component, entry in [('SDL2', record), ('freetype', record.replace(b'subprojects/dlg', b'other')),
                                 ('freetype', record.replace(b'72df', b'0000'))]:
            with self.assertRaises(RuntimeError):
                source_package.unbuilt_submodules(component, entry)

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / 'component'
        self.source.mkdir()
        self.destination = self.root / 'materialized'

    def authored(self, name, text='authored source'):
        target = self.source / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text)
        return target

    def copy(self, names, **kwargs):
        self.links = {}
        return source_package.copy_files(self.source, self.destination, [Path(n) for n in names],
                                         materialized_links=self.links, **kwargs)

    def test_directory_link_copies_only_tracked_contents_and_mode(self):
        script = self.authored('real/sub/build.sh', '#!/bin/sh\nexit 0\n')
        script.chmod(0o751)
        self.authored('real/untracked.txt', 'must never be packaged')
        (self.source / 'alias').symlink_to('real', target_is_directory=True)
        hashes = self.copy(['alias', 'real/sub/build.sh'], required=True)
        copied = self.destination / 'alias/sub/build.sh'
        self.assertEqual(copied.read_bytes(), script.read_bytes())
        self.assertFalse((self.destination / 'alias').is_symlink())
        self.assertFalse((self.destination / 'alias/untracked.txt').exists())
        self.assertFalse((self.destination / 'real/untracked.txt').exists())
        self.assertEqual(stat.S_IMODE(copied.stat().st_mode), stat.S_IMODE(script.stat().st_mode))
        self.assertEqual(hashes['alias/sub/build.sh'], hashlib.sha256(script.read_bytes()).hexdigest())
        self.assertEqual(self.links['alias'], {'kind': 'directory', 'target': 'real', 'via': ['alias']})

    def test_tracked_file_link_chain_is_materialized_and_recorded(self):
        target = self.authored('tool.sh', '#!/bin/sh\nexit 0\n')
        target.chmod(0o755)
        (self.source / 'middle').symlink_to('tool.sh')
        (self.source / 'entry').symlink_to('middle')
        hashes = self.copy(['entry', 'middle', 'tool.sh'], required=True)
        copied = self.destination / 'entry'
        self.assertFalse(copied.is_symlink())
        self.assertEqual(copied.read_bytes(), target.read_bytes())
        self.assertEqual(stat.S_IMODE(copied.stat().st_mode), stat.S_IMODE(target.stat().st_mode))
        self.assertEqual(hashes['entry'], hashes['tool.sh'])
        self.assertEqual(self.links['entry'], {'kind': 'file', 'target': 'tool.sh', 'via': ['entry', 'middle']})

    def test_untracked_file_target_is_rejected(self):
        self.authored('untracked.txt')
        (self.source / 'entry').symlink_to('untracked.txt')
        with self.assertRaisesRegex(RuntimeError, 'not in the tracked inventory'):
            self.copy(['entry'], required=True)

    def test_directory_without_tracked_targets_is_rejected(self):
        self.authored('real/untracked.txt')
        (self.source / 'entry').symlink_to('real', target_is_directory=True)
        with self.assertRaisesRegex(RuntimeError, 'no tracked contents'):
            self.copy(['entry'], required=True)

    def test_untracked_intermediate_link_is_rejected(self):
        self.authored('tracked.txt')
        (self.source / 'middle').symlink_to('tracked.txt')
        (self.source / 'entry').symlink_to('middle')
        with self.assertRaisesRegex(RuntimeError, 'not in the tracked inventory'):
            self.copy(['entry', 'tracked.txt'], required=True)

    def test_external_file_directory_and_returning_link_are_rejected(self):
        outside = self.root / 'outside'
        outside.mkdir()
        (outside / 'file.txt').write_text('external source')
        self.authored('tracked.txt')
        (outside / 'returning').symlink_to(self.source / 'tracked.txt')
        for name, target, directory in [('file', outside / 'file.txt', False),
                                         ('directory', outside, True),
                                         ('returning', outside / 'returning', False)]:
            with self.subTest(name=name):
                (self.source / name).symlink_to(target, target_is_directory=directory)
                with self.assertRaisesRegex(RuntimeError, 'leaves its component'):
                    self.copy([name, 'tracked.txt'], required=True)

    def test_file_and_directory_loops_are_rejected(self):
        (self.source / 'first').symlink_to('second')
        (self.source / 'second').symlink_to('first')
        with self.assertRaisesRegex(RuntimeError, 'loop'):
            self.copy(['first', 'second'], required=True)
        (self.source / 'directory').symlink_to('.', target_is_directory=True)
        with self.assertRaisesRegex(RuntimeError, 'loop'):
            self.copy(['directory'], required=True)

    def test_parent_component_after_link_uses_resolved_directory(self):
        self.authored('real/sub/keep.txt')
        target = self.authored('real/tool.sh', 'resolved parent source')
        (self.source / 'jump').symlink_to('real/sub', target_is_directory=True)
        (self.source / 'entry').symlink_to('jump/../tool.sh')
        self.copy(['entry', 'jump', 'real/sub/keep.txt', 'real/tool.sh'], required=True)
        self.assertEqual((self.destination / 'entry').read_bytes(), target.read_bytes())
        self.assertEqual(self.links['entry']['target'], 'real/tool.sh')

    def test_dangling_link_is_rejected_even_for_optional_project_files(self):
        (self.source / 'entry').symlink_to('missing.txt')
        with self.assertRaisesRegex(RuntimeError, 'Missing dependency source file'):
            self.copy(['entry', 'missing.txt'])


if __name__ == '__main__':
    unittest.main()
