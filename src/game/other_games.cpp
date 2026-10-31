// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/other_games.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

#include "formats/binary.hpp"
#include "game/cup.hpp"
#include "game/schedule.hpp"

namespace bmh {
namespace {

constexpr uint32_t kActiveSelectors = ::bmh::layout::ActiveSelectors;
constexpr uint32_t kSelectorCount = 8;
constexpr size_t kTextCompetitionNames = (::bmh::layout::CupDraw_FormatError_ExecutableField6 - ::bmh::layout::TextPointerTable) / 4;
constexpr size_t kTextCompetitionButtonNames = (::bmh::layout::OtherGames_Layout_ExecutableField - ::bmh::layout::TextPointerTable) / 4;
constexpr size_t kTextRoundHeadings = (::bmh::layout::CupDraw_FormatError_ExecutableField4 - ::bmh::layout::TextPointerTable) / 4;
constexpr size_t kTextGroupRound = (::bmh::layout::Calendar_Information_TextPointer2 - ::bmh::layout::TextPointerTable) / 4;
constexpr size_t kTextGroupCaption = (::bmh::layout::CupDraw_FormatError_ExecutableField5 - ::bmh::layout::TextPointerTable) / 4;

[[noreturn]] void invalid(const char* why) { throw FormatError(std::string("other games: ") + why); }

std::string text(const TextDb& texts, size_t index) {
    if (index >= texts.strings.size()) invalid("missing Source TEXTD entry");
    return texts.strings[index];
}

bool in_descriptor_table(int entry) { return entry >= 0 && entry < kCupEntries; }

// Bounded port of FindCup's literal scan cursor. On a matching descriptor, its +0x17 link
// wins, else +0x16; a zero link falls back to entry0 in Source. The scan cursor itself advances
// only on a nonmatch and returns the just-followed pointer immediately when the cursor is0.
// Malformed/no-match paths fail closed instead of returning Source's invalid one-past-table ptr.
int resolve_linked_entry(const StaticMemory& memory, int competition, int ordinal) {
    if (ordinal < 0 || ordinal >= kCupEntries) invalid("linked occurrence outside Source bounds");
    int cursor = 0, current = 0, remaining = ordinal;
    std::vector<uint8_t> seen(size_t(kCupEntries) * kCupEntries * kCupEntries, 0);
    for (size_t step = 0; step < seen.size(); ++step) {
        if (!in_descriptor_table(cursor) || !in_descriptor_table(current))
            invalid("Source linked resolver left descriptor table");
        const size_t key = (size_t(cursor) * kCupEntries + size_t(current)) * kCupEntries + size_t(remaining);
        if (seen[key]) invalid("cycle in Source linked resolver");
        seen[key] = 1;
        const uint32_t address = cup_entry_address(current);
        if (!memory.has(address + 0x19)) invalid("truncated Source descriptor ID");
        if (int16_t(memory.u16(address + 0x18)) == competition) {
            if (remaining == 0) return current;
            const int parallel = int8_t(memory.u8(address + 0x17));
            const int next = int8_t(memory.u8(address + 0x16));
            if (parallel != 0) current = parallel;
            else if (next != 0) current = next;
            else invalid("Source linked occurrence reaches its no-link fallback");
            if (!in_descriptor_table(current)) invalid("invalid Source linked descriptor link");
            --remaining;
            if (cursor == 0) return current;
        } else {
            ++cursor;
            if (cursor >= kCupEntries) invalid("Source linked resolver cursor wrapped without a match");
            current = cursor;
        }
    }
    invalid("Source linked resolver exceeded bounded traversal");
}

std::vector<OtherGamesLinkedPage> source_linked_pages(const StaticMemory& memory, int selected) {
    std::vector<OtherGamesLinkedPage> pages;
    int first = selected;
    std::array<bool, kCupEntries> seen{};
    while (in_descriptor_table(first)) {
        if (seen[size_t(first)]) invalid("cycle while locating first Source group selector");
        seen[size_t(first)] = true;
        const uint8_t group = memory.u8(cup_entry_address(first) + 0x1e);
        if ((group >> 4) == 0) break;
        if (first == 0) invalid("Source group selector would walk before descriptor table");
        --first;  // InDescriptorTable moves one adjacent descriptor left until group index0.
    }
    while (true) {
        if (!in_descriptor_table(first) || seen[size_t(first)]) {
            // The first adjacent scan above marks visited entries; reset its cycle guard before
            // following signed previous-stage links.
            if (!in_descriptor_table(first)) invalid("invalid Source first group descriptor");
        }
        seen[size_t(first)] = true;
        const int previous = int8_t(memory.u8(cup_entry_address(first) + 0x15));
        if (previous == -1) break;
        if (!in_descriptor_table(previous) || seen[size_t(previous)]) invalid("invalid Source previous-stage link");
        first = previous;
    }
    // InDescriptorTable suppresses the linked-page selector before enumerating groups when the first
    // stage has no +0x16 continuation.
    if (memory.u8(cup_entry_address(first) + 0x16) == 0) return pages;
    int stage = first;
    std::array<bool, kCupEntries> stage_seen{};
    while (in_descriptor_table(stage)) {
        if (stage_seen[size_t(stage)]) invalid("cycle in Source linked-stage selector");
        stage_seen[size_t(stage)] = true;
        int group_entry = stage;
        const uint8_t count = memory.u8(cup_entry_address(stage) + 0x1e) & 0x0f;
        for (int group = 0; group < count; ++group) {
            if (!in_descriptor_table(group_entry)) invalid("parallel group left descriptor table");
            const uint32_t address = cup_entry_address(group_entry);
            pages.push_back({int(pages.size()), group_entry, int16_t(memory.u16(address + 0x18)),
                             int(memory.u8(address + 0x1e) >> 4), int(count)});
            const bool has_adjacent = memory.u8(address + 0x17) != 0;
            if (group + 1 < count && !has_adjacent)
                invalid("parallel group count lacks adjacent Source entry");
            // Source checks +0x17 after every emitted sibling, including the final one; when
            // present it advances to the adjacent descriptor before reading its +0x16 stage.
            if (has_adjacent) ++group_entry;
        }
        const int next = int8_t(memory.u8(cup_entry_address(group_entry) + 0x16));
        if (next == 0) break;
        if (!in_descriptor_table(next)) invalid("invalid Source next-stage selector link");
        stage = next;
    }
    return pages;
}

void validate_team(const GameState& game, uint16_t team) {
    // CupGroupOverviewEffects indexes ClubIdentities directly; this screen has no empty-club fallback.
    if (team == 0x0fff) invalid("Source row references the empty-club sentinel");
    if (team >= game.club_ids.size()) invalid("club id outside loaded name table");
}

OtherGamesRow make_row(const GameState& game, int home, int away, uint16_t result, int match) {
    if (home < 0 || home > 0xffff || away < 0 || away > 0xffff) invalid("invalid team id");
    const uint16_t h = uint16_t(home), a = uint16_t(away);
    validate_team(game, h);
    validate_team(game, a);
    OtherGamesRow row;
    row.home_team = home;
    row.away_team = away;
    row.home_name = game.club_name(home);
    row.away_name = game.club_name(away);
    row.source_result = result;
    row.played = result != 0xcccc;
    if (row.played) {
        row.home_goals = (result >> 5) & 0x1f;
        row.away_goals = result & 0x1f;
    }
    row.source_match = match;
    return row;
}

OtherGamesClubMetadata source_metadata(const GameState& game, int team) {
    OtherGamesClubMetadata out;
    bool manager_owned = game.manager_of(team) >= 0;
    constexpr size_t kNationalOriginalClub = 4 * 0xb0c + 0x290;
    if (!manager_owned && !game.manager_teams.empty() && game.manager_teams[0] == kNoTeam &&
        game.manager_records.size() >= kNationalOriginalClub + 2) {
        const int original_club = int(uint16_t(game.manager_records[kNationalOriginalClub] |
            uint16_t(game.manager_records[kNationalOriginalClub + 1]) << 8));
        manager_owned = original_club == team;
    }
    if (team >= runtime_dimensions.record_teams && !manager_owned) {
        const size_t at = size_t(team - runtime_dimensions.record_teams);
        if (at >= game.foreign_strength.size()) invalid("missing Source foreign-strength byte");
        out.foreign_strength = game.foreign_strength[at];
        return out;
    }
    if (team >= runtime_dimensions.record_teams && team != kNoTeam)
        invalid("manager-controlled foreign-club row needs live TeamRating W-generator context");
    constexpr std::array<uint16_t, 5> kFlags{0x0f1, 0x0f2, 0x0f4, 0x0f8, 0x0ff};
    for (size_t i = 0; i < kFlags.size(); ++i)
        out.averages[i] = team_average(game, team, kFlags[i]).value;
    out.has_squad_values = true;
    return out;
}

void attach_source_values(const GameState& game, OtherGamesRow& row) {
    row.home_metadata = source_metadata(game, row.home_team);
    row.away_metadata = source_metadata(game, row.away_team);
}

void add_second_leg_and_winner(OtherGamesRow& row, uint16_t second, bool present,
                               bool visible, uint8_t flags) {
    row.source_second_leg = second;
    row.second_leg_array_present = present;
    row.second_leg_visible = visible;
    if (present) {
        // The second fixture is reversed relative to the primary row: its low field belongs
        // to the first displayed club and its high field to the second displayed club.
        row.second_leg_home_goals = second & 0x1f;
        row.second_leg_away_goals = (second >> 5) & 0x1f;
    }
    if (row.source_result == 0xcccc) return;
    int home = row.home_goals;
    int away = row.away_goals;
    // CupTie reads the second result whenever the descriptor has an array pointer,
    // independently of whether its separate detail line is visible or contains 0xcccc.
    const uint16_t winner_second = present ? second : 0;
    const int home_total = home + (winner_second & 0x1f);
    const int away_total = away + ((winner_second >> 5) & 0x1f);
    home = home_total;
    away = away_total;
    if ((flags & 0x10) && home == away && (row.source_result >> 11) == 0) {
        home += winner_second & 0x1f;
        away += row.away_goals;
    }
    row.source_winner = home > away ? 0 : away > home ? 1 : 2;
}

}  // namespace

OtherGamesProjection other_games_from_game(const GameState& game, const TextDb& texts,
                                            int competition, int linked_page, int requested_round,
                                            OtherGamesDisplayMode display_mode, int participant_offset,
                                            bool source_new_selection) {
    const StaticMemory& memory = game.memory;
    if (competition < 0 || competition >= 0x14) invalid("unsupported competition selector");
    if (linked_page < 0 || linked_page >= kCupEntries) invalid("invalid linked-page index");
    if (!memory.has(kActiveSelectors) || !memory.has(kActiveSelectors + kSelectorCount - 1))
        invalid("missing live active-competition selector list");

    OtherGamesProjection out;
    for (uint32_t i = 0; i < kSelectorCount; ++i) {
        const uint8_t id = memory.u8(kActiveSelectors + i);
        if (id == 0x14) {
            out.selectors.push_back({int(i), -1, {}, false});
            continue;
        }
        if (id > 0x14 || !in_descriptor_table(find_cup(memory, id)))
            invalid("active selector has no supported competition descriptor");
        out.selectors.push_back({int(i), int(id), text(texts, kTextCompetitionButtonNames + id), true});
    }
    int entry = resolve_linked_entry(memory, competition, linked_page);
    if (source_new_selection) {
        std::array<bool, kCupEntries> advanced{};
        while (in_descriptor_table(entry)) {
            if (advanced[size_t(entry)]) invalid("cycle while advancing completed Source stages");
            advanced[size_t(entry)] = true;
            const uint32_t at = cup_entry_address(entry);
            const uint8_t next = memory.u8(at + 0x16);
            if (next == 0) break;
            const int count = memory.u8(at + 0x1b);
            if (cup_round(memory, entry) < count) break;
            if (!in_descriptor_table(next)) invalid("completed stage points outside descriptor table");
            entry = next;
        }
    }
    const uint32_t address = cup_entry_address(entry);
    const uint8_t flags = memory.u8(address + 0x14);
    const int team_count = memory.u8(address + 0x1a);
    const int rounds = memory.u8(address + 0x1b);
    const int live_round = cup_round(memory, entry);
    if (team_count < 0 || team_count > 0x40 || rounds <= 0 || rounds > 0x40)
        invalid("descriptor dimensions outside Source bounds");
    const int round = requested_round < 0 ? std::min(live_round, rounds - 1) : requested_round;
    if (round < 0 || round >= rounds) invalid("requested round outside descriptor");

    out.competition = competition;
    out.descriptor_competition = int16_t(memory.u16(address + 0x18));
    out.linked_page = linked_page;
    out.entry_index = entry;
    out.entry_address = address;
    out.flags = flags;
    out.knockout = (flags & 4) != 0;
    out.two_legs = (flags & 1) != 0;
    out.current_round = live_round;
    out.round = round;
    out.rounds = rounds;
    out.group_index = memory.u8(address + 0x1e) >> 4;
    out.group_count = memory.u8(address + 0x1e) & 0xf;
    out.title = text(texts, kTextCompetitionNames + size_t(competition));
    out.linked_pages = source_linked_pages(memory, entry);

    int round_text = ((flags & 4) ? round : 0) + 1 + memory.u8(address + 0x1c);
    if (memory.u8(address + 0x1c) == 1 && round == 0) --round_text;
    if (round_text < 0 || round_text >= 32) invalid("Source round-heading index outside table");
    out.round_heading = text(texts, kTextRoundHeadings + size_t(round_text));
    if (kTextRoundHeadings + size_t(round_text) == kTextGroupRound && out.round_heading.size() > 0x10)
        out.round_heading[0x10] = char('1' + linked_page);
    if (kTextRoundHeadings + size_t(round_text) == kTextGroupCaption && out.round_heading.size() > 0x19)
        out.round_heading[0x19] = char('0' + (linked_page == 0 ? 1 : linked_page));

    // Source +0x1a is participants in this round. The detail/compact toggle is gated on this
    // value, and detail mode's ordinary window is 16 participant slots (eight matches).
    out.source_participant_count = team_count >> (round & 31);
    out.display_mode_toggle_available = out.source_participant_count > 0x10;
    if (display_mode != OtherGamesDisplayMode::compact && display_mode != OtherGamesDisplayMode::detail)
        invalid("unknown Source display mode");
    const OtherGamesDisplayMode effective_mode = out.source_participant_count <= 0x10
        ? OtherGamesDisplayMode::detail : display_mode;
    out.display_mode = effective_mode;
    if (participant_offset < 0 || participant_offset > 0x7f || participant_offset % 0x10 != 0)
        invalid("participant window offset is not a Source 16-slot boundary");
    // Compact draws the whole round, but the caller separately retains local14.
    // Detail redraw only ORs CareerSession_DispatchOtherGames_RuntimeField's high bit; it never resets a retained cursor,
    // even when a Source ID-versus-slot selection quirk leaves it past this list.
    if (effective_mode == OtherGamesDisplayMode::compact) participant_offset = 0;
    out.participant_offset = participant_offset;
    out.round_arrows_visible = effective_mode == OtherGamesDisplayMode::detail && rounds > 1;

    // CupGroupOverviewEffects selects the second-leg result array only for descriptors with the exact
    // two-leg/knockout flags and when the displayed round has reached that leg.
    const uint8_t played = memory.u8(address + 0x1d);
    const bool second_leg = out.knockout && (played % 2 != 0 || round < played / 2) && (flags & 5) == 5;

    // CupGroupOverviewEffects gates the fixture panel if this knockout round is in the future, or any
    // descriptor in its +0x15 predecessor chain has not completed all its source rounds.
    int predecessor = int8_t(memory.u8(address + 0x15));
    std::array<bool, kCupEntries> seen_predecessors{};
    while (predecessor != -1) {
        if (!in_descriptor_table(predecessor) || seen_predecessors[size_t(predecessor)])
            invalid("invalid/cyclic predecessor descriptor chain");
        seen_predecessors[size_t(predecessor)] = true;
        const uint32_t pred = cup_entry_address(predecessor);
        if (cup_round(memory, predecessor) < memory.u8(pred + 0x1b)) out.unfinished_predecessor = true;
        predecessor = int8_t(memory.u8(pred + 0x15));
    }
    out.fixtures_available = !out.unfinished_predecessor && (!out.knockout || round <= live_round);
    out.display_mode_toggle_available = out.fixtures_available && out.source_participant_count > 0x10;

    if (out.descriptor_competition == 12) out.scheduled_day_mode = true;
    // Source paints TEXTD1186 and returns before touching team/result/schedule arrays for a
    // blocked future or unfinished stage. Preserve that boundary for incomplete live records.
    if (!out.fixtures_available) return out;

    const uint32_t team_list = memory.pointer(address);
    const uint32_t first_results = memory.pointer(address + 0x0c);
    const uint32_t second_results = memory.pointer(address + 0x10);
    if (team_count != 0 && team_list == 0) invalid("missing Source team list");
    if (team_count != 0 && first_results == 0) invalid("missing Source result list");
    if (out.knockout && second_leg && second_results == 0) invalid("missing Source second-leg results");

    int participant_base = 0;
    std::vector<Fixture> group_fixtures;
    if (out.knockout) {
        participant_base = uint8_t(cup_round_offset(memory, entry, round));
    } else {
        const int table_teams = cup_group_schedule_teams(memory, address);
        if (table_teams <= 0) invalid("unsupported Source group-round schedule table");
        const int rounds_per_leg = rounds / ((flags & 1) + 1);
        if (rounds_per_leg <= 0) invalid("invalid group schedule leg count");
        group_fixtures = generate_fixtures(table_teams, round + 1, rounds_per_leg, 0);
    }

    int row_participants = out.knockout ? out.source_participant_count : team_count;
    int visible_participants = row_participants;
    if (out.knockout && effective_mode == OtherGamesDisplayMode::detail)
        visible_participants = std::min(visible_participants, 0x10);
    const int begin = participant_offset;
    const int end = out.knockout ? std::min(row_participants, begin + visible_participants) : row_participants;
    if (out.scheduled_day_mode && !memory.has(::bmh::layout::CalendarState_FormatError_RuntimeField3)) invalid("missing live Source date for scheduled competition");
    for (int p = out.knockout ? begin : 0; p + 1 < end; p += 2) {
        int home = -1, away = -1, match = p / 2;
        size_t result_slot = 0;
        if (out.knockout) {
            const int slot = participant_base + p;
            if (!memory.has(team_list + uint32_t(slot) * 2 + 1)) invalid("truncated Source knockout team list");
            home = memory.u16(team_list + uint32_t(slot) * 2);
            away = memory.u16(team_list + uint32_t(slot + 1) * 2);
            result_slot = size_t(slot / 2);
        } else {
            const int fixture = p / 2;
            if (size_t(fixture) >= group_fixtures.size()) invalid("group fixture count does not match descriptor");
            const Fixture& f = group_fixtures[size_t(fixture)];
            if (!memory.has(team_list + uint32_t(f.home) * 2 + 1) ||
                !memory.has(team_list + uint32_t(f.away) * 2 + 1)) invalid("truncated Source group team list");
            home = memory.u16(team_list + uint32_t(f.home) * 2);
            away = memory.u16(team_list + uint32_t(f.away) * 2);
            result_slot = size_t(round) * size_t(team_count / 2) + size_t(fixture);
        }
        const uint32_t result_address = first_results + uint32_t(result_slot * 2);
        if (!memory.has(result_address + 1)) invalid("truncated Source result list");
        const uint16_t result = memory.u16(result_address);
        OtherGamesRow row = make_row(game, home, away, result, match);
        if (out.scheduled_day_mode) {
            row.kind = OtherGamesRowKind::scheduled_competition;
            row.played = false;
            row.home_goals = row.away_goals = 0;
            row.scheduled_source_day = uint32_t(memory.u16(::bmh::layout::CalendarState_FormatError_RuntimeField3)) +
                uint32_t(int32_t(int16_t(result)));
        } else if (out.knockout) {
            uint16_t second = 0xcccc;
            const bool second_present = second_results != 0;
            if (second_results != 0) {
                const uint32_t second_address = second_results + uint32_t(result_slot * 2);
                if (!memory.has(second_address + 1)) invalid("truncated Source second-leg result list");
                second = memory.u16(second_address);
            }
            add_second_leg_and_winner(row, second, second_present, second_leg, flags);
        }
        attach_source_values(game, row);
        out.rows.push_back(std::move(row));
    }
    if (!out.knockout && team_count > 2) out.group_tables.push_back(cup_group_table(memory, address));
    return out;
}

}  // namespace bmh
