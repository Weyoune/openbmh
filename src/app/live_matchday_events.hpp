// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <SDL.h>

#include <optional>
#include <vector>

namespace bmh::app {

// EndPhase accepts physical left/right DOWN after minute2, except at the
// first minute of a period. Its home interval ends at0x73, leaving0x74 out.
// Returning only a side keeps the current fixture/manager lookup in Main.
inline std::optional<int> live_matchday_board_side(const SDL_Event& event, int minute, bool indoor) {
    if (event.type != SDL_MOUSEBUTTONDOWN ||
        (event.button.button != SDL_BUTTON_LEFT && event.button.button != SDL_BUTTON_RIGHT) ||
        minute <= 2 || (indoor ? minute == 21 : (minute == 46 || minute == 91 || minute == 106)))
        return {};
    if (event.button.x >= 0 && event.button.x <= 0x73) return 0;
    if (event.button.x >= 0xa0 && event.button.x <= 0x113) return 1;
    return {};
}

// Drain only while the engine is synchronously paused in its observer hook.
// ESC skips future presentation; QUIT is reported to the app. Only noninteractive
// system/window notifications are put back. User input is discarded so it cannot
// become an accidental desk/result action after the synchronous match finishes.
inline bool pump_live_matchday_events(bool& skip_presentation) {
    std::vector<SDL_Event> deferred;
    SDL_Event event;
    bool quit = false;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            quit = true;
            skip_presentation = true;
        } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
            skip_presentation = true;
        } else if (event.type == SDL_USEREVENT || event.type == SDL_WINDOWEVENT ||
                   event.type == SDL_DISPLAYEVENT || event.type == SDL_SYSWMEVENT) {
            deferred.push_back(event);
        }
    }
    for (auto pending : deferred) SDL_PushEvent(&pending);
    return quit;
}

} // namespace bmh::app
