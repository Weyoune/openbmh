// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/match_engine.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include "game/lineup.hpp"
#include "game/match.hpp"
#include "game/matchday.hpp"
#include "game/messages.hpp"
#include "game/rating.hpp"
#include "game/reality_match.hpp"

namespace bmh {

namespace {

// Goal threshold by chance quality index 5..24 (table at layout field).
constexpr uint8_t kChanceTable[25] = {11, 12, 13, 14, 16, 18, 20, 21, 22, 22, 23, 25, 26,
                                      27, 28, 28, 29, 29, 30, 30, 31, 31, 32, 32, 33};
// player_value attributes by pick mode (table at layout field): 0 shooter, 1 assistant,
// 2 penalty taker, 3 free kick / corner taker.
constexpr uint16_t kPickAttributes[4] = {0x1297, 0x0127, 0x0080, 0x0802};

int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

}  // namespace

MinuteReport MatchEngine::minute(int minute, int last_minute, int stoppage) {
    MinuteReport report;
    if (reality_rules && !state.reality.initialized) {
        for (size_t i = 0; i < 5; ++i) state.reality.cue_actors[i] = reality_rules->u8(::bmh::layout::MatchEngine_Minute_ExecutableField2 + uint32_t(i));
        state.reality.card_kind = reality_rules->u8(::bmh::layout::MatchEngine_Minute_ExecutableField);
        state.reality.deflected = reality_rules->u8(::bmh::layout::MatchEngine_Minute_ExecutableField3) != 0;
        state.reality.distance = reality_rules->u8(::bmh::layout::MatchEngine_Minute_ExecutableField4);
        state.reality.initialized = true;
    }
    for (MatchGroup& g : groups) {
        for (int m = 0; m < g.matches(); m++) {
            uint32_t bit = 1u << m;
            if ((g.ended & bit) || (g.void_mask() & bit)) continue;
            // End of match / half. The Watcom roll is only drawn after the regular time.
            if ((last_minute < minute && rand.range(0, 1) != 0) || minute == stoppage + last_minute) {
                g.end_minute.at(m) = uint8_t(minute);
                g.ended |= bit;
                continue;
            }
            // Attitudes are read once per match and minute and swapped between the sides
            // (local_a4 = own, local_8c = opponent).
            int own = g.records.at(m * 18 + 8), opp = g.records.at(m * 18 + 17);
            for (int s = 0; s < 2 && !report.unsupported; s++) {
                try {
                    side(g, m, s, minute, own, opp, report);
                } catch (const std::exception& ex) {
                    report.fail("internal error in match " + std::to_string(m) + " side " + std::to_string(s) + " (teams " +
                                std::to_string(g.fixtures.at(m * 2)) + "-" + std::to_string(g.fixtures.at(m * 2 + 1)) +
                                "): " + ex.what());
                }
                std::swap(own, opp);
            }
            if (report.unsupported) return report;
        }
    }
    return report;
}

// OpenBMH deviates from the original: supports designated set-piece takers, the enhanced injury tables, and the extended suspension fields.
// PickTaker via DesignatedSlot / PickPlayer: picks a line-up player of `team`. Up to 199
// tries: a random line-up slot, the player's value for the mode plus a position bonus,
// accepted if WatcomRandom(0, 180) < value; a goalkeeper (depth 0) is rejected 99 of 100
// times. Sets last_pick_quality (MatchEngineRuntimeField) to the value of the last candidate - 81.
// `preset` (supported rule profile patch, PickPlayer2/80c20/80c40: mode bits 2-5 = slot + 1) fixes the
// slot and accepts without the Watcom roll; only the goalkeeper check remains.
// Human teams use the places on their tactics board, also for the player's value.
int MatchEngine::pick_player(int team, int mode, int exclude, MinuteReport& report, int preset) {
    mode &= 0x7F;  // 0x80 = reuse the squad of the previous call, same team here
    if (team >= 138 && team != kNoTeam) return 0xFFF;  // clubs without squad: nobody, no draws
    SquadRange range = squad_range(team);
    if (range.count == 0) return 0xFFF;
    RatingContext ctx{state.chance_rain, state.chance_temperature};
    for (int tries = 0; tries < 199; tries++) {
        int slot = preset >= 0 ? preset : rand.range(0, 10);
        // MatchEngineRuntimeField2 / MatchEngineRuntimeField3: slot of the last candidate (shooter / assistant modes)
        if (mode == 1 || mode == 3) last_assist_slot = slot;
        else last_shooter_slot = slot;
        uint16_t entry = state.squad_slots.at(range.first + slot);
        if (!(entry & kSlotInLineup)) continue;
        int player = entry & 0x0FFF;
        if (player == exclude || player == 0xFFF) continue;
        int x = 0, y = 0;
        state.formation_position(team, slot, x, y);
        ctx.stale_condition = pick_stack_condition;
        int v = int16_t(player_value(state, player, kPickAttributes[mode], 100, team, ctx, int16_t(x), int16_t(y)));
        if (kPickAttributes[mode] & 1) pick_stack_condition = state.players.at(player).condition();
        if (mode == 0) v = int16_t(v + x * x / 4 - (4 > y ? 4 - y : y - 4) * 15);
        else if (mode == 1) v = int16_t(v + 81 - (10 > x ? 10 - x : x - 10) * 8);
        else v = int16_t(v + 81);
        last_pick_quality = int16_t(v - 81);
        bool accepted = preset >= 0 ? true : rand.range(0, 180) < v;
        if (x == 0 && rand.range(0, 100) != 0) accepted = false;
        if (accepted) return player;
    }
    return 0xFFF;
}

// SetPieceAnimation + Range4: the animated shot on goal of the live desk. Only the random
// numbers are reproduced here (the picture is the UI's business): the side of the goal,
// the ball's end point and the goalkeeper's reaction frames.
void MatchEngine::shot_animation(bool miss, bool follow_up, int acting_side, int group, int match, int minute) {
    static const uint8_t kEndLane[2][2] = {{83, 100}, {217, 234}};  // MatchEngineRuntimeField4, by side
    // Frame at which the keeper reacts, by reaction step (0-3) and height band (0-2)
    // (LiveMatchday_FormatError_RuntimeField6, 4 bytes per step).
    static const uint8_t kReactionFrame[4][4] = {{3, 4, 4, 12}, {7, 8, 13, 13}, {9, 15, 15, 15}, {0, 0, 0, 0}};

    int side = rand.range(0, 1);  // left or right of the goal (index into kEndLane)
    int flip = side;              // local_c: mirror the goal picture
    // A scored follow-up is sometimes shown as a plain shot (param_2 cleared).
    if (!miss && follow_up) {
        if (rand.range(0, 2) != 0) follow_up = false;
        else flip ^= 1;
    } else {
        follow_up = false;
    }
    int height = rand.range(0x17, 0x3e);                             // param_3 of Range4
    int lane = rand.range(kEndLane[side][0], kEndLane[side][1]);     // param_2

    // Range4: the ball's end point; a goal gets some scatter.
    int x = lane, y = height;  // iVar8, local_6c
    if (!miss) {
        if (!follow_up) {
            x += rand.range(-5, 5) + (lane < 160 ? 5 : lane > 160 ? -5 : 0);
            y = rand.range(0, 10) + height - 5;
            y = y < 0x1b ? 0x1b : y > 0x46 ? 0x46 : y;
        } else {
            x = rand.range(kEndLane[flip][0], kEndLane[flip][1]);
            y = rand.range(0x17, 0x3e);
        }
    }
    const int band = (y < 0x2f) + (y < 0x22);  // local_14
    int reached = 0, step = 0, delay = 0;      // local_40, local_44, local_48
    int keeper_delay = 0;                      // (for the picture: the first reaction comes late)
    for (int frame = 0; frame < 18; frame++) {
        if (frame == kReactionFrame[step][band] && reached < 3) {
            reached++;
            // The first reaction of the keeper on a goal comes 2-4 frames late (the
            // original draws rand(0, 0) first, which is always 0).
            if (!miss && step == 0 && rand.range(0, 0) == 0) delay = keeper_delay = rand.range(2, 4);
            else delay = 1;
        }
        if (int16_t(delay) != 0) {
            if (delay == 1) step = reached;
            delay--;
        }
    }
    if (show) {
        ShowEvent e{Show::ShotOnGoal};
        e.side = uint8_t(side), e.kind = miss;
        e.flag = uint8_t((follow_up ? 1 : 0) | (flip ? 2 : 0));
        e.a = height, e.b = lane, e.c = y, e.d = keeper_delay;
        emit(std::move(e));
    }
    if (live_shot)
        live_shot({minute, group, match, acting_side, lane, height, x, y, keeper_delay, miss, flip != 0});
}

// LiveSetPieceFrames: the animated set piece of the live desk (kind 1 penalty/free kick from
// `distance`, 2 follow-up, 3 corner-like set piece). The original builds a small sprite
// scene (25 figures with position, speed, direction and a frame timer) and records up to
// 0x576 frames, drawing random numbers while placing the figures and at the "phases" of
// the scene, which start whenever a figure's timer runs out. Only the parts that decide
// the random numbers are reproduced:
//   - the placement draws (they depend on each other, not on the sprite state),
//   - the phase timeline: the goalkeeper's timer (3 frames) starts phase 0, the kicker's
//     run-up timer (t or r frames) phase 1 and so on,
//   - the x speed of figure 3 (MatchEngineRuntimeField5), the only sprite state a draw depends on.
// The frame list can never overflow here (at most ~30 frames of <= 26 entries).
// `side` is MatchEngineRuntimeField6 (1 = the home side attacks, the picture is mirrored).
// An installed observer retains those same draws for the UI's figure reconstruction.
void MatchEngine::set_piece_animation(int kind, int distance, int side, int group, int match, int minute) {
    std::vector<LiveSetPieceDraw> draws;
    const auto draw = [&](int low, int high) {
        const int value = rand.range(low, high);
        if (live_set_piece) draws.push_back({low, high, value});
        return value;
    };
    static const int16_t kSpotX[2] = {153, 845};        // LiveSetPiece_Run_ExecutableField5: ball x of a follow-up
    static const int8_t kDirection[2] = {-1, 1};        // LiveSetPiece_Run_ExecutableField6
    static const int8_t kPlayerX[8] = {0, 20, 30, 20, 30, 30, 30, 30};           // LiveSetPiece_Run_ExecutableField11
    static const uint8_t kPlayerY[8] = {90, 120, 160, 160, 170, 170, 80, 80};    // LiveSetPiece_Run_ExecutableField4
    static const int8_t kAttackerX[2] = {-20, 20};      // LiveSetPiece_Run_ExecutableField9
    static const int16_t kKeeperX[2] = {65, 936};       // LiveSetPiece_Run_ExecutableField
    const int dir = kDirection[side];
    auto clamp1 = [](int v) { return v > 1 ? 1 : v < -1 ? -1 : v; };

    int param = distance ? uint8_t(distance - 15) : 0;
    int ball_x = 0, ball_y = 0;  // local_38, DI
    int kicker_timer = 0;        // frames until phase 1
    if (kind == 1 || kind == 2) {
        if (kind == 2) {
            ball_x = kSpotX[side];
            ball_y = 0x77;
        } else {
            ball_y = draw(0x4c, 0xb0);  // MatchEngineRuntimeField7 is 0 outside the animated scene
            if (param > 0x23) param = 0x23;
            // Int16T2: x offset of the goal line at height y
            int offset = int16_t(((int16_t(ball_y) - 0x17) * 0x3d) / 0x96);
            if (side) {
                int base = int16_t(offset + 0x2fd);
                ball_x = param ? int16_t(base - param * 5) : draw(0x2ae, base);
            } else {
                int base = int16_t(-offset + 0xef);
                ball_x = param ? int16_t(base + param * 5) : draw(base, 0x136);
            }
        }
        kicker_timer = int16_t(draw(10, 40)) >> 1;  // run-up of figure 1
        draw(0, 2);                                  // its y speed
    }
    int r = 0;
    if (kind == 3) {
        ball_y = draw(0, 1) ? 0xe6 : -20;
        ball_x = kSpotX[side] + int16_t(((int16_t(ball_y) - 0x17) * 0x3d) / 0x96) * (side ? 1 : -1) + dir * 0x19;
        draw(-20, 20);
        draw(0x6d, 0x8b);
        r = draw(8, 12);  // flight time of the ball = timer of kicker and ball
        draw(0, 5);
        draw(1, 4);
        draw(1, 6);
    }
    // Further players (figures LiveSetPiece_Run_ExecutableField10), placed unless too close to the ball.
    int placed = 0;
    if (kind == 1 || kind == 3) {
        for (int i = 0; i < (kind == 3 ? 8 : 4); i++) {
            if (kind != 3 && draw(0, 1) == 0) continue;
            int x = int16_t(kSpotX[side] + draw(kPlayerX[i] - 10, kPlayerX[i] + 10));
            int y = int16_t(draw(kPlayerY[i] - 10, kPlayerY[i] + 10));
            if (std::abs(x - int16_t(ball_x)) < 100 && std::abs(y - int16_t(ball_y)) < 60) continue;
            placed++;
        }
    }
    // Penalty/free kick: two attackers next to the ball (figures 2 and 3); figure 3 may run
    // towards the ball (0x6a) or the goalkeeper (0x5f) and then has an x speed.
    int speed3 = 0;
    if (kind == 1) {
        for (int j = 0; j < 2; j++) {
            int x, lo, hi;
            if (j == 0 && draw(0, 1) != 0) {
                x = ball_x;
                lo = 3, hi = 6;
            } else {
                x = int16_t(ball_x + draw(kAttackerX[j] - 10, kAttackerX[j] + 10));
                lo = 5, hi = 20;
            }
            x = int16_t(x + dir * int16_t(draw(lo, hi)));
            draw(4, 10);
            if (draw(0, 1) == 0) draw(0, 10);
            int code = 0xaa;
            if (draw(0, 1) == 0 && x - kKeeperX[side] > int16_t(ball_x) - kKeeperX[side])
                code = draw(0, 1) ? 0x5f : 0x6a;
            if (j == 1) {
                if (code == 0x6a) speed3 = clamp1(int16_t(ball_x) - x);
                else if (code == 0x5f) speed3 = clamp1(kKeeperX[side] - x);
            }
        }
    }

    // --- phases -------------------------------------------------------------------------
    if (kind == 1 || kind == 2) {
        // frame 3: goalkeeper (phase 0, no draws); frame t: the shot (phase 1); frame t+1:
        // phase 2 ends a follow-up, a penalty/free kick ends at phase 3 (no draws).
        draw(7, 12);  // ball speed; the figure of this number may run in (kind 1)
        draw(0, 1);
        if (kind == 1) {
            draw(0, 1);
            if (speed3 == 0) draw(0, 1);
        }
        (void)kicker_timer;
    } else if (kind == 3) {
        // Phase 0 (goalkeeper, frame 3), 1 (kicker, frame r), 2 (ball, same frame), 3 and 4:
        // after every phase each placed player turns (one draw each).
        auto players_turn = [&]() {
            for (int i = 0; i < placed; i++) draw(0, 1);
        };
        players_turn();                                  // phase 0
        draw(0, 2);                                // phase 1: header height
        players_turn();
        draw(8, 10);                               // phase 2: ball speed
        draw(0, 1);
        draw(3, 5);  // always: the flag it tests (local_3c) is never 0 by then
        players_turn();
        players_turn();                                  // phase 3
        players_turn();                                  // phase 4 (end)
    }
    if (show) {
        ShowEvent e{Show::SetPiece};
        e.side = uint8_t(side), e.kind = uint8_t(kind);
        e.a = ball_x, e.b = ball_y, e.c = kicker_timer, e.d = r;
        emit(std::move(e));
    }
    if (live_set_piece)
        live_set_piece({minute, group, match, side, kind, distance, std::move(draws)});
}

// OpenBMH deviates from the original: supports designated set-piece takers, the enhanced injury tables, and the extended suspension fields.
// Emit (supported rule profile patch): line-up slot (0-10) of the taker a human manager has
// designated for a set piece (manager record field at `offset`: 0xafc penalty, 0xafe
// penalty follow-up, 0xb08 free kick), or -1. The taker must play, be fit and differ from
// `exclude`. The original reads the record of the "current" manager ActiveManager and looks
// for the player in the shooting team; for computer teams that never matches, so the
// team's own manager is used here (identical with one human manager).
int MatchEngine::designated_slot(int team, int offset, int exclude) const {
    int manager = state.manager_of(team);
    if (manager < 0 || state.manager_records.empty()) return -1;
    const uint8_t* rec = state.manager_records.data() + size_t(manager) * 0xB0C;
    auto u16 = [&](int at) { return int(rec[at] | rec[at + 1] << 8); };
    if (u16(0xb0a) != 0x3253) return -1;
    int id = u16(offset);
    if (id == 0 || id > 0xFFF) return -1;
    id--;
    SquadRange range = squad_range(team);
    for (int s = 0; s < 11 && range.count != 0; s++) {
        uint16_t entry = state.squad_slots.at(range.first + s);
        if (!(entry & kSlotInLineup)) continue;
        int player = entry & 0x0FFF;
        if (player != id || player == exclude || player == 0xFFF) continue;
        if (state.players.at(player).w364 >> 16 & 0x1F) continue;  // injured or banned
        return s;
    }
    return -1;
}

// DesignatedSlot / PickPlayer: pick with a designated taker first (penalty shooter when
// kind 1, free kick taker when kind 3); if that fails, the normal pick.
int MatchEngine::pick_taker(int team, int mode, int exclude, int kind, MinuteReport& report) {
    int preset = -1;
    if (kind == 1 && (mode & 0x7F) == 2) preset = designated_slot(team, 0xafc, exclude);
    if (kind == 3 && (mode & 0x7F) == 3) preset = designated_slot(team, 0xb08, exclude);
    if (preset >= 0) {
        int player = pick_player(team, mode, exclude, report, preset);
        if (player != 0xFFF || report.unsupported) return player;
    }
    return pick_player(team, mode, exclude, report);
}

// Injure through the line-up module; human teams are reported as unsupported.
bool MatchEngine::substitute(int team, int off, int mode, MinuteReport& report) {
    try {
        return bmh::substitute(state, team, off, mode, rand);
    } catch (const std::exception& ex) {
        report.fail(std::string("substitution: ") + ex.what());
        return false;
    }
}

// BuildSideRecords3 for one side of match m: rebuilds the side record after a line-up change
// (substitution, sending-off, injury). MatchRuntimeField points to the own record at every call
// site in the match loop, so its byte 6 is the "previous" balance.
void MatchEngine::rerate_team(int team, MinuteReport& report) {
    for (MatchGroup& g : groups)
        for (int m = 0; m < g.matches(); m++)
            for (int s = 0; s < 2; s++)
                if (g.fixtures.at(size_t(m * 2 + s)) == team) {
                    rerate(g, m, s, report);
                    return;
                }
    report.fail("re-rating: the team plays no match today");
}

void MatchEngine::rerate(MatchGroup& g, int m, int s, MinuteReport& report) {
    if (g.spectators.size() < size_t(g.matches())) {
        report.fail("re-rating needs the attendance (not recorded)");
        return;
    }
    uint8_t* own = g.records.data() + m * 18 + s * 9;
    MatchSetup setup;
    setup.home = g.fixtures.at(m * 2);
    setup.away = g.fixtures.at(m * 2 + 1);
    setup.match_index = m;
    setup.climate_effect = (g.flags() & 2) != 0;
    setup.temperature = int8_t(g.desc.at(0x30 + m));
    setup.spectators = int(g.spectators[m]);
    setup.month = month;
    try {
        rate_side(state, setup, s, own[6], rand, own, &game);
    } catch (const std::exception& ex) {
        report.fail(std::string("re-rating: ") + ex.what());
    }
}

namespace {

// OpenBMH deviates from the original: supports designated set-piece takers, the enhanced injury tables, and the extended suspension fields.
// Injury type tables of InjurePlayer3 (supported rule profile patch): WatcomRandom(0, 999) is mapped to an
// injury type by the first threshold it does not exceed; beyond the last one the type is
// 30. Match injuries use the second table (types 6-30, the light types 0-5 of the first
// table only happen elsewhere, e.g. in training).
constexpr uint16_t kInjuryThresholdsOther[30] = {
    0x01f, 0x03f, 0x05f, 0x07f, 0x09f, 0x0bf, 0x0f1, 0x122, 0x153, 0x184, 0x1b5, 0x1e6, 0x217, 0x248, 0x279,
    0x2aa, 0x2db, 0x30c, 0x33d, 0x36e, 0x37e, 0x38e, 0x39e, 0x3ae, 0x3be, 0x3c9, 0x3d4, 0x3da, 0x3e0, 0x3e6};
constexpr uint16_t kInjuryThresholdsMatch[24] = {
    0x03c, 0x079, 0x0b6, 0x0f3, 0x130, 0x16d, 0x1aa, 0x1e7, 0x224, 0x260, 0x29c, 0x2d8,
    0x315, 0x351, 0x365, 0x379, 0x38d, 0x3a1, 0x3b5, 0x3c3, 0x3d1, 0x3d8, 0x3df, 0x3e6};
// Injury duration (matchdays) by type, table at layout field. Types 0-2 cause no absence.
constexpr uint8_t kInjuryDuration[31] = {1, 1, 1, 2, 2, 2, 2, 3, 2, 3, 2, 2, 2, 2, 3, 4,
                                         2, 3, 2, 3, 6, 8, 6, 8, 10, 12, 16, 20, 24, 28, 30};
// Event log codes by event kind (MatchEngineRuntimeField8): yellow, red, injury. A second yellow card
// is logged as red - 1 = 4.
constexpr uint8_t kEventLogCode[3] = {3, 5, 6};
// Class of each competition (MatchEngineRuntimeField9, by descriptor byte 0): 0 for league (0) and
// the competitions 10 and 20-23, which count cards per team; 0xFF = unused id.
constexpr std::array<uint8_t, 24> kCompetitionClass = {0, 1, 2, 2, 2, 3, 3, 3, 3, 255, 0, 255,
                                                       4, 255, 255, 255, 255, 255, 255, 1, 0, 0, 0, 0};

}  // namespace

// OpenBMH deviates from the original: supports designated set-piece takers, the enhanced injury tables, and the extended suspension fields.
// InjurePlayer(slot, roll, match, manager) with its supported rule profile continuation (InjurePlayer4,
// InjurePlayer3, InjurePlayer2, Int16T4, tail at layout field): injures the player in the
// absolute squad slot. Returns the injury type; human insurance, club doctor,
// tactics-board flags and messages are handled below. The watched-match caller
// owns any subsequent interactive substitution through live_event.
int injure_player(GameState& g, int slot, int type, bool match, int manager, WatcomRandom& rand) {
    const int id = g.squad_slots.at(size_t(slot)) & 0x0FFF;
    Player& p = g.players.at(size_t(id));
    // Bits 13-15 of w364 protect the player (not decoded yet): nothing happens.
    if (p.w364 >> 8 & 0xE0) return type;

    // OpenBMH deviates from the original: supports designated set-piece takers, the enhanced injury tables, and the extended suspension fields.
// InjurePlayer3 (supported rule profile) ignores the type passed in and draws it
    const int roll = rand.range(0, 999);
    type = 30;
    if (match) {
        for (int i = 0; i < 24; i++)
            if (roll <= kInjuryThresholdsMatch[i]) { type = i + 6; break; }
    } else {
        for (int i = 0; i < 30; i++)
            if (roll <= kInjuryThresholdsOther[i]) { type = i; break; }
    }
    int duration = type > 2 ? kInjuryDuration[type] : 0;
    // w35c bits 27-31 remember the last injury type + 1: a player injured before is out
    // 1/8 longer (match injuries only).
    if (match && (p.w35c >> 27) != 0) duration = uint8_t(duration + (duration >> 3));
    if (manager >= 0) {
        // layout field: a player insured on the tactics board (slot byte 0x43, bits 6-7 the
        // cover) earns base pay * (50 + 50 * cover) / 100 per week while out (SquadRange2);
        // the manager is told so ("Ihre Versicherung zahlt Ihnen ...", MatchEngineRuntimeField10 " ")
        const SquadRange range = squad_range(g.manager_teams.at(size_t(manager)));
        const size_t board = size_t(manager) * 0x840 + (size_t(slot) - range.first) * 0x58;
        const uint8_t cover = g.tactics.at(board + 0x43);
        if (cover != 0 && duration != 0) {
            const int32_t base = int32_t(g.tactics.at(board + 0x50) | g.tactics.at(board + 0x51) << 8 |
                                         g.tactics.at(board + 0x52) << 16 | g.tactics.at(board + 0x53) << 24);
            const int32_t pay = base * ((int8_t(cover) >> 6 & 3) * 0x32 + 0x32) / 100;
            post_composed(g, manager, message_text(g, ::bmh::layout::MatchEngine_InjurePlayer_TextPointer) + g.player_name(id) + " " + format_amount(g, pay) +
                                          message_text(g, ::bmh::layout::MatchEngine_InjurePlayer_TextPointer2));
        }
    }
    if (manager >= 0 && duration != 0) {
        // A human manager's club doctor (record +0x8ae) shortens the absence by
        // duration * (doctor / 100 - 5) / 16, at most 1 matchday for types 3-19, up to 5 for
        // the heavy ones (Int16T4); a poor doctor makes it longer.
        const int doctor = int16_t(g.manager_records.at(size_t(manager) * 0xB0C + 0x8ae) |
                                   g.manager_records.at(size_t(manager) * 0xB0C + 0x8af) << 8);
        // (the / 16 is Watcom's SAR/SHL/SBB idiom at 00054453: (x + 7) >> 4 for negative
        //  x, so a poor doctor lengthens by one matchday from x = -8 on)
        const int x = duration * (doctor / 100 - 5);
        int cut = (x + (x < 0 ? 7 : 0)) >> 4;
        int cap = type >= 3 ? 1 : 0;
        if (type >= 0x14) cap = ((type - 0x10) >> 2) + 1 + (type >= 0x1a ? 1 : 0);
        if (cut > cap) cut = cap;
        duration = uint8_t(duration - cut);
    }
    duration = std::clamp(int(int8_t(duration)), 0, 30);  // Int8T
    p.w35c = (p.w35c & 0x07FFFFFFu) | uint32_t(type + 1) << 27;  // BH = 1 at InjurePlayer
    p.w364 = (p.w364 & 0xFFE0FFFFu) | uint32_t(duration) << 16;
    return type;
}

bool MatchEngine::injure(int slot, bool match, int manager, MinuteReport& report) {
    (void)report;
    injure_player(state, slot, 0, match, manager, rand);
    return true;
}

// EventImpl(-1, 0, side): a foul by `side` in match m. After the referee and team rolls
// the event is one of
//   kind 2  injury (1 in 39): a random line-up player is injured and substituted,
//   kind 1  straight red card (1 in 28 of the bookings), banned for 1-4 matches,
//   kind 0  yellow card; a second yellow in the same match turns into a red card (ban 1).
// The ordinary minute loop ignores the return byte. Its draw order, immediate
// substitutions and synchronous presenters remain the Source mode0 path.
void MatchEngine::event(MatchGroup& g, int m, int s, int minute, MinuteReport& report) {
    (void)event_impl(g, m, s, minute, -1, report);
}

ForcedMatchEvent MatchEngine::forced_event(MatchGroup& g, int m, int s, int minute,
                                           int absolute_slot, MinuteReport& report) {
    if (s < 0 || s > 1 || m < 0 || m >= g.matches())
        throw FormatError("Reality event needs a valid fixture and side");
    const auto range = squad_range(g.fixtures.at(m * 2 + s));
    if (absolute_slot < int(range.first) || absolute_slot >= int(range.first + range.count))
        throw FormatError("Reality offender is outside the selected team's squad");
    return event_impl(g, m, s, minute, absolute_slot, report);
}

ForcedMatchEvent MatchEngine::event_impl(MatchGroup& g, int m, int s, int minute,
                                          int fixed_slot, MinuteReport& report) {
    ForcedMatchEvent result;
    const bool forced = fixed_slot >= 0;
    if (match_events_disabled) return result;
    show_at(g, m, minute);
    // The stricter the referee (bits 3-4 of his table byte), the likelier an event.
    int referee = g.desc.at(0x10 + m);
    int k = int8_t(3 - (referees.at(referee) >> 3 & 3)) + 1;
    if (rand.range(0, k) != 0) return result;

    int team = g.fixtures.at(m * 2 + s);
    int opp_team = g.fixtures.at(m * 2 + 1 - s);  // MatchEngineRuntimeField11
    // Small-squad teams (id >= 90) only half of the time; teams without squads never. The
    // national team (0x165, squad nominated on the national team days) only while manager
    // slot 0 holds it (CupDraw_FormatError_RuntimeField2 == 0x165: a Bundestrainer's match).
    if (!(team < 90 || rand.range(0, 1) == 0)) return result;
    if (!(team < 138 || team == kNoTeam)) return result;
    if (team == kNoTeam && (state.manager_teams.empty() || state.manager_teams[0] != kNoTeam)) return result;
    const int manager = state.manager_of(team);  // FindManagerForClub, local_6c
    const bool human = manager >= 0;
    SquadRange range = squad_range(team);        // SquadRange4

    int kind = 0;       // local_70: 0 yellow, 1 red, 2 injury
    int card_delta = 0;  // added to the team's card counters
    int ban = 0;        // local_80: matches banned for a red card (0 = no red card)
    if (!forced && rand.range(0, 38) == 0) {
        kind = 2;
        rand.range(10, 30);  // local_7c: passed to InjurePlayer but overwritten there
    } else {
        if (rand.range(0, 2) == 0) return result;  // no booking after all
        // local_5c = the team's card counters DeskRecords_Teams_RuntimeField (low byte yellow, high byte red;
        // red cards only counted in competitions 0 and 10)
        if (rand.range(0, 27) == 0) {
            kind = 1;
            ban = rand.range(1, 4);
            if (g.desc.at(0) == 0 || g.desc.at(0) == 10) card_delta = 0x100;
        } else {
            card_delta = 1;
        }
    }
    if (range.count == 0) return result;

    // Pick the offender: mode0 draws slot0-11, mode1 keeps the supplied slot.
    // Empty or not playing ends the event. Players
    // with more condition than technique foul more; goalkeepers rarely (1 in 5, injuries
    // 1 in 2).
    int slot = 0, player = 0;
    for (int tries = 0;; tries++) {
        slot = forced ? fixed_slot : int(range.first) + rand.range(0, 11);
        uint16_t entry = state.squad_slots.at(slot);
        if (entry == 0x0FFF) return result;
        if (!(entry & kSlotInLineup)) return result;
        player = entry & 0x0FFF;
        const Player& p = state.players.at(player);
        int balance = int8_t(uint8_t(p.condition() - p.technique()));
        if (rand.range(0, 20) > balance + 15 - int8_t(tries)) continue;
        if (p.position() != 0 || rand.range(0, kind == 2 ? 1 : 4) == 0) break;
    }
    Player& p = state.players.at(player);

    if (kind == 2) {
        if (human && rand.range(0, 2) != 0) {
            // layout field: with 2 in 3 the slot's byte 0x0f of the tactics board protects the
            // player of a human team: no injury if W(0, 60) exceeds it (the event ends)
            const int protection = state.tactics.at(size_t(manager) * 0x840 + (size_t(slot) - range.first) * 0x58 + 0xf);
            if (rand.range(0, 60) > protection) return result;
        }
        if (!injure(slot, true, manager, report)) return result;
    }

    bool second_yellow = false;  // local_74
    if (kind == 0 && (state.squad_slots.at(slot) & 0x1000)) {
        kind = 1;
        second_yellow = true;
        ban = 1;
    }
    if (kind == 0) {
        int yellow = int(p.w368 >> 14 & 0xF);  // yellow cards this season
        if (yellow < 15) yellow++;
        p.w368 = (p.w368 & 0xFFFC3FFFu) | uint32_t(yellow) << 14;
        state.squad_slots.at(slot) |= 0x1000;  // booked in this match
    }
    auto bump = [&](int offset) {  // descriptor counters: +0xbc bookings, +0xc0 red cards
        uint32_t v = le32(g.desc, offset) + 1;
        for (int i = 0; i < 4; i++) g.desc.at(offset + i) = uint8_t(v >> (8 * i));
    };
    if (ban != 0) {
        bump(0xc0);
        // OpenBMH deviates from the original: supports designated set-piece takers, the enhanced injury tables, and the extended suspension fields.
// Absence (supported rule profile): the ban goes to w364 bits 16-20 (duration | 0x10 = banned
        // instead of injured). A player with an injury history (w35c bits 27-31) keeps a
        // running injury; if he is fit, the history is cleared outside competitions
        // (MatchEngineRuntimeField9 flag of the competition, local_9c).
        bool set_ban = true;
        if (p.w35c >> 27) {
            bool competition = kCompetitionClass.at(g.desc.at(0)) != 0;
            if ((p.w364 >> 16 & 0x1F) == 0 && !competition) p.w35c &= 0x07FFFFFFu;
            else set_ban = false;
        }
        if (set_ban) {
            p.w364 &= 0xFFE0FFFFu;
            p.w364 |= uint32_t(ban) << 16 | 0x100000u;
        }
        // layout field: team flag bit 0 (sent-off player) and w368 bits 24-26. The original
        // clears the field before reading it back, so the "counter" always becomes 1.
        state.team_flags.at(team) |= 1;
        p.w368 = (p.w368 & 0xF8FFFFFFu) | 0x01000000u;
    } else if (kind != 2) {
        bump(0xbc);
    }
    // Sent off or injured: the player leaves the pitch (slot flags 0x1000-0x8000 cleared).
    if (ban != 0 || kind == 2) state.squad_slots.at(slot) &= 0x0FFF;

    g.log.push_back(uint16_t(player + (kEventLogCode[kind] - second_yellow) * 0x1000));
    g.log.push_back(uint16_t(team * 0x80 + minute));

    // When one of the teams is watched live (MatchEngineRuntimeField12, team flag bits 6/7 of team or
    // opponent) the event is shown in a message box (DcThat, DrawnFirstBy). That is
    // pure presentation without random numbers (verified against the matchday 4
    // recording): a Booking ShowEvent for the UI (presentation.hpp).
    if (show || live_booking) {
        const int shown_kind = second_yellow ? 3 : kind;     // bStack_3c
        const int foul = second_yellow ? 0 : kind;           // local_44
        const int flags = (state.team_morale.at(team) & 0xC0) | (state.team_morale.at(opp_team) & 0x80);
        const bool has_live_matches = !state.live_matches.empty();
        const bool box = !forced && has_live_matches && ((flags == 0x80 && (foul != 0 || !button(::bmh::layout::MatchEngine_EventImpl_ExecutableField))) ||
                                   (flags == 0x40 && ((foul == 0 && !button(::bmh::layout::MatchEngine_EventImpl_ExecutableField2)) || (foul != 0 && !button(::bmh::layout::MatchEngine_EventImpl_ExecutableField3)))));
        if (show) {
            ShowEvent e{Show::Booking};
            e.side = uint8_t(s), e.kind = uint8_t(shown_kind), e.flag = box;
            e.team = int16_t(team), e.player = int16_t(player);
            e.a = int32_t(p.w368 >> 14 & 0xF);  // yellow cards of the season (shown for a yellow card)
            e.b = ban;
            e.text = message_text(state, ::bmh::layout::LiveMatchday_FormatError_TextPointer2 + uint32_t(shown_kind) * 4) + state.player_name(player);
            emit(std::move(e));
        }
        if (live_booking && box)
            live_booking({minute, show_group, m, s, shown_kind, team, player, int(p.w368 >> 14 & 0xf), ban});
    }

    // Substitution: always after an injury, after a yellow card 1 in 13 (Injure with
    // mode position + 20: a substitute of the same position). A sent-off player is not
    // replaced.
    bool change = kind == 2 || (kind == 0 && ban == 0 && rand.range(0, 12) == 0);
    if (trace)
        std::fprintf(stderr, "  event m%d s%d team %d kind %d second %d ban %d player %d slot %d change %d\n", m, s, team, kind,
                     int(second_yellow), ban, player, int(slot - range.first), int(change));
    // A human team that is watched (team byte bit 7, option LiveDeskDisabled off) is changed
    // automatically only when the assistant may substitute (option byte 1); otherwise the
    // manager gets the substitution screen after a red card or an injury (PitchScreen).
    const bool watched = !live_desk_disabled && (state.team_morale.at(team) & 0x80);
    const bool assistant = !human || !watched || state.manager_option(manager, 1);
    if (change && assistant && forced) {
        result.change = DeferredRealityChange{false, team, slot, p.position() + 20};
    } else if (change && assistant) {
        const bool changed = substitute(team, slot, p.position() + 20, report);
        if (report.unsupported) return result;
        if (changed && show) {
            ShowEvent e{Show::Substitution};
            e.side = uint8_t(s), e.team = int16_t(team), e.player = int16_t(player);
            e.other = int16_t(state.squad_slots.at(slot) & 0x0FFF);  // the substitute took the slot
            emit(std::move(e));
        }
    } else if (kind != 0 && human && watched && !state.manager_option(manager, 1)) {
        if (forced) {
            result.change = DeferredRealityChange{true, opp_team, 0xff, manager};
            result.code = 0x80;
        } else if (live_event) {
            const size_t group_index = size_t(&g - groups.data());
            live_event(LiveMatchEvent{group_index, m, s, team, opp_team, player,
                kind, minute, manager});
        } else if (!leave_screens) {
            report.fail("substitution screen of a human manager after an event (PitchScreen)");
            return result;
        }
    }
    // the team's card counters (league-class competitions) and a human manager's card
    // statistics by minute (record +0x1ae)
    if (kind < 2 && kCompetitionClass.at(g.desc.at(0)) == 0) {
        if (state.memory.has(::bmh::layout::DeskRecords_Teams_RuntimeField)) {
            const uint32_t at = ::bmh::layout::DeskRecords_Teams_RuntimeField + uint32_t(team) * 2;
            state.memory.set16(at, uint16_t(state.memory.u16(at) + card_delta));
        }
        if (human) {
            const int bucket = std::min(minute, 0x59) / 5;
            state.manager_records.at(size_t(manager) * 0xB0C + 0x1ae + size_t(bucket) + size_t((kind == 1) + 2) * 0x12)++;
        }
    }

    // Red card or injury: the side plays with a different line-up - re-rate it. The minute
    // loop keeps using the rhythm and attitudes it read before the event.
    if (kind != 0) rerate(g, m, s, report);
    result.code |= uint8_t(kEventLogCode[kind] - second_yellow - 2);
    return result;
}

void MatchEngine::side(MatchGroup& g, int m, int& s, int minute, int& own_att, int& opp_att, MinuteReport& report) {
    uint8_t* rec = g.records.data() + m * 18;
    uint8_t* own = rec + s * 9;
    uint8_t* other = rec + (1 - s) * 9;
    const int away_penalty = s == 1 ? (g.flags() & 2) >> 1 : 0;  // local_b0[side]
    show_at(g, m, minute);

    int rhythm = clamp(own[7], 44, 80);  // MatchRuntimeField2

    // --- event roll (AlternateEventRoll selects an alternative range, separately from indoor) ---
    bool trigger = alternate_event_roll ? rand.range(0, 10) > 7 : rand.range(0, 220) < rhythm - 30;
    if (trigger) {
        event(g, m, s, minute, report);
        if (report.unsupported) return;
        // The synchronous Source event presenter may stage/restore GameState and
        // per-match record buffers. Reacquire the record pointers before continuing
        // the chance path for this same side/minute.
        rec = g.records.data() + m * 18;
        own = rec + s * 9;
        other = rec + (1 - s) * 9;
    }

    // --- chance roll ---------------------------------------------------------------------------
    int roll = game.range(0, away_penalty * 4 + 44);
    if (int16_t(opp_att + own_att + 3 + (indoor ? 14 : 0)) < roll) return;  // no chance

    int team = g.fixtures.at(m * 2 + s), opp_team = g.fixtures.at(m * 2 + 1 - s);
    // A side whose record byte 2 (its attack strength) is 0 gets no chance at all: the
    // original only notes the two clubs (MatchEngineRuntimeField13/afafe) and skips the whole chance
    // block, without the result-screen part after it (WEEK240).
    if (own[2] == 0) return;
    int attack = int16_t(own[2] + game.range(0, rhythm) / 10 + own_att * 2 - 9);  // uVar5
    if (minute < clamp(own[6], 50, 130)) attack = int16_t(attack + game.range(6, 12));
    int defence = int16_t(other[2] + (3 - opp_att) * 2);  // local_a0
    if (game.range(0, 100) < other[6]) defence = int16_t(defence + 5);

    // MatchScore (read) + BuildSideRecords2: adapt the attitude to the current score. Called
    // 1 in 41 times per chance roll, and always around a chance in the match shown live.
    auto update_attitude = [&]() {
        // A human manager sets the attitude himself (manager record +0xd2) unless his
        // assistant coach is in charge (option byte 2); substitutions need option byte 1.
        const int manager = state.manager_of(team);
        if (manager >= 0 && !state.manager_option(manager, 2)) {
            if (state.manager_records.empty()) {
                report.fail("attitude of a human team needs the manager records (savegame)");
                return;
            }
            own[8] = state.manager_records.at(size_t(manager) * 0xB0C + 0xd2);
            return;
        }
        int home = 0, away = 0;
        if (!g.results.empty()) {
            home = g.results.at(m) >> 4;
            away = g.results.at(m) & 0x0F;
        } else if (g.cup_results.size() == size_t(g.matches())) {
            home = g.cup_results[m] >> 5 & 0x1F;
            away = g.cup_results[m] & 0x1F;
        } else {
            report.fail("attitude update needs the cup score (not recorded)");
            return;
        }
        int substitution = -1;
        int attitude = attitude_update(own, other, s, m, s == 0 ? home : away, s == 0 ? away : home, minute, rand, substitution);
        if (substitution >= 0 && (manager < 0 || state.manager_option(manager, 1))) {
            // BuildSideRecords2 calls Injure(team, 0, mode) before storing the attitude.
            substitute(team, 0, substitution, report);
            if (report.unsupported) return;
        }
        own[8] = uint8_t(attitude);
    };

    // --- match shown live (local_100) ------------------------------------------------------
    // Team flag 0x80 (of either team): the match is shown on the live desk; the attitude is
    // updated at once and the ball position of the display is drawn (local_f4, Watcom
    // 10-35, sign = direction of play). Team flag 0x40: ticker mode (local_fc), mode 2.
    // LiveDeskDisabled suppresses both. The animated scene (RealityReporterPanel) additionally needs
    // LiveSceneVisible and both clubs below the executable squad limit.
    int live = 0;
    bool ticker = false;
    if (!live_desk_disabled) {
        const int flags = state.team_morale.at(team) | state.team_morale.at(opp_team);
        if (flags & 0x80) {
            update_attitude();
            if (report.unsupported) return;
            live = 1;
            rand.range(10, 35);
        }
        if (flags & 0x40) {
            live = 2;
            ticker = true;
        }
    }

    if (trace && live) std::fprintf(stderr, "  live m%d s%d team %d mode %d minute %d\n", m, s, team, live, minute);
    if (game.range(0, 40) == 0) {
        update_attitude();
        if (report.unsupported) return;
    }
    // Random substitution; if one happens the side is re-rated (BuildSideRecords3).
    if (game.range(0, 40) == 0 && substitute(team, 0, 10, report)) rerate(g, m, s, report);
    if (report.unsupported) return;
    state.chance_rain = g.desc.at(0x50 + m);
    state.chance_temperature = int8_t(g.desc.at(0x30 + m));

    int quality = int16_t((attack - defence) / 2 + 13 - away_penalty * 8);
    if (defence < attack && quality < 20) {
        quality = int16_t(quality + ((attack - defence) >> 1));
        if (quality > 20) quality = 20;
    }
    quality = clamp(quality, 5, 24);
    int threshold = kChanceTable[quality];
    if (trace)
        std::fprintf(stderr, "  attack m%d s%d attack %d defence %d quality %d threshold %d away %d\n", m, s, attack, defence,
                     quality, threshold, away_penalty);
    if (game.range(0, 4 - away_penalty) != 0 && game.range(0, away_penalty * 4 + 56) >= threshold)
        return;  // the attack comes to nothing

    // --- a real chance ------------------------------------------------------------------------
    int shooter = 0xFFF, assistant = 0xFFF, keeper = 0xFFF;
    state.last_shooter = 0xFFF;
    bool attempt = false;  // local_80
    int kind = 0;          // MatchEngineRuntimeField14: 0 open play, 1 free kick, 2 penalty, 3 corner
    if (game.range(0, 5) == 0) kind = 1;
    if (game.range(0, 6) == 0) kind = 3;

    int keeper_value = 1;  // local_a0 from here on
    int shot = 0;          // uVar5
    int shot_limit = 0;    // uVar14
    // local_3c is set after every shooter pick; local_84 only when an assistant is picked,
    // otherwise it keeps the value of an earlier chance (it is never initialised)
    int shooter_q = 0;
    int& assistant_q = stale_assistant_quality;
    bool reality_played = false, reality_attempted = false;
    int reality_distance = 0;
    if (kind == 0 && live == 1 && live_scene_visible && !indoor) {
        if (!reality_rules) {
            report.fail("Reality chance needs the loaded executable (RealityReporterPanel)");
            return;
        }
        const auto& exe = *reality_rules;
        if (team < exe.u16(::bmh::layout::CareerSession_DispatchLineup_RuntimeField) && opp_team < exe.u16(::bmh::layout::CareerSession_DispatchLineup_RuntimeField)) {
            const int displacement = (1 - s * 2) * rand.range(21, 45);
            if (live_ball && button(::bmh::layout::MatchEngine_Side_ExecutableField3) == 0)
                live_ball({minute, show_group, m, displacement});
            auto context = reality_runtime.context;
            context.rating = RatingContext{state.chance_rain, state.chance_temperature};
            context.rating.game = &game; context.rating.live_scene = true;
            context.retained_cue_actors = state.reality.cue_actors;
            context.retained_card_kind = state.reality.card_kind;
            context.retained_deflection = state.reality.deflected;
            context.retained_distance = state.reality.distance;
            // Muted is the Source sound switch; the routine still draws its
            // qualified random sound selector, regardless of the observer.
            context.muted = button(::bmh::layout::MatchEngine_Side_ExecutableField4) != 0;
            context.sound = original_sound;
            RealityMatchScene owner;
            owner.scene = std::move(reality_runtime);
            owner.scene.setup.grid = state.reality.grid;
            initialize_reality_match_scene(*this, size_t(show_group), m, s, minute, exe, context, owner);
            if (live_reality && owner.scene.setup.ready)
                live_reality({LiveRealityStage::begin, minute, show_group, m, s, {}});
            while (!owner.scene.finished) {
                auto tick = advance_reality_match_scene(*this, owner, exe, report);
                if (report.unsupported) return;
                if (live_reality && live_reality({LiveRealityStage::round, minute, show_group, m, s, std::move(tick)}) &&
                    !owner.scene.finished)
                    exit_reality_scene_after_animation(owner.scene);
            }
            if (live_reality && owner.scene.setup.ready)
                live_reality({LiveRealityStage::settling, minute, show_group, m, s, {}});
            if (owner.scene.result.scene_status == 3 && original_sound) original_sound(18); // RealityReporterPanel.
            settle_reality_match_scene(*this, owner, report);
            if (report.unsupported) return;
            // Assign2(1) clears MatchdaySetupRuntimeField12 at its first active fixture. AutoLineup
            // rebuilds the watched list in group/match order and calls the
            // existing drawless Int2 for each displayed pair (mode0..7).
            state.live_matches.clear();
            for (const auto& group : groups) for (int match = 0; match < group.matches(); ++match) {
                if (((group.ended | group.void_mask()) >> match) & 1) continue;
                const int home = group.fixtures.at(size_t(match)*2), away = group.fixtures.at(size_t(match)*2+1);
                if (live_desk_disabled || state.live_matches.size() >= 4 ||
                    !((state.team_morale.at(size_t(home)) | state.team_morale.at(size_t(away))) & 0x80) ||
                    std::find(state.live_matches.begin(),state.live_matches.end(),home) != state.live_matches.end()) continue;
                const int mode = int(state.live_matches.size())*2;
                auto_lineup(state,home,mode); auto_lineup(state,away,mode+1);
                state.live_matches.push_back(home);
            }
            if (live_reality && owner.scene.setup.ready) {
                RealitySceneTick ended; ended.finished = true; ended.result = owner.scene.result;
                live_reality({LiveRealityStage::end, minute, show_group, m, s, std::move(ended)});
            }
            const auto result = owner.scene.result;
            reality_runtime = std::move(owner.scene);
            if (result.return_flags & 0x80) s ^= 1; // also changes EndPhase's next side iteration
            team = g.fixtures.at(m * 2 + s); opp_team = g.fixtures.at(m * 2 + 1 - s);
            // A genuine human intervention may replace the buffers in place.
            rec = g.records.data() + m * 18; own = rec + s * 9; other = rec + (1 - s) * 9;
            kind = result.restart;
            attempt = (result.return_flags & 0x7f) != 0;
            reality_attempted = result.shot_attempted;
            shooter = result.shooter; assistant = result.assistant; keeper = result.keeper;
            last_shooter_slot = result.shooter_row; last_assist_slot = result.assistant_row;
            state.last_shooter = shooter;
            reality_distance = result.distance;
            state.reality.cue_actors = reality_runtime.context.retained_cue_actors;
            state.reality.card_kind = reality_runtime.context.retained_card_kind;
            state.reality.deflected = result.deflected;
            state.reality.distance = result.distance;
            state.reality.grid = reality_runtime.setup.grid;
            reality_played = true;
        }
    }
    if (kind != 0 || !reality_played) {
        int rating = own[3 + kind];
        shot = rating;
        int dice = 0, close = 0;
        if (rating == 0) {
            shot_limit = 0;
            close = 1;
        } else {
            keeper_value = other[1];
            shot_limit = clamp(rating + 2 - keeper_value, 3, 15);
            close = game.range(0, 4);
            dice = rand.range(0, 90);
        }
        if (kind != 0 || close == 0 || dice < shot_limit * 4) {
            if (live) rand.range(21, 45);  // ball position of the live display
            shooter = pick_taker(team, kind == 1 ? 2 : 0, 0xFFE, kind, report);
            if (report.unsupported) return;
            state.last_shooter = shooter;
            shooter_q = uint8_t(last_pick_quality);
            if (shooter != 0xFFF && kind != 1) {
                assistant = pick_taker(team, (kind == 3 ? 3 : 1) | 0x80, shooter, kind, report);
                if (report.unsupported) return;
                assistant_q = uint8_t(last_pick_quality);
            }
            attempt = true;
        }
    }

    // Goalkeeper of the opponent: the player in squad slot 0 if the formation puts it in goal.
    if (!reality_played && (opp_team < 168 || opp_team == kNoTeam)) {
        int x = 0, y = 0;
        state.formation_position(opp_team, 0, x, y);
        if (x == 0 && y == 4) keeper = state.squad_slots.at(squad_range(opp_team).first) & 0x0FFF;
    }

    if (kind == 0 && !attempt) {
        // An offside/boundary shot has bdb7b=1 but no returned attempt. Source
        // logs this miss with its raw grid distance and omits goal rolls/UI.
        if (reality_played && reality_attempted) {
            g.log.push_back(uint16_t(shooter + 0x1000));
            g.log.push_back(uint16_t(team * 0x80 + minute));
            g.log.push_back(uint16_t(assistant + 0x2000));
            g.log.push_back(uint16_t(team * 0x80 + reality_distance));
        }
        return;
    }
    const int ball_position = live ? (1 - s * 2) * rand.range(40, 50) : 0;

    RatingContext ctx{state.chance_rain, state.chance_temperature};
    int distance = 0;          // MatchEngine_Minute_ExecutableField4, shot difficulty
    bool deflected = state.reality.deflected;    // MatchEngine_Minute_ExecutableField3
    int& penalty_score = stale_set_piece_score; // local_9c is unwritten for a Reality penalty
    int shown_distance = 0;    // MatchEngineRuntimeField15: distance shown by the live desk (kind 1)
    if (reality_played) {
        distance = uint8_t(reality_distance * 5 / 2);
    } else {
        int spread = 30;
        if (keeper_value < shot - 20) spread = 30 - (shot - keeper_value);
        if (spread < 25) spread = 25;
        distance = uint8_t(game.range(9, spread));
        if (distance < 17 && kind != 0) distance = uint8_t(game.range(9, spread));
        deflected = game.range(0, 5) == 0;
    }
    if (kind == 0) {
        if (team >= 168 && team != kNoTeam) {
            // clubs without squad: the attack rating (TeamRating, profile 3), drawn
            RatingContext drawn;
            drawn.rand = &rand;
            shot = int16_t(team_rating(state, team, kRatingProfiles[3], drawn));
        } else {
            shot = int16_t(player_value(state, shooter, 0x87, 100, team, ctx));
        }
    }
    if (keeper == 0xFFF) {
        if (opp_team >= 168 && opp_team != kNoTeam) {
            // clubs without squad: the goalkeeping rating (profile 0), drawn
            RatingContext drawn;
            drawn.rand = &rand;
            keeper_value = int16_t(team_rating(state, opp_team, kRatingProfiles[0], drawn));
        } else {
            keeper_value = 0;
        }
    } else {
        int v = player_value(state, keeper, 7, 100, opp_team, ctx);
        keeper_value = int16_t(v);
        if ((state.players.at(keeper).w364 & 3) != 0 && game.range(10, 25) > distance)
            keeper_value = int16_t(int16_t(v) / 4);  // not a real goalkeeper in goal
    }
    if (kind == 0) {
        if (deflected && keeper_value < game.range(0, 100)) {
            shot = int16_t(80 - distance);
            keeper_value = 30;
        }
        keeper_value = int16_t((keeper_value << 2) / 5);
        shot = int16_t(int16_t(shot * (100 - distance) / 100) - distance);
    } else {
        if (!reality_played && distance > 30) distance = uint8_t(game.range(17, 35));
        if (!reality_played && distance < 17 && kind == 1) {
            // The keeper parries the penalty: follow-up shot (kind 2).
            kind = 2;
            deflected = false;
            // OpenBMH deviates from the original: supports designated set-piece takers, the enhanced injury tables, and the extended suspension fields.
// DesignatedSlot2 (supported rule profile): a human manager's designated follow-up taker
            // (manager record +0xafe) takes the shot if he plays; the shooter quality is
            // then his. It works on the caller's stack frame (shooter quality byte).
            if (int preset = designated_slot(team, 0xafe, 0xFFE); preset >= 0) {
                int saved = last_pick_quality;
                int taker = pick_player(team, 2, 0xFFE, report, preset);
                if (report.unsupported) return;
                if (taker != 0xFFF) {
                    shooter = taker;
                    shooter_q = uint8_t(last_pick_quality);
                } else {
                    last_pick_quality = saved;
                }
            }
            last_pick_quality = int16_t((int16_t(last_pick_quality) + shooter_q) / 2);
            // Range5(shooter, 0, 0): success if G(0, 14) > limit, limit G(2, 4) or
            // 2 when G(0, 4) < quality / 33 (a good shooter). Success means a penalty score
            // of 150, i.e. almost certainly a goal.
            int limit = game.range(2, 4);
            if (shooter == 0xFFF || game.range(0, 4) < int16_t(last_pick_quality) / 33) limit = 2;
            penalty_score = game.range(0, 14) > limit ? 150 : 0;
        }
        if (kind == 1) {
            penalty_score = int16_t((distance - 16) * -2 + 50) + shooter_q / 2;
            if (distance > 22) distance = uint8_t((distance / 5) * 5);
            if (live) shown_distance = distance;
            if (live && indoor) distance >>= 1;  // the live desk halves it (also in the log)
        }
        if (kind == 3) penalty_score = assistant_q / 2 + shooter_q / 2;
        if (kind == 1 || kind == 3) {
            int half = int16_t(keeper_value / 2);
            keeper_value = half;
            penalty_score = half < penalty_score ? penalty_score - half : 0;
            if (deflected) {
                int d = game.range(0, 40) - 20;
                int low = int8_t(penalty_score & 0xFF);
                penalty_score = (penalty_score & ~0xFF) | uint8_t(clamp(d + low, 0, 100));
            }
        }
    }
    state.reality.deflected = deflected;
    state.reality.distance = distance;
    if (kind == 0) shot_limit = clamp(int16_t(shot - keeper_value + game.range(5, 20)), 4, 21);
    // chances per 5 minutes (record +0x1c0, minutes above 89 count as 89) for the human
    // managers of both teams, the defending one first
    {
        const size_t bucket = size_t(std::min(minute, 0x59) / 5);
        for (int t : {opp_team, team}) {
            const int mgr = state.manager_of(t);
            if (mgr >= 0) state.manager_records.at(size_t(mgr) * 0xB0C + 0x1c0 + bucket)++;
        }
    }

    bool goal;
    auto open_play_roll = [&]() {
        return keeper_value == 0 || (kind == 0 && game.range(0, 3) != 0 && game.range(0, 20) < shot_limit);
    };
    if (kind == 0) goal = open_play_roll();
    else goal = game.range(1, 150) < int16_t(penalty_score) ? true : open_play_roll();

    // Event log (Matches) and goal bookkeeping.
    auto log = [&](int code, int player, int value) {
        g.log.push_back(uint16_t(player + code * 0x1000));
        g.log.push_back(uint16_t(team * 0x80 + value));
    };
    if (trace)
        std::fprintf(stderr, "  chance m%d s%d team %d kind %d goal %d shooter %x assistant %x distance %d\n", m, s, team, kind,
                     int(goal), shooter, assistant, distance);
    log(goal ? 0 : 1, shooter, minute);
    log(kind == 0 ? 2 : kind + 6, assistant, distance);
    const LiveChanceEvent chance{LiveChanceStage::preview, minute, show_group, m, s, kind,
        shooter, assistant, kind == 0 && indoor ? distance >> 1 : distance,
        goal, ticker, deflected};
    // Button + description/pause occur before MatchScore writes the new score.
    // The ticker's miss switch suppresses both presentation boundaries only.
    if (live_chance && live && (goal || live != 2 || button(::bmh::layout::MatchEngine_Side_ExecutableField5) == 0))
        live_chance(chance);
    if (goal) {
        uint32_t goals = le32(g.desc, 0xc8) + 1;  // descriptor goal counter (b6434)
        for (int i = 0; i < 4; i++) g.desc.at(0xc8 + i) = uint8_t(goals >> (8 * i));
        // MatchScore in write mode: add the goal to the current score.
        if (!g.results.empty()) {
            uint8_t& r = g.results.at(m);
            if ((r & 0xCC) != 0xCC) r = uint8_t(r + (s == 0 ? 0x10 : 0x01));
        } else {
            if (g.cup_results.size() < size_t(g.matches())) g.cup_results.resize(g.matches(), 0);
            uint16_t& r = g.cup_results[m];
            if (r != 0xCCCC) {
                int home = (r >> 5 & 0x1F) + (s == 0), away = (r & 0x1F) + (s == 1);
                r = uint16_t(g.phase * 0x400 + away + home * 0x20);
            }
        }
        // PlayerStatistic: goal for the shooter, assist (open play and set pieces of kind 3).
        const int manager = state.manager_of(team);  // MatchEngineRuntimeField16
        if (shooter != 0xFFF) player_statistic(state, g.desc.at(0), shooter, manager, last_shooter_slot, 1, minute);
        if ((kind == 0 || kind == 3) && assistant != 0xFFF)
            player_statistic(state, g.desc.at(0), assistant, manager, last_assist_slot, 2, 0x5a);
    } else if (live == 2 && button(::bmh::layout::MatchEngine_Side_ExecutableField5)) {
        live = 0;  // ticker mode shows misses only if this button is off
    }

    // --- live desk after the chance (layout field-layout field) ------------------------------------
    // Text, pictures and pauses are presentation. Random numbers are drawn by the attitude
    // update (with the new score) and by two optional animations.
    if (live) {
        // Source local_38 is replaced by +/-52 after a goal. BallFrameCount
        // checks D5 (MatchEngine_Side_ExecutableField3) inside the presentation routine; disabling it must
        // still consume the exact same earlier Watcom draws.
        if (!reality_played && live_ball && button(::bmh::layout::MatchEngine_Side_ExecutableField3) == 0)
            live_ball({minute, show_group, m, goal ? (1 - s * 2) * 52 : ball_position});
        if (show) {
            ShowEvent e{Show::Chance};
            e.side = uint8_t(s), e.kind = uint8_t(kind), e.flag = goal;
            e.team = int16_t(team), e.player = int16_t(shooter), e.other = int16_t(assistant);
            e.a = indoor ? shown_distance >> 1 : shown_distance;  // MatchEngine_Minute_ExecutableField4 as shown
            e.b = ticker;
            if (shooter != 0xFFF) e.text = state.player_name(shooter);
            emit(std::move(e));
        }
        update_attitude();
        if (report.unsupported) return;
        if (buttons.empty()) {
            report.fail("live desk needs the button settings (savegame)");
            return;
        }
        // LiveSetPieceFrames: animated penalty / follow-up / free kick (button MatchEngine_Side_ExecutableField: 1-2 in
        // the live match, 3+ also in ticker mode).
        const int set_pieces = button(::bmh::layout::MatchEngine_Side_ExecutableField);
        if (set_pieces && kind != 0 && (!ticker || set_pieces >= 3)) {
            set_piece_animation(kind, shown_distance, 1 - s, show_group, m, minute);  // MatchEngineRuntimeField6 = 1 - side
        }
        // SetPieceAnimation: animated shot on goal (button MatchEngine_Side_ExecutableField2: 1 set pieces, 2 all
        // chances, 3 also set pieces in ticker mode, 4 everything); otherwise only a sound.
        const int shots = button(::bmh::layout::MatchEngine_Side_ExecutableField2);
        bool animate = false;
        if (shots) {
            if (kind != 0 && !ticker) animate = true;
            else if (shots >= 2 && !ticker) animate = true;
            else if (shots >= 3 && ticker && kind != 0) animate = true;
            else animate = shots >= 4;
        }
        if (animate) shot_animation(!goal, kind == 2, s, show_group, m, minute);
        else if (original_sound) original_sound(goal ? 19 : 20); // EndPhase: local_a0 + 0x13.
        if (live_chance) {
            auto result = chance;
            result.stage = LiveChanceStage::result;
            live_chance(result);
        }
    }
}

void MatchEngine::penalty_shootout(MatchGroup& g, int m, MinuteReport& report) {
    const int pair[2] = {g.fixtures.at(size_t(m) * 2), g.fixtures.at(size_t(m) * 2 + 1)};
    // shown live (team flag 0x80 or the ticker 0x40 of either team, CupTie2 local_10):
    // every kick is animated (below); text/header observers draw no randomness.
    const bool live =
        (state.team_morale.at(size_t(pair[0])) & 0xC0) || (state.team_morale.at(size_t(pair[1])) & 0xC0);
    const int group_index = int(&g - groups.data());
    const uint16_t original_score = g.cup_results.at(size_t(m));
    if (live && live_shootout)
        live_shootout({LiveShootoutStage::start,0x79,group_index,m,3,0xfff,
                       int(original_score >> 5 & 31),int(original_score & 31),0,5});
    // PenaltyShootout
    int last_side = 3, kicks = 0;
    int goals[2] = {0, 0};
    int rounds_left = 5;  // local_14
    int of = 5;           // local_18: "of 5", 1 in the sudden death (the live text)
    const int first = rand.range(0, 1);
    const int order[2] = {first, 1 - first};
    uint8_t forced = uint8_t(game.range(0, 1));  // local_28: bit 7 set = results alternate
    int taken[2][12] = {};
    int count[2] = {0, 0};
    while (rounds_left != 0 || goals[0] == goals[1]) {
        for (int k = 0; k < 2; k++) {
            if (k == 0 && goals[0] == 11 && goals[1] == 11) forced |= 0x80;  // no end in sight
            forced ^= 1;
            const int side = order[k];
            const int team = pair[side];
            int player = 0xFFF;
            if (team < 138 || team == kNoTeam) {
                // a player who has not taken a penalty yet (after 100 tries the list restarts)
                int tries = 0;
                bool fresh;
                do {
                    player = pick_player(team, 2, 0xFFE, report);
                    if (report.unsupported) return;
                    fresh = true;
                    for (int i = 0; i < count[side]; i++)
                        if (taken[side][i] == player) fresh = false;
                    if (++tries == 100) {
                        if (count[side] == 0) player = 0xFFF;
                        else count[side] = 0;
                        tries = 0;
                    }
                } while (player != 0xFFF && !fresh);
            }
            if (count[side] < 12) taken[side][count[side]++] = player;
            last_side = side;
            if (live && live_shootout)
                live_shootout({LiveShootoutStage::preview,0x79,group_index,m,side,player,
                    kicks == 0 ? int(original_score >> 5 & 31) : goals[0],
                    kicks == 0 ? int(original_score & 31) : goals[1],
                    int(of == 5) + of - rounds_left,of});
            ++kicks;
            // Range5: the kick. The quality check uses the shooter of the last real
            // chance of the match loop (MatchEngineRuntimeField17) and the quality of the last pick.
            int miss_limit = game.range(2, 4);
            bool keep = false;
            if (state.last_shooter != 0xFFF) {
                const int quality = int16_t(last_pick_quality);
                keep = quality / 33 <= game.range(0, 4);
            }
            if (!keep) miss_limit = 2;
            bool goal = miss_limit < game.range(0, 14);
            if (forced & 0x80) goal = (forced & 0x7F) != 0;
            if (live) {
                // Range5 live: the follow-up scene LiveSetPieceFrames(2, 0) with MatchEngineRuntimeField6 =
                // the other side, then the shot on goal SetPieceAnimation(miss, 1) - always, no
                // button decides here
                set_piece_animation(2, 0, 1 - side, int(&g - groups.data()), m, 0x79);
                shot_animation(!goal, true, side, int(&g - groups.data()), m, 0x79);
            }
            if (goal) goals[side]++;
            if (show) {
                show_at(g, m, 0x79);
                ShowEvent e{Show::PenaltyKick};
                e.side = uint8_t(side), e.flag = goal, e.kind = live;
                e.team = int16_t(team), e.player = int16_t(player);
                e.a = goals[0], e.b = goals[1];
                // "<c>. Elfmeter" (LiveMatchday_FormatError_TextPointer4), "of <d>" (LiveMatchday_FormatError_TextPointer5)
                e.c = (of == 5) + (of - rounds_left), e.d = of;
                if (player != 0xFFF) e.text = state.player_name(player);
                emit(std::move(e));
            }
            // event log (Matches): the kick and its result
            g.log.push_back(uint16_t(player + (goal ? 0 : 1) * 0x1000));
            g.log.push_back(uint16_t(team * 0x80 + 0x79));
            g.log.push_back(uint16_t(0xFFF + 2 * 0x1000));
            g.log.push_back(uint16_t(team * 0x80 + 1));
            const int own = goals[side], other = goals[1 - side];
            if (rounds_left != 0 && (own + rounds_left - 1 < other || rounds_left + other - k < own)) {
                rounds_left = 0;  // decided before the five rounds are over
                break;
            }
        }
        if (rounds_left != 0 && --rounds_left == 0) of = 1;
    }
    if (show && live) {
        show_at(g, m, 0x79);
        ShowEvent e{Show::ShootoutEnd};
        e.a = goals[0], e.b = goals[1];
        e.text = message_text(state, ::bmh::layout::LiveMatchday_FormatError_TextPointer6);
        emit(std::move(e));
    }
    if (live && live_shootout)
        live_shootout({LiveShootoutStage::end,0x79,group_index,m,last_side,0xfff,
                       goals[0],goals[1],0,of});
    // CupTie2: the shoot-out goals are added to the score (at most 29 together)
    uint16_t& r = g.cup_results.at(size_t(m));
    int home = goals[0] + (r >> 5 & 0x1F), away = goals[1] + (r & 0x1F);
    while (away + home > 0x1D) away--, home--;
    home = std::clamp(int(int8_t(home)), 0, 15);
    away = std::clamp(int(int8_t(away)), 0, 15);
    if (r != 0xCCCC) r = uint16_t(2 * 0x400 + home * 0x20 + away);
}

}  // namespace bmh
