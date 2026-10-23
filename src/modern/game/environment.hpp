// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/environment.hpp"
#include <array>
#include <cstdint>
#include <string>
#include "game/random.hpp"
#include "modern/game/modern_state.hpp"

namespace bmh::modern {
constexpr int kEnvironmentItems = 6;
// OpenBMH deviates from the original: uses six monthly environment budgets across data editions while retaining the shared staff prices and academy player-generation strength bands.
inline constexpr std::array<int32_t, kEnvironmentItems> kEnvironmentStep = {2000, 4000, 5000, 5000, 5000, 5000};
EnvironmentState environment_state(const ModernState& g, int manager);
// Preserve already saved lump-sum academy contracts until the user edits them.
int32_t environment_monthly_cost(const ModernState& g, int manager, int item);
bool environment_academy_active(const ModernState& g, int manager);
// Set a monthly budget to level 0..6, without an immediate debit or random draw.
EnvironmentResult set_environment(ModernState& g, int manager, int item, int level, std::string* message = nullptr);
// Youth results advance with first-division matchdays. Promotion/relegation and
// the handover to the next age group happen before annual accounts.
void advance_youth_teams(ModernState& g, int manager, WatcomRandom& rand);
void finish_youth_season(ModernState& g, int manager);
}  // namespace bmh::modern
