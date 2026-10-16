// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "game/desk_actions.hpp"
#include "game/weekly.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace bmh {

// Source choices 8..15 map to eight independent A–H record slots. These must
// not be confused with StadiumInvestment's facilities 0..7.
enum class StadiumBlockSlot : uint8_t { A = 0, B, C, D, E, F, G, H };

struct StadiumBlockExpansionConstants {
    std::array<uint32_t, 8> price_per_1000{};
    std::array<uint8_t, 8> weeks{};
    std::array<uint32_t, 8> max_places{};
};

// An immutable quote binds the Source owner and every record/table value that
// affects the quote. Commit recomputes this value before it touches money.
struct StadiumBlockExpansionQuote {
    int manager = -1;
    int manager_club = -1;
    StadiumBlockSlot slot = StadiumBlockSlot::A;
    uint8_t current_build_weeks = 0;
    uint8_t current_raw_flags = 0;
    uint8_t current_capacity_thousands = 0;
    uint8_t current_future_capacity_thousands = 0;
    uint32_t total_target_places = 0;
    uint8_t desired_features = 0; // Source low bits: seats=1, roof=2.
    int company_price_percent = 0;
    uint32_t price_per_1000 = 0;
    uint32_t max_places = 0;
    uint32_t new_places = 0;
    uint32_t new_places_cost = 0;
    uint32_t seats_cost = 0;
    uint32_t roof_cost = 0;
    uint32_t undiscounted_cost = 0;
    uint32_t base_price = 0;
    uint8_t base_weeks = 0;
    bool operator==(const StadiumBlockExpansionQuote&) const = default;
};

// Pure Source projection. Manual totals need not be multiples of 1000; only the
// eventual +54 byte is floored to thousands. The supported source boundary is
// 0..427 company percent, 0..255 thousand places, a positive duration, and
// trusted table maxima <=255000. Current/target placement must satisfy
// current<=target<=maximum; busy blocks, unknown idle feature bits, owner
// mismatches, disabled existing features, malformed constants and any
// intermediate that overflows Source's signed 32-bit arithmetic have no quote.
// A valid zero-price quote remains previewable; commit refuses to construct it.
std::optional<StadiumBlockExpansionQuote> quote_stadium_block_expansion(
    const GameState& g, int manager, StadiumBlockSlot slot, uint32_t total_target_places,
    uint8_t desired_features, int company_price_percent,
    const StadiumBlockExpansionConstants& constants);

// Commit an already resolved tip decision exactly once. `Declined` proceeds at
// the base quote; `Accepted` uses its precomputed price and weeks. A zero-price
// quote never constructs. Notice exceptions propagate so the caller can restore
// its full transaction checkpoint.
bool build_stadium_block_expansion(
    GameState& g, int manager, StadiumBlockSlot slot,
    const StadiumBlockExpansionConstants& constants,
    const StadiumBlockExpansionQuote& quote, const StandBlockOffer& resolved_offer,
    WatcomRandom& rand, const ConstructionNoticeHook& payment_notice = {},
    const SeasonTicketCompensationNoticeHook& compensation_notice = {});

} // namespace bmh
