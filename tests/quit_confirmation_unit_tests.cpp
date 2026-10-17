// SPDX-License-Identifier: GPL-3.0-only
#include "app/quit_confirmation.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void push(Uint32 type) {
    SDL_Event event{}; event.type = type;
    check(SDL_PushEvent(&event) == 1, "cannot enqueue authored event");
}
}

int main(int, char**) {
    using bmh::app::QuitConfirmation;
    try {
        check(SDL_Init(SDL_INIT_EVENTS) == 0, "SDL events initialization");
        SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
        int presentations = 0, answer = 0;
        const auto present = [&](const SDL_MessageBoxData* data, int* selected) {
            ++presentations;
            check(std::string(data->title) == "OpenBMH beenden?", "German prompt");
            check(std::string(data->message).find("gespeicherte") != std::string::npos, "unsaved-state warning");
            check(data->numbuttons == 2 && std::string(data->buttons[0].text) == "Abbrechen", "German buttons");
            check((data->buttons[0].flags & SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT) &&
                (data->buttons[0].flags & SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT), "Enter and Escape must default to cancellation");
            *selected = answer; return 0;
        };
        QuitConfirmation german(nullptr, false, present);
        push(SDL_QUIT); push(SDL_USEREVENT);
        SDL_Event event{};
        check(german.poll(&event) == 1 && event.type == SDL_USEREVENT && presentations == 1,
            "cancelled window close must preserve subsequent UI input");
        push(SDL_QUIT);
        check(german.peep(&event, 1, SDL_PEEKEVENT, SDL_QUIT, SDL_QUIT) == 1 && presentations == 1,
            "peeking must not open or consume a dialog");
        check(german.peep(&event, 1, SDL_GETEVENT, SDL_QUIT, SDL_QUIT) == 0 && presentations == 2,
            "cancelled animation/live close must not abort its owner");
        answer = 1; push(SDL_QUIT);
        check(german.peep(&event, 1, SDL_GETEVENT, SDL_QUIT, SDL_QUIT) == 1 && event.type == SDL_QUIT,
            "confirmed close must reach existing shutdown handling");
        check(german.confirm() && presentations == 3, "confirmed shutdown must not ask twice");
        bool english_seen = false;
        QuitConfirmation english(nullptr, true, [&](const SDL_MessageBoxData* data, int* selected) {
            english_seen = std::string(data->title) == "Quit OpenBMH?" &&
                std::string(data->buttons[0].text) == "Cancel" && std::string(data->buttons[1].text) == "Quit";
            *selected = 1; return 0;
        });
        check(english.confirm() && english_seen, "Football Limited uses English confirmation");
        QuitConfirmation failed(nullptr, false, [](const SDL_MessageBoxData*, int*) { return -1; });
        check(!failed.confirm(), "failed native dialog must keep the application alive");
        SDL_Quit();
        std::cout << "Quit confirmation language, defaults, cancel, accept and event ownership passed\n";
        return 0;
    } catch (const std::exception& error) {
        SDL_Quit(); std::cerr << error.what() << '\n'; return 1;
    }
}
