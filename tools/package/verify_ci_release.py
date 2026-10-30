#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind release archives to one successful CI revision and its test reports."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET
import zipfile

PLATFORMS = ('windows-x64', 'macos-arm64', 'linux-x64')


def verify(dist, reports, tag, source_sha, ci_run, repository):
    match = re.fullmatch(r'v([0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9][A-Za-z0-9.-]*)?)', tag)
    if not match or not re.fullmatch(r'[0-9a-f]{40}', source_sha) or ci_run <= 0:
        raise ValueError('Invalid release identity')
    version = match.group(1)
    expected = {f'OpenBMH-{version}-{p}.zip' for p in (*PLATFORMS, 'source')}
    if {p.name for p in dist.glob('*.zip')} != expected:
        raise ValueError('Release archive set does not match the tag/platforms')
    with zipfile.ZipFile(dist / f'OpenBMH-{version}-source.zip') as source:
        manifests = [n for n in source.namelist() if n.endswith('/SOURCE_MANIFEST.json')]
        if len(manifests) != 1:
            raise ValueError('Missing or ambiguous corresponding-source manifest')
        manifest = json.loads(source.read(manifests[0]))
    if manifest['version'] != version or manifest['project_commit'] != source_sha:
        raise ValueError('Corresponding source is from a different version or revision')
    tests = {}
    for platform in PLATFORMS:
        root = ET.parse(reports / ('tests-' + platform) / 'test-results.xml').getroot()
        suites = list(root.iter('testsuite'))
        if not suites or next(root.iter('failure'), None) is not None or next(root.iter('error'), None) is not None:
            raise ValueError('Missing or failed platform test results')
        values = {key: sum(int(s.get(key, '0')) for s in suites)
                  for key in ('tests', 'failures', 'errors', 'skipped')}
        if values['failures'] or values['errors'] or values['tests'] <= values['skipped']:
            raise ValueError('Platform test results did not execute passing tests')
        tests[platform] = values
    archives = {name: hashlib.sha256((dist / name).read_bytes()).hexdigest() for name in sorted(expected)}
    report = {'version': version, 'tag': tag, 'source_commit': source_sha,
              'ci_run': ci_run, 'ci_url': f'https://github.com/{repository}/actions/runs/{ci_run}',
              'tests': tests, 'archive_sha256': archives}
    output = dist / 'QA-REPORT.json'
    output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    checksums = dict(archives)
    checksums[output.name] = hashlib.sha256(output.read_bytes()).hexdigest()
    (dist / 'SHA256SUMS').write_text(''.join(f'{digest}  {name}\n' for name, digest in sorted(checksums.items())), encoding='utf-8')
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--dist', type=Path, required=True)
    p.add_argument('--reports', type=Path, required=True)
    p.add_argument('--tag', required=True)
    p.add_argument('--source-sha', required=True)
    p.add_argument('--ci-run', type=int, required=True)
    p.add_argument('--repository', required=True)
    a = p.parse_args()
    verify(a.dist, a.reports, a.tag, a.source_sha, a.ci_run, a.repository)
    print('Release archives, source revision and all platform reports verified')


if __name__ == '__main__':
    main()
