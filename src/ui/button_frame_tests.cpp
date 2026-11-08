// SPDX-License-Identifier: GPL-3.0-only
// button_frame.hpp: the shared wide push button must produce exactly the pixels of the drawing that every dialog used
// before it was shared (the reference below is that drawing, written out once). Needs no game data: the art is synthetic.
#include <algorithm>
#include <cstdio>

#include "ui/button_frame.hpp"

using namespace bmh;

namespace {

int failures = 0;

// The drawing as the dialogs had it: shadow or erased frame, left part, repeated 8 pixel strips, right end.
int reference(Image& out, const Image& art, int x, int y, int width, bool pressed, int key, int bg) {
    const auto copy = [&](int sx, int sy, int dx, int dy, int w, int h) {
        for (int yy = 0; yy < h; ++yy) for (int xx = 0; xx < w; ++xx) {
            const uint8_t c = art.pixels[size_t(sy + yy) * art.width + size_t(sx + xx)];
            if (c != key && dx + xx >= 0 && dx + xx < out.width && dy + yy >= 0 && dy + yy < out.height)
                out.at(dx + xx, dy + yy) = c;
        }
    };
    const auto rect = [&](int rx, int ry, int w, int h, uint8_t c) {
        for (int yy = std::max(0, ry); yy < std::min(out.height, ry + h); ++yy)
            for (int xx = std::max(0, rx); xx < std::min(out.width, rx + w); ++xx) out.at(xx, yy) = c;
    };
    if (pressed) {
        const uint8_t c = bg >= 0 ? uint8_t(bg) : out.at(x, y + 10);
        rect(x, y, width + 1, 2, c); rect(x, y, 2, 11, c); x += 2; y += 2;
    } else { rect(x + 2, y + 10, width, 2, 0x40); rect(x + width, y + 2, 2, 10, 0x40); }
    const int sy = pressed ? 44 : 74;
    copy(24, sy, x, y, 31, 10);
    int at = 31, left = width - 31;
    while (left > 20) { const int n = std::min(8, left - 20); copy(24, sy + 10, x + at, y, n, 10); at += n; left -= n; }
    copy(45 - left, sy + 20, x + at, y, left, 10);
    return pressed ? 2 : 0;
}

}  // namespace

int main() {
    Image art(80, 110);
    for (size_t i = 0; i < art.pixels.size(); ++i) art.pixels[i] = uint8_t((i * 37 + i / 7) % 251);
    art.at(30, 80) = 0xc0; art.at(31, 80) = 0xc0;   // transparent samples inside the used slices
    art.at(30, 50) = 0xc0;
    for (const int width : {32, 51, 56, 60, 64, 70, 75, 80, 94, 125})
        for (const bool pressed : {false, true})
            for (const int key : {-1, 0xc0})
                for (const int bg : {-1, 0x42}) {
                    Image a(320, 240), b(320, 240);
                    for (size_t i = 0; i < a.pixels.size(); ++i) a.pixels[i] = b.pixels[i] = uint8_t(i % 97 + 3);
                    const int shift_a = ui::draw_wide_button_frame(a, art, 61, 166, width, pressed, key, bg);
                    const int shift_b = reference(b, art, 61, 166, width, pressed, key, bg);
                    if (shift_a != shift_b || a.pixels != b.pixels) {
                        std::fprintf(stderr, "FAIL: width %d pressed %d key %d background %d\n", width, int(pressed), key, bg);
                        ++failures;
                    }
                }
    // a button at the very edge of the picture is clipped, not an error
    Image edge(320, 240);
    ui::draw_wide_button_frame(edge, art, 300, 232, 75, true);
    ui::draw_wide_button_frame(edge, art, -10, -3, 75, false);
    if (failures) return 1;
    std::puts("PASS shared wide button frame");
    return 0;
}
