// SPDX-License-Identifier: GPL-3.0-only
#include <SDL.h>

#include <stdexcept>

#include "app/live_matchday_events.hpp"

// SDL.h renames main to SDL_main, which SDL2main calls as int SDL_main(int, char**)
int main(int, char**) {
    // format field boundaries are independent of which fixture side is human.
    // An UP (including one from a previous period) cannot claim a fresh menu.
    for (const auto button : {SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT}) {
        SDL_Event pointer{};
        pointer.type = SDL_MOUSEBUTTONDOWN;
        pointer.button.button = button;
        for (const int minute : {1, 2, 46, 91, 106}) {
            pointer.button.x = 200;
            if (bmh::app::live_matchday_board_side(pointer, minute, false))
                throw std::runtime_error("Source period start/early minute admitted a manual menu");
        }
        for (const int minute : {3, 45, 47, 90, 92, 105, 107}) {
            for (const int x : {0, 115}) {
                pointer.button.x = x;
                if (bmh::app::live_matchday_board_side(pointer, minute, false) != 0)
                    throw std::runtime_error("Source home endpoint was lost");
            }
            for (const int x : {160, 275}) {
                pointer.button.x = x;
                if (bmh::app::live_matchday_board_side(pointer, minute, false) != 1)
                    throw std::runtime_error("Source away endpoint was lost");
            }
            for (const int x : {-1, 116, 159, 276, 319}) {
                pointer.button.x = x;
                if (bmh::app::live_matchday_board_side(pointer, minute, false))
                    throw std::runtime_error("Source gap/outside pixel selected a team");
            }
        }
        pointer.button.x = 200;
        if (bmh::app::live_matchday_board_side(pointer, 21, true) ||
            bmh::app::live_matchday_board_side(pointer, 20, true) != 1)
            throw std::runtime_error("indoor period gate is inconsistent");
        pointer.type = SDL_MOUSEBUTTONUP;
        if (bmh::app::live_matchday_board_side(pointer, 4, false) ||
            bmh::app::live_matchday_board_side(pointer, 47, false))
            throw std::runtime_error("release reopened a live menu");
    }
    SDL_Event middle{};
    middle.type = SDL_MOUSEBUTTONDOWN;
    middle.button.button = SDL_BUTTON_MIDDLE;
    middle.button.x = 200;
    if (bmh::app::live_matchday_board_side(middle, 4, false))
        throw std::runtime_error("non-Source mouse mode opened a live menu");

    if (SDL_Init(SDL_INIT_EVENTS) != 0) throw std::runtime_error(SDL_GetError());
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    SDL_Event unrelated{};
    unrelated.type = SDL_USEREVENT;
    unrelated.user.code = 71;
    SDL_Event escape{};
    escape.type = SDL_KEYDOWN;
    escape.key.keysym.sym = SDLK_ESCAPE;
    SDL_Event click{};
    click.type = SDL_MOUSEBUTTONUP;
    click.button.button = SDL_BUTTON_LEFT;
    SDL_Event enter{};
    enter.type = SDL_KEYDOWN;
    enter.key.keysym.sym = SDLK_RETURN;
    SDL_Event window{};
    window.type = SDL_WINDOWEVENT;
    window.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
    if (SDL_PushEvent(&unrelated) != 1 || SDL_PushEvent(&click) != 1 ||
        SDL_PushEvent(&enter) != 1 || SDL_PushEvent(&window) != 1 || SDL_PushEvent(&escape) != 1)
        throw std::runtime_error("cannot enqueue test events");
    bool skipped = false;
    if (bmh::app::pump_live_matchday_events(skipped) || !skipped)
        throw std::runtime_error("ESC must skip only presentation");
    SDL_Event event{};
    int preserved = 0;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_USEREVENT && event.user.code == 71) ++preserved;
        else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) ++preserved;
        else throw std::runtime_error("interactive input was replayed after the live view");
    }
    if (preserved != 2) throw std::runtime_error("window/system events were swallowed");

    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    SDL_Event quit{};
    quit.type = SDL_QUIT;
    if (SDL_PushEvent(&escape) != 1 || SDL_PushEvent(&quit) != 1 || SDL_PushEvent(&unrelated) != 1)
        throw std::runtime_error("cannot enqueue quit test events");
    skipped = true; // ESC was already pressed on the prior frame.
    if (!bmh::app::pump_live_matchday_events(skipped) || !skipped)
        throw std::runtime_error("window close must exit after synchronous kickoff");
    if (SDL_PollEvent(&event) != 1 || event.type != SDL_USEREVENT || event.user.code != 71 || SDL_PollEvent(&event))
        throw std::runtime_error("unrelated input was swallowed while preserving quit");
    SDL_Quit();
    return 0;  // SDL_main has no implicit return 0
}
