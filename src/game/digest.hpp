// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// A fingerprint of the complete game state, for behaviour-preserving refactoring.
//
// Two runs of the same career must produce the same digest after every turn. The digest covers
//   - everything a savegame holds (the decrypted stream of save_stream: all state blocks and the
//     four name pools), and
//   - the run-time values that are not saved but steer later draws (the chance weather, the last
//     shooter, the transfer list toggle, the autosave counters, ...).
// The states of the two random generators are passed in by the caller, because they do not belong
// to GameState.
//
// It is a regression tool only: the value has no meaning of its own and may change whenever the
// engine's behaviour is changed on purpose (then the reference files are simply recorded again).

#include <cstdint>

#include "game/state.hpp"

namespace bmh {

// FNV-1a (64 bit) over the saved stream and the unsaved run-time values of `g`, the game
// generator's seed and the Watcom generator's seed.
uint64_t state_digest(const GameState& g, int32_t game_seed, uint32_t rand_seed);

}  // namespace bmh
