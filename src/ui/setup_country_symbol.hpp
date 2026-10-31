// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/palette.hpp"
#include "formats/vga.hpp"
namespace bmh::ui {
// Preserve the original native-sized control and replace only its inner glyph.
Image dual_country_button(const Image& original,const Palette&,bool active);
}
