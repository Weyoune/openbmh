// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <SDL.h>
#include <stdexcept>
#include <string>
namespace bmh::app {
// Establish SDL's native application before any file picker can create NSApp.
// Cocoa otherwise retains the picker's non-keyboard activation policy.
class VideoSession {
public:
    VideoSession() {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            const std::string error = SDL_GetError();
            SDL_Quit();
            throw std::runtime_error(error);
        }
    }
    ~VideoSession() { SDL_Quit(); }
    VideoSession(const VideoSession&) = delete;
    VideoSession& operator=(const VideoSession&) = delete;
};
}
