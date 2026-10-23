// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/club_player_detail.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "formats/le.hpp"
#include "game/random.hpp"
#include "modern/game/modern_state.hpp"

namespace bmh::modern {


// The list owns all row identities. A loan row has physical_row==-1 and its
// explicit Source Career_CareerManagersEnd_RuntimeField index. rendered_row is the visible list index, which
// may differ from a filtered/sorted market-pool entry.


// OpenTransferEditor passes actual slots CareerRuntimeField + entry (0..39) for ordinary
// transfer-market pool cards. It never turns the entry into a squad row.
constexpr int kClubPlayerDetailMarketPoolCount = 40;

// Resolves runtime club, explicit row and Source loan/national ownership without
// changing ModernState or either random generator. Invalid/stale identities throw.
ClubPlayerDetailContext club_player_detail_context(const ModernState&,
                                                    const ClubPlayerDetailSelection&);


// One actual ClubPlayerDetailFromGame card rebuild: Copy2/21213 portrait draws, followed
// by422ac/42307/PlayerCardMarketValue market valuation. Pure renderer calls consume this owned
// result repeatedly and never draw RNG. The caller passes Source heap gate.
ClubPlayerDetailRedraw club_player_detail_redraw(const ModernState&,
                                                const ClubPlayerDetailSelection&,
                                                const LeImage& original,
                                                GameRandom&, WatcomRandom&,
                                                bool portrait_available = true);

// ClubInfoFromGame temporarily uses (bbcde!=2), then restores its caller's value.
constexpr uint8_t club_player_detail_input_mode(uint8_t prior) { return prior != 2; }

// The actual Source nomination/remove button. Returns false if the national
// squad is full; the Source lineup refresh still follows the attempted add.
bool club_player_detail_national_action(ModernState&, const ClubPlayerDetailSelection&,
                                       WatcomRandom&, MinuteReport&);

} // namespace bmh::modern
