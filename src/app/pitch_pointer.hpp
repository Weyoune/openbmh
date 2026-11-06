// SPDX-License-Identifier: GPL-3.0-only
// Mouse pointer for the pitch page. SDL owns the pointer position; the UI only hands over
// the original 11x11 pixel bitmap (121 ARGB values). PitchPointer builds an SDL colour
// cursor from it, enlarged by an integer scale derived from the window size in points, and
// caches it until the pixels or the scale change. set_default() sets the bitmap used on all
// other pages. Header-only; the cursor is freed in clear() / the destructor.
#pragma once

#include <SDL.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace bmh::app {

// SDL owns pointer position; the UI supplies only the original bitmap/colors. Everywhere but on the
// pitch page the pointer is the original's default 11x11 bitmap (set_default); the pitch page
// passes the same bitmap with its position-colour fill.
class PitchPointer {
public:
    ~PitchPointer() { clear(); }
    void clear() {
        if (cursor_) {
            SDL_SetCursor(SDL_GetDefaultCursor());
            SDL_FreeCursor(cursor_);
            cursor_ = nullptr;
        }
        pixels_.reset();
    }
    void set_default(const std::optional<std::array<uint32_t,121>>& pixels) { default_ = pixels; }
    void show(const std::optional<std::array<uint32_t,121>>& requested, SDL_Renderer* renderer) {
        const auto& pixels = requested ? requested : default_;
        if (!pixels) { SDL_SetCursor(SDL_GetDefaultCursor()); return; }
        // Cursor bitmaps are sized in window points, not in pixels: on a high-DPI display (macOS
        // Retina) the renderer's output is larger than the window, and a bitmap scaled to it would
        // show twice as large. SDL_GetWindowSize gives the points.
        int width = 320, height = 240;
        if (SDL_Window* window = SDL_RenderGetWindow(renderer)) SDL_GetWindowSize(window, &width, &height);
        else SDL_GetRendererOutputSize(renderer, &width, &height);
        const int scale = std::max(1, std::min(width / 320, height / 240));
        if (!cursor_ || pixels != pixels_ || scale != scale_) {
            std::vector<uint32_t> enlarged(size_t(11 * scale) * size_t(11 * scale));
            for (int y = 0; y < 11 * scale; ++y) for (int x = 0; x < 11 * scale; ++x)
                enlarged[size_t(y) * size_t(11 * scale) + size_t(x)] =
                    pixels->at(size_t(y / scale) * 11 + size_t(x / scale));
            SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(enlarged.data(),
                11 * scale, 11 * scale, 32, 11 * scale * 4, SDL_PIXELFORMAT_ARGB8888);
            SDL_Cursor* next = surface ? SDL_CreateColorCursor(surface, 0, 0) : nullptr;
            if (surface) SDL_FreeSurface(surface);
            if (!next) return; // A headless SDL driver may not support host cursors.
            clear(); cursor_ = next; pixels_ = pixels; scale_ = scale;
        }
        SDL_SetCursor(cursor_);
    }
private:
    SDL_Cursor* cursor_ = nullptr;
    std::optional<std::array<uint32_t,121>> pixels_, default_;
    int scale_ = 1;
};

} // namespace bmh::app
