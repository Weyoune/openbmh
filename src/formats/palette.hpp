// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// M3PC.COL: 256 x RGB, 6 bit per channel (docs/formats/COL.md)
#include <array>
#include <cstdint>
#include <filesystem>

namespace bmh {

struct Palette {
    std::array<uint32_t, 256> argb{};  // 0xAARRGGBB, 8 bit per channel

    // index of the palette entry closest to the given colour
    uint8_t nearest(uint8_t r, uint8_t g, uint8_t b) const;
};

// Expand the low six VGA-DAC bits to the nearest RGB8 level. Verified against
// the running Original; truncation darkens many colours by one channel step.
constexpr uint8_t vga_dac_rgb8(uint8_t channel) {
    return uint8_t(((channel & 0x3f) * 255u + 31u) / 63u);
}

Palette load_palette(const std::filesystem::path& path);

}  // namespace bmh
