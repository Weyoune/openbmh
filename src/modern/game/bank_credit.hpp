// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/bank_credit.hpp"

#include <array>
#include <cstdint>
#include <vector>
#include "formats/le.hpp"
#include "modern/game/modern_state.hpp"

namespace bmh::modern {

// MaxSharesToBuy case 4 (the bank) and case 5 (another manager lends). Row c of
// each manager's native 0x78-byte loan matrix (Loans + borrower * 0x78 + c * 0x18)
// is the credit from the bank (c = 0) or from manager c - 1, and contains principal,
// total, monthly payment, term, elapsed months and payments made. Case 5 differs only
// in: the principal is limited by the lender's balance plus credit line, the monthly
// payment (field 2) can be set within 20 % of the annuity, there are no fees and no
// fee question, and every booking is mirrored on the lender. The original asks the
// lender nothing. Projection and draft editing are read-only.




BankCreditContext bank_credit_context(const ModernState&, const LeImage&, int manager, int counterparty = 0);
// ChangeBankCreditInterest: apply the automatic interest change to the bank loan record.
bool change_bank_credit_interest(ModernState&, int manager, uint8_t new_rate);
BankCreditDraft bank_credit_draft(const BankCreditContext&);
BankCreditBounds bank_credit_bounds(const BankCreditContext&, const BankCreditDraft&, int field);
bool edit_bank_credit(const BankCreditContext&, BankCreditDraft&, int field, uint32_t value);


// Replay the local Source edits against the current native record before
// booking; fees and refund are derived here, never trusted from a UI amount.
BankCreditResult commit_bank_credit(ModernState&, const LeImage&, int manager,
    const std::array<uint32_t, 6>& expected, const std::vector<BankCreditEdit>& edits,
    bool accept_fees, int counterparty = 0);

} // namespace bmh::modern
