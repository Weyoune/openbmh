// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include "formats/le.hpp"
#include "game/state.hpp"

namespace bmh {

// MaxSharesToBuy case 4 (the bank) and case 5 (another manager lends). Row c of
// each manager's native 0x78-byte loan matrix (Loans + borrower * 0x78 + c * 0x18)
// is the credit from the bank (c = 0) or from manager c - 1, and contains principal,
// total, monthly payment, term, elapsed months and payments made. Case 5 differs only
// in: the principal is limited by the lender's balance plus credit line, the monthly
// payment (field 2) can be set within 20 % of the annuity, there are no fees and no
// fee question, and every booking is mirrored on the lender. The original asks the
// lender nothing. Projection and draft editing are read-only.
struct BankCreditContext {
    int counterparty = 0;            // 0 the bank, else lender manager + 1
    int32_t lender_liquidity = 0;    // case 5: lender balance (+0x314) + credit line (+0x31c)
    std::array<uint32_t, 6> saved{};
    std::array<uint8_t, 49> rate_table{};
    double divisor = 0;
    int32_t cash = 0, overdraft = 0;
    bool available = false;
};

struct BankCreditEdit {
    int field = -1;
    uint32_t value = 0;
    friend bool operator==(const BankCreditEdit&, const BankCreditEdit&) = default;
};

struct BankCreditDraft {
    std::array<uint32_t, 8> values{};
    int32_t fees = 0, interest_adjustment = 0, refund = 0, principal_paid = 0;
    bool initial_display = true;
    std::vector<BankCreditEdit> edits;
};

struct BankCreditBounds {
    uint32_t minimum = 0, maximum = 0, initial = 0;
    bool available = false;
};

BankCreditContext bank_credit_context(const GameState&, const LeImage&, int manager, int counterparty = 0);
// ChangeBankCreditInterest: apply the automatic interest change to the bank loan record.
bool change_bank_credit_interest(GameState&, int manager, uint8_t new_rate);
BankCreditDraft bank_credit_draft(const BankCreditContext&);
BankCreditBounds bank_credit_bounds(const BankCreditContext&, const BankCreditDraft&, int field);
bool edit_bank_credit(const BankCreditContext&, BankCreditDraft&, int field, uint32_t value);

enum class BankCreditStatus {
    accepted, no_change, invalid_manager, invalid_state, invalid_request,
    stale_request, confirmation_required
};
struct BankCreditResult {
    BankCreditStatus status = BankCreditStatus::invalid_request;
    int32_t fees = 0, refund = 0;
};

// Replay the local Source edits against the current native record before
// booking; fees and refund are derived here, never trusted from a UI amount.
BankCreditResult commit_bank_credit(GameState&, const LeImage&, int manager,
    const std::array<uint32_t, 6>& expected, const std::vector<BankCreditEdit>& edits,
    bool accept_fees, int counterparty = 0);

} // namespace bmh
