// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/vga.hpp"
namespace bmh::ui {
// Prepared from the two supplied Original atlases; off/on frames are stacked.
Image dual_country_button(const Image& prepared,bool active);
}
