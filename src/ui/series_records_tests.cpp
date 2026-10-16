// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "series_records.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "game/schedule.hpp"
#include "../../tests/fixture_tables.hpp"

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}

int main(int argc, char** argv) {
    using namespace bmh;
    // The public tree carries no fixture tables; install the authored test copy.
    set_fixture_tables(test::authored_fixture_tables());
    GameState game;
    game.manager_records.assign(5 * 0xb0c, 0);
    game.manager_records[0x290] = 0;
    game.manager_records[0x291] = 0;
    game.manager_teams = {0};
    game.club_table.assign(168 * 0x4a, 0);
    game.club_names = {'T','e','a','m',0,'R','i','v','a','l',0};
    game.club_ids.assign(168, 0);
    game.club_ids[0] = 0;
    game.league_rounds.assign(9, 0);
    game.league_rounds[0] = 1;
    game.results.assign(kResultsSize, kNotPlayed);

    const auto fixtures = generate_fixtures(league_table_teams(0), 1,
                                             league_table_teams(0) - 1, league_first_team(0));
    size_t target = fixtures.size();
    for (size_t i = 0; i < fixtures.size(); ++i)
        if (fixtures[i].home == 0 || fixtures[i].away == 0) { target = i; break; }
    require(target < fixtures.size(), "fixture for selected club exists");
    const bool selected_home = fixtures[target].home == 0;
    game.results[result_index(0, 1, int(target))] = selected_home ? 0x21 : 0x12;

    const size_t club = 0;
    game.club_table[club + 0x15] = 0x21;
    game.club_table[club + 0x2a] = 5;
    game.club_table[club + 0x2b] = 0;
    game.club_table[club + 0x3a] = 123;
    game.club_table[club + 0x3b] = 0;
    game.club_table[club + 0] = 4;
    game.club_table[club + 1] = 5;
    game.club_table[club + 2] = 6;
    game.manager_records[0x5e] = 7;
    game.manager_records[0x5f] = 8;
    game.manager_records[0x60] = 9;

    std::vector<uint8_t> exe_bytes(::bmh::layout::FixtureExecutableStorageSize, 0);
    exe_bytes[::bmh::layout::TableFlags - ::bmh::layout::GermanDataSegmentBegin] = 1;
    exe_bytes[::bmh::layout::FixtureSeriesFirstMetric - ::bmh::layout::GermanDataSegmentBegin] = 2;
    exe_bytes[::bmh::layout::FixtureSeriesSecondMetric - ::bmh::layout::GermanDataSegmentBegin] = 3;
    exe_bytes[::bmh::layout::SeriesSelector - ::bmh::layout::GermanDataSegmentBegin] = 1;
    exe_bytes[::bmh::layout::FixtureAllTimePrimaryMetric - ::bmh::layout::GermanDataSegmentBegin] = 2;
    exe_bytes[::bmh::layout::FixtureAllTimeSecondaryMetric - ::bmh::layout::GermanDataSegmentBegin] = 0;
    const LeImage exe = LeImage::from_bytes(::bmh::layout::GermanDataSegmentBegin, std::move(exe_bytes));

    const auto projection = bmh::ui::series_records_from_game(game, exe, 0);
    require(projection.source_flags == std::array<uint8_t, 3>{1, 2, 3},
            "three independent legacy routine source flag columns");
    require(projection.series_indices == std::array<uint8_t, 3>{1, 2, 0},
            "current and saved streak columns use the three Original selectors");
    for (size_t col = 0; col < 3; ++col) {
        const auto expected = compute_league_table(game, 0, 0, 1, projection.source_flags[col], 0);
        require(projection.tables[col].goals_for == expected.goals_for &&
                projection.tables[col].goals_against == expected.goals_against &&
                projection.tables[col].rank_word == expected.rank_word,
                "each column equals a saved-round legacy routine Core query");
    }
    const auto& home_table = projection.tables[0];
    const auto& away_table = projection.tables[1];
    const auto& all_table = projection.tables[2];
    require((home_table.games(0) == 1) == selected_home &&
            (away_table.games(0) == 1) != selected_home,
            "home and away columns filter the selected source fixture independently");
    require(all_table.games(0) == 1, "combined column includes either venue");
    require(projection.current_streaks[0] == 4 && projection.manager_best_streaks[0] == 7,
            "current club streak and manager best mark remain separate saved arrays");
    require(projection.records[0].score_byte == 0x21 &&
            projection.records[0].opponent_name_offset == 5 &&
            projection.records[0].opponent_name == "Rival" &&
            projection.records[0].saved_date == 123,
            "record opponent and date come from their saved club-table words");
    if (argc > 1) {
        bmh::Image parent(320, 240);
        bmh::ui::SeriesRecordsScreen screen(argv[1], projection, std::move(parent));
        const auto rendered = screen.render();
        require(rendered.width == 320 && rendered.height == 240,
                "Original resources render the page into the accepted parent pane");
    }
    std::cout << "series records projection checks passed\n";
}
