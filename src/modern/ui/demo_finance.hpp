// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "modern/ui/catalog.hpp"

namespace bmh::modern_ui {
// Full presentation state: cash/deposit/debt remain the sole shared balances.
// Money in the model/projections uses currency minor units. Action editor values
// use whole displayed currency units, or quantities for asset transactions.
int64_t finance_currency_factor(const Catalog &);
Json demo_contracts(const Catalog &, const std::string &managed_club = "");
bool migrate_demo_finance(Json &state, const Catalog &,
                          const std::string &managed_club = "");
bool valid_demo_finance(const Json &state, const Catalog &,
                        const std::string &managed_club = "");
std::string edit_demo_finance(Json &state, const Json &action, const Catalog &,
                              const std::string &managed_club = "");
// Helpers only project synthetic prices/rates. They never accrue money or time.
Json demo_bank_assets(const Catalog &);
Json demo_bank_terms();
// {kind:deposit|withdraw|loan|repay} or
// {kind:property-buy|property-sell|stock-buy|stock-sell,index:int}.
// Returns ok,kind,min,max,quantity,index,unit_price (raw),step. A max of zero
// disables confirmation. Every commit recomputes this quote from current state.
Json demo_bank_quote(const Json &state, const Json &request, const Catalog &);
Json demo_bank_summary(const Json &state, const Catalog &);
Json demo_balance_rows(const Json &state, const Catalog &);
// Returns {ok,club,rows:[catalogue values + stored contract],salary_total,
// salary_known,salary_unknown,bonuses}; total covers known salaries only.
Json demo_contract_rows(const Json &state, const Catalog &,
                        const std::string &managed_club = "");
} // namespace bmh::modern_ui
