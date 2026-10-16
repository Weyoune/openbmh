// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
// table_print - prints the league table computed from a savegame (game/table.hpp), for
// comparison with the table screen of the original.
//
// Usage: table_print <savegame.FBM> [league] [game directory]
#include <cstdio>
#include <string>

#include "formats/fbm.hpp"
#include "runtime_fixture_tables.hpp"
#include "formats/savestate.hpp"
#include "formats/textd.hpp"
#include "game/schedule.hpp"
#include "game/table.hpp"

using namespace bmh;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: table_print <savegame.FBM> [league] [game directory]\n");
        return 1;
    }
    const auto data = argc > 3 ? std::filesystem::path(argv[3]) : std::filesystem::path(argv[1]).parent_path();
    bmh::test::load_executable(data / "BMH.exe");
    Savegame s = load_savegame(argv[1]);
    SaveState st(s.stream);
    GameState g = GameState::from_save(st);
    int league = argc > 2 ? std::stoi(argv[2]) : 0;
    const Bytes& pool = st.pool(0);
    auto name = [&](int team) {
        const uint8_t* p = st.data(::bmh::layout::ClubIdentities) + team * 2;
        return iso646de_to_utf8(reinterpret_cast<const char*>(pool.data() + (p[0] | p[1] << 8)));
    };
    // as legacy routine: the whole season so far, deductions, three points for a win
    LeagueTable t = compute_league_table(g, league, 0, league_matchdays(league), 7, 1);
    std::printf("league %d after %d rounds\n", league, g.league_rounds.at(league));
    std::printf("Pl. prev  %-24s  Sp  S  U  N  Tore    Pkt  form\n", "Verein");
    const int first = league_first_team(league);
    for (int pos = 0; pos < t.size; pos++)
        for (int c = 0; c < t.size; c++) {
            if (t.rank(c) != pos) continue;
            int games = t.games(c), w = t.wins[c], l = t.defeats[c];
            std::printf("%3d. %3d.  %-24s  %2d %2d %2d %2d  %2d:%-2d  %4d  %06o\n", pos + 1, t.previous_rank(c) + 1,
                        name(first + c).c_str(), games, w, games - w - l, l, t.goals_for[c], t.goals_against[c],
                        t.points[c], t.form[c] & 0777777);
        }
    return 0;
}
