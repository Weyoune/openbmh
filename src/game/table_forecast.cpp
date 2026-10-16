// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/table_forecast.hpp"

#include <bit>
#include <stdexcept>

#include "game/schedule.hpp"
#include "game/source_tip.hpp"

namespace bmh {

LeagueTableForecast compute_league_table_forecast(
    const GameState& game, int league, int source_param2, int matchdays,
    int flags, int normal_mode, int sort_extra,
    const std::array<uint8_t, 10>& gap, const GameRandom& game_random) {
    if (league < 0 || league >= kLeagueCount)
        throw std::invalid_argument("table forecast: invalid league");
    if (source_param2 != 0)
        throw std::invalid_argument("table forecast: only Source param2 zero is reviewed");
    const int source_matchdays = league_matchdays(league);
    if (matchdays != source_matchdays)
        throw std::invalid_argument("table forecast: matchday count differs from Source league");
    if (flags < 0 || flags > 7)
        throw std::invalid_argument("table forecast: unsupported Source aggregation flags");
    if (normal_mode < 0 || normal_mode > 1 || sort_extra < 0 || sort_extra > 14 || (sort_extra & 1))
        throw std::invalid_argument("table forecast: unsupported Source points/sort parameters");
    if (game.league_rounds.size() < size_t(kLeagueCount) || game.results.size() < kResultsSize)
        throw std::invalid_argument("table forecast: Native round/results inputs unavailable");

    const int points_mode = normal_mode + sort_extra;
    LeagueTableForecast out;
    out.table = compute_league_table(game, league, 0, matchdays, flags, points_mode);
    out.game_after = game_random;
    const int raw_round = int(game.league_rounds[size_t(league)]);
    out.first_simulated_round = raw_round + (source_param2 != 0);
    const int first_team = league_first_team(league);
    const int teams = league_table_teams(league);
    int32_t source_seed = 0;
    if (out.first_simulated_round < matchdays) {
        if (!game.memory.has(::bmh::layout::TableForecast_InvalidArgument_RuntimeField))
            throw std::invalid_argument("table forecast: Native Source seed unavailable for future rounds");
        source_seed = std::bit_cast<int32_t>(game.memory.u32(::bmh::layout::CurrentMatchday));
        if (source_seed < 0 || source_seed > 10000)
            throw std::invalid_argument("table forecast: Native Source seed outside 0..10000");
    }

    for (int round = out.first_simulated_round; round < matchdays; ++round) {
        const auto fixtures = generate_fixtures(teams, round + 1, matchdays / 2, first_team);
        // ComputeLeagueTableForecast is invoked once per round and reseeds once at entry,
        // before iterating that round's fixture pairs.
        out.game_after.set_seed(source_seed);
        for (const auto& fixture : fixtures) {
            const int home_primary = team_average(game, fixture.home, 0x3f3).value;
            const int home_secondary = team_average(game, fixture.home, 0x3fc).value;
            const int away_primary = team_average(game, fixture.away, 0x3f3).value;
            const int away_secondary = team_average(game, fixture.away, 0x3fc).value;

            // GameRandom::set_seed deliberately preserves the cumulative call count.
            const uint8_t encoded = source_tip_result(out.game_after, gap,
                home_primary, away_primary, home_secondary, away_secondary);
            const int clubs[2] = {fixture.home - first_team, fixture.away - first_team};
            const uint8_t goals[2] = {uint8_t(encoded >> 4), uint8_t(encoded & 0x0f)};
            add_result(out.table, clubs, goals, flags & 3, normal_mode);
        }
        if (matchdays < round + 3) sort_table(out.table, sort_extra >> 1);
        ++out.simulated_rounds;
    }
    return out;
}

} // namespace bmh
