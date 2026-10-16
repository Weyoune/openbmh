// SPDX-License-Identifier: GPL-3.0-only
#include "game/match.hpp"

#include <cstdlib>
#include <stdexcept>

#include "game/rating.hpp"
#include "game/schedule.hpp"

namespace bmh {

namespace {

// Average monthly temperature in °C, indexed directly by CareerSession_PairingsRuntimeDraw_ExecutableField5 (1-12; table at
// MatchRuntimeField4, 13 entries; the month code indexes it directly, not modulo 12).
constexpr int8_t kMonthTemperature[13] = {2, 0, 8, 12, 15, 21, 26, 26, 18, 11, 6, 2, 0};
// Temperature offset of a team's home region (table at MatchRuntimeField3, index = team_region()):
// 0 top leagues, 1-4 regional leagues, from 10 on the foreign countries (7 clubs each).
// The last entries lie beyond the table in the original and are kept as found there.
constexpr int8_t kRegionTemperature[48] = {0,  -1, 5,  0,  0, 5,  0, -1, -5, -4, -2, 0,   0,  -3, -1, -1,
                                           -1, 6,  6,  4, -3, 4, -6, 7,  0,  0,  0,  0,   0,  2,  0,  -3,
                                           8,  8,  8,  8, 10, 9,  3,  11, -29, 15, 10, 0, -27, 15, 10, 0};
// Crowd factor per league in thousands (table at layout field): the home side gets its bonus if
// the attendance exceeds a random value in [factor * 1000 + 3000, factor * 1000 + 6000].
constexpr uint8_t kLeagueCrowd[10] = {27, 12, 7, 7, 3, 27, 27, 27, 27, 27};
// First league that counts as a regional league (LeaguePairings_LeaguePairingsConstants_RuntimeField).
constexpr int kFirstRegionalLeague = 5;
// Number of cup-only teams before the amateur blocks (CareerSession_InstallLeagueSizes_RuntimeField6).
constexpr int kCupTeams = 30;

int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// BuildSideRecords2 evaluated at kick-off (score 0:0, minute 0) for a computer team:
// start from 2 (home) or 1 (away), one step more defensive if the opponent's attack/
// midfield value (byte 2) is more than 10 higher, one step more offensive if it is more
// than 10 lower - but only for match_index != 0 (the original compares the match index
// where the minute was probably meant). A 1/201 roll picks a special event code that is
// not stored.
int initial_attitude(const SideRecord& own, const SideRecord& other, int side, int match_index, WatcomRandom& rand) {
    int substitution = -1;  // cannot happen at minute 0
    return attitude_update(own.b.data(), other.b.data(), side, match_index, 0, 0, 0, rand, substitution);
}

}  // namespace

int attitude_update(const uint8_t* own, const uint8_t* other, int side, int match_index, int own_goals, int opp_goals,
                    int minute, WatcomRandom& rand, int& substitution_mode) {
    int own2 = int8_t(own[2]), other2 = int8_t(other[2]);
    int base = 2 - side;
    if (own2 + 10 < other2) base = 1 - side;
    int low = base;  // attitude (low byte of uVar5 in the original)
    int high = 0;    // "special" code (high byte), only used for the substitution request
    if (other2 < own2 - 10 && match_index != 0) low = base + 1;
    if (own_goals < opp_goals && opp_goals - 2 <= own_goals) {  // behind by one or two
        low = low + 1 + (minute > 80 ? 1 : 0);
        high = minute > 80 ? 8 : 6;
    }
    if (opp_goals < own_goals && own_goals - 1 <= opp_goals) {  // ahead by one
        if (minute > 75) low--;
        if (own2 < other2 || (match_index != 0 && minute > 80)) low--;
        high = minute > 80 ? 1 : 3;
    }
    low = int8_t(low);
    if (low < 0) low = 0;
    if (low > 3) low = 3;
    if (high == 0 && rand.range(0, 200) == 0) high = 0x0B;
    substitution_mode = -1;
    if (minute > 60 && high != 0 && rand.range(0, 60) == 0) substitution_mode = high - 1;  // Injure(team, 0, high - 1)
    return low;
}

int region_temperature(int region) { return region >= 0 && region < 48 ? kRegionTemperature[region] : 0; }
int month_temperature(int month) { return kMonthTemperature[month >= 0 && month <= 12 ? month : 0]; }

int team_region(int team) {
    if (team == kNoTeam) return 0;
    int t = team;
    for (int league = 0; league < kLeagueCount; league++) {
        t -= kLeagueSizes[league];
        if (t < 0) return league >= kFirstRegionalLeague ? league - kFirstRegionalLeague + 1 : 0;
    }
    if (t - kCupTeams >= 0) return (t - kCupTeams) / 7 - kFirstRegionalLeague + 10;
    return 0;
}

void rate_side(GameState& g, const MatchSetup& m, int side, int previous6, WatcomRandom& rand, uint8_t* r, GameRandom* game) {
    int team = side ? m.away : m.home, opponent = side ? m.home : m.away;
    const int manager = g.manager_of(team);  // local_14
    // (the original also has a special case for the pseudo team 0x165 of a human manager,
    //  which does not play league or cup matches)

    // --- modifier for byte 2 ---------------------------------------------------------------
    int modifier = 0;
    if (side != 0 && m.climate_effect) {
        int region = team_region(team);
        if (region != team_region(opponent)) {
            int expected = rand.range(0, 6) + month_temperature(m.month) + region_temperature(region) - 3;
            // Abs2 on the temperature byte read unsigned (frost counts as 200+)
            int deviation = std::abs(int(uint8_t(m.temperature)) - expected);
            // The original subtracts byte 6 of the record MatchRuntimeField points to at that
            // moment, both operands as signed chars (a balance above 127 counts negative).
            modifier = int8_t(int8_t(deviation / 2) - int8_t(previous6));
        }
    }
    if (side == 0) {
        int league = team > 0x164 ? 0 : team_league(team);  // TeamLeague2
        int base = kLeagueCrowd[league < 10 ? league : 9] * 1000;
        if (unsigned(rand.range(base + 3000, base + 6000)) < unsigned(m.spectators)) {
            // a human manager's side with record +0x289 set suffers more from a full house
            modifier -= manager >= 0 && g.manager_records.at(size_t(manager) * 0xB0C + 0x289) != 0 ? 9 : 6;
        }
    }
    modifier = clamp(int8_t(modifier), -10, 10);

    // --- Uint8T3: ratings, fit-player scaling, balance ------------------------------
    // one context for all six profiles: profiles 4 and 5 see the stale technique of profile 3
    RatingContext weather{g.chance_rain, g.chance_temperature};
    weather.rand = &rand;  // clubs without squad draw their ratings
    weather.game = game;   // human teams: motivation draws
    weather.live_scene = g.live_scene;
    for (int i = 0; i < 6; i++) r[i] = uint8_t(team_rating(g, team, kRatingProfiles[i], weather));
    using namespace avg;
    TeamAverage condition = team_average(g, team, LINEUP_ONLY | ALL_POSITIONS | CONDITION, &rand);
    TeamAverage technique = team_average(g, team, LINEUP_ONLY | ALL_POSITIONS | TECHNIQUE, &rand);
    int players = technique.players;  // count of the last call, like local_10
    if (players < 11 && players > 2)
        for (int i = 1; i < 4; i++) r[i] = uint8_t((players - 3) * r[i] / 10);
    r[6] = uint8_t(clamp((condition.value - technique.value) * 4 + 100, 0, 200));

    r[2] = uint8_t(clamp(r[2] - modifier, 0, 99));
    // (the original lets two cheat codes, CareerSession_FormatError_RuntimeField9, set a human side's six ratings to
    //  fixed values; not ported)
    if (manager < 0) {
        int rhythm = r[6] >> 1;  // computer teams: half the balance, at least 50
        r[7] = uint8_t(rhythm < 50 ? 50 : rhythm);
    } else {
        // human teams: record +0xd9 (signed) * 5 plus the balance, scaled to 0-100
        const int8_t d9 = int8_t(g.manager_records.at(size_t(manager) * 0xB0C + 0xd9));
        r[7] = uint8_t((d9 * 5 + r[6]) * 100 / 0xFF);
    }
}

std::array<SideRecord, 2> build_side_records(GameState& g, const MatchSetup& m, WatcomRandom& rand, GameRandom* game) {
    std::array<SideRecord, 2> rec{};
    int previous6 = 0;  // byte 6 of the record built last (MatchRuntimeField still points to it)

    for (int side = 0; side < 2; side++) {
        SideRecord& r = rec[side];
        rate_side(g, m, side, previous6, rand, r.b.data(), game);
        previous6 = r.b[6];

        // --- BuildSideRecords2 at 0:0 ----------------------------------------------------------
        // Bug in the original: for the home side the away record is not built yet. The 18
        // bytes come from malloc without clearing (ManagerOf2 flag 0), so the home side
        // compares against leftover heap contents. That cannot be derived from the game
        // state; OpenBMH uses a cleared record (0) as a deterministic stand-in.
        const int manager = g.manager_of(side ? m.away : m.home);
        if (manager >= 0 && !g.manager_option(manager, 2))
            r.b[8] = g.manager_records.at(size_t(manager) * 0xB0C + 0xd2);  // the manager's own setting
        else
            r.b[8] = uint8_t(initial_attitude(r, rec[1 - side], side, m.match_index, rand));
    }
    return rec;
}

}  // namespace bmh
