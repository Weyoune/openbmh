// SPDX-License-Identifier: GPL-3.0-only
#include <cstdio>
#include <stdexcept>

#include "ui/setup.hpp"
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
            SetupScreen dual(argv[1],SetupLeagueSymbol::germany_england);
            const auto source_image=screen.render(),dual_image=dual.render();
            const auto setup_source=bmh::load_le(asset_path(argv[1],"BMH.exe"));
            const int league_x=setup_source.u8(bmh::layout::Setup_FormatError_RuntimeField9);
            int changed_pixels=0;
            for(int y=0;y<240;++y)for(int x=0;x<320;++x)if(source_image.pixels[size_t(y)*320+x]!=dual_image.pixels[size_t(y)*320+x]) {
                check(x>=league_x && x<league_x+51 && y>=199 && y<239,"dual map changes another setup control");++changed_pixels;
            }
            check(changed_pixels>100 && dual.options()==screen.options(),"dual-country symbol missing or changes gameplay options");
            check(dual.click(league_x+15,214).changed && !dual.options().league,"dual map no longer toggles league play");
            check(dual.render().pixels!=dual_image.pixels,"disabled dual map has no visible state");
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
