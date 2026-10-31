// SPDX-License-Identifier: GPL-3.0-only
#include <cstdio>
#include <stdexcept>

#include "ui/setup.hpp"
#include "ui/flow.hpp"
#include "formats/executable_layout.hpp"
#include "formats/resource_bundle.hpp"

using namespace bmh::ui;
static void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("ui_setup_tests requires original data directory");
        SetupScreen screen(argv[1]);
        const auto before = screen.options();
        check(before == SetupOptions{}, "original defaults");
        check(screen.render().pixels.size() == 320*240, "native framebuffer");
        if(bmh::resource_exists(std::filesystem::path(argv[1])/"KB"/"SETUP12.VGA")) {
            SetupScreen countries(argv[1],SetupLeagueSymbol::country_selection);
            const auto source_image=screen.render(),country_image=countries.render();
            int changed=0;
            for(int y=0;y<240;++y)for(int x=0;x<320;++x)if(source_image.pixels[size_t(y)*320+x]!=country_image.pixels[size_t(y)*320+x]) {
                check(x>=46 && x<274 && y>=199,"country controls changed another setup row");++changed;
            }
            check(changed>100 && countries.options().country_selection,"four-country controls missing");
            check(countries.click(65,214).changed && !countries.options().germany && countries.options().england,"Germany is not independently selectable");
            check(countries.click(124,214).changed && !countries.options().league && countries.options().world,"International-only selection unavailable");
            check(!countries.click(183,214).changed && countries.options().world,"last active game scope can be disabled");
            check(countries.render().pixels!=country_image.pixels,"disabled country controls are not crossed out");
            countries.click(65,214);countries.click(183,214);
            check(setup_game_scope(countries.options())==bmh::world12::GameScope{true,false,false},"Germany-only scope mapping incorrect");
            check(countries.click(242,214).request==SetupRequest::load_game,"fourth control does not load");
            check(!countries.click(100,214).changed,"control gap toggles a country");
            countries.click(183,214);countries.click(260,135);
            check(countries.options().managers==2 && !countries.click(65,214).changed && countries.options().league,
                "multiplayer can remove its last club system");
            StartupFlow pending(argv[1],{},SetupLeagueSymbol::country_selection);
            check(pending.click(10,210,true).request==FlowRequest::new_career && pending.pending_career() &&
                pending.pending_career()->managers.empty(),"country selection entered a legacy German manager picker");
        }
        check(!screen.click(-1, 30).changed && !screen.click(320, 239, true).changed, "outside viewport");
        check(screen.click(130, 214).request == SetupRequest::none, "gap between controls");
        check(screen.click(214, 215).request == SetupRequest::load_game, "disk requests load");
        check(screen.click(120, 100, true).request == SetupRequest::new_game, "right button requests manager setup");
        check(screen.options() == before, "requests do not modify setup or start engine");

        for (int i=0; i<20; ++i) screen.click(30, 30);
        check(screen.options().duration == 1, "minimum season duration");
        for (int i=0; i<20; ++i) screen.click(260, 30);
        check(screen.options().duration == 5, "unlimited season duration");
        for (int i=0; i<20; ++i) screen.click(30, 85);
        check(screen.options().difficulty == 0, "minimum difficulty is displayed as one ball");
        for (int i=0; i<20; ++i) screen.click(260, 85);
        check(screen.options().difficulty == 9, "maximum difficulty is displayed as ten balls");
        check(screen.render().pixels.size() == 320*240, "render maximum difficulty");
        for (int i=0; i<20; ++i) screen.click(260, 135);
        check(screen.options().managers == 4, "four manager support");
        check(!screen.click(95, 215).changed && screen.options().league, "league cannot be disabled with several managers");
        for (int i=0; i<20; ++i) screen.click(30, 135);
        check(screen.options().managers == 1, "minimum one manager");
        check(screen.click(95, 215).changed && !screen.options().league, "single-manager world-only option");
        check(!screen.click(260, 135).changed && screen.options().managers == 1, "world-only limits manager count");
        check(!screen.click(155, 215).changed && screen.options().world, "cannot disable both competitions");
        check(screen.click(95, 215).changed, "enable league again");
        check(screen.click(155, 215).changed && !screen.options().world, "league-only option");
        check(!screen.click(95, 215).changed && screen.options().league, "cannot disable remaining competition");
        for (int i=0; i<20; ++i) screen.click(30, 180);
        check(screen.options().mode == 1, "minimum function mode");
        for (int i=0; i<20; ++i) screen.click(260, 180);
        check(screen.options().mode == 3, "maximum function mode");
        check(screen.render().pixels.size() == 320*240, "render modified options");

        SetupOptions current{2, 2, 2, 3, true, true};
        SetupScreen joining(argv[1], current);
        check(joining.options() == current, "joining setup preserves current options");
        check(joining.render().pixels.size() == 320*240, "joining setup native framebuffer");
        check(!joining.click(30, 30).changed && joining.options().duration == 2,
              "joining setup locks duration");
        check(joining.click(30, 85).changed && joining.options().difficulty == 1,
              "joining setup permits difficulty adjustment");
        check(joining.click(260, 85).changed && joining.options().difficulty == 2,
              "joining setup permits difficulty increase");
        check(joining.click(30, 85).changed && joining.click(30, 85).changed &&
              joining.options().difficulty == 0,
              "joining setup can reduce an initially editable difficulty to raw zero");
        check(joining.click(260, 85).changed && joining.options().difficulty == 1,
              "initially editable difficulty arrows stay enabled at raw zero");
        check(joining.click(260, 135).changed && joining.options().managers == 3,
              "joining setup adds a manager");
        check(joining.click(30, 135).changed && joining.options().managers == 2,
              "joining setup preserves existing managers");
        check(!joining.click(30, 135).changed && joining.options().managers == 2,
              "joining setup cannot remove an existing manager");
        check(!joining.click(260, 180).changed && joining.options().mode == 3,
              "joining setup locks mode");
        check(!joining.click(95, 215).changed && joining.options().league,
              "joining setup hides and locks competition toggles");
        check(joining.click(120, 100, true).request == SetupRequest::new_game,
              "joining setup preserves right-button new-game request");

        current.difficulty = 0;
        SetupScreen minimum_difficulty_joining(argv[1], current);
        check(!minimum_difficulty_joining.click(30, 85).changed &&
              !minimum_difficulty_joining.click(260, 85).changed &&
              minimum_difficulty_joining.options().difficulty == 0,
              "joining setup locks difficulty arrows at raw zero");
        current = SetupOptions{2, 2, 2, 3, false, true};
        SetupScreen world_only_joining(argv[1], current);
        check(world_only_joining.click(260, 135).changed &&
              world_only_joining.options().managers == 3,
              "joining setup can add a manager in an existing world-only career");
        std::puts("UI setup tests passed: rendering, limits, competition guards, requests");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1;
    }
}
