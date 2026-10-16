#include "../../tests/test_inputs.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/season_entrants.hpp"

#include <iostream>
#include <stdexcept>

#include "formats/textd.hpp"
#include "game/state.hpp"
#include "ui/manager.hpp"

namespace {
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

bmh::GameState fixture() {
    bmh::GameState g;
    g.club_ids.resize(90);
    g.club_heap_78.assign(90 * 2, 0);
    for (int i = 0; i < 90; ++i) {
        g.club_ids[size_t(i)] = uint16_t(g.club_names.size());
        const std::string name = "CLUB " + std::to_string(i);
        for (unsigned char c : name) g.club_names.push_back(c);
        g.club_names.push_back(0);
        const uint16_t badge = uint16_t(i & 7);
        g.club_heap_78[size_t(i) * 2] = uint8_t(badge);
        g.club_heap_78[size_t(i) * 2 + 1] = uint8_t(badge >> 8);
    }
    g.memory.add_block(::bmh::layout::SeasonEntrants_SeasonEntrantsFromGame_TextPointer, 1);
    g.memory.add_block(::bmh::layout::PlainBackground, 1);
    g.memory.add_block(::bmh::layout::CompetitionSelection, 4);
    g.memory.set32(::bmh::layout::CompetitionSelection, 1);
    g.memory.add_block(::bmh::layout::Career_EuropeanClub_ExecutableField, 1);
    g.memory.add_block(::bmh::layout::Career_EuropeanClub_ExecutableField2, 1);
    g.memory.add_block(::bmh::layout::Career_EuropeanClub_ExecutableField3, 1);
    g.memory.add_block(::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField25, 10);
    g.memory.add_block(::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField14, 4);
    g.memory.add_block(::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField19, 2);
    const int uefa[] = {2, 3, 90, 4, 5};
    g.memory.set8(::bmh::layout::Career_EuropeanClub_ExecutableField, 5);
    for (int i = 0; i < 5; ++i) g.memory.set16(::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField25 + uint32_t(i * 2), uint16_t(uefa[i]));
    g.memory.set8(::bmh::layout::Career_EuropeanClub_ExecutableField2, 2);
    g.memory.set16(::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField14, 6); g.memory.set16(::bmh::layout::FixtureSecondSeasonEntrant, 7);
    g.memory.set8(::bmh::layout::Career_EuropeanClub_ExecutableField3, 1); g.memory.set16(::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField19, 8);
    return g;
}
}

int main(int argc, char** argv) try {
    if (argc != 2) throw std::runtime_error("expected original data directory");
    if (!*argv[1]) { std::cout << "season entrants: skipped (Original data not configured)\n"; return 77; }
    const auto data = std::filesystem::path(argv[1]);
    const auto exe = bmh::load_le(bmh::ui::asset_path(data, "BMH.exe"));
    const auto texts = bmh::load_textd(bmh::ui::asset_path(data, "TEXTD.BLK"));
    auto game = fixture();

    const auto before_count = game.memory.u8(::bmh::layout::Career_EuropeanClub_ExecutableField);
    const auto before_first = game.memory.u16(::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField25);
    const auto pages = bmh::ui::season_entrants_from_game(game, exe, texts);
    check(pages.size() == 3, "Source creates UEFA, Champions League and Cup Winners views");
    check(pages[0].heading == bmh::test::expected_text("season_entrants_tests-external-2").c_str() && pages[0].footer == bmh::test::expected_text("season_entrants_tests-external-4").c_str(), "original German heading and UEFA footer");
    check(pages[1].footer == bmh::test::expected_text("season_entrants_tests-external-1").c_str() && pages[2].footer == bmh::test::expected_text("season_entrants_tests-external-3").c_str(), "ordered original competition footers");
    check(pages[0].teams.size() == 4, "foreign entries are omitted from the German-team view");
    check(pages[0].teams[0].runtime_team_id == 2 && pages[0].teams[1].runtime_team_id == 3 &&
          pages[0].teams[2].runtime_team_id == 4 && pages[0].teams[3].runtime_team_id == 5,
          "domestic entries preserve their original pool order");
    // Independent native coordinates observed in SESEND01.avi, rather than
    // repeating the renderer's lookup into the executable placement tables.
    check(pages[0].teams[0].x == 62 && pages[0].teams[0].y == 47 &&
          pages[0].teams[1].x == 216 && pages[0].teams[1].y == 47 &&
          pages[0].teams[2].x == 62 && pages[0].teams[2].y == 140 &&
          pages[0].teams[3].x == 216 && pages[0].teams[3].y == 140,
          "four-team placements match the Original video");
    check(game.memory.u8(::bmh::layout::Career_EuropeanClub_ExecutableField) == before_count && game.memory.u16(::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField25) == before_first,
          "projection leaves saved competition pools unchanged");

    bmh::ui::SeasonEntrantsScreen screen(data, pages[0]);
    const auto image = screen.render();
    check(image.width == 320 && image.height == 240, "renderer uses original native canvas size");
    game.club_names[0] = 'X';
    game.memory.set16(::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField25, 1);
    check(pages[0].teams[0].runtime_team_id == 2 && pages[0].teams[0].name == "CLUB 2",
          "projected teams and names own their data after the source changes");


    game.memory.set8(::bmh::layout::SeasonEntrants_SeasonEntrantsFromGame_TextPointer, 1);
    check(bmh::ui::season_entrants_from_game(game, exe, texts).empty(), "a254e suppresses the original entrant pages");
    game.memory.set8(::bmh::layout::SeasonEntrants_SeasonEntrantsFromGame_TextPointer, 0); game.memory.set32(::bmh::layout::CompetitionSelection, 0);
    check(bmh::ui::season_entrants_from_game(game, exe, texts).empty(), "non-league mode suppresses the pages");
    std::cout << "season entrants projection and renderer passed\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "ui_season_entrants_tests: " << e.what() << '\n';
    return 1;
}
