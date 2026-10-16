// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/environment.hpp"

#include <algorithm>

#include "game/messages.hpp"
#include "game/schedule.hpp"
#include "game/week.hpp"

namespace bmh {

namespace {

constexpr uint32_t kSpending = ::bmh::layout::Spending;  // + manager * 0x24 + item * 4
constexpr size_t kRecordSize = 0xb0c;
constexpr size_t kYouthPaid = 0x880;     // youth expenses booked this season
// EnvironmentRuntimeField: the step of each item in thousands (items 3..5: 5 000 per level)
constexpr int kUnit[kEnvironmentItems] = {2, 4, 5, 5, 5, 5};

uint32_t spending_at(int manager, int item) { return kSpending + uint32_t(manager) * 0x24 + uint32_t(item) * 4; }

// TeamLeague3: the academy's season price by the club's league
int32_t youth_price(const GameState& g, int manager) {
    const int league = team_league(g.manager_teams.at(size_t(manager)));
    return league == 0 ? 1500000 : league == 1 ? 1000000 : league <= 3 ? 580000 : 280000;
}

// YouthOpen: July (unless the Bundesliga is past its 17th round) or August
bool youth_open(const GameState& g) {
    const int month = month_code(g.date);
    if (month == 7) return !(g.league_rounds.at(0) > 0x11);
    return month == 8;
}

bool youth_paid(const GameState& g, int manager) {
    const size_t at = size_t(manager) * kRecordSize + kYouthPaid;
    const uint32_t paid = uint32_t(g.manager_records.at(at)) | uint32_t(g.manager_records.at(at + 1)) << 8 |
                          uint32_t(g.manager_records.at(at + 2)) << 16 | uint32_t(g.manager_records.at(at + 3)) << 24;
    return paid >= uint32_t(youth_price(g, manager));
}

}  // namespace

EnvironmentState environment_state(const GameState& g, int manager) {
    EnvironmentState s;
    for (int i = 0; i < kEnvironmentItems; i++) {
        s.monthly[size_t(i)] = int32_t(g.memory.u32(spending_at(manager, i)));
        // the stars (SizeT7): staff value / step, the academy 6 when on, 0/2 none
        if (i >= 3) s.level[size_t(i)] = std::clamp(s.monthly[size_t(i)] / (kUnit[i] * 1000), 0, 6);
        else if (i == kYouthAcademy) s.level[size_t(i)] = s.monthly[size_t(i)] != 0 ? 6 : 0;
    }
    s.youth_price = youth_price(g, manager);
    s.youth_paid = youth_paid(g, manager);
    s.youth_open = youth_open(g);
    return s;
}

EnvironmentResult set_environment(GameState& g, int manager, int item, int level, std::string* message) {
    if (manager < 0 || size_t(manager) >= g.manager_teams.size() || item < 0 || item >= kEnvironmentItems || level < 0 ||
        level > 6 || !g.memory.has(spending_at(manager, item) + 3))
        return EnvironmentResult::invalid;
    const uint32_t at = spending_at(manager, item);
    const int32_t before = int32_t(g.memory.u32(at));
    int32_t value = 0;
    EnvironmentResult result = EnvironmentResult::changed;
    if (item >= 3) {
        value = level * kUnit[item] * 1000;
    } else if (item == kYouthAcademy) {
        // YouthPrice: a click on an active academy means "off"
        int wanted = level;
        if (before != 0 && wanted != 0) wanted = 0;
        const int32_t price = youth_price(g, manager);
        if (!youth_paid(g, manager)) {
            if (wanted == 0) {
                value = 0;
            } else if (!youth_open(g)) {
                value = 0;
                result = EnvironmentResult::too_late;
                if (message) *message = message_text(g, ::bmh::layout::Environment_SetEnvironment_ExecutableField);  // "Zu spät für diese Saison..."
            } else {
                value = price;
            }
        } else {
            // paid this season: stays on; a click meant as "off" brings the second string of
            // the same text block (*Environment_SetEnvironment_ExecutableField + 0x42)
            value = price;
            if (wanted == 0) {
                result = EnvironmentResult::contract_runs;
                if (message) {
                    *message = message_text_following(g, ::bmh::layout::Environment_SetEnvironment_ExecutableField);
                    // OpenBMH-authored notice when an edition has no academy tail text.
                    if (message->empty()) *message = "This academy contract stays active until the season ends.";
                }
            }
        }
    }
    g.memory.set32(at, uint32_t(value));
    if (result == EnvironmentResult::changed && value == before) result = EnvironmentResult::unchanged;
    return result;
}

}  // namespace bmh
