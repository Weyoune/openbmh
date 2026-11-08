// SPDX-License-Identifier: GPL-3.0-only
// The frame of a wide push button (width > 31) as every dialog of the game draws it, in both looks:
//   raised  - drop shadow at the right and bottom, bevel art at y=74 of the widget image;
//   pressed - no shadow, the button moved two pixels down and right, bevel art at y=44.
// A button without a lasting state (Kaufen, Verkaufen, Angebot, Abbruch, ...) is shown pressed while the mouse button is
// held down; the screens keep the held button in `pressed_*` members and draw it with this helper. Buttons that carry a
// lasting state (a selected competition, a toggle that is on) use the same pressed look.
//
// This is the single place of the drawing; the dialogs only choose the position, width, state and label.
#pragma once

#include <algorithm>
#include <cstdint>

#include "formats/vga.hpp"

namespace bmh::ui {

// Draws the frame of a button of the given width (10 pixels high) with its top left corner at (x,y).
// Returns the offset (0 raised, 2 pressed) that the label must be moved by; the label colour mode of the
// pressed look is the one after the raised one in the executable (Advertising_Button_RuntimeField + 1).
//   transparent: a palette index of the widget image that is not copied (the dialogs that draw over a picture use 0xc0), -1 none.
//   background:  the colour that replaces the drop shadow of a pressed button; -1 takes the pixel under the button's
//                lower left corner, as the original does.
inline int draw_wide_button_frame(Image& out, const Image& art, int x, int y, int width, bool pressed,
                                  int transparent = -1, int background = -1) {
    constexpr int height = 10;
    const auto put = [&](int sx, int sy, int dx, int dy, int w, int h) {
        for (int yy = 0; yy < h; ++yy) for (int xx = 0; xx < w; ++xx) {
            const int tx = dx + xx, ty = dy + yy, px = sx + xx, py = sy + yy;
            if (tx < 0 || ty < 0 || tx >= out.width || ty >= out.height || px < 0 || py < 0 ||
                px >= art.width || py >= art.height) continue;
            const uint8_t color = art.pixels[size_t(py) * size_t(art.width) + size_t(px)];
            if (color != transparent) out.at(tx, ty) = color;
        }
    };
    const auto fill = [&](int fx, int fy, int w, int h, uint8_t color) {
        for (int yy = std::max(0, fy); yy < std::min(out.height, fy + h); ++yy)
            for (int xx = std::max(0, fx); xx < std::min(out.width, fx + w); ++xx) out.at(xx, yy) = color;
    };
    if (pressed) {
        // the place of the shadow shows the background again
        const uint8_t shade = background >= 0 ? uint8_t(background)
            : out.at(std::clamp(x, 0, out.width - 1), std::clamp(y + height, 0, out.height - 1));
        fill(x, y, width + 1, 2, shade);
        fill(x, y, 2, height + 1, shade);
        x += 2;
        y += 2;
    } else {
        fill(x + 2, y + height, width, 2, 0x40);
        fill(x + width, y + 2, 2, height, 0x40);
    }
    const int top = pressed ? 44 : 74;
    put(24, top, x, y, 31, height);
    int at = 31, left = width - 31;
    while (left > 20) {
        const int n = std::min(8, left - 20);
        put(24, top + 10, x + at, y, n, height);
        at += n;
        left -= n;
    }
    put(45 - left, top + 20, x + at, y, left, height);
    return pressed ? 2 : 0;
}

}  // namespace bmh::ui
