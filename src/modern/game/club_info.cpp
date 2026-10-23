// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "modern/game/modern_state.hpp"
#include "modern/game/club_info.hpp"
#include <stdexcept>
namespace bmh::modern {
ClubInfoEntry prepare_club_info_entry(ModernState& g, int team) {
    if (team < 0 || (team >= league_clubs() && team != no_team()))
        throw std::invalid_argument("club info: team has no squad");
    const SquadRange range = squad_range(team);
    if (range.first > g.squad_slots.size() || range.count > g.squad_slots.size() - range.first)
        throw std::invalid_argument("club info: truncated squad range");
    size_t occupied = 0;
    for (size_t row = 0; row < range.count; ++row) {
        const int id = g.squad_slots[range.first + row] & 0x0fff;
        if (id == 0x0fff) continue;
        if (size_t(id) >= g.players.size())
            throw std::invalid_argument("club info: invalid player ID");
        ++occupied;
    }
    // Allocate the owned pre-cleanup drawing rows before the first engine write.
    ClubInfoEntry entry;
    entry.displayed_slots.assign(g.squad_slots.begin() + range.first,
                                 g.squad_slots.begin() + range.first + range.count);
    // SquadCount(team,2) counts occupied IDs over the complete range. PrepareLineupEntry
    // scans that count-sized prefix, even if the supplied map is sparse.
    int last = int(occupied) - 1;
    while (last >= 0 && (g.squad_slots[range.first + size_t(last)] & 0xc000) == 0) --last;
    for (int row = 0; row <= last && row < 17; ++row) {
        auto& slot = g.squad_slots[range.first + size_t(row)];
        slot = uint16_t((slot & 0x3fff) | (row < 11 ? 0x8000 : 0x4000));
    }
    entry.displayed_slots.assign(g.squad_slots.begin() + range.first,
                                 g.squad_slots.begin() + range.first + range.count);
    // PrepareClubInfoEntry clears bits13..15 for injured players over the entire range.
    // Empty slots are a dummy ID in Original; preserve the sentinel explicitly.
    for (size_t row = 0; row < range.count; ++row) {
        auto& slot = g.squad_slots[range.first + row];
        const int id = slot & 0x0fff;
        if (id != 0x0fff && ((g.players[size_t(id)].w364 >> 16) & 0x1f) != 0)
            slot &= 0x1fff;
    }
    return entry;
}
uint8_t set_club_info_category(ModernState& g, int team, int requested) {
    constexpr size_t scan_count = 0x147;
    if (team < 0 || size_t(team) >= g.team_morale.size() ||
        g.team_morale.size() < scan_count || requested < 0 || requested > 2)
        throw std::invalid_argument("club info: invalid category request or club table");
    const size_t manager_count = g.memory.has(::bmh::layout::ManagerCount)
        ? g.memory.u8(::bmh::layout::ManagerCount) : g.manager_teams.size();
    if (manager_count > 4 || manager_count > g.manager_teams.size())
        throw std::invalid_argument("club info: truncated manager teams");
    const bool national = !g.manager_teams.empty() && g.manager_teams.front() == no_team();
    constexpr size_t national_club = 4 * 0xb0c + 0x290;
    if (national && g.manager_records.size() < national_club + 2)
        throw std::invalid_argument("club info: truncated national manager record");
    // FindManagerForClub matches the first ManagerCount teams, irrespective of manager name,
    // then the fifth record when the first manager is the national coach.
    const auto human = [&](int club) {
        for (size_t m = 0; m < manager_count; ++m)
            if (g.manager_teams[m] == club) return true;
        return national && (g.manager_records[national_club] |
               uint16_t(g.manager_records[national_club + 1]) << 8) == club;
    };
    uint8_t category = uint8_t(requested);
    uint8_t seen = 0;
    if (category == 2) {
        for (size_t club = 0; club < scan_count; ++club) {
            if ((g.team_morale[club] >> 6) != 2) continue;
            ++seen;
            if (seen != 4 || category != 2) continue;
            if (!human(team)) category = 0;
            else {
                // Source local10 stays0: every search restarts at the first club.
                for (size_t other = 0; other < scan_count; ++other) {
                    if ((g.team_morale[other] >> 6) == 2 && !human(int(other))) {
                        g.team_morale[other] = uint8_t((g.team_morale[other] & 0x3f) | 0x40);
                        --seen;
                        break;
                    }
                }
            }
        }
    }
    g.team_morale[size_t(team)] = uint8_t((g.team_morale[size_t(team)] & 0x3f) | (category << 6));
    return category;
}

}
