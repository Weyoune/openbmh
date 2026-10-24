// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "modern/ui/catalog.hpp"

namespace bmh::modern_ui {
// Only state.advertising is owned here. Monetary model/projection values are
// currency minor units; the spending editor action uses whole display units.
// Selection and offer navigation belong to the App, never the saved state.
bool migrate_demo_advertising(Json &state, const Catalog &);
bool valid_demo_advertising(const Json &state, const Catalog &);
std::string edit_demo_advertising(Json &state, const Json &action, const Catalog &);
// {kind:"accept",slot:0..15,offer:0..2} recomputes the deterministic offer.
// {kind:"spending",index:0..2,value:0..99999999} changes only that budget.
// Sources bind commits to the observed revision; queries never mutate it.
Json demo_advertising_quote(const Json &state, int slot, int offer, const Catalog &);
// {ok,slots:[{slot,label,name,sponsor,months,amount,active}],
//  spending:[{index,name,amount,suggestion_step}],income,expenses,balance}.
Json demo_advertising_view(const Json &state, const Catalog &);
} // namespace bmh::modern_ui
