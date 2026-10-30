// SPDX-License-Identifier: GPL-3.0-only
#include "formats/font.hpp"

namespace bmh {

Font load_font(const std::filesystem::path& path) {
    Bytes d = read_file(path);
    if (d.size() < 28) throw FormatError("FNT: file too short");
    Font f;
    f.height = le16(d, 0);
    f.flags = d[3];
    f.nominal_width = le16(d, 4);
    f.baseline = le16(d, 6);
    f.lo_char = d[8];
    f.hi_char = d[9];
    f.modulo = le16(d, 16);

    size_t n = size_t(f.hi_char - f.lo_char + 2);
    size_t bmp = 28, loc = bmp + size_t(f.height) * f.modulo, spc = loc + n * 4;
    size_t end = spc + (f.flags == 2 ? 0 : n * 2);
    if (end != d.size()) throw FormatError("FNT: size mismatch in " + path.string());

    f.bitmap.assign(d.begin() + bmp, d.begin() + loc);
    for (size_t i = 0; i < n; i++) {
        int16_t adv = f.flags == 2 ? int16_t(f.nominal_width) : int16_t(le16(d, spc + i * 2));
        f.glyphs.push_back({le16(d, loc + i * 4), le16(d, loc + i * 4 + 2), adv});
    }
    return f;
}

const Font::Glyph& Font::glyph(uint8_t c) const {
    if (c < lo_char || c > hi_char) return glyphs.back();
    return glyphs[c - lo_char];
}

int Font::text_width(std::string_view text) const {
    int w = 0;
    for (char c : text) w += glyph(uint8_t(c)).advance;
    return w;
}

int Font::draw(Image& target, int x, int y, std::string_view text, uint8_t color) const {
    for (char c : text) {
        const Glyph& g = glyph(uint8_t(c));
        for (int row = 0; row < height; row++) {
            for (int b = 0; b < g.bit_width; b++) {
                int bit = g.bit_offset + b;
                if (!(bitmap[size_t(row) * modulo + bit / 8] & (0x80 >> (bit % 8)))) continue;
                int px = x + b, py = y + row;
                if (px >= 0 && py >= 0 && px < target.width && py < target.height) target.at(px, py) = color;
            }
        }
        x += g.advance;
    }
    return x;
}

}  // namespace bmh
