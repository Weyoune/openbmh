#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Combine supplied German/English Original controls into private KB/SETUP12.VGA.

All source art and the generated VGA remain externally supplied content. The
public repository contains this compositor and synthetic tests only.
"""
import argparse
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
    """Copy the four complete Original controls without repainting any texel."""
    rows = bytearray(WIDTH * 4 * HEIGHT * 2)
    for active in range(2):
        controls = [tile(german, 0, active * HEIGHT),
                    tile(english, 0, active * HEIGHT),
                    tile(german, WIDTH, active * HEIGHT),
                    tile(german, 0, HEIGHT * 2)]
        for control, pixels in enumerate(controls):
            for y in range(HEIGHT):
                start = (active * HEIGHT + y) * WIDTH * 4 + control * WIDTH
                rows[start:start + WIDTH] = pixels[y * WIDTH:(y + 1) * WIDTH]
    return encode_vga(WIDTH * 4, HEIGHT * 2, rows)


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
        'dimensions': [WIDTH * 4, HEIGHT * 2], 'controls': ['germany', 'england', 'international', 'load'], 'frames': ['crossed-out', 'enabled'],
        'method': 'Four independent Original controls, 1:1 texels, including Original crossed-out states'}
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
