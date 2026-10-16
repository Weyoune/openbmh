// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Player and team ratings used by the match engine.
//
// Ported from BMH.exe V2.24:
//   player_value  PlayerValue  weighted value of one player for a set of attributes
//   team_rating   TeamRating  rating of a line-up for one of six rating profiles
//
// For the teams of human managers the players are placed by the tactics board (block
// TacticsBoards) and their values are corrected by the board settings, the expected bonus
// (prediction.hpp) and random motivation draws.

#include <cstdint>

#include "game/random.hpp"
#include "game/state.hpp"

namespace bmh {

// Attribute bits of player_value (the "param_2" of PlayerValue). Each selected
// attribute adds value/weight to a weighted mean:
namespace attr {
constexpr uint16_t CONDITION = 1 << 0;   // KO, weight 100
constexpr uint16_t TECHNIQUE = 1 << 1;   // TE, weight 100
constexpr uint16_t FORM = 1 << 2;        // not averaged: (form / 8 - 6) percent on the multiplier
constexpr uint16_t DEFENSIVE = 1 << 3;   // 9 - 3 * position (goalkeeper 9 .. forward 0), weight 9
constexpr uint16_t OFFENSIVE = 1 << 4;   // 3 * position, weight 9
constexpr uint16_t TEAMPLAY = 1 << 5;    // skill * 5, weight 15 (bits 5-11 = the 7 skills)
constexpr uint16_t SPEED = 1 << 6;
constexpr uint16_t SHOOTING = 1 << 7;
constexpr uint16_t PASSING = 1 << 8;
constexpr uint16_t TACKLING = 1 << 9;
constexpr uint16_t FREE_KICK = 1 << 10;
constexpr uint16_t CORNER = 1 << 11;
constexpr uint16_t EXPERIENCE = 1 << 12; // min(3 * (w364 bits 2-7), 20), weight 20
}  // namespace attr

// Match context globals the original reads while rating (ChanceRain, ChanceTemperature).
// They are per-match values of the running match; outside a match they keep whatever the
// last match left behind. Zero is the value after program start.
struct RatingContext {
    int chance_rain = 0;
    int chance_temperature = 0;
    // local_1c of PlayerValue is only written when the condition attribute (bit 0) is
    // selected; otherwise it holds whatever the previous call at the same stack depth left
    // there. For the free kick taker (attributes 0x0802) that is the condition of the last
    // candidate of the shooter pick just before. -1 = unknown, 0 is used.
    int stale_condition = -1;
    // local_20, the same for the technique (bit 1). Rating profiles 4 and 5 of the side
    // record select the condition but not the technique, so the condition/technique
    // comparison uses the technique of the player rated last with profile 3.
    // (Now and then the original finds a larger value there: nothing in the program writes
    //  the slot in between, so a hardware interrupt on the stack must have - not modelled.)
    int stale_technique = -1;
    // Watcom generator for clubs without squad, whose ratings are drawn (TeamRating).
    WatcomRandom* rand = nullptr;
    // Game generator, needed for the players of human managers (the bonus motivation).
    GameRandom* game = nullptr;
    // LiveSceneVisible: the live scene is shown; then the board's slot byte 5 counts.
    bool live_scene = false;
};

// PlayerValue for computer teams:
//   player    player id (0xFFF gives 0)
//   attrs     attribute bits (namespace attr)
//   scale     result scale, normally 100
//   team      team the player plays for (its morale counts)
//   Result: scale * (sum * multiplier / 100) / weight, where multiplier =
//   100 + bonus/2 + form term - (6 - morale/4), with extra rules for KO/TE imbalance.
//   x, y      place on the tactics board (FormationPosition), 0xFF = none; only used for
//             human managers from game level 4 on
int player_value(const GameState& g, int player, uint16_t attrs, int scale, int team, const RatingContext& ctx = {},
                 int x = 0xFF, int y = 0);

// The six rating profiles of the side record (table at layout field): weights for the
// goalkeeper, defence, midfield and attack groups plus the player_value attributes.
struct RatingProfile {
    int16_t weight[4];
    uint16_t attrs;
};
constexpr RatingProfile kRatingProfiles[6] = {
    {{80, 10, 10, 0}, 0x0007},   // 0 goalkeeping: KO TE form
    {{25, 55, 10, 10}, 0x0267},  // 1 defence: + teamplay speed tackling
    {{20, 15, 45, 20}, 0x0367},  // 2 midfield: + passing
    {{10, 10, 30, 50}, 0x0067},  // 3 attack: KO TE form teamplay speed
    {{0, 33, 33, 34}, 0x0481},   // 4 KO shooting free kick
    {{0, 33, 33, 34}, 0x0A01},   // 5 KO tackling corner
};

// TeamRating for computer teams. Walks the line-up (squad slots flagged in-line-up,
// stopping at the first empty slot), puts every player into a group by formation depth
// (x < 1 goalkeeper, < 8 defence, < 13 midfield, < 22 attack) and a lane (defence and
// midfield: left/centre/right = y / 3), sums player_value per bucket, corrects each group
// by 10 % per player above/below the nominal 1-4-4-2, weights and averages the buckets,
// scales down for fewer than 11 players and caps the result at 110.
//
// Side effects like the original: sets bit 12 of w364 for every line-up player and
// returns the goalkeeper's player id in *keeper (0xFFF if none) (RatingRuntimeField8).
// `ctx` carries the stale locals from call to call (see RatingContext) and is updated.
int team_rating(GameState& g, int team, const RatingProfile& profile, RatingContext& ctx, int* keeper = nullptr);
inline int team_rating(GameState& g, int team, const RatingProfile& profile) {
    RatingContext ctx;
    return team_rating(g, team, profile, ctx);
}

}  // namespace bmh
