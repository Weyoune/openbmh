// SPDX-License-Identifier: GPL-3.0-only
#include "formats/vga.hpp"

namespace bmh {

void Image::blit(const Image& src, int x, int y) {
    for (int sy = 0; sy < src.height; sy++) {
        int dy = y + sy;
        if (dy < 0 || dy >= height) continue;
        for (int sx = 0; sx < src.width; sx++) {
            int dx = x + sx;
            if (dx >= 0 && dx < width) at(dx, dy) = src.pixels[size_t(sy) * src.width + sx];
        }
    }
}

Image decode_vga(const Bytes& d) {
    if (d.size() < 8) throw FormatError("VGA: file too short");
    Image img(le16(d, 0), le16(d, 2));
    size_t end = 8 + size_t(le32(d, 4));
    if (end > d.size()) throw FormatError("VGA: length field exceeds file");

    size_t si = 8, di = 0, n = img.pixels.size();
    while (di < n) {
        if (si >= end) throw FormatError("VGA: RLE data ends early");
        uint8_t c = d[si++];
        if (c >= 0x80) {  // run: next byte repeated c - 0x7F times
            if (si >= end) throw FormatError("VGA: truncated run");
            uint8_t v = d[si++];
            for (int k = 0; k < c - 0x7F && di < n; k++) img.pixels[di++] = v;
        } else {  // literal: c + 1 bytes
            for (int k = 0; k <= c && di < n; k++) {
                if (si >= end) throw FormatError("VGA: truncated literal");
                img.pixels[di++] = d[si++];
            }
        }
    }
    // exactly one trailing byte follows the pixel data in every original file
    if (si + 1 != end) throw FormatError("VGA: unexpected data after image");
    return img;
}

Image load_vga(const std::filesystem::path& path) { return decode_vga(read_file(path)); }

}  // namespace bmh
