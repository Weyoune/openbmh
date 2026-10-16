// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// One engine uses a shared rule profile across supported editions.
// OpenBMH deviates from the original: normalizes live-scene options, league
// strength ranges, option defaults, stadium prices and capacities, betting limits,
// and the foreign-player limit. Texts, currency, calendar, league structure, screen
// positions, season values, and edition-specific resort content stay loaded from data.

#include "formats/le.hpp"

namespace bmh {

// Applies the shared rule profile to compatible German executable images.
// Images without the matching data segment are left unchanged.
void apply_rule_base(LeImage& exe);

}  // namespace bmh
