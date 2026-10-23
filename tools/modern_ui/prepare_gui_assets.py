#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prepare a separate private GUI resource directory from the approved HTML mockup.

Never modifies the source study or an OBW world. Original reference directories,
club catalogues and player databases are not copied into the GUI resource set.
"""
import argparse
import ast
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mockup', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source = args.mockup.resolve(strict=True)
    output = args.output.resolve()
    if output == source or source in output.parents or output in source.parents:
        parser.error('Source and output must be independent directories.')
    output.mkdir(parents=True, exist_ok=True)
    items = {}

    def add(key, relative, crop=None):
        path = (source / relative).resolve(strict=True)
        if not path.is_relative_to(source):
            raise ValueError('GUI source escapes the supplied mockup directory.')
        relative = path.relative_to(source).as_posix()
        destination = output / relative
        if not destination.resolve().is_relative_to(output):
            raise ValueError("GUI output escapes the supplied output directory.")
        destination.parent.mkdir(parents=True, exist_ok=True)
        if not destination.exists() or destination.read_bytes() != path.read_bytes():
            shutil.copyfile(path, destination)
        item = {'file': relative, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
        if crop:
            item['crop'] = crop
        items[key] = item

    add('background', 'assets/vendor/stadium-night.png')
    data = (source / 'desktop/assets.js').read_text()
    buttons = json.loads(data[data.index('{'):data.rfind('}') + 1])['buttons']
    routes = ['tables', 'league', 'strengths', 'lineup', 'achievements', 'other',
              'friendlies', 'postpone', 'calendar', 'indoor', 'load', 'save',
              'kickoff', 'toto', 'bank', 'camp', 'training', 'transfer', 'tactics',
              'settings', 'scorers', 'advertising', 'stadium', 'statistics',
              'environment', 'fairplay', 'highscore', 'contracts', 'balance', 'newplayer']
    for route, button in zip(routes, buttons, strict=True):
        add('button.' + route, button['src'].removeprefix('../'))
    add('button.news', 'assets/vector/desktop-news.svg')
    for i in range(9):
        add('training.skill.' + str(i), 'assets/studies/submenus/training-icons-v2.png',
            [(i % 3) / 3, (i // 3) / 3, 1 / 3, 1 / 3])
    add('training.ball', 'assets/studies/submenus/training-ball-v1.png')
    for i in range(12):
        add('manager.' + str(i), 'assets/studies/submenus/manager-portraits-photo-v1.png',
            [(i % 4) / 4, (i // 4) / 3, 1 / 4, 1 / 3])
        add('manager-painted.' + str(i), 'assets/studies/submenus/manager-portraits-v1.png',
            [(i % 4) / 4, (i // 4) / 3, 1 / 4, 1 / 3])
    for folder in ['assets/studies/submenus', 'assets/vector']:
        for path in sorted((source / folder).rglob('*')):
            if path.is_file() and path.suffix.lower() in ('.png', '.svg', '.jpg', '.webp'):
                add('art.' + path.relative_to(source / folder).as_posix(), path.relative_to(source).as_posix())
    map_source = (source / 'desktop/camp-map-data.js').read_text()
    maps = json.loads(re.search(r'Object.freeze\((.*)\);', map_source).group(1))
    regions = {'germany': 'de', 'europe': 'eu', 'gulf': 'me'}
    map_rows = []
    for key, value in maps.items():
        add('map.' + regions[key], value['file'])
        map_rows.append({'id': 'map:' + regions[key], 'name': value['label'],
                         'width': value['view_box'][2], 'height': value['view_box'][3]})
    camp_source = (source / 'desktop/camp-menu.js').read_text()
    block = re.search(r'const resorts=\[(.*?)\n\];', camp_source, re.S).group(1)
    camps = ast.literal_eval('[' + re.sub(r'\b(\w+):', r'"\1":', block) + ']')
    camp_rows = []
    for index, camp in enumerate(camps):
        crop = None
        if 'image' in camp:
            image = camp['image']
        elif camp['region'] == 'gulf':
            image = 'camp-gulf-resort-v1.png'
        else:
            photo = camp['photo']
            image = 'camp-resorts-a-v1.png' if photo < 4 else 'camp-resorts-b-v2.png'
            cell = photo % 4
            crop = [(cell % 2) / 2, (cell // 2) / 2, .5, .5]
        add('camp.' + str(index), 'assets/studies/submenus/' + image, crop)
        projection = maps[camp['region']]['project']
        x = (projection['ox'] + (math.radians(camp['lon']) - projection['xmin']) * projection['scale'])
        y = projection['oy'] + (projection['ymax'] - math.log(math.tan(math.pi / 4 + math.radians(camp['lat']) / 2))) * projection['scale']
        camp_rows.append({'id': 'resort:' + str(index), 'name': camp['name'], 'place': camp['city'],
                          'map': 'map:' + regions[camp['region']], 'x': round(x), 'y': round(y),
                          'quality': camp['quality'], 'price': camp['price'], 'gui_image': 'camp.' + str(index)})
    try:
        revision = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    except subprocess.CalledProcessError:
        revision = 'unversioned'
    document = {'schema': 'openbmh-gui-assets@1', 'source_revision': revision,
                'distribution': 'private-reference-assets', 'items': items,
                'demo': {'maps': map_rows, 'training_camps': camp_rows}}
    (output / 'gui-assets.json').write_text(json.dumps(document, ensure_ascii=False, indent=2) + '\n')
    print(f'Prepared {len(items)} GUI entries, {len(map_rows)} maps and {len(camp_rows)} demo camps.')


if __name__ == '__main__':
    main()
