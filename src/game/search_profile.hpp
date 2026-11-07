// SPDX-License-Identifier: GPL-3.0-only
// The search profile of a human manager (transfer market buttons "Suchen" and "Vorschlag", original
// sub_19409 / sub_18F06 / sub_190B5). The profile is 18 bytes in the manager record at +0x1f6:
//   [0..3]   positions wanted (goalkeeper, defender, midfielder, forward); none set = every position
//   [4..5]   age range (stored as 0..24, i.e. age - 16), [6..7] strength range (0..99)
//   [8..10]  strength is judged by condition / technique / form (none set = all three)
//   [11..14] foot and wing preference, [15..17] price classes wanted (asking price 70..130 %)
// Bit 0x40 of byte 0 marks the profile as switched off. Implementation: search_profile.cpp.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "game/state.hpp"

namespace bmh {

using SearchProfile = std::array<uint8_t, 18>;
constexpr size_t kSearchProfileOffset = 0x1f6;
constexpr uint8_t kSearchProfileOff = 0x40;

// The stored profile of a manager slot (all zero for an invalid slot).
SearchProfile search_profile(const GameState& g, int manager);

// Whether the values can come out of the dialog: bytes 0..3 and 8..17 are switches (0 or 1, the "off" bit is
// ignored in byte 0), the age bar has 25 cells (bytes 4 and 5 are 0..24) and the strength bar 100 (bytes 6 and 7 are 0..99).
bool search_profile_valid(SearchProfile profile);

// Stores the profile the way the original dialog does when it is closed: a profile with every value 0 or
// every value 1 is switched off (bit 0x40), and so is any profile that was left by right click
// (`cancelled`). Returns whether the stored profile is active. Fails (false, nothing stored) for an
// invalid manager slot or invalid values (see search_profile_valid) - check these first to tell
// "refused" from "switched off".
bool set_search_profile(GameState& g, int manager, SearchProfile profile, bool cancelled);

// Average strengths per position class of the manager's own squad (original sub_19A38): index 0..3
// goalkeeper..forward, index 4 the whole squad. `whole_squad` also counts the substitutes (24 slots
// instead of the first 11).
std::array<uint16_t, 5> position_strengths(const GameState& g, int club, bool whole_squad);

// The "Vorschlag" profile (original sub_18F06) from the squad of the manager: positions with too few
// players or the weakest line, age range 2..12, strength from the condition average to +20, and the
// attribute (condition or technique) in which the squad is clearly weaker.
SearchProfile suggest_search_profile(const GameState& g, int manager, bool whole_squad);

// The matching rule itself for the listing record at `list` (EditorList entry address) holding `player`.
// Shared with the news of month.cpp (search_profile_news).
bool search_profile_matches_listing(const GameState& g, int manager, uint32_t list, int player);

// Whether the listing in transfer-list entry `entry` (0..39) fits the manager's active profile
// (original sub_190B5 up to the message). Listings of his own club never match.
bool search_profile_matches(const GameState& g, int manager, int entry);

struct SearchProfileHit {
    int entry = -1;       // transfer-list entry 0..39
    int player = -1;      // player id
    std::string message;  // "<player name> ..." as shown in the original message box
};

// Confirming the dialog with "Suchen": every listing that fits the profile right now (original
// sub_190B5 with the message-box variant), in list order.
std::vector<SearchProfileHit> search_profile_hits(const GameState& g, int manager);

}  // namespace bmh
