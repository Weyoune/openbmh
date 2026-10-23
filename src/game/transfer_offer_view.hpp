// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Read-only views of the weekly transfer list decision. GameState callbacks refer to them,
// so they are shared by the Classic and the Modern season cores.

#include <array>
#include <cstdint>

namespace bmh {

// Projection passed to the optional original-style seller decision callback
// (TransferListCountdown -> IncomingTransferScreen). Rows retain raw bid slots 0..3; ranking,
// quote RNG, and transaction remain in the synchronous weekly call site.
struct TransferOfferCandidateView {
    int bid_index = -1;
    bool present = false;
    int buyer = -1;
    int buyer_manager = -1;
    uint32_t fee = 0;
    uint32_t monthly_salary = 0;
    uint32_t fee_reference = 0; // RateTransferBidSource for this offer's term
    uint32_t salary_reference = 0;
    uint8_t contract_months = 0; // raw term low 7 bits; high bit is loan
    bool loan = false;
    std::array<uint8_t, 7> bonuses{};
    bool affordable = false;
    bool has_squad_room = false;
    bool passes_foreign_limit = false;
    bool loan_permitted = true;
    bool transaction_supported = true; // false for human-involved loans until return lifecycle is ported
    bool eligible = false;
};

struct TransferOfferListView {
    bool valid = false;
    int list_entry = -1;
    int player = -1;
    int seller = -1;
    int seller_manager = -1;
    int seller_loan_count = -1; // DeskActions_Unsupported_RuntimeField[seller_manager], -1 when unavailable
    int preferred_bid_index = -1;
    bool preferred_bid_passed_random_check = false; // TransferListCountdown uVar6 < ranking score
    int rejected_bid_index = -1;
    uint32_t notice_text_address = 0;
    uint32_t quoted_fee = 0;
    uint32_t quoted_monthly_salary = 0;
    uint8_t quoted_years = 0;
    uint32_t asking_fee = 0;
    uint32_t asking_monthly_salary = 0;
    int offer_count = 0;
    std::array<TransferOfferCandidateView, 4> offers{};
};

// Source-backed read-only events around transfer settlement and countdown
// notices. `offers` owns a snapshot of all four raw bid slots; selected_bid_index
// is the ranked raw row, not a compact row. last_week is the system modal at
// countdown 2 -> 1 and carries result_text_address PendingOffer_TransferLastWeekDialog_ExecutableField.
enum class TransferDecisionEventKind { evaluation, confirmation, last_week };
struct TransferDecisionEvent {
    TransferDecisionEventKind kind = TransferDecisionEventKind::evaluation;
    TransferOfferListView offers;
    int selected_bid_index = -1;
    bool accepted = false; // evaluation: the one W(90,110) result; confirmation: accepted/declined
    int viewer_manager = -1; // ActiveManager at evaluation; human seller first, otherwise buyer
    uint32_t result_text_address = 0; // evaluation: OK/NEIN; confirmation: PendingOffer_FormatError_ExecutableField2 / PendingOffer_PendingOfferNoticeDialog_ExecutableField
};

}  // namespace bmh
