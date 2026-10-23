// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "modern/game/modern_state.hpp"
#include "game/stadium_blocks.hpp"

#include "modern/game/desk_actions.hpp"
#include "modern/game/weekly.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace bmh::modern {

// Source choices 8..15 map to eight independent A–H record slots. These must
// not be confused with StadiumInvestment's facilities 0..7.


// An immutable quote binds the Source owner and every record/table value that
// affects the quote. Commit recomputes this value before it touches money.

// Pure Source projection. Manual totals need not be multiples of 1000; only the
// eventual +54 byte is floored to thousands. The supported source boundary is
// 0..427 company percent, 0..255 thousand places, a positive duration, and
// trusted table maxima <=255000. Current/target placement must satisfy
// current<=target<=maximum; busy blocks, unknown idle feature bits, owner
// mismatches, disabled existing features, malformed constants and any
// intermediate that overflows Source's signed 32-bit arithmetic have no quote.
// A valid zero-price quote remains previewable; commit refuses to construct it.
std::optional<StadiumBlockExpansionQuote> quote_stadium_block_expansion(
    const ModernState& g, int manager, StadiumBlockSlot slot, uint32_t total_target_places,
    uint8_t desired_features, int company_price_percent,
    const StadiumBlockExpansionConstants& constants);

// Commit an already resolved tip decision exactly once. `Declined` proceeds at
// the base quote; `Accepted` uses its precomputed price and weeks. A zero-price
// quote never constructs. Notice exceptions propagate so the caller can restore
// its full transaction checkpoint.
bool build_stadium_block_expansion(
    ModernState& g, int manager, StadiumBlockSlot slot,
    const StadiumBlockExpansionConstants& constants,
    const StadiumBlockExpansionQuote& quote, const StandBlockOffer& resolved_offer,
    WatcomRandom& rand, const ConstructionNoticeHook& payment_notice = {},
    const SeasonTicketCompensationNoticeHook& compensation_notice = {});

} // namespace bmh::modern
