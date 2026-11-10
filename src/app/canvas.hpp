// SPDX-License-Identifier: GPL-3.0-only
// The game picture on the window: the 320x240 canvas of palette indices is converted with a palette,
// uploaded to the streaming texture and shown.
//
// src/app/main.cpp repeated these six statements at about twenty places (every presenter that shows
// a picture). Canvas::show() is that sequence, in the same order and with the same disregard of SDL
// errors as the copies had; presenters that check the SDL calls or update only a part of the texture
// keep their own code. The members are the window objects main() creates; the pixel buffer stays
// main()'s vector, because a few presenters fill parts of it themselves.
#pragma once

#include <SDL.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "app/robot.hpp"
#include "formats/palette.hpp"
#include "formats/vga.hpp"

namespace bmh::app {

struct Canvas {
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    std::vector<uint32_t>& pixels;   // 320 * 240 ARGB values

    // Converts the picture with the palette, uploads it and shows it (clear, copy, present).
    void show(const Image& image, const Palette& palette) {
        for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = palette.argb[image.pixels[i]];
        SDL_UpdateTexture(texture, nullptr, pixels.data(), 320 * 4);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        present(renderer);
    }
};

} // namespace bmh::app
