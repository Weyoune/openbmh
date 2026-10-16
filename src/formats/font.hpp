// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// .FNT bitmap fonts, Amiga TextFont layout (docs/formats/FNT.md), original loader Glyph
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

#include "formats/vga.hpp"

namespace bmh {

struct Font {
    int height = 0;
    int baseline = 0;
    int nominal_width = 0;
    uint8_t flags = 0;
    uint8_t lo_char = 0, hi_char = 0;
    int modulo = 0;
    std::vector<uint8_t> bitmap;  // height * modulo, 1 bpp MSB first
    struct Glyph { uint16_t bit_offset, bit_width; int16_t advance; };
    std::vector<Glyph> glyphs;    // hi - lo + 2, last one = replacement glyph

    const Glyph& glyph(uint8_t c) const;
    int text_width(std::string_view text) const;
    // text in the game's ISO 646-DE encoding; returns the x after the last glyph
    int draw(Image& target, int x, int y, std::string_view text, uint8_t color) const;
};

Font load_font(const std::filesystem::path& path);

}  // namespace bmh
