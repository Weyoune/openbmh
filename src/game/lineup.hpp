// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// OpenBMH deviates from the original: disables the foreign-player limit and supports owner-bound designated set-piece selection.
// Automatic line-up of computer teams, port of Int2.
//
// The squad of a team is a run of squad slots (squad_range). The first 11 slots are the
// starting line-up in formation order (slot i plays at formation position i, see
// GameState::formation_position), the following slots are the bench and reserves.
// Slot flags: 0x8000 in the starting line-up, 0x4000 on the bench.
//
// The optimiser draws no random numbers. For every fit player (no injury, w364 bits
// 16-20 zero) it computes a base value
//     value = condition + technique + form / 3
// and then fills the positions one after another: position p takes the remaining player
// with the best value - mismatch, where mismatch is
//     0                      for bench slots of type 9 (reserves)
//     64 * 25                if exactly one of player and position is a goalkeeper
//     64 * |player position - position type|   otherwise
// Position types come from the formation depth for the first 11 slots (table layout field:
// 0 goalkeeper, 1 defence, 2 midfield, 3 attack) and from table layout field for the bench.
// The chosen player is swapped into slot p and flagged 0x8000 (slots 0-10) or 0x4000.
//
// Ported: assistant training-camp familiarity penalty and detailed position scoring at
// difficulty >= 6. The foreign-player limit (ForeignPlayerLimitEnabled) is off in supported rule profile.

#include <cstdint>
#include <array>

#include "game/random.hpp"
#include "game/state.hpp"

namespace bmh {

// Games: in-place quicksort (Hoare partition, middle element as pivot) of `keys`
// descending (ascending if `ascending`), comparing as unsigned 32-bit, moving `items`
// along. Ported exactly because the order of equal keys depends on the algorithm.
void quicksort_by_key(int16_t* items, uint32_t* keys, int count, bool ascending = false);

// Maximum number of substitutions per match (CareerSession_DispatchLiveSubstitution_ExecutableField).
constexpr int kMaxSubstitutions = 3;

// Injure: substitution for a computer team.
//   off   absolute squad slot of the player to take off, used only for mode >= 20
//   mode  < 10   tactical change towards trait value `mode` (w35c bits 21-23)
//         10     random change (called during the match), keeps the last substitution
//                for emergencies
//         20+t   replace the player in `off` (injured or booked) by a bench player of
//                position type t (t = 0 prefers a goalkeeper)
// Picks the best fit bench player (not injured, not yet played), for modes < 20 also the
// player to take off, swaps them, re-sorts the eleven (auto_lineup mode 0x65) and counts
// the substitution in the team flags (bits 5-7). Returns false if nothing was changed.
// Draws from `rand` like the original.
bool substitute(GameState& g, int team, int off, int mode, WatcomRandom& rand);

// A substitution the manager makes himself during his live match (the line-up screen
// PitchScreen opened from the match, "AUFSTELLUNG AUTO"): the player in line-up slot
// `field_slot` (0-10, relative to the squad) leaves, the bench player in `bench_slot` (11..)
// takes his place on the pitch and on the tactics board (the place, board bytes
// 0x41/0x42, stays with the slot). The flags and the counter are those of `substitute`:
// the incoming player has played (w364 bit 12), the slots get 0x8000 / 0x4000, the team flags
// count the change. Returns false (nothing changed) for an empty or not lined-up field slot,
// a bench player who already played or after kMaxSubstitutions. Rate the side again
// afterwards (MatchEngine::rerate_team). No random numbers.
bool manual_substitution(GameState& g, int team, int field_slot, int bench_slot);

// mode as in the original: < 100 only re-sorts the current first 11 slots (used right
// before a match, AutoLineup), 100 picks from the whole squad, 101 (0x65) re-sorts the
// first 11 after a substitution. Bit 0x80 additionally improves field rows 1-10
// from the reserves after the first positional selection, as in the original.
void auto_lineup(GameState& g, int team, int mode);

// Explicit FindManagerForClub record4 lookup for the club parked during a national
// takeover. Validate the raw record/board; never extend the active manager list.
void auto_lineup_parked_club_source(GameState& g, int team, int mode);

// PrepareLineupEntry: re-flag the already chosen field and bench rows on lineup-screen entry.
// This does not rank, replace or otherwise choose players and consumes no RNG.
void prepare_lineup_entry(GameState& g, int team);

// Deterministic Source pitch actions for a live, mode-bound human-manager
// desk (outdoor or indoor). Each helper stages the complete GameState and returns
// true only after a complete Source action can be committed; false is a
// neutral rejection with Native state untouched. None consumes W/G RNG.
// The caller still owns active-screen, revision, prepared-entry and ACK checks.
bool set_pitch_automatic_weight_source(GameState& g, int manager_slot, int runtime_club,
                                       int weight_index, uint8_t old_weights, uint8_t new_weights);
bool move_pitch_player_source(GameState& g, int manager_slot, int runtime_club,
                              int row, int player_id, uint16_t slot,
                              int old_depth, int old_lane, int new_depth, int new_lane);
bool apply_pitch_formation_source(GameState& g, int manager_slot, int runtime_club,
                                  int formation, uint8_t expected_weights);
bool save_pitch_formation_source(GameState& g, int manager_slot, int runtime_club,
                                 int formation, uint8_t expected_weights,
                                 const std::array<uint8_t, 22>& formation_positions);

// OpenBMH deviates from the original: disables the foreign-player limit and supports owner-bound designated set-piece selection.
// supported rule profile NormalizePitchSetPieceSource/NormalizePitchSetPieceSource3/NormalizePitchSetPieceSource2/NormalizePitchSetPieceSource4: deterministic, owner-bound
// set-piece presentation state. Stored takers are player_id+1; zero is AUTO.
bool normalize_pitch_set_piece_source(GameState& g, int manager_slot, int runtime_club);
bool initialize_pitch_set_piece_source(GameState& g, int manager_slot, int runtime_club);
bool set_pitch_set_piece_taker_source(GameState& g, int manager_slot, int runtime_club,
                                      int selector, int row, int player_id, uint16_t slot,
                                      uint16_t expected_old_value);
bool auto_pitch_set_piece_takers_source(GameState& g, int manager_slot, int runtime_club,
                                        const std::array<uint16_t, 3>& expected_old_values);

// ORDER (PitchScreen with desk flag 0x20, H2 p. 43): the four instructions of a starter on
// the tactics board, bytes +2..+5 - 0 defensive/offensive, 1 running, 2 commitment, 3 fighting
// (the live scene reads +5, rating.cpp). A click in a bar sets the value from the mouse
// position or moves it by one; ClampInteger keeps it within 2..40. `row` is the board/squad
// row of the manager's club, `player` its expected player. Returns false (nothing changed)
// for a stale or invalid request.
constexpr int kOrderMin = 2, kOrderMax = 40;
bool set_player_order_source(GameState& g, int manager_slot, int runtime_club, int row, int player, int aspect, int value);

}  // namespace bmh
