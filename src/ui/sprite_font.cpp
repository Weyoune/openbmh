// SPDX-License-Identifier: GPL-3.0-only
#include "formats/badge_policy.hpp"
#include "formats/executable_layout.hpp"
#include "ui/sprite_font.hpp"

#include "ui/setup.hpp"
#include "ui/manager.hpp"

namespace bmh::ui {
namespace {
std::string original_bytes(std::string_view utf8) {
    try { return utf8_to_original(utf8); }
    catch (const FormatError&) {
        std::string safe;
        for (unsigned char c : utf8) safe += c < 128 ? char(c) : '?';
        return safe;
    }
}
}

SpriteFont::SpriteFont(const std::filesystem::path& data_directory)
    : atlas_(load_vga(asset_path(data_directory / "KB", "54.VGA"))),
      exe_(load_le(asset_path(data_directory, "BMH.exe"))) {
    install_original_badge_policy(exe_);
    if (atlas_.height < 21 || atlas_.width <= 0)
        throw FormatError("sprite font: unsupported KB/54 atlas");
}

int SpriteFont::centered_title_x(int measured_width) {
    // DrawnFirstBy centers in [172, 282] using two integer divisions.
    return 172 + (282 - 172) / 2 - measured_width / 2;
}

int SpriteFont::text_width(std::string_view utf8) const {
    int width = 0;
    for (unsigned char c : original_bytes(utf8)) {
        if (c <= 0x1f || c >= 0x80) continue;
        const int glyph = exe_.u8(::bmh::layout::ClubInfo_Raw_RuntimeField3 + c);
        width += glyph ? exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField2 + glyph * 2) - exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField + glyph * 2) : 2;
    }
    return width;
}

int SpriteFont::draw(Image& target, int x, int baseline, std::string_view utf8, int mode) const {
    if (mode < 4 || mode > 6) throw FormatError("sprite font: unsupported mode");
    const int glyph_y = (mode - 4) * 7;
    for (unsigned char c : original_bytes(utf8)) {
        if (c <= 0x1f || c >= 0x80) continue;
        const int glyph = exe_.u8(::bmh::layout::ClubInfo_Raw_RuntimeField3 + c);
        if (!glyph) {
            // ClearsSharedScratchRowToIndex clears the shared scratch row to index 0. The
            // final PathTreatsActive -> UsesForTheseSprites path treats the active
            // DesktopRuntimeField index (0 in this UI path) as transparent, so spaces
            // reveal the already-rendered background.
            x += 2;
            continue;
        }
        const int sx = exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField + glyph * 2);
        const int width = exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField2 + glyph * 2) - sx;
        if (sx < 0 || width < 0 || sx + width > atlas_.width)
            throw FormatError("sprite font: glyph outside KB/54 atlas");
        for (int yy = 0; yy < 7; ++yy) for (int xx = 0; xx < width; ++xx) {
            const int dx = x + xx, dy = baseline - 6 + yy;
            const uint8_t source = atlas_.pixels[size_t(glyph_y + yy) * size_t(atlas_.width) + size_t(sx + xx)];
            // PathTreatsActive uses DesktopRuntimeField (0 for this UI path) as its
            // transparent key. Index 0xff is an ordinary opaque palette value.
            if (source != 0 && dx >= 0 && dy >= 0 && dx < target.width && dy < target.height)
                target.at(dx, dy) = source;
        }
        x += width;
    }
    return x;
}

} // namespace bmh::ui
