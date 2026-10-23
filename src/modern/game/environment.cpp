// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "modern/game/modern_state.hpp"
#include "modern/game/environment.hpp"
#include <algorithm>
#include <cstdlib>

namespace bmh::modern {
namespace {
uint32_t youth_at(int manager) { return layout::EnvironmentYouthTeams + uint32_t(manager) * 0x24; }
uint32_t spending_at(int manager, int item) {
    return layout::Spending + uint32_t(manager) * 0x24 + uint32_t(item) * 4;
}
void validate_manager(const ModernState& g, int manager) {
    if (manager < 0 || size_t(manager) >= g.manager_teams.size() ||
        !g.memory.has(youth_at(manager) + 8) || !g.memory.has(spending_at(manager, 5) + 3))
        throw FormatError("environment: invalid manager or truncated budgets");
}
int direction(int value) { return (value > 0) - (value < 0); }
int signed_clamp(int value, int low, int high) {
    return std::clamp(int(int8_t(uint8_t(value))), low, high);
}
bool legacy_academy(const ModernState& g, int manager) {
    const auto value = g.memory.u32(spending_at(manager, kYouthAcademy));
    return value == 1500000 || value == 1000000 || value == 580000 || value == 280000;
}
uint32_t academy_paid(const ModernState& g, int manager) {
    const auto at = size_t(manager) * 0xb0c + 0x880;
    return uint32_t(g.manager_records.at(at)) | uint32_t(g.manager_records.at(at + 1)) << 8 |
        uint32_t(g.manager_records.at(at + 2)) << 16 | uint32_t(g.manager_records.at(at + 3)) << 24;
}
}
int32_t environment_monthly_cost(const ModernState& g, int manager, int item) {
    const auto value = g.memory.u32(spending_at(manager, item));
    if (item == kYouthAcademy && legacy_academy(g, manager) && academy_paid(g, manager) >= value) return 0;
    return int32_t(value);
}
bool environment_academy_active(const ModernState& g, int manager) {
    const auto value = g.memory.u32(spending_at(manager, kYouthAcademy));
    return value != 0 && (!legacy_academy(g, manager) || academy_paid(g, manager) >= value);
}
EnvironmentState environment_state(const ModernState& g, int manager) {
    validate_manager(g, manager);
    EnvironmentState result;
    for (int i = 0; i < kEnvironmentItems; ++i) {
        result.monthly[size_t(i)] = int32_t(g.memory.u32(spending_at(manager, i)));
        result.level[size_t(i)] = std::clamp(result.monthly[size_t(i)] / kEnvironmentStep[size_t(i)], 0, 6);
    }
    for (uint32_t i = 0; i < 3; ++i) {
        result.youth_strength[i] = g.memory.u8(youth_at(manager) + i);
        result.youth_position[i] = g.memory.u8(youth_at(manager) + 3 + i);
        result.youth_league[i] = g.memory.u8(youth_at(manager) + 6 + i);
        if (result.youth_position[i] > 11 || result.youth_league[i] > 2)
            throw FormatError("environment: invalid youth table");
    }
    return result;
}
EnvironmentResult set_environment(ModernState& g, int manager, int item, int level, std::string* message) {
    if (message) message->clear();
    if (manager < 0 || size_t(manager) >= g.manager_teams.size() || item < 0 || item >= kEnvironmentItems ||
        level < 0 || level > 6 || !g.memory.has(spending_at(manager, item) + 3))
        return EnvironmentResult::invalid;
    const auto at = spending_at(manager, item);
    const auto value = uint32_t(level * kEnvironmentStep[size_t(item)]);
    if (g.memory.u32(at) == value) return EnvironmentResult::unchanged;
    g.memory.set32(at, value);
    return EnvironmentResult::changed;
}
void advance_youth_teams(ModernState& g, int manager, WatcomRandom& rand) {
    const auto observed = modern::environment_state(g, manager);
    std::array<uint8_t, 44> movement{};
    uint8_t step = 4;
    for (auto& value : movement) {
        value = step;
        if (step != 0 && rand.range(0, 8) == 0) --step;
    }
    const auto round = g.league_rounds.at(0);
    if (round >= movement.size()) throw FormatError("environment: youth round outside season");
    constexpr int division_strength[] = {85, 70, 57};
    for (size_t i = 0; i < 3; ++i) {
        int strength = observed.youth_strength[i];
        int position = observed.youth_position[i];
        const int opponent = uint8_t(division_strength[observed.youth_league[i]] - position);
        const int ratio = strength == 0 ? 100 :
            int((((int64_t(observed.monthly[i]) * 100 / ((1 << i) * 10000)) >> 1) + 40) * 100 / strength);
        int change = direction(ratio - 100);
        if (rand.range(0, 4) == 0) change = rand.range(0, 2) - 1;
        if (std::abs(ratio - 100) <= rand.range(0, 30)) change = 0;
        if (rand.range(0, 2) == 0) strength = uint8_t(strength + change);
        strength = signed_clamp(strength, 10, 85);
        g.memory.set8(youth_at(manager) + uint32_t(i), uint8_t(strength));
        const int difference = strength - opponent;
        change = direction(difference);
        if (rand.range(0, 4) == 0) change = rand.range(0, 2) - 1;
        if (rand.range(0, 3) != 0 && std::abs(difference) * 2 < rand.range(1, 18)) change = 0;
        position = signed_clamp(uint8_t(position - change * rand.range(1, movement[round])), 0, 11);
        g.memory.set8(youth_at(manager) + 3 + uint32_t(i), uint8_t(position));
    }
}
void finish_youth_season(ModernState& g, int manager) {
    const auto observed = modern::environment_state(g, manager);
    for (int i = 2; i >= 0; --i) {
        auto division = observed.youth_league[size_t(i)];
        const int position = observed.youth_position[size_t(i)];
        int next_position = 5;
        if (position < 2 && division != 0) { --division; next_position = position + 10; }
        if (position > 10 && division < 2) { ++division; next_position = position - 10; }
        if (i != 0) g.memory.set8(youth_at(manager) + uint32_t(i),
            uint8_t((int(observed.youth_strength[size_t(i - 1)]) + observed.youth_strength[size_t(i)] * 2) / 3));
        g.memory.set8(youth_at(manager) + 3 + uint32_t(i), uint8_t(next_position));
        g.memory.set8(youth_at(manager) + 6 + uint32_t(i), division);
    }
}
}  // namespace bmh::modern
