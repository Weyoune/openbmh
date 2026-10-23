// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/other_games.hpp"

#include <cstdint>
#include <array>
#include <string>
#include <vector>

#include "formats/textd.hpp"
#include "modern/game/modern_state.hpp"
#include "game/table.hpp"

namespace bmh::modern {

// Source local20==0 (`compact`) draws the complete round in dense six-pixel rows;
// local20==1 (`detail`) draws a 16-participant window in the taller 16-pixel layout.


// One Source row in the ordinary CupGroupOverviewEffects desk view. Names and text are kept in
// Original encoding so the presentation layer can pass them through the existing FNT3 path.




// Const projection of the ordinary menu-5 call to CupGroupOverviewEffects. This API intentionally models
// only the visible values: Original entry separately updates serialized CareerSession_FormatError_RuntimeField3/selector-12 state,
// while complete/compact and page navigation persist the active manager's CareerSession_DispatchOtherGames_RuntimeField byte. It is
// therefore not evidence that the entire Original menu interaction is Native-neutral.
// `linked_page` follows the descriptor's Source +0x17 parallel-group link, then +0x16 next-stage
// link. `round == -1` selects CupRound's live round; otherwise it is range-checked.
// `participant_offset` is Source's low-seven-bit display cursor (multiples of 16). The
// displayed-round group row list remains complete; the mode/offset applies to knockout rows.
OtherGamesProjection other_games_from_game(const ModernState& game, const TextDb& texts,
                                            int competition = 0, int linked_page = 0,
                                            int round = -1,
                                            OtherGamesDisplayMode display_mode = OtherGamesDisplayMode::detail,
                                            int participant_offset = 0,
                                            bool source_new_selection = true);

}  // namespace bmh::modern
