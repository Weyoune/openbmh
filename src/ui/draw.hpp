// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The pixel loops shared by the screens: filling a rectangle and copying a rectangle of a picture
// (an atlas) into another, with or without a transparent colour.
//
// Nearly every screen had its own copy of these loops in an anonymous namespace (about 75 copies).
// The screens keep their own small wrappers, because what they check before drawing differs and is
// part of their contract: some reject a source rectangle outside the atlas with their own error
// text, some reject a destination outside the screen, some clip silently. Only the loops are here.
//
// Conventions: `key` is the transparent palette index; a value outside 0..255 (the default -1)
// copies every pixel. Rectangles are x, y, width, height; empty or negative sizes draw nothing.

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "formats/vga.hpp"

namespace bmh::ui::draw {

// Fills the part of the rectangle that lies inside the image.
inline void fill(Image& out, int x, int y, int w, int h, uint8_t color) {
    const int x0 = std::max(0, x), y0 = std::max(0, y);
    const int x1 = std::min(out.width, x + w), y1 = std::min(out.height, y + h);
    for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx) out.pixels[size_t(yy) * size_t(out.width) + size_t(xx)] = color;
}

// True when the rectangle lies completely inside the image (and has no negative size).
inline bool inside(const Image& image, int x, int y, int w, int h) {
    return x >= 0 && y >= 0 && w >= 0 && h >= 0 && x + w <= image.width && y + h <= image.height;
}

// Copies w x h pixels from (sx, sy) of `src` to (x, y) of `out`; pixels that fall outside `out` are
// skipped. The source rectangle must lie inside `src` (the caller checks that and reports its own error).
inline void copy_clipped(Image& out, const Image& src, int sx, int sy, int x, int y, int w, int h, int key = -1) {
    for (int yy = 0; yy < h; ++yy) {
        const int ty = y + yy;
        if (ty < 0 || ty >= out.height) continue;
        for (int xx = 0; xx < w; ++xx) {
            const int tx = x + xx;
            if (tx < 0 || tx >= out.width) continue;
            const uint8_t c = src.pixels[size_t(sy + yy) * size_t(src.width) + size_t(sx + xx)];
            if (int(c) != key) out.pixels[size_t(ty) * size_t(out.width) + size_t(tx)] = c;
        }
    }
}

// The same without clipping: both rectangles must lie inside their images (checked by the caller).
inline void copy_unclipped(Image& out, const Image& src, int sx, int sy, int x, int y, int w, int h, int key = -1) {
    for (int yy = 0; yy < h; ++yy)
        for (int xx = 0; xx < w; ++xx) {
            const uint8_t c = src.pixels[size_t(sy + yy) * size_t(src.width) + size_t(sx + xx)];
            if (int(c) != key) out.pixels[size_t(y + yy) * size_t(out.width) + size_t(x + xx)] = c;
        }
}

}  // namespace bmh::ui::draw
