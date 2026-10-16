// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/strengths_state.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#include "formats/binary.hpp"
#include "game/schedule.hpp"
#include "game/season.hpp"
#include "ui/sprite_font.hpp"

namespace bmh::ui {
namespace {

constexpr uint32_t kManagerSettings = ::bmh::layout::ManagerSettings;
constexpr size_t kManagerRecordSize = 0xb0c;
constexpr std::array<uint16_t, 4> kCriterionFlags{0x001, 0x002, 0x004, 0x008};

[[noreturn]] void invalid(const std::string& reason) {
    throw FormatError("strengths: " + reason);
}

void require_memory(const GameState& game, uint32_t address, size_t size = 1) {
    for (size_t i = 0; i < size; ++i) {
        if (uint64_t(address) + i > std::numeric_limits<uint32_t>::max() ||
            !game.memory.has(address + uint32_t(i)))
            invalid("missing saved memory at " + std::to_string(uint64_t(address) + i));
    }
}

uint8_t source_name_color(const GameState& game, int team) {
    const uint8_t managers = game.memory.u8(::bmh::layout::ManagerCount);
    const uint8_t active_manager = game.memory.u8(::bmh::layout::ActiveManager);
    const uint8_t highlight_active = game.memory.u8(::bmh::layout::DeskPosition);
    for (uint8_t manager = 0; manager < managers; ++manager) {
        const size_t record = size_t(manager) * kManagerRecordSize;
        if (record >= game.manager_records.size() ||
            record + kManagerRecordSize > game.manager_records.size())
            invalid("incomplete manager record table for Source name color");
        if (game.manager_records[record + 0xa8] != 0 &&
            size_t(manager) < game.manager_teams.size() &&
            game.manager_teams[manager] == team) {
            return manager == active_manager && highlight_active < managers ? 0x7a : 0x75;
        }
    }
    if (active_manager >= managers || size_t(active_manager) >= game.manager_teams.size())
        invalid("active manager is outside the saved manager table");
    return game.manager_teams[active_manager] == team ? 0xf7 : 0x5f;
}

std::vector<uint8_t> source_age_scratch(const GameState& game, int league, bool lineup_only) {
    const int first = league_first_team(league);
    std::vector<uint8_t> result;
    result.reserve(kLeagueSizes[league]);
    for (int team = first; team < first + kLeagueSizes[league]; ++team) {
        const SquadRange range = squad_range(team);
        if (range.first > game.squad_slots.size() ||
            range.count > game.squad_slots.size() - range.first)
            invalid("incomplete squad slots for domestic club " + std::to_string(team));
        uint32_t sum = 0;
        uint8_t count = 0;
        for (size_t offset = 0; offset < range.count; ++offset) {
            const uint16_t slot = game.squad_slots[range.first + offset];
            if (slot == kEmptySlot) break; // SizeT4 stops at the first sentinel.
            if (lineup_only && !(slot & kSlotInLineup)) continue;
            const size_t player = size_t(slot & 0x0fff);
            if (player >= game.players.size())
                invalid("squad references missing player " + std::to_string(player));
            sum = (sum & 0xffffu) + ((game.players[player].w364 >> 21) & 0x1fu);
            count = uint8_t(count + 1);
        }
        result.push_back(count == 0 ? uint8_t(0x22) : uint8_t((sum * 10u) / count));
    }
    return result;
}

// Literal partition order from Games. Ties are swapped along with their team IDs;
// this intentionally does not use std::sort or std::stable_sort.
void source_quicksort(std::vector<StrengthsRow>& rows, size_t begin, size_t count, bool ascending) {
    if (count <= 1) return;
    // Source initializes its unsigned left cursor to 0xffff; the first increment wraps to 0.
    std::ptrdiff_t left = -1;
    std::ptrdiff_t right = std::ptrdiff_t(count);
    const uint32_t pivot = rows[begin + count / 2].source_sort_key;
    for (;;) {
        if (!ascending) {
            do { ++left; } while (pivot < rows[begin + size_t(left)].source_sort_key);
            do { --right; } while (rows[begin + size_t(right)].source_sort_key < pivot);
        } else {
            do { ++left; } while (rows[begin + size_t(left)].source_sort_key < pivot);
            do { --right; } while (pivot < rows[begin + size_t(right)].source_sort_key);
        }
        if (right <= left) break;
        std::swap(rows[begin + size_t(left)], rows[begin + size_t(right)]);
    }
    source_quicksort(rows, begin, size_t(left), ascending);
    source_quicksort(rows, begin + size_t(left), count - size_t(left), ascending);
}

} // namespace

StrengthsDrawWords strengths_draw_words_source(uint16_t raw) {
    StrengthsDrawWords out;
    out.raw_settings_word = raw;
    out.row_settings_word = uint16_t(raw & 0x00ff);
    if ((raw & 0x000f) == 0 || (raw & 0x000f) == 0x000f)
        out.row_settings_word = uint16_t(out.row_settings_word | 0x000f);
    if ((raw & 0x00f0) == 0 || (raw & 0x00f0) == 0x00f0)
        out.row_settings_word = uint16_t(out.row_settings_word | 0x00f0);
    out.effective_average_flags = uint16_t(out.row_settings_word | ((raw & 0x8000) ? 0x0100 : 0));

    uint16_t persisted = raw;
    if ((raw & 0x00ff) == 0x00ff)
        persisted = uint16_t(persisted | 0x0100);
    else
        persisted = uint16_t((persisted & 0x00ff) | (uint16_t(uint8_t(persisted >> 8) & 0xf2) << 8));
    if ((persisted & 0x02ff) == 0)
        persisted = uint16_t(persisted | 0x01ff);
    out.post_draw_settings_word = persisted;
    return out;
}

std::optional<uint16_t> strengths_control_word_source(uint16_t word, StrengthsControl control) {
    const uint8_t index = uint8_t(control);
    if (index <= 7) {
        if (word & 0x0200) return std::nullopt;
        uint16_t out = uint16_t(word ^ uint16_t(1u << index));
        // Reset2 clears high-byte bit 0 after a changed criterion unless
        // the resulting low nine bits are the full 0x1ff selection.
        if ((out & 0x01ff) != 0x01ff) out = uint16_t(out & 0xfeff);
        return out;
    }
    if (control == StrengthsControl::reset) {
        if ((word & 0x01ff) != 0x01ff) return uint16_t(0x01ff);
        return word;
    }
    if (control == StrengthsControl::age) {
        uint16_t out = uint16_t(word & 0xff00);
        out = uint16_t(out & 0x7200);
        out = uint16_t(out ^ 0x0200);
        return out;
    }
    if (control == StrengthsControl::lineup) return uint16_t(word ^ 0x8000);
    return std::nullopt;
}

uint16_t strengths_family_word_source(uint16_t raw_family_word, uint8_t points_mode) {
    return uint16_t((raw_family_word & 0xfc0f) | 0x0170 | (points_mode != 0 ? 0x0080 : 0));
}

StrengthsProjection strengths_from_game(const GameState& game, int manager_slot, int league,
                                        const StrengthsProjectionContext& context) {
    if (league < 0 || league >= kLeagueCount) invalid("league is outside Source's nine domestic leagues");
    require_memory(game, ::bmh::layout::ManagerCount);
    require_memory(game, ::bmh::layout::ActiveManager);
    require_memory(game, ::bmh::layout::DeskPosition);
    const uint8_t manager_count = game.memory.u8(::bmh::layout::ManagerCount);
    if (manager_count == 0 || manager_count > 4)
        invalid("saved manager count is outside the four Strengths settings words");
    if (manager_slot < 0 || manager_slot >= 4 || size_t(manager_slot) >= game.manager_teams.size() ||
        manager_slot >= manager_count)
        invalid("manager slot is not present in the saved game");
    require_memory(game, kManagerSettings + uint32_t(manager_slot) * 2, 2);
    if (game.manager_records.size() < size_t(manager_count) * kManagerRecordSize)
        invalid("manager record table is truncated");
    if (game.manager_teams.size() < manager_count)
        invalid("manager team table is truncated");
    if (league < 5 && !game.texts) invalid("TEXTD data is unavailable for the league title");

    const int first = league_first_team(league);
    const int size = kLeagueSizes[league];
    if (first < 0 || first + size > 138) invalid("league bounds exceed domestic club table");
    if (game.club_ids.size() < size_t(first + size)) invalid("club ID table is truncated");
    if (game.club_names.empty()) invalid("club name pool is empty");
    for (int team = first; team < first + size; ++team) {
        if (game.club_ids[size_t(team)] >= game.club_names.size())
            invalid("club name offset is outside the save pool");
        if (size_t(team) >= game.team_morale.size()) invalid("team morale table is truncated");
        const SquadRange range = squad_range(team);
        if (range.first > game.squad_slots.size() ||
            range.count > game.squad_slots.size() - range.first)
            invalid("squad slot table is truncated");
        for (size_t offset = 0; offset < range.count; ++offset) {
            const uint16_t slot = game.squad_slots[range.first + offset];
            if (slot == kEmptySlot) break;
            if (size_t(slot & 0x0fff) >= game.players.size()) invalid("squad references a missing player");
        }
    }

    const uint16_t raw = game.memory.u16(kManagerSettings + uint32_t(manager_slot) * 2);
    const StrengthsDrawWords words = strengths_draw_words_source(raw);
    StrengthsProjection out;
    out.manager_slot = manager_slot;
    out.league = league;
    out.first_team = first;
    out.words = words;

    if (context.cached_age) {
        const auto& cached = *context.cached_age;
        if (cached.league != league || cached.tenths.size() != size_t(size))
            invalid("cached age scratch does not match the selected league");
        if (cached.basis == StrengthsAgeBasis::cached_for_same_league)
            invalid("cached age scratch must retain its original Source basis");
        if (cached.basis != StrengthsAgeBasis::entry_or_league_change_bit10 &&
            cached.basis != StrengthsAgeBasis::header_toggle_bit15)
            invalid("cached age scratch has an invalid Source basis");
        if(context.retain_validated_owner_age) {
            if(!context.cached_age)invalid("validated age retention has no owner cache");
        } else {
            const auto expected = source_age_scratch(game, league, cached.lineup_only);
            if (cached.tenths != expected) invalid("cached age scratch is stale or forged");
        }
        out.age_scratch = cached;
    } else {
        if(context.retain_validated_owner_age)
            invalid("validated age retention requires the exact owner cache");
        if (context.recompute_age_as != StrengthsAgeBasis::entry_or_league_change_bit10 &&
            context.recompute_age_as != StrengthsAgeBasis::header_toggle_bit15)
            invalid("age recomputation requires a Source bit-10 or bit-15 basis");
        const uint8_t basis_bit = context.recompute_age_as == StrengthsAgeBasis::header_toggle_bit15 ? 15 : 10;
        const bool lineup_only = ((raw >> basis_bit) & 1) != 0;
        out.age_scratch.league = league;
        out.age_scratch.basis = context.recompute_age_as;
        out.age_scratch.lineup_only = lineup_only;
        out.age_scratch.tenths = source_age_scratch(game, league, lineup_only);
    }

    if (league < 5) {
        out.league_name = iso646de_to_utf8(league_name(game, league).c_str());
    } else {
        const int title_team = 0x165 + league - 4;
        if (size_t(title_team) >= game.club_ids.size() ||
            game.club_ids[size_t(title_team)] >= game.club_names.size())
            invalid("foreign league title is missing from the save name pool");
        const std::string title = game.club_name(title_team);
        out.league_name = iso646de_to_utf8(title.c_str());
    }
    out.rows.reserve(size_t(size));
    for (int row = 0; row < size; ++row) {
        const int team = first + row;
        StrengthsRow projected;
        projected.runtime_team = team;
        projected.source_row = size_t(row);
        projected.age_tenths = out.age_scratch.tenths[size_t(row)];
        projected.name_color = source_name_color(game, team);
        const std::string original_name = game.club_name(team);
        projected.team_name = iso646de_to_utf8(original_name.c_str());
        const uint16_t aggregate_flags = words.effective_average_flags;
        const uint16_t selection_flags = uint16_t(aggregate_flags & 0x01f0);
        for (size_t criterion = 0; criterion < kCriterionFlags.size(); ++criterion) {
            try {
                projected.criterion_values[criterion] = uint16_t(team_average(
                    game, team, uint16_t(selection_flags | kCriterionFlags[criterion])).value);
            } catch (const std::exception& e) {
                invalid("cannot project criterion for club " + std::to_string(team) + ": " + e.what());
            }
        }
        try {
            projected.aggregate_value = uint16_t(team_average(game, team, aggregate_flags).value);
        } catch (const std::exception& e) {
            invalid("cannot project aggregate for club " + std::to_string(team) + ": " + e.what());
        }
        projected.source_sort_key = (raw & 0x0200) ? projected.age_tenths : projected.aggregate_value;
        out.rows.push_back(std::move(projected));
    }
    source_quicksort(out.rows, 0, out.rows.size(), (raw & 0x0200) != 0);
    return out;
}

} // namespace bmh::ui
