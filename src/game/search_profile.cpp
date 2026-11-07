// SPDX-License-Identifier: GPL-3.0-only
// Search profile of a human manager: storage, closing rule, "Vorschlag" and matching against the transfer list
// (see search_profile.hpp). Original routines: sub_19409 (dialog, closing rule), sub_18F06 (suggestion),
// sub_19A38 (position strengths), sub_190B5 (match one listing) of the Diamond 3.7 executable.
#include "game/search_profile.hpp"

#include <algorithm>
#include <cstdlib>

#include "formats/executable_layout.hpp"
#include "game/messages.hpp"

namespace bmh {
namespace {

constexpr size_t kRecord = 0xB0C;                    // manager record
constexpr uint32_t kTransferList = ::bmh::layout::EditorList;
constexpr uint32_t kListEntry = 0x58;
constexpr int kPoolFirst = 0xb70;                    // the transfer list lives in the first 40 pool slots
// Fewest players per position (goalkeeper, defender, midfielder, forward) before the suggestion asks for one.
constexpr int kMinimumPerPosition[4] = {2, 6, 7, 5};

// Strength of a player as shown in the squad bars (original sub_199D0 with the halving flag set):
// (first attribute + second attribute) * (94 + form / 8) / 100 / 2, limited to 0..99.
int bar_strength(uint32_t w35c) {
    const int first = int((w35c >> 14) & 0x7F), second = int((w35c >> 7) & 0x7F), form = int(w35c & 0x7F);
    int16_t value = int16_t(int16_t(first + second) * ((form >> 3) + 94) / 100);
    value = int16_t(value / 2);
    if (value < 0) value = 0;
    if (value > 99) value = 99;
    return value;
}

uint8_t record_byte(const GameState& g, int manager, size_t offset) {
    return g.manager_records.at(size_t(manager) * kRecord + offset);
}

}  // namespace

SearchProfile search_profile(const GameState& g, int manager) {
    SearchProfile profile{};
    if (manager < 0 || size_t(manager) >= g.manager_teams.size() ||
        (size_t(manager) + 1) * kRecord > g.manager_records.size()) return profile;
    for (size_t i = 0; i < profile.size(); ++i) profile[i] = record_byte(g, manager, kSearchProfileOffset + i);
    return profile;
}

bool search_profile_valid(SearchProfile profile) {
    profile[0] &= 0x3F;  // the dialog starts from the stored profile without the "off" bit
    for (size_t i = 0; i < profile.size(); ++i) {
        const bool range = i >= 4 && i <= 7;
        if (!range && profile[i] > 1) return false;                // switches
        if (i >= 4 && i <= 5 && profile[i] > 24) return false;     // age bar: 25 cells
        if (i >= 6 && i <= 7 && profile[i] > 99) return false;     // strength bar: 100 cells
    }
    return true;
}

bool search_profile_active(SearchProfile profile, bool cancelled) {
    profile[0] &= 0x3F;
    int sum = 0;
    for (uint8_t value : profile) sum += value;
    return !(sum == 0 || sum == 18 || cancelled);
}

bool set_search_profile(GameState& g, int manager, SearchProfile profile, bool cancelled) {
    if (manager < 0 || size_t(manager) >= g.manager_teams.size() ||
        (size_t(manager) + 1) * kRecord > g.manager_records.size() || !search_profile_valid(profile)) return false;
    profile[0] &= 0x3F;
    if (!search_profile_active(profile, cancelled)) profile[0] |= kSearchProfileOff;
    for (size_t i = 0; i < profile.size(); ++i)
        g.manager_records.at(size_t(manager) * kRecord + kSearchProfileOffset + i) = profile[i];
    return (profile[0] & kSearchProfileOff) == 0;
}

std::array<uint16_t, 5> position_strengths(const GameState& g, int club, bool whole_squad) {
    unsigned sum[5] = {}, count[5] = {};
    const SquadRange range = squad_range(club);
    for (size_t i = 0; i < 24; ++i) {
        if (i >= 11 && !whole_squad) continue;
        const size_t slot = range.first + i;
        if (slot >= g.squad_slots.size()) break;
        const int player = g.squad_slots[slot] & 0xFFF;
        if (player == 0xFFF) continue;
        const Player& p = g.players.at(size_t(player));
        const int position = int(p.w364 & 3);
        const int value = bar_strength(p.w35c);
        ++count[position]; ++count[4];
        sum[position] += unsigned(value); sum[4] += unsigned(value);
    }
    std::array<uint16_t, 5> average{};
    for (size_t k = 0; k < 5; ++k) average[k] = count[k] ? uint16_t(sum[k] / count[k]) : 0;
    return average;
}

SearchProfile suggest_search_profile(const GameState& g, int manager, bool whole_squad) {
    SearchProfile profile{};
    if (manager < 0 || size_t(manager) >= g.manager_teams.size()) return profile;
    const int club = g.manager_teams[size_t(manager)];
    // Players per position in the first 24 squad slots (the count stops at the first empty slot).
    int present[4] = {};
    const SquadRange range = squad_range(club);
    for (size_t i = 0; i < 24; ++i) {
        const size_t slot = range.first + i;
        if (slot >= g.squad_slots.size()) break;
        const int player = g.squad_slots[slot] & 0xFFF;
        if (player == 0xFFF) break;
        ++present[g.players.at(size_t(player)).w364 & 3];
    }
    const auto average = position_strengths(g, club, whole_squad);
    int weakest = 0, weakest_value = 127;
    for (int position = 0; position < 4; ++position) {
        if (present[position] - kMinimumPerPosition[position] < 0) profile[size_t(position)] = 1;
        if (int(average[size_t(position)]) < weakest_value) {
            weakest_value = int(int8_t(average[size_t(position)]));
            weakest = position;
        }
    }
    profile[size_t(weakest)] = 1;
    // Whole squad, all four positions: team averages of the first and of the second attribute.
    const int first = team_average(g, club, 0xF1).value;
    const int second = team_average(g, club, 0xF2).value;
    int ratio = 100;
    if (uint16_t(second) != 0) ratio = 100 * uint16_t(first) / uint16_t(second);
    if (std::abs(100 - ratio) > 9) profile[size_t(8 + (ratio > 100 ? 1 : 0))] = 1;
    int16_t upper = int16_t(second + 20);
    upper = std::clamp<int16_t>(upper, 0, 99);
    profile[4] = 2;
    profile[5] = 12;
    profile[6] = uint8_t(second);
    profile[7] = uint8_t(upper);
    return profile;
}

bool search_profile_matches_listing(const GameState& g, int manager, uint32_t list, int player) {
    if (manager < 0 || size_t(manager) >= g.manager_teams.size() ||
        (size_t(manager) + 1) * kRecord > g.manager_records.size()) return false;
    const StaticMemory& mem = g.memory;
    const auto b = [&](size_t offset) { return int(record_byte(g, manager, offset)); };
    const int team = g.manager_teams[size_t(manager)];
    player &= 0xFFF;
    if (team == kNoTeam || (mem.u16(list + 8) & 0x7FFF) == team || (b(kSearchProfileOffset) & kSearchProfileOff)) return false;
    const uint32_t w364 = g.players.at(size_t(player)).w364, w35c = g.players.at(size_t(player)).w35c;
    // position wanted (or none selected) and age within the range
    if (b(0x1f6 + (w364 & 3)) == 0 && b(0x1f9) + b(0x1f8) + b(0x1f7) + b(0x1f6) != 0) return false;
    const int age = int((w364 >> 21) & 0x1F);
    if (age < b(0x1fa) || age > b(0x1fb)) return false;
    // strength by the selected attributes (first, second, form; none = all three)
    int8_t selected = int8_t(b(0x200) + b(0x1fe) + b(0x1ff));
    if (selected == 0) selected = 3;
    int sum = 0, n = 0, form = 0;
    if (b(0x1fe) != 0 || selected == 3) sum = int((w35c >> 14) & 0x7F), n = 1;
    if (b(0x1ff) != 0 || selected == 3) sum = int16_t(sum) + int((w35c >> 7) & 0x7F), n++;
    if (b(0x200) != 0 || selected == 3) form = int(w35c & 0x7F);
    int16_t strength;
    if (n == 0) {
        strength = int16_t((form - 0x23) * 3 + 10);
    } else {
        strength = int16_t(uint32_t(sum) / uint32_t(n));
        if (form != 0) strength = int16_t(uint32_t(strength * int16_t((int16_t(form) >> 3) + 0x5e)) / 100);
    }
    if (strength < b(0x1fc) || strength > b(0x1fd)) return false;
    // left / right foot and wing preferences (w35c bits 21-23 and 24-26)
    const int lane = int((w35c >> 21) & 7), foot = int((w35c >> 24) & 7);
    bool low_foot = b(0x201) || b(0x203), high_foot = b(0x202) || b(0x204);
    if (low_foot && high_foot) low_foot = high_foot = false;
    bool low_lane = b(0x201) || b(0x202), high_lane = b(0x203) || b(0x204);
    if (low_lane && high_lane) low_lane = high_lane = false;
    if ((low_lane && lane >= 3) || (high_lane && lane <= 4) || (low_foot && foot >= 3) || (high_foot && foot <= 4)) return false;
    // the price class (asking price 70..130 %) against the classes wanted
    int8_t price_class = int8_t((mem.u8(list + 0x54) - 0x46) / 0x14);
    price_class = std::clamp<int8_t>(price_class, 0, 2);
    const int8_t classes = int8_t(b(0x207) + b(0x205) + b(0x206));
    int highest = 2;
    while (highest >= 0 && b(0x205 + size_t(highest)) == 0) highest--;
    return classes == 0 || price_class <= highest;
}

bool search_profile_matches(const GameState& g, int manager, int entry) {
    if (entry < 0 || entry >= 40 || size_t(kPoolFirst + entry) >= g.squad_slots.size()) return false;
    const int player = g.squad_slots[size_t(kPoolFirst + entry)] & 0xFFF;
    if (player == 0xFFF) return false;
    return search_profile_matches_listing(g, manager, kTransferList + uint32_t(entry) * kListEntry, player);
}

std::vector<SearchProfileHit> search_profile_hits(const GameState& g, int manager) {
    std::vector<SearchProfileHit> hits;
    for (int entry = 0; entry < 40; ++entry) {
        if (!search_profile_matches(g, manager, entry)) continue;
        const int player = g.squad_slots[size_t(kPoolFirst + entry)] & 0xFFF;
        hits.push_back({entry, player,
            message_text(g, ::bmh::layout::Month_SearchProfileNews_TextPointer) + g.player_name(player) +
            message_text(g, ::bmh::layout::Month_SearchProfileNews_TextPointer3)});
    }
    return hits;
}

}  // namespace bmh
