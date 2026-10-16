// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
// probe - development helper: prints computed team values of a savegame so they can be
// compared with what the original shows on screen.
//
// Usage: probe <savegame.fbm> [league]
#include <cstdio>
#include <string>

#include "formats/fbm.hpp"
#include "formats/savestate.hpp"
#include "game/rating.hpp"
#include "game/schedule.hpp"
#include "game/state.hpp"

using namespace bmh;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: probe <savegame.fbm> [league]\n");
        return 1;
    }
    Savegame s = load_savegame(argv[1]);
    SaveState st(s.stream);
    GameState g = GameState::from_save(st);
    int league = argc > 2 ? std::stoi(argv[2]) : 0;

    const Bytes& pool = st.pool(0);
    auto name = [&](int team) {
        const uint8_t* p = st.data(::bmh::layout::ClubIdentities) + team * 2;
        return iso646de_to_utf8(reinterpret_cast<const char*>(pool.data() + (p[0] | p[1] << 8)));
    };

    using namespace avg;
    const uint16_t lineup = LINEUP_ONLY | ALL_POSITIONS;
    std::printf("%-22s %3s %3s %3s %3s | %4s %4s %4s | %3s\n", "team", "K", "T", "F", "M", "KT", "KTF", "KTFM", "n");
    int first = league_first_team(league);
    for (int t = first; t < first + kLeagueSizes[league]; t++) {
        TeamAverage k = team_average(g, t, lineup | CONDITION);
        std::printf("%-22s %3d %3d %3d %3d | %4d %4d %4d | %3d\n", name(t).c_str(), k.value,
                    team_average(g, t, lineup | TECHNIQUE).value, team_average(g, t, lineup | FORM).value,
                    team_average(g, t, lineup | MORALE).value, team_average(g, t, lineup | CONDITION | TECHNIQUE).value,
                    team_average(g, t, lineup | CONDITION | TECHNIQUE | FORM).value,
                    team_average(g, t, lineup | CONDITION | TECHNIQUE | FORM | MORALE).value, k.players);
    }

    std::printf("\nside ratings (legacy routine, computer teams): goal def mid att p4 p5, formation\n");
    for (int t = first; t < first + kLeagueSizes[league]; t++) {
        if (g.manager_of(t) >= 0) {
            std::printf("%-22s (human manager)\n", name(t).c_str());
            continue;
        }
        std::printf("%-22s", name(t).c_str());
        for (const RatingProfile& p : kRatingProfiles) {
            GameState copy = g;  // team_rating marks players, keep the probe side-effect free
            std::printf(" %4d", team_rating(copy, t, p));
        }
        std::printf("   f%d\n", g.formation(t));
    }
    return 0;
}
