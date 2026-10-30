#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
import sys
import tempfile
from pathlib import Path
import unittest
from build_runnable_tests import targets_for_tests


class BuildDiscoveryTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='ci build ')
        self.addCleanup(self.temporary.cleanup)
        self.build = Path(self.temporary.name) / 'build'
        self.build.mkdir()

    def write(self, text):
        (self.build / 'CTestTestfile.cmake').write_text(text, encoding='utf-8')

    def test_required_programs_and_optional_data_skip(self):
        self.write(f'add_test([=[unit]=] "{self.build.as_posix()}/unit_suite.exe")\n'
                   f'add_test([=[private]=] "{self.build.as_posix()}/openbmh_test_skip.exe" "missing input")\n'
                   f'add_test([=[python]=] "{Path(sys.executable).as_posix()}" "check.py")\n')
        targets, count = targets_for_tests(self.build)
        self.assertEqual(count, 3)
        self.assertEqual(targets, ['openbmh_cli', 'openbmh_play', 'openbmh_test_skip', 'unit_suite'])

    def test_nested_test_and_product_output_name(self):
        self.write(f'add_test([=[version]=] "{self.build.as_posix()}/OpenBMH.exe" "--version")\n')
        nested = self.build / 'src' / 'ui'
        nested.mkdir(parents=True)
        (nested / 'CTestTestfile.cmake').write_text(
            f'add_test([=[ui]=] "{nested.as_posix()}/ui_suite")\n', encoding='utf-8')
        targets, count = targets_for_tests(self.build)
        self.assertEqual(count, 2)
        self.assertEqual(targets, ['openbmh_cli', 'openbmh_play', 'ui_suite'])

    def test_unknown_external_binary_is_not_silently_dropped(self):
        outside = self.build.parent / 'unknown-suite'
        self.write(f'add_test([=[outside]=] "{outside.as_posix()}")\n')
        with self.assertRaisesRegex(RuntimeError, 'external'):
            targets_for_tests(self.build)

    def test_unrecognized_command_and_empty_configuration_fail(self):
        self.write('add_test(unsupported-syntax)\n')
        with self.assertRaises(RuntimeError):
            targets_for_tests(self.build)
        self.write('# no tests\n')
        with self.assertRaises(RuntimeError):
            targets_for_tests(self.build)


if __name__ == '__main__':
    unittest.main()
