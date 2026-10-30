// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include "game/random.hpp"
#include "formats/le.hpp"
#include "game/state.hpp"

namespace bmh {
// Verified original-program capability; unknown environment variants are not guessed.
void install_environment_rules(const LeImage&);
bool environment_rules_supported();
constexpr int kEnvironmentItems = 6;
enum EnvironmentItem : int { kYouthYoungest = 0, kYouthAcademy = 1, kYouthOldest = 2,
                            kCoTrainer = 3, kDoctor = 4, kPhysio = 5 };
// Monthly environment model verified in the supported CD and Diamond 3.6/3.7 programs.
inline std::array<int32_t, kEnvironmentItems> kEnvironmentStep = {2000, 4000, 5000, 5000, 5000, 5000};
struct EnvironmentState {
    std::array<int32_t, kEnvironmentItems> monthly{};
    std::array<int, kEnvironmentItems> level{};
    std::array<uint8_t, 3> youth_strength{}, youth_position{}, youth_league{};
};
EnvironmentState environment_state(const GameState& g, int manager);
// Preserve already saved lump-sum academy contracts until the user edits them.
int32_t environment_monthly_cost(const GameState& g, int manager, int item);
bool environment_academy_active(const GameState& g, int manager);
enum class EnvironmentResult : uint8_t { changed, unchanged, invalid };
// Set a monthly budget to level 0..6, without an immediate debit or random draw.
EnvironmentResult set_environment(GameState& g, int manager, int item, int level, std::string* message = nullptr);
// Youth results advance with first-division matchdays. Promotion/relegation and
// the handover to the next age group happen before annual accounts.
void advance_youth_teams(GameState& g, int manager, WatcomRandom& rand);
void finish_youth_season(GameState& g, int manager);
}  // namespace bmh
