// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "modern/ui/catalog.hpp"
namespace bmh::modern_ui {
// Presentation-only coupon: six independent columns, eleven games, raw money.
// These helpers accept the full UI state and never debit the shared wallet.
bool migrate_demo_toto(Json &, const Catalog &);
bool valid_demo_toto(const Json &, const Catalog &);
std::string edit_demo_toto(Json &, const Json &, const Catalog &);
Json demo_toto_summary(const Json &, const Catalog &);
// Fixed programme independent of report filters: 9 first-tier + 2 second-tier
// matches. Missing clubs yield an incomplete projection, never invented teams.
Json demo_toto_programme(const Catalog &);
} // namespace bmh::modern_ui
