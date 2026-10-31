// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "formats/le.hpp"
#include "game/random.hpp"
#include "game/state.hpp"

namespace bmh {

enum class ClubPlayerDetailOrigin : uint8_t { squad, market_pool };

// The list owns all row identities. A loan row has physical_row==-1 and its
// explicit Source Career_CareerManagersEnd_RuntimeField index. rendered_row is the visible list index, which
// may differ from a filtered/sorted market-pool entry.
struct ClubPlayerDetailSelection {
    int runtime_team = -1;
    int player_id = -1;
    int rendered_row = -1;
    int physical_row = -1;
    int human_loan_index = -1;
    ClubPlayerDetailOrigin origin = ClubPlayerDetailOrigin::squad;
    int market_pool_slot = -1; // Actual Native CareerRuntimeField+entry identity, never a visible row.
};

struct ClubPlayerDetailContext {
    ClubPlayerDetailSelection selection;
    int source_slot = -1; // Resolved Source slot; national prelude may remap any origin.
    int source_manager = -1;
    int primary_club = -1;
    int secondary_club = -1;
    int loan_duration = 0;
    int loan_record = -1;
    // Literal local1c from ClubPlayerDetailFromGame:0 ordinary,1 national nomination candidate,
    // 2 currently selected national player.
    uint8_t national_mode = 0;
    int active_manager = -1;
    int active_offer = -1; // ResolvePendingOffer fallback; -1 for a direct national label.
    Player attributes;
    uint16_t displayed_slot = 0x0fff;
    uint16_t cards = 0;
    uint8_t bonus = 0;
    std::string original_name;
    std::optional<std::array<uint8_t, 0x58>> board;
    bool human_loan = false;
    bool market_pool = false;
    bool national_action_available = false;
};

// OpenTransferEditor passes actual slots CareerRuntimeField + entry (0..39) for ordinary
// transfer-market pool cards. It never turns the entry into a squad row.
inline const int& kClubPlayerDetailMarketPoolFirst = runtime_dimensions.pool_first;
constexpr int kClubPlayerDetailMarketPoolCount = 40;

// Resolves runtime club, explicit row and Source loan/national ownership without
// changing GameState or either random generator. Invalid/stale identities throw.
ClubPlayerDetailContext club_player_detail_context(const GameState&,
                                                    const ClubPlayerDetailSelection&);

struct ClubPlayerDetailRedraw {
    ClubPlayerDetailContext context;
    bool portrait_available = false;
    uint8_t portrait_base = 0;
    std::array<uint8_t, 5> portrait_features{};
    uint16_t portrait_burn_count = 0;
    // Human-loan fields2..5 use Source ClubPlayerDetail_Copy_TextPointer8 and do not compute a market value.
    std::optional<uint32_t> market_value;
};

// One actual ClubPlayerDetailFromGame card rebuild: Copy2/21213 portrait draws, followed
// by422ac/42307/PlayerCardMarketValue market valuation. Pure renderer calls consume this owned
// result repeatedly and never draw RNG. The caller passes Source heap gate.
ClubPlayerDetailRedraw club_player_detail_redraw(const GameState&,
                                                const ClubPlayerDetailSelection&,
                                                const LeImage& original,
                                                GameRandom&, WatcomRandom&,
                                                bool portrait_available = true);

// ClubInfoFromGame temporarily uses (bbcde!=2), then restores its caller's value.
constexpr uint8_t club_player_detail_input_mode(uint8_t prior) { return prior != 2; }

struct MinuteReport;
// The actual Source nomination/remove button. Returns false if the national
// squad is full; the Source lineup refresh still follows the attempted add.
bool club_player_detail_national_action(GameState&, const ClubPlayerDetailSelection&,
                                       WatcomRandom&, MinuteReport&);

} // namespace bmh
