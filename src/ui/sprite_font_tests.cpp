// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/sprite_font.hpp"

#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>

#include "ui/setup.hpp"

using namespace bmh;
using namespace bmh::ui;

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: ui_sprite_font_tests <BMH data>");
        const std::filesystem::path data(argv[1]);
        const SpriteFont font(data);
        const auto exe = load_le(asset_path(data, "BMH.exe"));
        const auto atlas = load_vga(asset_path(data / "KB", "54.VGA"));
        const int glyph = exe.u8(::bmh::layout::ClubInfo_Raw_RuntimeField3 + 'A');
        const int source_x = exe.u16(::bmh::layout::ClubInfo_Raw_RuntimeField + glyph * 2);
        const int glyph_width = exe.u16(::bmh::layout::ClubInfo_Raw_RuntimeField2 + glyph * 2) - source_x;
        check(glyph > 0 && glyph_width > 0, "mode-5 A uses a nonempty source atlas glyph");
        check(font.text_width("A A") == glyph_width * 2 + 2,
              "mode-5 width uses glyph interval and the two-pixel space advance");
        check(font.text_width("Ä") == font.text_width("["),
              "UTF-8 German letters use the game's ISO-646-DE glyph mapping");

        Image output(40, 24, 0x7e);
        const int end_x = font.draw(output, 3, 13, "A");
        check(end_x == 3 + glyph_width, "draw returns the original variable glyph advance");
        bool saw_zero_key = false, saw_opaque_255 = false, saw_visible_shade = false;
        for (int y = 0; y < 7; ++y) for (int x = 0; x < glyph_width; ++x) {
            const uint8_t source = atlas.pixels[size_t(7 + y) * size_t(atlas.width) + size_t(source_x + x)];
            const uint8_t expected = source == 0 ? 0x7e : source;
            check(output.at(3 + x, 7 + y) == expected,
                  "mode-5 draw leaves index zero transparent and preserves all other pixels at baseline minus 6");
            saw_zero_key |= source == 0;
            saw_opaque_255 |= source == 0xff;
            saw_visible_shade |= source == 51 || source == 53 || source == 59;
        }
        check(saw_zero_key && saw_opaque_255 && saw_visible_shade,
              "glyph probe distinguishes transparent index zero, opaque 0xff, and visible KB/54 shades");

        Image spaced(40, 24, 0x7e);
        font.draw(spaced, 3, 13, "A A");
        for (int y = 7; y < 14; ++y)
            for (int x = 3 + glyph_width; x < 3 + glyph_width + 2; ++x)
                check(spaced.at(x, y) == 0x7e,
                      "space's cleared index-zero scratch reveals the preexisting background");

        std::array<bool, 256> palette_values{};
        for (int c = 0x20; c < 0x7f; ++c) {
            const int index = exe.u8(::bmh::layout::ClubInfo_Raw_RuntimeField3 + uint32_t(c));
            if (!index) continue;
            const int sx = exe.u16(::bmh::layout::ClubInfo_Raw_RuntimeField + index * 2);
            const int width = exe.u16(::bmh::layout::ClubInfo_Raw_RuntimeField2 + index * 2) - sx;
            Image glyph_image(width + 1, 7, 0x7e);
            const std::string byte(1, char(c));
            check(font.draw(glyph_image, 0, 6, byte) == width,
                  "each printable ASCII sprite glyph returns its source interval width");
            for (int y = 0; y < 7; ++y) for (int x = 0; x < width; ++x) {
                const uint8_t source = atlas.pixels[size_t(7 + y) * size_t(atlas.width) + size_t(sx + x)];
                palette_values[source] = true;
                check(glyph_image.at(x, y) == (source == 0 ? 0x7e : source),
                      "each printable glyph preserves nonzero pixels and treats only zero as transparent");
            }
        }
        for (uint8_t palette : {uint8_t(0), uint8_t(51), uint8_t(53), uint8_t(59), uint8_t(0xff)})
            check(palette_values[palette], "mode-5 atlas shades and transparency values are represented in the glyph set");

        const std::string settings_title = "EINSTELLUNGEN";
        Image centered_title(320, 240, 0x48);
        const int title_width = font.text_width(settings_title);
        const int title_x = SpriteFont::centered_title_x(title_width);
        check(title_width == 74 && title_x == 190,
              "captured Settings title uses source width 74 and legacy routine center x 190");
        check(font.draw(centered_title, title_x, 13, settings_title) == title_x + title_width,
              "captured Settings title advances by its original mode-5 width");
        const int title_first_glyph = exe.u8(::bmh::layout::ClubInfo_Raw_RuntimeField3 + 'E');
        const int title_first_x = exe.u16(::bmh::layout::ClubInfo_Raw_RuntimeField + title_first_glyph * 2);
        const uint8_t title_first_pixel = atlas.pixels[size_t(7) * size_t(atlas.width) + size_t(title_first_x)];
        check(centered_title.at(title_x, 7) == (title_first_pixel == 0 ? 0x48 : title_first_pixel),
              "the Settings title preserves opaque source pixels and reveals background only for source zero");

        Image clipped(4, 4, 0x55);
        check(font.draw(clipped, -1, 1, "A") == -1 + glyph_width,
              "mode-5 drawing clips safely at output bounds");
        std::string control_bytes(1, '\x01'); control_bytes += 'A'; control_bytes.push_back('\x02');
        check(font.text_width(control_bytes) == glyph_width,
              "nonprintable and unmapped high bytes follow the original skip behavior");
        std::cout << "Original mode-5 sprite font passed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
