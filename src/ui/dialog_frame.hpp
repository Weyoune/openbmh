// SPDX-License-Identifier: GPL-3.0-only
// The frame of a panel or dialog window as the game draws it everywhere: an optional drop shadow (2 pixels right and
// below), the 16x16 background pattern that the executable carries, the four 4x4 corners of the widget image and its edge
// strips (copied in pieces of at most 74 pixels horizontally and 96 vertically).
#pragma once

#include <algorithm>
#include <cstdint>

#include "formats/executable_layout.hpp"
#include "formats/le.hpp"
#include "formats/vga.hpp"

namespace bmh::ui {

// Draws the frame of the window with top left corner (x,y) and size w x h.
inline void draw_dialog_frame(Image& out, const Image& widgets, const LeImage& exe,
                              int x, int y, int w, int h, bool shadow = true) {
    const auto fill = [&](int fx, int fy, int fw, int fh, uint8_t color) {
        for (int yy = std::max(0, fy); yy < std::min(out.height, fy + fh); ++yy)
            for (int xx = std::max(0, fx); xx < std::min(out.width, fx + fw); ++xx) out.at(xx, yy) = color;
    };
    const auto put = [&](int sx, int sy, int dx, int dy, int cw, int ch) {
        for (int yy = 0; yy < ch; ++yy) for (int xx = 0; xx < cw; ++xx) {
            const int tx = dx + xx, ty = dy + yy, px = sx + xx, py = sy + yy;
            if (tx < 0 || ty < 0 || tx >= out.width || ty >= out.height || px < 0 || py < 0 ||
                px >= widgets.width || py >= widgets.height) continue;
            out.at(tx, ty) = widgets.pixels[size_t(py) * size_t(widgets.width) + size_t(px)];
        }
    };
    if (shadow) { fill(x + 2, y + h, w, 2, 0x40); fill(x + w, y + 2, 2, h, 0x40); }
    for (int yy = 4; yy < h - 3; ++yy) for (int xx = 4; xx < w - 3; ++xx) {
        const int tx = x + xx, ty = y + yy;
        if (tx < 0 || ty < 0 || tx >= out.width || ty >= out.height) continue;
        out.at(tx, ty) = exe.u8(::bmh::layout::Advertising_PatternRect_RuntimeField +
                                size_t((yy - 4) & 15) * 16 + size_t((xx - 4) & 15));
    }
    put(0, 0, x, y, 4, 4);
    put(4, 0, x + w - 4, y, 4, 4);
    put(0, 4, x, y + h - 4, 4, 4);
    put(4, 4, x + w - 4, y + h - 4, 4, 4);
    for (int at = 4; at < w - 4; at += 74) {
        const int n = std::min(74, w - 4 - at);
        put(0, 104, x + at, y, n, 4);
        put(0, 108, x + at, y + h - 4, n, 4);
    }
    for (int at = 4; at < h - 4; at += 96) {
        const int n = std::min(96, h - 4 - at);
        put(0, 8, x, y + at, 4, n);
        put(4, 8, x + w - 4, y + at, 4, n);
    }
}

}  // namespace bmh::ui
