// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/turn_driver.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "game/match_engine.hpp"
#include "modern/game/desk.hpp"
#include "game/random.hpp"
#include "modern/game/modern_state.hpp"
#include "modern/game/season.hpp"
#include "game/sportschau_presentation.hpp"
#include "game/transfer_allocator.hpp"

namespace bmh::modern {


// Controller-level sound events carry stable identities. An original cue is optional:
// only attach one when its concrete BMH call site is mapped to a cue by evidence.

// WaitingAtDesk has not claimed the driver: its original request may be retried.

// Read-only-ready copy of the most recently completed human matchday. Kept apart
// from `groups`, which is replaced while seeking the next calendar event.

// Runtime state between a desk's kickoff request and the next desk. The group objects,
// referee table, presentation settings and random generators remain owned by the caller/
// controller for each synchronous advance; only this cursor survives the desk handoff.

// Bind an event already prepared by prepare_event and with its first human desk open.
// No generators are drawn here; the object becomes the only accepted continuation for it.
bool begin_turn_driver(TurnDriver& driver, const ModernState& g, std::vector<MatchGroup> groups,
                       uint16_t calendar_entry, std::vector<uint8_t> referees,
                       uint64_t session_id, uint64_t revision, std::string& error);

// Accept the current desk's advance request, show the next manager desk when applicable,
// then play the event in the background and advance through calendar days to the next desk.
// Rejected/stale requests leave state and both random generators untouched.
TurnAdvanceResult advance_to_next_desk(TurnDriver& driver, ModernState& g,
                                       const TurnAdvanceRequest& request,
                                       GameRandom& game, WatcomRandom& rand,
                                       MinuteReport& report);

}  // namespace bmh::modern
