#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
import json
from pathlib import Path
import tempfile
import unittest
import zipfile
from verify_ci_release import PLATFORMS, verify


class ReleaseVerificationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.dist = self.root / 'dist'; self.dist.mkdir()
        self.reports = self.root / 'reports'; self.reports.mkdir()
        self.sha = 'a' * 40
        for platform in (*PLATFORMS, 'source'):
            with zipfile.ZipFile(self.dist / f'OpenBMH-1.0.0-{platform}.zip', 'w') as z:
                if platform == 'source':
                    z.writestr('source/SOURCE_MANIFEST.json', json.dumps({'version': '1.0.0', 'project_commit': self.sha}))
                else:
                    z.writestr('synthetic.txt', 'synthetic package')
        for platform in PLATFORMS:
            folder = self.reports / ('tests-' + platform); folder.mkdir()
            (folder / 'test-results.xml').write_text('<testsuite tests="4" failures="0" skipped="1"/>')

    def run_verify(self, sha=None):
        return verify(self.dist, self.reports, 'v1.0.0', sha or self.sha, 123, 'example/project')

    def test_matching_release_records_skips_and_checksums(self):
        report = self.run_verify()
        self.assertEqual(report['tests']['linux-x64']['skipped'], 1)
        self.assertEqual(len(report['archive_sha256']), 4)
        self.assertEqual(len((self.dist / 'SHA256SUMS').read_text().splitlines()), 5)

    def test_wrong_revision_rejected(self):
        with self.assertRaisesRegex(ValueError, 'different'):
            self.run_verify('b' * 40)

    def test_missing_platform_archive_rejected(self):
        (self.dist / 'OpenBMH-1.0.0-windows-x64.zip').unlink()
        with self.assertRaisesRegex(ValueError, 'archive set'):
            self.run_verify()

    def test_failed_or_all_skipped_reports_rejected(self):
        path = self.reports / 'tests-windows-x64' / 'test-results.xml'
        path.write_text('<testsuite tests="4" failures="0"><testcase><failure/></testcase></testsuite>')
        with self.assertRaisesRegex(ValueError, 'failed'):
            self.run_verify()
        path.write_text('<testsuite tests="4" failures="0" skipped="4"/>')
        with self.assertRaisesRegex(ValueError, 'execute'):
            self.run_verify()


if __name__ == '__main__':
    unittest.main()
