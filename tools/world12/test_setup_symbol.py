# SPDX-License-Identifier: GPL-3.0-only
import tempfile
import unittest
from pathlib import Path
import zipfile
from prepare_setup_symbol import compose, prepare, read_member, decode_vga, encode_vga


class SetupSymbolTests(unittest.TestCase):
    def atlases(self):
        german = (102, 120, bytearray([7] * (102 * 120)))
        english = (51, 80, bytearray([7] * (51 * 80)))
        for atlas, left, color in ((german, 12, 23), (english, 13, 47)):
            width, _, pixels = atlas
            for y in range(5, 35):
                for x in range(left, left + 20):
                    pixels[(y + 40) * width + x] = color + y % 3
        german[2][20 * 102 + 51 + 25] = 199  # authored off-state cross texel
        return german, english

    def test_four_complete_original_controls(self):
        from prepare_setup_symbol import tile
        de, en = self.atlases()
        width, height, result = decode_vga(compose(de, en))
        self.assertEqual((width, height), (204, 80))
        for state in range(2):
            expected = [tile(de, 0, state * 40), tile(en, 0, state * 40),
                        tile(de, 51, state * 40), tile(de, 0, 80)]
            for control in range(4):
                for y in range(40):
                    start = (state * 40 + y) * 204 + control * 51
                    self.assertEqual(result[start:start + 51], expected[control][y * 51:(y + 1) * 51])
        self.assertEqual(result[20 * 204 + 2 * 51 + 25], 199)
        self.assertNotEqual(result[20 * 204 + 2 * 51 + 25], result[60 * 204 + 2 * 51 + 25])

    def test_rejects_unsupported_atlas(self):
        _, en = self.atlases()
        with self.assertRaises(ValueError):
            compose((50, 40, bytearray(50 * 40)), en)

    def test_source_archive_and_palette_check(self):
        de, en = self.atlases()
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            a, b = root / 'de.OBD', root / 'en.OBD'
            palette = bytes(range(256)) * 3
            for path, atlas in ((a, de), (b, en)):
                with zipfile.ZipFile(path, 'w') as archive:
                    archive.writestr('kb/20.vga', encode_vga(*atlas))
                    archive.writestr('M3PC.COL', palette)
            self.assertEqual(read_member(a, 'KB/20.VGA'), encode_vga(*de))
            result, report = prepare(a, b)
            self.assertEqual(decode_vga(result)[:2], (204, 80))
            self.assertEqual(report['frames'], ['crossed-out', 'enabled'])
            with zipfile.ZipFile(b, 'w') as archive:
                archive.writestr('KB/20.VGA', encode_vga(*en))
                archive.writestr('M3PC.COL', bytes(768))
            with self.assertRaisesRegex(ValueError, 'palettes differ'):
                prepare(a, b)


if __name__ == '__main__':
    unittest.main()
