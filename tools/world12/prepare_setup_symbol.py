#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Combine supplied German/English Original controls into private KB/SETUP12.VGA.

All source art and the generated VGA remain externally supplied content. The
public repository contains this compositor and synthetic tests only.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'poc'))
from prepare_international import decode_vga, encode_vga

WIDTH, HEIGHT = 51, 40


def read_member(source, name):
    source = Path(source)
    if source.is_dir():
        direct = source / name
        if direct.is_file():
            return direct.read_bytes()
        bundles = [p for p in source.iterdir() if p.is_file() and p.suffix.lower() == '.obd']
        if len(bundles) != 1:
            raise ValueError(f'Missing {name}: provide the source directory or its OBD')
        source = bundles[0]
    with zipfile.ZipFile(source) as archive:
        matches = [n for n in archive.namelist() if n.upper() == name.upper()]
        if len(matches) != 1:
            raise ValueError(f'Missing or ambiguous source member: {name}')
        return archive.read(matches[0])


def tile(atlas, x, y):
    width, height, pixels = atlas
    if x < 0 or y < 0 or x + WIDTH > width or y + HEIGHT > height:
        raise ValueError('Unsupported source atlas layout')
    return bytearray(pixels[(y + row) * width + x + col] for row in range(HEIGHT) for col in range(WIDTH))


def compose(german, english):
    """Keep original texels at 1:1 size; overlap the two existing silhouettes."""
    de_on, en_on = tile(german, 0, 40), tile(english, 0, 40)
    background = collections.Counter(de_on).most_common(1)[0][0]
    if collections.Counter(en_on).most_common(1)[0][0] != background:
        raise ValueError('The two controls have incompatible background indices')
    enabled = de_on[:]
    for y in range(1, HEIGHT - 1):
        for x in range(1, WIDTH - 1):
            enabled[y * WIDTH + x] = background
    # English control behind, German control in front. Both retain the source
    # shading/texture, with no scaling, generated contours or flag repainting.
    for sprite, destination_x in ((en_on, 1), (de_on, 21)):
        points = [(x, y) for y in range(1, HEIGHT - 1) for x in range(1, WIDTH - 1)
                  if sprite[y * WIDTH + x] != background]
        if not points:
            raise ValueError('Source country sprite is empty')
        left = min(x for x, _ in points)
        right = max(x for x, _ in points)
        if destination_x + right - left >= WIDTH - 1:
            raise ValueError('Source country silhouette exceeds the native control')
        for x, y in points:
            enabled[y * WIDTH + destination_x + x - left] = sprite[y * WIDTH + x]
    # Recover the actual Original red cross (including its shaded edge) from
    # the globe's off/on difference. Do not invent a grey-only disabled state.
    world_off, world_on = tile(german, 51, 0), tile(german, 51, 40)
    disabled = enabled[:]
    cross_pixels = 0
    for y in range(1, HEIGHT - 1):
        for x in range(1, WIDTH - 1):
            index = y * WIDTH + x
            if world_off[index] != world_on[index]:
                disabled[index] = world_off[index]
                cross_pixels += 1
    if not cross_pixels:
        raise ValueError('Source disabled control has no cross overlay')
    return encode_vga(WIDTH, HEIGHT * 2, disabled + enabled)


def prepare(german, english):
    de_atlas, en_atlas = read_member(german, 'KB/20.VGA'), read_member(english, 'KB/20.VGA')
    de_palette, en_palette = read_member(german, 'M3PC.COL'), read_member(english, 'M3PC.COL')
    if de_palette != en_palette:
        raise ValueError('Original palettes differ; implicit recoloring is not supported')
    result = compose(decode_vga(de_atlas), decode_vga(en_atlas))
    sha = lambda value: hashlib.sha256(value).hexdigest()
    report = {'format': 'openbmh-setup-symbol-12', 'sources': {
        'german_atlas_sha256': sha(de_atlas), 'english_atlas_sha256': sha(en_atlas),
        'palette_sha256': sha(de_palette)}, 'output_sha256': sha(result),
        'dimensions': [WIDTH, HEIGHT * 2], 'frames': ['crossed-out', 'enabled'],
        'method': 'Original texels, 1:1 size, English behind German, Original globe cross overlay'}
    return result, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--german', type=Path, required=True)
    parser.add_argument('--english', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    result, report = prepare(args.german, args.english)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(result)
    args.out.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
