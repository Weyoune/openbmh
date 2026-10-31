// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/world12/match_projection.hpp"
#include "game/world12/fixtures.hpp"
#include "game/random.hpp"
namespace bmh::world12 {
struct AiLeagueConditions {
    int month=0,temperature=0,rain=0;
    uint32_t spectators=0,date=0;
    uint8_t referee=0;
    std::vector<uint8_t> referee_properties;
    std::vector<uint8_t> presentation_buttons;
};
struct AiLeagueOutcome {
    Fixture fixture;
    LeagueResult score;
    uint8_t end_minute=0;
    uint64_t game_draws=0,watcom_draws=0;
};
// Actual Original scoring phases and AI events; no generated/test score input.
// Conditions/formation tables come from the session owner. After-match economy,
// development, standings and human tactics are deliberately outside this API.
// World match mutations and both RNGs publish atomically on successful completion.
AiLeagueOutcome simulate_ai_league_match(World&,const GameState& rules_template,
    Fixture,const AiLeagueConditions&,GameRandom&,WatcomRandom&);
}
