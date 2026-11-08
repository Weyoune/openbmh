// SPDX-License-Identifier: GPL-3.0-only
// button_frame.hpp: the shared wide push button must produce exactly the pixels of the drawing that every dialog used
// before it was shared (the reference below is that drawing, written out once). Needs no game data: the art is synthetic.
#include <algorithm>
#include <cstdio>

#include "formats/diamond_image.hpp"
#include "ui/button_frame.hpp"
#include "ui/calculator_widgets.hpp"
#include "ui/dialog_frame.hpp"

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

// The window frame as every dialog drew it before it was shared.
void reference_frame(Image& o, const Image& a, const LeImage& exe, int x, int y, int w, int h, bool shadow) {
    const auto fill = [&](int fx, int fy, int fw, int fh, uint8_t c) {
        for (int yy = std::max(0, fy); yy < std::min(o.height, fy + fh); ++yy)
            for (int xx = std::max(0, fx); xx < std::min(o.width, fx + fw); ++xx) o.at(xx, yy) = c;
    };
    const auto copy = [&](int sx, int sy, int dx, int dy, int cw, int ch) {
        for (int yy = 0; yy < ch; ++yy) for (int xx = 0; xx < cw; ++xx)
            o.at(dx + xx, dy + yy) = a.pixels[size_t(sy + yy) * a.width + size_t(sx + xx)];
    };
    if (shadow) { fill(x + 2, y + h, w, 2, 0x40); fill(x + w, y + 2, 2, h, 0x40); }
    for (int yy = 4; yy < h - 3; ++yy) for (int xx = 4; xx < w - 3; ++xx)
        o.at(x + xx, y + yy) = exe.u8(::bmh::layout::Advertising_PatternRect_RuntimeField + size_t((yy - 4) & 15) * 16 + size_t((xx - 4) & 15));
    copy(0, 0, x, y, 4, 4); copy(4, 0, x + w - 4, y, 4, 4); copy(0, 4, x, y + h - 4, 4, 4); copy(4, 4, x + w - 4, y + h - 4, 4, 4);
    for (int at = 4; at < w - 4; at += 74) { const int n = std::min(74, w - 4 - at); copy(0, 104, x + at, y, n, 4); copy(0, 108, x + at, y + h - 4, n, 4); }
    for (int at = 4; at < h - 4; at += 96) { const int n = std::min(96, h - 4 - at); copy(0, 8, x, y + at, 4, n); copy(4, 8, x + w - 4, y + at, 4, n); }
}

void frames_and_calculator(const Image& art) {
    const LeImage exe = diamond_constants_image();
    for (const auto [w, h] : {std::pair{217, 99}, std::pair{319, 90}, std::pair{113, 140}, std::pair{42, 42}, std::pair{300, 200}})
        for (const bool shadow : {false, true}) {
            Image a(320, 240, 5), b(320, 240, 5);
            ui::draw_dialog_frame(a, art, exe, 0, 11, w, h, shadow);   // inside the picture: the reference does not clip
            reference_frame(b, art, exe, 0, 11, w, h, shadow);
            if (a.pixels != b.pixels) { std::fprintf(stderr, "FAIL: dialog frame %dx%d shadow %d\n", w, h, int(shadow)); ++failures; }
        }
    // the calculator: the panel is the 113x136 picture with its shadow, a key the 15x14 (pressed 14x13) picture
    Image big(130, 160);
    for (size_t i = 0; i < big.pixels.size(); ++i) big.pixels[i] = uint8_t((i * 29 + 5) % 253);
    for (const bool pressed : {false, true}) {
        Image a(320, 240, 7), b(320, 240, 7);
        const int shift = ui::draw_calculator_key(a, big, 50, 60, pressed);
        const int p = pressed ? 1 : 0;
        for (int yy = 0; yy < 14 - p; ++yy) for (int xx = 0; xx < 15 - p; ++xx)
            b.at(50 + p + xx, 60 + p + yy) = big.pixels[size_t(136 + yy) * big.width + size_t(15 * p + xx)];
        if (shift != p || a.pixels != b.pixels) { std::fprintf(stderr, "FAIL: calculator key pressed %d\n", int(pressed)); ++failures; }
    }
    Image panel(320, 240, 7), expected(320, 240, 7);
    ui::draw_calculator_panel(panel, big, 103, 55);
    for (int yy = 0; yy < 136; ++yy) for (int xx = 0; xx < 113; ++xx) expected.at(103 + xx, 55 + yy) = big.pixels[size_t(yy) * big.width + size_t(xx)];
    for (int yy = 0; yy < 136; ++yy) for (int xx = 0; xx < 2; ++xx) expected.at(103 + 113 + xx, 55 + 2 + yy) = 0x40;
    for (int yy = 0; yy < 2; ++yy) for (int xx = 0; xx < 113; ++xx) expected.at(103 + 2 + xx, 55 + 136 + yy) = 0x40;
    if (panel.pixels != expected.pixels) { std::fprintf(stderr, "FAIL: calculator panel\n"); ++failures; }
    if (!ui::calculator_key_hit(103, 70, 103, 70) || ui::calculator_key_hit(119, 70, 103, 70) ||
        ui::calculator_key_hit(103, 84, 103, 70)) { std::fprintf(stderr, "FAIL: calculator key hit test\n"); ++failures; }
}

}  // namespace

int main() {
    Image art(80, 120);
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
    frames_and_calculator(art);
    if (failures) return 1;
    std::puts("PASS shared button, dialog frame and calculator widgets");
    return 0;
}
