// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <SDL.h>
#include <functional>
#include <utility>

namespace bmh::app {

// OpenBMH deviates from the original: confirms application exit with a localized native dialog, with cancellation selected by default.
// All event retrieval remains on the UI thread. An event filter/watch could run
// on SDL's event thread and must not own a native modal dialog.
class QuitConfirmation {
public:
    using Presenter = std::function<int(const SDL_MessageBoxData*, int*)>;
    QuitConfirmation(SDL_Window* window, bool english, Presenter presenter = SDL_ShowMessageBox)
        : window_(window), english_(english), presenter_(std::move(presenter)) {}

    bool confirm() {
        if (confirmed_) return true;
        const SDL_MessageBoxButtonData buttons[] = {
            {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,
                0, english_ ? "Cancel" : "Abbrechen"},
            {0, 1, english_ ? "Quit" : "Beenden"}
        };
        const SDL_MessageBoxData dialog{SDL_MESSAGEBOX_WARNING, window_,
            english_ ? "Quit OpenBMH?" : "OpenBMH beenden?",
            english_ ? "Do you really want to quit OpenBMH?\nUnsaved changes will be lost." :
                "OpenBMH wirklich beenden?\nNicht gespeicherte Änderungen gehen verloren.",
            2, buttons, nullptr};
        int selected = 0;
        if (presenter_(&dialog, &selected) < 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Cannot show quit confirmation: %s", SDL_GetError());
            return false; // A failed dialog must not silently discard the session.
        }
        confirmed_ = selected == 1;
        return confirmed_;
    }

    int poll(SDL_Event* event) {
        if (!event) return SDL_PollEvent(nullptr);
        while (SDL_PollEvent(event)) {
            if (event->type != SDL_QUIT || confirm()) return 1;
        }
        return 0;
    }

    int peep(SDL_Event* events, int count, SDL_eventaction action, Uint32 first, Uint32 last) {
        const int received = SDL_PeepEvents(events, count, action, first, last);
        if (received <= 0 || !events || action != SDL_GETEVENT) return received;
        int kept = 0;
        for (int i = 0; i < received; ++i)
            if (events[i].type != SDL_QUIT || confirm()) events[kept++] = events[i];
        return kept;
    }

private:
    SDL_Window* window_;
    bool english_, confirmed_ = false;
    Presenter presenter_;
};

} // namespace bmh::app
