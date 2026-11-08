// SPDX-License-Identifier: GPL-3.0-only
// The number calculator window of the game (bank, offers, advertising, contracts, stadium, ...): its panel with the
// drop shadow and its keys. The position of the keys and presets comes from tables of the executable, the labels and
// the title are drawn by each dialog; only the shared pictures are drawn here.
//   panel: 113x136 picture from the calculator art at (0,0), shadow 2 pixels right and below.
//   key:   15x14 picture at (0,136) of the art; a key held down by the mouse is the 14x13 picture at (15,136) drawn one
//          pixel down and right, and its legend moves by the same pixel.
#pragma once

#include <algorithm>
#include <cstdint>

#include "formats/vga.hpp"

namespace bmh::ui {

// A key of the calculator window is 16x14 pixels wide for hit tests; this tells whether (x,y) lies in the key at (kx,ky).
inline bool calculator_key_hit(int x, int y, int kx, int ky) { return x >= kx && x < kx + 16 && y >= ky && y < ky + 14; }

// Draws the calculator panel with its top left corner at (x0,y0).
inline void draw_calculator_panel(Image& out, const Image& art, int x0, int y0) {
    const auto put = [&](int sx, int sy, int dx, int dy, int w, int h) {
        for (int yy = 0; yy < h; ++yy) for (int xx = 0; xx < w; ++xx) {
            const int tx = dx + xx, ty = dy + yy, px = sx + xx, py = sy + yy;
            if (tx < 0 || ty < 0 || tx >= out.width || ty >= out.height || px < 0 || py < 0 ||
                px >= art.width || py >= art.height) continue;
            out.at(tx, ty) = art.pixels[size_t(py) * size_t(art.width) + size_t(px)];
        }
    };
    const auto fill = [&](int fx, int fy, int w, int h) {
        for (int yy = std::max(0, fy); yy < std::min(out.height, fy + h); ++yy)
            for (int xx = std::max(0, fx); xx < std::min(out.width, fx + w); ++xx) out.at(xx, yy) = 0x40;
    };
    put(0, 0, x0, y0, 113, 136);
    fill(x0 + 113, y0 + 2, 2, 136);
    fill(x0 + 2, y0 + 136, 113, 2);
}

// Draws the picture of the key whose top left corner is (x,y). Returns the offset (0 or 1) by which the legend moves.
inline int draw_calculator_key(Image& out, const Image& art, int x, int y, bool pressed) {
    const int p = pressed ? 1 : 0;
    for (int yy = 0; yy < 14 - p; ++yy) for (int xx = 0; xx < 15 - p; ++xx) {
        const int tx = x + p + xx, ty = y + p + yy, px = 15 * p + xx, py = 136 + yy;
        if (tx < 0 || ty < 0 || tx >= out.width || ty >= out.height || px >= art.width || py >= art.height) continue;
        out.at(tx, ty) = art.pixels[size_t(py) * size_t(art.width) + size_t(px)];
    }
    return p;
}

}  // namespace bmh::ui
