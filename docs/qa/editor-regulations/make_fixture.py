#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Make a synthetic display fixture; no real-world or installed game data."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('editor', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='obw-rule-fixture-') as temporary:
        root = Path(temporary)
        settings = root / 'settings.json'
        settings.write_text('{"mcp_enabled":false}')
        subprocess.run([str(args.editor.resolve()), '--headless', '--demo', '--settings', str(settings),
                        '--output', str(root / 'demo.obw')], check=True)
        with zipfile.ZipFile(root / 'demo.obw') as source:
            docs = {file: source.read(file) for file in source.namelist()}
        comps = json.loads(docs['world/competitions.json'])
        league = comps['items'][0]
        league.update({'tier': 2, 'stages': [{'id': 'regular', 'format': 'league', 'teams': 18, 'legs': 2,
            'points': {'win': 3, 'draw': 1, 'loss': 0}, 'tiebreakers': ['points', 'goal_difference', 'goals_for']}],
            'movement': [{'ranks': [1, 2], 'to': 'comp:upper'},
                         {'ranks': [3], 'to': 'comp:playoff', 'role': 'challenger'},
                         {'ranks': [16, 18], 'to': 'comp:lower'}]})
        clubs = json.loads(docs['world/clubs.json'])
        for i in range(16):
            club_id = 'club:fixture-' + str(i)
            clubs['items'].append({'id': club_id, 'name': 'Testverein ' + str(i + 1), 'type': 'club',
                                  'nation': 'nation:de', 'competition': 'comp:demo'})
            league['members'].append(club_id)
        for code, title, tier in [('upper', 'Oberliga (Demo)', 1), ('lower', 'Unterliga (Demo)', 3)]:
            comps['items'].append({'id': 'comp:' + code, 'name': title, 'nation': 'nation:de', 'type': 'league',
                                  'tier': tier, 'stages': [{'id': 'regular', 'format': 'league', 'teams': 18}]})
        comps['items'].append({'id': 'comp:playoff', 'name': 'Relegation (Demo)', 'type': 'cup'})
        comps['items'].append({'id': 'comp:international', 'name': 'Internationaler Demo-Pokal',
            'type': 'club_international', 'slots': [{'from': 'comp:demo', 'ranks': [1, 4], 'stage': 'main',
                'fallback': {'from': 'comp:upper', 'next_unqualified': True}}]})
        for key, value in [('world/competitions.json', comps), ('world/clubs.json', clubs)]:
            docs[key] = json.dumps(value, ensure_ascii=False).encode()
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(args.output, 'w', zipfile.ZIP_DEFLATED) as output:
            for file, data in docs.items():
                output.writestr(file, data)


if __name__ == '__main__':
    main()
