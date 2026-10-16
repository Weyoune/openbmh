// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <filesystem>
#include <string_view>

#include "formats/le.hpp"
#include "formats/vga.hpp"

namespace bmh::ui {

// Original CareerSession_DispatchLeagueTableControl_RuntimeField2 mode 5. The caller owns no asset data and may reuse one
// instance for any screen whose original text path selects this sprite font.
class SpriteFont {
public:
    explicit SpriteFont(const std::filesystem::path& data_directory);
    static int centered_title_x(int measured_width);
    int text_width(std::string_view utf8) const;
    // Original modes 5 and 6 share the executable glyph map and select
    // seven-pixel rows from KB/54.VGA.
    int draw(Image& target, int x, int baseline, std::string_view utf8, int mode = 5) const;

private:
    Image atlas_;
    LeImage exe_;
};

} // namespace bmh::ui
