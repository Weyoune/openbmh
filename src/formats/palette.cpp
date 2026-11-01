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

std::optional<Palette> load_optional_embedded_palette(const std::filesystem::path& path) {
    const Bytes bytes = read_file(path);
    if (bytes.size() < 8) throw FormatError("VGA palette source is truncated");
    const size_t rle_end = 8 + size_t(le32(bytes, 4));
    if (rle_end > bytes.size()) throw FormatError("VGA RLE length exceeds palette source");
    const size_t trailing = bytes.size() - rle_end;
    if (trailing == 0) return std::nullopt;
    if (trailing < 768) throw FormatError("truncated embedded VGA palette");
    Palette palette;
    for (size_t i = 0; i < palette.argb.size(); ++i) {
        const auto channel = [&](size_t component) {
            return uint32_t(vga_dac_rgb8(bytes[rle_end + i * 3 + component]));
        };
        palette.argb[i] = 0xff000000u | channel(0) << 16 | channel(1) << 8 | channel(2);
    }
    return palette;
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
