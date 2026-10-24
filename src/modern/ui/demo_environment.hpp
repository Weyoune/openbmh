// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "modern/ui/catalog.hpp"

namespace bmh::modern_ui {
// FULL demo state, canonical environment={levels:[six integers 0..6]}.
// Only an absent environment is initialized. Rejections are atomic; unrelated
// state and the OBW staff table are never rewritten or interpreted as budgets.
bool migrate_demo_environment(Json &, const Catalog &);
bool valid_demo_environment(const Json &, const Catalog &);
// {kind:"level",index:0..5,value:0..6}; selection is immediate, with no cash
// booking, salary changes, time advancement or engine/RNG access.
std::string edit_demo_environment(Json &, const Json &, const Catalog &);
// {ok,rows:[{id,name,level,rate,cost,image,cell,rank,league}],total}.
// Rates, costs and total use raw currency minor units. Youth places are explicit
// fictional presentation fixtures, independent of any league/report selection.
Json demo_environment_rows(const Json &, const Catalog &);
} // namespace bmh::modern_ui
