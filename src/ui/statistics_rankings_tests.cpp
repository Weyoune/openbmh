#include "../../tests/test_inputs.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/statistics_rankings.hpp"
#include "ui/setup.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace bmh;
using namespace bmh::ui;

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

GameState sample() {
    GameState game;
    game.season = 25;
    game.manager_records.assign(4 * 0xb0c, 0);
    game.manager_records[0x2aa] = 7;
    game.club_ids.resize(1, 0);
    game.club_names = {'B','a','y','e','r',0};
    game.manager_teams.resize(1, kNoTeam);
    game.memory.add_block(::bmh::layout::Titles, 900);
    for (uint32_t i = 0; i < 900; ++i) game.memory.set8(::bmh::layout::Titles + i, 0xff);
    game.memory.set16(::bmh::layout::Titles + 0 * 100 + 2 * 2, 0);
    return game;
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: ui_statistics_rankings_tests <BMH data>");
        const std::filesystem::path data(argv[1]);
        const LeImage exe = load_le(asset_path(data, "BMH.exe"));
        GameState game = sample();
        Image underlay(320, 240);
        for (int y = 0; y < 240; ++y) for (int x = 0; x < 320; ++x)
            underlay.at(x, y) = uint8_t((x + y) & 0xff);

        auto state = statistics_rankings_from_game(game, exe, 0, 0, 0, underlay);
        check(state.categories.size() == 9 && state.category == 0 && state.current_season == 25,
              "statistics parent index 7 enters legacy routine, whose inner default category is 0");
        for (const auto& category : state.categories)
            check(category.title_id < 9,
                  "category title ids and TEXTD selector choice come from Original EXE tables");
        const int category = 0;
        check(state.title_holders[size_t(category)][2] == 0 &&
              state.club_names[size_t(category)][2] == "Bayer",
              "legacy routine reads format field by selected category index, independently of label id");
        StatisticsRankingsScreen screen(data, state);
        check(state.season_number_base == int(exe.u16(::bmh::layout::Achievements_FormatError_RuntimeField2)) + 7,
              "legacy routine year base is format field at +0x2aa from manager record base b6c18");
        check(screen.category_label(0) == bmh::test::expected_text("statistics_rankings_tests-external-2").c_str() && screen.category_label(1) == bmh::test::expected_text("statistics_rankings_tests-external-1").c_str(),
              "format field/format field use TEXTD slots 623/601, with each EXE title id");
        check(screen.render().pixels.size() == 320U * 240U,
              "title history renders on the accepted parent underlay");
        screen.scroll(1);
        check(screen.state().first_season == 4, "initial history can page before choosing another title");
        check(screen.pointer_down(23, 154) && screen.release(23, 154) && !screen.pending() &&
              screen.state().first_season == 8,
              "Source click on the already-selected button retains category and advances the page");

        check(screen.pointer_down(23, 166), "Source category 1 hitbox is inclusive at its origin");
        check(!screen.pending(), "category DOWN shows pressed feedback without dispatching early");
        check(screen.move(150, 40), "moving off a pressed category cancels its held state");
        check(!screen.release(150, 40) && !screen.pending(), "orphan release after cancellation does nothing");
        check(screen.pointer_down(23, 166), "category can be pressed again after cancellation");
        check(screen.release(23, 166), "matching category UP commits the Source action");
        check(screen.pending() && screen.pending()->kind == StatisticsRankingsRequestKind::select_category &&
              screen.pending()->category == 1, "category selection is a typed parent action");
        auto accepted = state;
        accepted.category = 1;
        accepted.first_season = 0;
        screen.acknowledge_category(accepted);
        check(screen.state().category == 1 && screen.state().first_season == 0 && !screen.pending(),
              "Source resets the local page when a different category is accepted");
        screen.scroll(1);
        check(screen.state().first_season == 4, "Source scroll advances by four seasons");
        screen.scroll(1);
        check(screen.state().first_season == 8, "Source scroll advances a second time while 16 rows remain");
        screen.scroll(1);
        check(screen.state().first_season == 12, "Source scroll advances when the prior page still has 16 seasons");
        screen.scroll(1);
        check(screen.state().first_season == 12, "Source scroll stops when the prior start+16 exceeds current season");
        screen.scroll(-1);
        check(screen.state().first_season == 8, "Source reverse scroll returns by four seasons");
        screen.scroll(-1);
        check(screen.state().first_season == 4, "Source reverse scroll continues in four-season steps");
        check(screen.pointer_down(100, 70) && screen.state().first_season == 0,
              "a Source list-area DOWN in the upper half scrolls back four seasons");
        check(!screen.release(100, 70), "row release does not emit navigation");
        check(screen.pointer_down(0, 0, true) && screen.pending() &&
              screen.pending()->kind == StatisticsRankingsRequestKind::close,
              "right-click is the Source parent-return request");

        GameState missing;
        bool rejected = false;
        try { (void)statistics_rankings_from_game(missing, exe, 0, 0, 0, underlay); }
        catch (const FormatError&) { rejected = true; }
        check(rejected, "missing format field fails closed rather than synthesizing histories");
        std::cout << "Statistics rankings Source title history projection/input passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
