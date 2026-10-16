// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "game/random.hpp"
#include "game/state.hpp"

namespace bmh {

struct MinuteReport;

// The unsaved PendingOfferAnswer observation is owned by CareerSession. Missing evidence
// is unsupported; zero is never an implicit default.
enum class HumanBuyerSaleAllocatorGate : uint8_t { unsupported, zero, blocked };
// OpenTransferEditor owns this unsaved transient: its ordinary AI-seller entry starts at
// zero, while targeted human-seller processing may mark a list entry. The
// session must supply this only after observing the actual ordinary UI entry.
enum class HumanBuyerSaleDirectMode : uint8_t { unsupported, ordinary_ai_purchase };

enum class HumanBuyerSaleStatus : uint8_t {
    moved, funds_refused, allocator_blocked, no_room, unsupported
};

// DispatchClubPlayerPendingOffers supplies param8 independently of FindManagerForClub's active-count
// lookup. Only the checked national pending caller may bind a retained raw
// owner or use parked record4 for a human buyer/movement board.
struct SourcePendingOwner {
    int manager = -1;
    int team = -1;
};

struct HumanBuyerSaleRequest {
    int list_entry = -1;
    int bid_slot = -1;
    uint32_t list_address = 0;
    size_t source_pool_slot = 0;
    int expected_seller = -1;
    int expected_buyer = -1;
    int expected_active_manager = -1;
    // -1 identifies a normal list row. Pending callers provide the explicit raw
    // OfferSlots slot; the shared helper never manufactures raw+40 from this value.
    int raw_offer_slot = -1;
    // Poaching (src/game/poach.hpp): a direct sale on the special row 40..43 that
    // Range2 prepared; no raw OfferSlots slot belongs to it.
    bool poach = false;
    // DayStepRest2/Range2 return-to-borrower re-offer on a special entry. This is
    // a separate non-poaching Source owner and requires its exact OfferSlots marker.
    bool loan_return_entry = false;
    // The day step's list decision (TransferListCountdown -> Void2) passes buyer and seller
    // managers to SettleHumanBuyerSale explicitly; whoever is on turn (ActiveManager) is not bound to
    // them. Set for that caller: the active-manager checks below do not apply.
    bool day_step = false;
    // Only raw pending entries with reduced national runtime and a matching
    // saved order/owner/record/pool qualify. Accounts use param8; movement still
    // uses FindManagerForClub. Never changes ordinary/day-step/poach defaults.
    std::optional<SourcePendingOwner> source_pending_owner = std::nullopt;
};

struct HumanBuyerSaleNotice {
    int manager = -1;
    int list_entry = -1;
    int raw_offer_slot = -1;
    uint32_t source_text_address = 0;
    std::string original_message;
    uint16_t source_flags = 0;
};

struct HumanBuyerSalePaid {
    int list_entry = -1;
    int bid_slot = -1;
    int raw_offer_slot = -1;
    int player = -1;
    int seller = -1;
    int buyer = -1;
    int seller_manager = -1; // SettleHumanBuyerSale accounting seller (DispatchClubPlayerPendingOffers param8)
    int buyer_manager = -1;
    uint32_t raw_fee = 0;
    uint32_t adjusted_fee = 0;
    uint32_t salary = 0;
    uint8_t term = 0;
};

struct HumanBuyerSaleHooks {
    HumanBuyerSaleAllocatorGate allocator_gate = HumanBuyerSaleAllocatorGate::unsupported;
    HumanBuyerSaleDirectMode direct_mode = HumanBuyerSaleDirectMode::unsupported;
    std::function<void(const GameState&, const HumanBuyerSaleNotice&)> notice;
    // Optional source caller acknowledgement after all deterministic transaction
    // validation and immediately before SettleHumanBuyerSale. The direct adapter uses this
    // for SubmitTransferBidSourceHumanBuyer's DeskActions_SubmitTransferBidImpl_ExecutableField ACK; pending callers leave it empty.
    std::function<void(const GameState&, const HumanBuyerSaleRequest&)> before_accounts;
    // Used only by the Void2 pending caller to preserve its severance/ACK/news
    // sequence between SettleHumanBuyerSale and Fail. The direct SubmitTransferBidSourceHumanBuyer caller leaves
    // this empty. It does not own list cleanup or Weekly_PendingSettleHumanBuyer_RuntimeField/DeskActions_EditorFail_RuntimeField2.
    std::function<void(GameState&, const HumanBuyerSalePaid&)> after_paid_accounts;
    // PresentManagerDialog yes/no question to a manager (poaching: the human seller agrees to the fee,
    // the buyer accepts the fee the DFB sets). Required only for poaching.
    std::function<bool(const GameState&, int manager, const std::string& text)> confirm;
};

struct HumanBuyerSaleResult {
    HumanBuyerSaleStatus status = HumanBuyerSaleStatus::unsupported;
    int player = -1;
    int seller = -1;
    int buyer = -1;
    int seller_manager = -1; // SettleHumanBuyerSale accounting seller (DispatchClubPlayerPendingOffers param8)
    int buyer_manager = -1;
    uint32_t raw_fee = 0;
    uint32_t adjusted_fee = 0;
};

// Shared exact SettleHumanBuyerSale account/subsidy and Fail ownership/board/history
// composition. Caller supplies explicit list and pool coordinates (ordinary
// 0..39 or special 40..43), owns staging, notices and any source-specific
// caller work. A successful move removes matching-player pool entries through
// ReleasePlayer. This helper never rewrites bids, performs caller-wide list/pool
// cleanup, or sets Weekly_PendingSettleHumanBuyer_RuntimeField/DeskActions_EditorFail_RuntimeField2. Unsupported boundaries require caller rollback.
HumanBuyerSaleResult settle_human_buyer_sale(GameState&, const HumanBuyerSaleRequest&,
    const HumanBuyerSaleHooks&, WatcomRandom&, MinuteReport&);

} // namespace bmh
