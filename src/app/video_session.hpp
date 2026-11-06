// SPDX-License-Identifier: GPL-3.0-only
// RAII guard for SDL video: SDL_Init(SDL_INIT_VIDEO) in the constructor (throws
// std::runtime_error on failure) and SDL_Quit in the destructor. It must exist before any
// native file picker is opened (see the comment on the class). Non-copyable, header-only.
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
