// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "modern/ui/catalog.hpp"

namespace bmh::modern_ui {
// Deterministic presentation fixtures only: no session, engine, or RNG access.
// Each function receives the reports subobject, not the complete DemoSource state.
Json demo_reports(const Catalog &);
bool valid_demo_reports(const Json &, const Catalog &);
// Adds only the missing indoor sort to an otherwise valid legacy reports object.
// Invalid existing fields reject atomically; current valid states are unchanged.
bool migrate_demo_reports(Json &, const Catalog &);
// Returns an empty string on success, an explicit error on rejection. Rejections
// leave the reports object unchanged. Actions use kind/value, or kind=round with
// step (+/-1) and optional other, or kind=sort with view/key.
std::string edit_demo_reports(Json &, const Json &action, const Catalog &);
// Rows retain stable catalog club IDs. Numeric strength components are null when
// the corresponding squad positions have no usable OBW strength information.
Json report_table_rows(const Catalog &, const Json &);
Json report_strength_rows(const Catalog &, const Json &);
// At most 16 clubs from the first German league, independent of report selection.
Json report_indoor_rows(const Catalog &, const Json &);
Json report_match_rows(const Catalog &, const Json &, bool other);
// The presentation-only opening draw: adjacent pairs from at most 64 German
// league clubs in catalog order; goals are null until a future runtime owns them.
Json report_opening_pairs(const Catalog &);
// A count (not a last index), always at least 1. Shared round state is clamped
// on selection changes; non-league other previews deliberately use 34 rounds.
int report_round_count(const Catalog &, const Json &, bool other = false);
} // namespace bmh::modern_ui
