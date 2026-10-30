// SPDX-License-Identifier: GPL-3.0-only
#include "formats/palette.hpp"

#include "formats/binary.hpp"

namespace bmh {

Palette load_palette(const std::filesystem::path& path) {
    Bytes b = read_file(path);
    if (b.size() < 768) throw FormatError("palette too short: " + path.string());
    Palette p;
    for (int i = 0; i < 256; i++) {
        auto c = [&](int k) { return uint32_t(vga_dac_rgb8(b[i * 3 + k])); };
        p.argb[i] = 0xFF000000u | c(0) << 16 | c(1) << 8 | c(2);
    }
    return p;
}

uint8_t Palette::nearest(uint8_t r, uint8_t g, uint8_t b) const {
    int best = 0, best_dist = INT32_MAX;
    for (int i = 0; i < 256; i++) {
        int dr = int(argb[i] >> 16 & 0xFF) - r, dg = int(argb[i] >> 8 & 0xFF) - g, db = int(argb[i] & 0xFF) - b;
        int dist = dr * dr + dg * dg + db * db;
        if (dist < best_dist) best = i, best_dist = dist;
    }
    return uint8_t(best);
}

}  // namespace bmh
