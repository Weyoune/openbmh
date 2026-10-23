// SPDX-License-Identifier: GPL-3.0-only
#include "launch/launcher_ui.hpp"
#include <SDL.h>
#include <cstdio>
#include <stdexcept>

// Run explicitly on a real display (Windows CI also forces the WARP device),
// outside the dummy-video CTest suite. This constructs the actual launcher UI.
int main(int, char **) {
    try {
        auto ui = bmh::launch::make_launcher_ui();
        const auto timer = SDL_AddTimer(
            1600,
            [](Uint32, void *) -> Uint32 {
                SDL_Event event{};
                event.type = SDL_QUIT;
                SDL_PushEvent(&event);
                return 0;
            },
            nullptr);
        if (!timer)
            throw std::runtime_error(SDL_GetError());
        const auto result = ui->next({});
        SDL_RemoveTimer(timer);
        if (result.action != bmh::launch::LauncherAction::end)
            return 1;
        std::puts("Native launcher rendered and exited successfully");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
