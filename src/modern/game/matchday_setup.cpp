// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "modern/game/modern_state.hpp"
#include "modern/game/matchday_setup.hpp"
#include "game/trace.hpp"

#include "modern/game/national.hpp"

#include <algorithm>
#include <stdexcept>
#include <cstdlib>
#include <string>
#include <utility>

#include "modern/game/cup.hpp"
#include "game/lineup.hpp"
#include "game/match.hpp"
#include "modern/game/matchday.hpp"
#include "game/schedule.hpp"
#include "game/messages.hpp"
#include "game/table.hpp"
#include "modern/game/desk_actions.hpp"
#include "modern/game/weekly.hpp"

namespace bmh::modern {

namespace {

constexpr size_t kManagerRecord = 0xB0C;
constexpr size_t kDescriptorSize = 0xD0;
// Competition class per descriptor type (MatchEngineRuntimeField9): which card counters apply.
constexpr uint8_t kCompetitionClass[24] = {0, 1, 2, 2, 2, 3, 3, 3, 3, 255, 0, 255,
                                           4, 255, 255, 255, 255, 255, 255, 1, 0, 0, 0, 0};
// League groups per competition bit 0-4 (CalendarFirstLeague first league, CalendarLeagueCounts count;
// the arrays are indexed with the bit for bits 0-3 and with 4..7 for bit 4).
// (kGroupLeague / kGroupLeagues: schedule.hpp, the calendar groups of the loaded game)
// Crowd interest per descriptor type (MatchdaySetupRuntimeField).
constexpr uint8_t kTypeInterest[24] = {5, 5, 10, 15, 12, 10, 5, 10, 5, 10, 5, 31, 10, 10, 10, 0, 0, 1, 0, 0, 0, 12, 0, 0};
// Number of clubs taking part in league play (CareerSession_DispatchLineup_RuntimeField).
// Squad slots checked by the driver (CareerRuntimeField).

// The attendance tables MatchdaySetup_Attendance_ExecutableField .. MatchdaySetupRuntimeField2 as one block, because the original
// indexes them with league classes 0-7 although they only have four entries: classes 4-7
// (the regional leagues) read the next table. Offsets relative to MatchdaySetup_Attendance_ExecutableField. The dword at
// MatchdaySetupRuntimeField6 is written at run time by the human home path (Attendance at layout field) and is
// only reached here as MatchdaySetup_Attendance_ExecutableField3[4], which is multiplied by 0 for the regional leagues.
constexpr uint8_t kAttendanceTables[0x6a] = {
    0xb8, 0x88, 0x00, 0x00, 0x98, 0x3a, 0x00, 0x00, 0x40, 0x1f, 0x00, 0x00, 0x70, 0x17, 0x00, 0x00,  // MatchdaySetup_Attendance_ExecutableField capacity
    0x10, 0x27, 0x00, 0x00, 0x88, 0x13, 0x00, 0x00, 0xe8, 0x03, 0x00, 0x00, 0xf4, 0x01, 0x00, 0x00,  // MatchdaySetup_Attendance_ExecutableField2 base
    0xf0, 0x00, 0x00, 0x00, 0xb4, 0x00, 0x00, 0x00, 0x96, 0x00, 0x00, 0x00, 0x7d, 0x00, 0x00, 0x00,  // MatchdaySetup_Attendance_ExecutableField3 per strength
    0x00, 0x00, 0x00, 0x00, 0x30, 0x75, 0x00, 0x00, 0x30, 0x75, 0x00, 0x00, 0xa8, 0x61, 0x00, 0x00,  // MatchdaySetupRuntimeField6 stadium steps
    0x20, 0x4e, 0x00, 0x00, 0xd8, 0x59, 0x00, 0x00, 0x10, 0x27, 0x00, 0x00, 0x98, 0x3a, 0x00, 0x00,
    0x20, 0x4e, 0x00, 0x00, 0xa8, 0x61, 0x00, 0x00,                                                  // (a3f0a)
    0x46, 0x30, 0x1d, 0x0e,  // MatchdaySetup_Attendance_ExecutableField4 strength offset
    0xa6, 0x82, 0x50, 0x20,  // MatchdaySetup_Attendance_ExecutableField5 interest divisor
    0x14, 0x12, 0x10, 0x0e,  // MatchdaySetup_Attendance_ExecutableField6 ticket price
    0x0c, 0x0f,              // MatchdaySetupRuntimeField9
    0x6e, 0x3c, 0x1e, 0x0a,  // MatchdaySetup_Attendance_ExecutableField7
};
int32_t table_u32(uint32_t address, int index) {
    size_t at = address - ::bmh::layout::MatchdaySetup_Attendance_ExecutableField + size_t(index) * 4;
    return int32_t(kAttendanceTables[at] | kAttendanceTables[at + 1] << 8 | kAttendanceTables[at + 2] << 16 |
                   uint32_t(kAttendanceTables[at + 3]) << 24);
}
int table_u8(uint32_t address, int index) { return kAttendanceTables[address - ::bmh::layout::MatchdaySetup_Attendance_ExecutableField + size_t(index)]; }

void put32(Bytes& b, size_t o, uint32_t v) {
    for (int i = 0; i < 4; i++) b.at(o + i) = uint8_t(v >> (8 * i));
}

uint32_t rd32(const Bytes& b, size_t o) {
    return uint32_t(b.at(o) | b.at(o + 1) << 8 | b.at(o + 2) << 16) | uint32_t(b.at(o + 3)) << 24;
}
void wr32(Bytes& b, size_t o, uint32_t v) { put32(b, o, v); }
void wr16(Bytes& b, size_t o, uint16_t v) { b.at(o) = uint8_t(v), b.at(o + 1) = uint8_t(v >> 8); }

// TeamLeague2: league of a club, 0 for the special ids above 0x164, 9 outside the leagues.
int league_of(int team) {
    if (team > 0x164) return 0;
    return team_league(team);
}

// RestoreForfeitSquad, called once per side after FinishEvent detects a forfeited fixture.
void restore_forfeit_squad(ModernState& g, int team) {
    const SquadRange range = squad_range(team);
    for (size_t slot_index = range.first; slot_index < range.first + range.count; slot_index++) {
        const uint16_t slot = g.squad_slots.at(slot_index);
        const size_t player = slot & 0x0fff;
        if (player == 0x0fff || player >= g.players.size() || player >= g.player_cards.size()) continue;
        Player& p = g.players[player];
        if ((p.w35c >> 27) != 0) continue;
        const uint16_t cards = g.player_cards[player];
        p.w368 = (p.w368 & 0xf8fc3fffu) |
                 (uint32_t(cards >> 9 & 7) << 24) |
                 (uint32_t(cards >> 5 & 0xf) << 14);
        p.w364 &= 0xffe0ffffu;
        p.w364 |= uint32_t(p.w364 >> 13 & 7) << 16;
    }
}

// BookMoney(manager, attendance, 0): income of the stadium's kiosks and restaurants:
// per facility 0-13 that exists, BalancePlanRuntimeField7[k] * attendance * level / 100 (level of
// facility 0 = its size step), then scaled by W(85, 115) %.
int32_t catering_income(const ModernState& g, int manager, int32_t spectators, WatcomRandom& rand) {
    static const uint8_t kPerVisitor[14] = {0, 10, 10, 3, 5, 15, 3, 3, 3, 3, 2, 2, 2, 2};  // BalancePlanRuntimeField7
    const size_t rec = size_t(manager) * kManagerRecord;
    uint32_t sum = 0;
    for (int k = 0; k < 14; k++) {
        const uint8_t v = g.manager_records.at(rec + size_t(k));
        if (v == 0) continue;
        const uint32_t level = k == 0 ? uint32_t((v - 1) / 0x55 + 1) & 0xFF : 1;  // Add32
        sum += kPerVisitor[k] * uint32_t(spectators) * level / 100;
    }
    if (sum != 0) sum = uint32_t(rand.range(0x55, 0x73)) * sum / 100;
    return int32_t(sum);
}

}  // namespace

namespace {
uint8_t sportschau_capacity(const ModernState& g) {
    const auto capacity = g.memory.has(::bmh::layout::CareerSession_FormatError_RuntimeField) ? g.memory.u8(::bmh::layout::CareerSession_FormatError_RuntimeField) : 0;
    if (capacity > 9) throw std::invalid_argument("Sportschau A8 exceeds the Original nine-match capacity");
    return capacity;
}
}

void reset_sportschau_selection(ModernState& g) {
    const auto capacity = sportschau_capacity(g);
    std::fill_n(g.sportschau_priorities.begin(), capacity, uint8_t(0xff));
    g.sportschau_selection_available = true;
}

bool consider_sportschau_fixture(ModernState& g, const MatchGroup& group,
                                 int home, int away, WatcomRandom& rand) {
    const int setting = modern::sportschau_source_setting_index(g, group);
    if (setting < 0 || setting >= 20 || !g.memory.has(::bmh::layout::Controller_FormatError_ExecutableField2 + uint32_t(setting) * 9) ||
        g.memory.u8(::bmh::layout::Controller_FormatError_ExecutableField2 + uint32_t(setting) * 9) != 0) return false;
    const uint8_t capacity = sportschau_capacity(g);
    int16_t priority = 0;
    for (int team : {home, away}) {
        if (g.memory.has(::bmh::layout::MatchdaySetup_ConsiderSportschauFixture_RuntimeField) && g.memory.u8(::bmh::layout::MatchdaySetup_ConsiderSportschauFixture_RuntimeField) == 0 && g.manager_of(team) >= 0)
            priority = int16_t(priority - 300);
        const auto average = team_average(g, team, 0x1ff, &rand);
        priority = int16_t(priority + ((100 - int(uint16_t(average.value))) >> 1));
        const int position = std::max(0, int(int8_t(modern::table_position(g, team))));
        const int threshold = g.sportschau_club_threshold;
        priority = int16_t(priority + position + (team >= threshold ? 50 : 0) + league_of(team) * 10);
    }
    if (priority <= 0) priority = 1;
    g.sportschau_priorities[capacity] = uint8_t(priority); // low byte, not a clamp
    g.sportschau_homes[capacity] = uint16_t(home);
    for (int i = capacity; i > 0; --i) {
        if (g.sportschau_priorities[size_t(i)] < g.sportschau_priorities[size_t(i - 1)]) {
            std::swap(g.sportschau_priorities[size_t(i)], g.sportschau_priorities[size_t(i - 1)]);
            std::swap(g.sportschau_homes[size_t(i)], g.sportschau_homes[size_t(i - 1)]);
        }
    }
    return true;
}

bool sportschau_selected(const ModernState& g, int home) {
    const auto capacity = sportschau_capacity(g);
    return std::find(g.sportschau_homes.begin(), g.sportschau_homes.begin() + capacity,
                     uint16_t(home)) != g.sportschau_homes.begin() + capacity;
}

void match_weather(MatchGroup& grp, int m, const MatchdayContext& ctx, GameRandom& game, WatcomRandom& rand) {
    const int home = grp.fixtures.at(size_t(m) * 2), away = grp.fixtures.at(size_t(m) * 2 + 1);
    const int region = team_region(home);
    int8_t base = int8_t(month_temperature(ctx.month) + region_temperature(region));
    const uint8_t temperature = uint8_t(rand.range(0, 6) + base - 3);
    grp.desc.at(0x30 + size_t(m)) = temperature;
    int rain = rand.range(0, 5);
    // the temperature is compared as an unsigned byte: only 0 and 1 degrees bring snow
    if (temperature < 2 && rain > 3) rain += 2;
    grp.desc.at(0x50 + size_t(m)) = uint8_t(rain);
    if (int16_t(home) < no_team() && uint16_t(away) < no_team() && !(grp.desc.at(1) & 0x80)) {
        // Range(1): a referee not yet chosen today
        std::vector<uint8_t>& refs = *ctx.referees;
        // OpenBMH deviates from the original: in Modern worlds a referee may take a second match on a day with more matches than referees.
        // Modern: a day can have more matches than referees (larger international leagues on
        // the dates of the German leagues). Once every referee has a match, one may referee a
        // second match that day (DIFFERENCES.md); Classic draws until a free one.
        // The pool is the world's (data format F9): referees 1..size-1, 49 with Classic data.
        const int last = int(refs.size()) - 1;
        bool free_referee = false;
        for (int k = 1; k <= last && !free_referee; ++k) free_referee = !(refs.at(size_t(k)) & 0x80);
        int r;
        do {
            r = game.range(1, last);
        } while (free_referee && (refs.at(size_t(r)) & 0x80));
        refs.at(size_t(r)) |= 0x80;
        grp.desc.at(0x10 + size_t(m)) = uint8_t(r);
    }
}

void prepare_team(ModernState& g, int team, int cls, int index) {
    uint16_t& flags = g.team_flags.at(size_t(team));
    flags = uint16_t((flags & 0xFF0F) | ((index % 2) << 4));
    const SquadRange range = squad_range(team);
    for (size_t s = range.first; s < range.first + range.count; s++) {
        uint16_t& slot = g.squad_slots.at(s);
        slot &= 0xCFFF;
        if ((slot & 0x0FFF) == 0x0FFF) break;
        const int player = slot & 0x0FFF;
        Player& p = g.players.at(size_t(player));
        if (p.w35c >> 27) continue;  // not available (loaned out or similar)
        uint16_t& cards = g.player_cards.at(size_t(player));
        // keep the league yellow cards of this season (bits 5-8 and 9-11)
        cards = uint16_t((cards & 0xF01F) | ((p.w368 >> 14) & 0xF) << 5);
        cards = uint16_t(cards | ((p.w368 >> 24) & 7) << 9);
        uint8_t injury = 0;
        if (cls != 0) {
            // the card counters of this competition replace the league ones for the match
            p.w368 &= 0xF8FC3FFFu;
            const uint8_t k = uint8_t(cls - 1);
            if (k <= 3) {
                uint32_t v = k == 0 ? p.w364 >> 8 : k == 1 ? p.w364 >> 10 : k == 2 ? uint32_t(cards >> 14) : uint32_t(cards >> 12);
                p.w368 |= (v & 3) << 14;
            }
            if (int8_t(cls) != -1 && ((p.w368 >> 14) & 3) > 1) injury = 1;  // banned
        } else {
            injury = uint8_t((p.w364 >> 16) & 0xF);
        }
        p.w364 &= 0xFFFF0FFFu;
        p.w364 |= ((p.w364 >> 16) & 7) << 13;
        p.w364 &= 0xFFE0FFFFu;
        if (injury) {
            slot &= 0x0FFF;
            p.w364 |= uint32_t(injury) << 16;
        }
    }
}

// Postponed league matches (Postponed): 40 entries of 6 bytes - the day of the replay
// (u16, 0 = free entry), the league round (+2), the league (+3) and the match (+4).
constexpr uint32_t kPostponed = ::bmh::layout::Postponed;
constexpr int kPostponedEntries = 0x28;
constexpr uint32_t kCalendar = ::bmh::layout::CompetitionCalendar;  // u16 per season day
// MatchdaySetupRuntimeField3 / MatchdaySetupRuntimeField4: first and last day of the winter break (static data, no
// replays are scheduled in between)
constexpr int kWinterBreakFirst = 0x96, kWinterBreakLast = 0xc8;
constexpr uint8_t kPostponedMark = 0xcd;  // result byte of a postponed match

uint16_t calendar_entry(const ModernState& g, int day) { return g.memory.u16(kCalendar + uint32_t(day) * 2); }

// RescheduleLeagueMatch (with param_5 = -0x33): finds a replay day for match `m` of round `round` in
// `league`, starting five days after `day`. A day qualifies when it and both neighbours are
// free in the calendar (the day before may carry bit 4), or when it already is a replay day
// (0x4000); a coin toss of the game generator may still refuse it, as may eight replays or a
// team clash on that day. Records the replay in entry `slot`, marks the result byte and the
// calendar day.
bool schedule_replay(ModernState& g, int day, int league, int m, int slot, int round, GameRandom& game) {
    for (int date = day + 5;; date++) {
        if (date > 0x11e) return false;
        if (date >= kWinterBreakFirst && date <= kWinterBreakLast) date = kWinterBreakLast + 1;
        const bool free = (calendar_entry(g, date) == 0 && (calendar_entry(g, date - 1) & 0xffef) == 0 &&
                           calendar_entry(g, date + 1) == 0) ||
                          calendar_entry(g, date) == 0x4000;
        if (!free) continue;
        bool ok = true;
        if (game.range(0, 1) == 0) {
            int same_day = 0;
            for (int e = 0; e < kPostponedEntries && ok; e++) {
                const uint32_t at = kPostponed + uint32_t(e) * 6;
                if (g.memory.u16(at) != date) continue;
                if (++same_day == 8) {
                    ok = false;
                    break;
                }
                // The original generates the other replay's fixtures with the parameters of
                // `league`, whatever league that entry belongs to, and compares only the low
                // bytes of the team ids.
                const std::vector<Fixture> other = generate_fixtures(league_table_teams(league), g.memory.u8(at + 2) + 1,
                                                                     league_matchdays(league) / 2, league_first_team(league));
                const std::vector<Fixture> own =
                    generate_fixtures(league_table_teams(league), round + 1, league_matchdays(league) / 2, league_first_team(league));
                const Fixture& o = other.at(g.memory.u8(at + 4));
                const Fixture& f = own.at(size_t(m));
                const uint8_t oh = uint8_t(o.home), oa = uint8_t(o.away), fh = uint8_t(f.home), fa = uint8_t(f.away);
                if (fh == oh || fa == oa || fa == oh || fh == oa) ok = false;
            }
        } else {
            ok = false;
        }
        if (ok) {
            const uint32_t at = kPostponed + uint32_t(slot) * 6;
            g.memory.set16(at, uint16_t(date));
            g.memory.set8(at + 2, uint8_t(round));
            g.memory.set8(at + 3, uint8_t(league));
            g.memory.set8(at + 4, uint8_t(m));
            g.results.at(size_t(m) + size_t(round) * 10 + size_t(league) * 0x17C) = kPostponedMark;
            g.memory.set8(kCalendar + uint32_t(date) * 2 + 1, g.memory.u8(kCalendar + uint32_t(date) * 2 + 1) | 0x40);
            return true;
        }
    }
}

// WinterPostponements: winter postponements of a league matchday (leagues 0-4, season days
// 0x6e..0xe6, unless MatchdaySetup_WinterPostponements_RuntimeField2 is set). The number of postponed matches peaks around day
// 0xaa and is capped by the option byte MatchdaySetup_WinterPostponements_RuntimeField.
void winter_postponements(ModernState& g, int day, int league, GameRandom& game, WatcomRandom& rand) {
    if (g.memory.u8(::bmh::layout::MatchdaySetup_WinterPostponements_RuntimeField2) != 0 || league >= 5 || day <= 0x6d || day >= 0xe7) return;
    const int16_t distance = int16_t(std::abs(day - 0xaa) / 10);
    int16_t most = int16_t(9 - distance);
    if (most != 0 && rand.range(0, 1) != 0) most = int16_t(8 - distance);
    int16_t count = int16_t(rand.range(0, most));
    if (int16_t(g.memory.u8(::bmh::layout::MatchdaySetup_WinterPostponements_RuntimeField)) < count) count = int16_t(g.memory.u8(::bmh::layout::MatchdaySetup_WinterPostponements_RuntimeField));
    const int round = g.league_rounds.at(size_t(league));
    int16_t before;
    do {
        before = count;
        if (count == 0) return;
        for (int slot = 0; slot < kPostponedEntries; slot++) {
            if (g.memory.u16(kPostponed + uint32_t(slot) * 6) != 0) continue;
            const int m = rand.range(0, kLeagueSizes[league] / 2 - 1);
            if ((g.results.at(size_t(m) + size_t(round) * 10 + size_t(league) * 0x17C) & 0xcf) == 0xcc &&
                schedule_replay(g, day, league, m, slot, round, game)) {
                count--;
                break;
            }
        }
    } while (before != count);
}

// ClubName3(team, buffer): the longest word of a club's name (separators MatchdaySetupRuntimeField5
// " %.1-"; of words with the same length the last one), written to `out` at `at`; a word
// ending in "ER" loses the "ER" ("HER": only the "R").
void longest_word(const ModernState& g, int team, std::vector<uint8_t>& out, size_t at) {
    static constexpr char kSeparators[] = {' ', '%', '.', '1', '-'};
    const std::string name = g.club_name(team);
    int start = 0, longest = 0;
    for (int i = 0; i <= int(name.size()); i++) {
        const char c = i < int(name.size()) ? name[size_t(i)] : '\0';
        for (char sep : kSeparators) {
            if (sep != c && c != '\0') continue;
            if (longest <= i - start) {
                longest = i - start;
                for (int k = 0; k < longest; k++) out.at(at + size_t(k)) = uint8_t(name[size_t(start + k)]);
                out.at(at + size_t(longest)) = 0;
            }
            start = i + 1;
        }
    }
    if (longest >= 3 && out.at(at + size_t(longest) - 1) == 'R' && out.at(at + size_t(longest) - 2) == 'E')
        out.at(at + size_t(longest + (out.at(at + size_t(longest) - 3) == 'H') - 2)) = 0;
}

// AddCompetition(id): puts a competition on the list of running ones (ActiveSelectors, 8 bytes,
// 0x14 = free); false if it is there already.
bool add_competition(ModernState& g, int id) {
    for (uint32_t i = 0; i < 8; i++) {
        if (int8_t(g.memory.u8(::bmh::layout::ActiveSelectors + i)) == id) return false;
        if (g.memory.u8(::bmh::layout::ActiveSelectors + i) == 0x14) {
            g.memory.set8(::bmh::layout::ActiveSelectors + i, uint8_t(id));
            return true;
        }
    }
    return true;
}

// IndoorTournamentDraw (with Cup_AdvanceCup_RuntimeField4 = CupRuntimeField = 0): the draw of the indoor tournament
// after the first half of the season. Eight clubs: at random one of the regional leagues
// (teams 90-137), 5-7 of the first league and the rest of the second, none twice; the second
// club drawn names the tournament. A human manager's club gets a starting bonus of 100 000.
// The order is then shuffled (PrepareEvent) and the tournament's strength class computed
// (IndoorTournamentReset).
void indoor_tournament_reset(ModernState& g) {
    StaticMemory& mem = g.memory;
    if (mem.u8(::bmh::layout::Cup_AdvanceCup_RuntimeField4) == 0)  // the first tournament: Masters points and invitations
        for (uint32_t i = 0; i < 0x50; i++) mem.set8(::bmh::layout::Cup_AdvanceCup_ExecutableField + i, 0), mem.set8(::bmh::layout::MatchdaySetup_IndoorTournamentDraw_RuntimeField + i, 0);
    if (g.tournament_name.size() < 0x24) g.tournament_name.resize(0x24, 0);
    const std::string in = message_text(g, ::bmh::layout::MatchdaySetup_IndoorTournamentReset_TextPointer);  // "In "
    for (size_t i = 0; i <= in.size() && i < 0x24; i++) g.tournament_name[i] = i < in.size() ? uint8_t(in[i]) : 0;
    for (uint32_t i = 0; i < 0x20; i++) mem.set8(::bmh::layout::MatchdayGroupWorkspace + i, 0xcc);
    for (int i = 0x19; i < 0x1e; i++) mem.set8(::bmh::layout::MatchdaySetup_IndoorTournamentReset_RuntimeField + uint32_t(i) * 0x28, 0);  // played counters
}

void indoor_tournament_draw(ModernState& g, GameRandom& game, WatcomRandom& rand) {
    StaticMemory& mem = g.memory;
    constexpr uint32_t kTeams = ::bmh::layout::Teams, kInvitations = ::bmh::layout::MatchdaySetup_IndoorTournamentDraw_RuntimeField;  // u16 per club < 0x28
    const int held = mem.u8(::bmh::layout::Cup_AdvanceCup_RuntimeField4);  // tournaments played so far (Cup_AdvanceCup_RuntimeField4)
    modern::indoor_tournament_reset(g);
    uint8_t attempts = 0;
    const int guest = rand.range(0, 1);
    if (guest != 0) mem.set16(kTeams, uint16_t(rand.range(90, 137)));
    int count = guest != 0;
    int n = rand.range(5, 7);
    uint8_t lo = 0, hi = uint8_t(kLeagueSizes[0]);
    uint32_t previous;
    do {
        for (int s = 0; s < n; s++) {
            bool again;
            uint16_t t;
            do {
                again = false;
                attempts++;
                t = uint16_t(rand.range(lo, hi - 1));
                if (int16_t(t) < 0x28 && int16_t(mem.u16(kInvitations + uint32_t(t) * 2)) > 1) {
                    again = true;
                    if (attempts > 0x14) attempts = 0xf, hi++;
                } else {
                    if (count == 1) {
                        // the second club drawn hosts the tournament, not a host of before
                        longest_word(g, t, g.tournament_name, 3);
                        for (int k = 0; k < held; k++)
                            if (t == mem.u8(::bmh::layout::LiveMatchday_FormatError_RuntimeField14 + uint32_t(k))) again = true;
                        mem.set8(::bmh::layout::LiveMatchday_FormatError_RuntimeField14 + uint32_t(held), uint8_t(t));
                    }
                    for (int k = 0; k < count; k++)
                        if (mem.u16(kTeams + uint32_t(k) * 2) == t) again = true;
                }
            } while (again);
            if (count < 8) mem.set16(kTeams + uint32_t(count++) * 2, t);
        }
        n = 8 - count;
        previous = hi;
        lo = hi;
        hi = uint8_t(hi + kLeagueSizes[1]);
    } while (previous < uint32_t(kLeagueSizes[0] + kLeagueSizes[1]));
    std::string title;
    for (size_t i = 3; i < g.tournament_name.size() && g.tournament_name[i]; i++) title.push_back(char(g.tournament_name[i]));
    for (uint32_t k = 0; k < 8; k++) {
        const uint16_t t = mem.u16(kTeams + k * 2);
        const int manager = g.manager_of(t);
        if (manager >= 0) {
            post_composed(g, manager, std::string(kIndoorInvitationText) + title);
            modern::book_money(g, manager, -100000, true, true);
        }
        if (t < 0x28) mem.set16(kInvitations + uint32_t(t) * 2, uint16_t(mem.u16(kInvitations + uint32_t(t) * 2) + 1));
    }
    for (uint32_t i = 0; i < 8; i++) {  // PrepareEvent(8, teams)
        const uint32_t j = uint32_t(game.range(0, 7));
        const uint16_t a = mem.u16(kTeams + i * 2);
        mem.set16(kTeams + i * 2, mem.u16(kTeams + j * 2));
        mem.set16(kTeams + j * 2, a);
    }
    int sum = 0;  // IndoorTournamentReset
    for (uint32_t k = 0; k < 8; k++) {
        const int t = mem.u16(kTeams + k * 2);
        const int league = t > 0x164 ? 0 : team_league(t);  // TeamLeague2
        if (league < 2) sum += (0x26 - modern::table_position(g, t)) - kLeagueSizes[0] * league;
        else if (league < 5) sum += 1;
        else sum += 0x19;
    }
    mem.set8(::bmh::layout::Cup_AdvanceCup_RuntimeField3, uint8_t(int16_t(sum + 7) / 8));
}

uint32_t day_competitions(const ModernState& g, uint16_t entry) {
    uint32_t v = entry & 0x7fff;
    if (g.memory.u8(::bmh::layout::CalendarState_FormatError_RuntimeField2) != 0) v = entry & 0x7fef;
    // (German: the indoor option off removes the indoor days; the English CycleManagerOptions2 has no
    // such test - its 0x4002 days are the League Cup)
    if (!kEnglishCompetitions && g.memory.u8(::bmh::layout::CalendarState_FormatError_RuntimeField) != 0 && v == 0x4002) v = 0;
    if ((v & 0x4001) == 0x4001) v = (v & 0xff0) | 0x8000;
    if (v == 0x4002) v = 0x10000;
    if (v == 0x4004) v = 0x40000;
    if (v == 0x4006) v = (g.memory.u8(::bmh::layout::CompetitionSelection) & 2) ? 0x800000 : 0;
    // the international tournament of the year (bits 9-12) turns with the season; the English
    // CycleManagerOptions2 counts from 1995 and adds 3 ((season + 3) % 4) to stay in step
    if (v & 0x1e00) v &= 1u << (0xc - (g.season + (kEnglishCompetitions ? 3 : 0)) % 4);
    return v;
}

void cycle_manager_options(ModernState& g) {
    for (size_t k = 0; k < g.manager_teams.size(); k++) {
        uint8_t option = g.manager_options.at(k * 5);
        const uint8_t enabled = g.manager_records.at(k * kManagerRecord + 0x218 + (g.indoor ? 1 : 0));
        if (option != 0 && enabled != 0)
            while (!((enabled >> (option - 1)) & 1)) {
                option++;
                if (option > 8) option = 1;
            }
        g.manager_options.at(k * 5) = option;
    }
}

// DayConflict(team, other, calendar entry, day): whether a friendly day concerns the team.
// A calendar entry other than 0 and 0x4001 without the top bit always does; otherwise the
// friendly list (FriendlyDays: 30 days, FriendlyTeams: the two teams of each) is searched
// for a friendly on `day` with the national team, `team` or `other`.
static bool national_friendly(const ModernState& g, int team, int other, int16_t entry, int day) {
    if (entry != 0 && entry != 0x4001 && !(uint16_t(entry) & 0x8000)) return true;
    const StaticMemory& mem = g.memory;
    if (!mem.has(::bmh::layout::FriendlyDays) || !mem.has(::bmh::layout::FriendlyTeams)) return false;
    for (int i = 0; i < 0x3c; i += 2) {
        if (int16_t(mem.u16(::bmh::layout::FriendlyDays + uint32_t(i / 2) * 2)) != day) continue;
        for (int k = 0; k < 2; k++) {
            const int t = int16_t(mem.u16(::bmh::layout::FriendlyTeams + uint32_t(i + k) * 2));
            if (t == no_team() || t == team || t == other) return true;
        }
    }
    return false;
}

bool matchday_needs_manager_desks(const ModernState& g, uint16_t calendar_entry,
                                  const std::vector<MatchGroup>& groups) {
    if (g.manager_teams.empty()) return false;
    const auto& mem = g.memory;
    const bool opening = g.season_day() == 0;
    const bool closing = mem.has(::bmh::layout::CareerSession_RuntimeError_RuntimeField4) && mem.u8(::bmh::layout::CareerSession_RuntimeError_RuntimeField4) != 0;
    int request = mem.u8(::bmh::layout::MatchdaySetup_MatchdayNeedsManagerDesks_RuntimeField) + int(opening) + int(closing);
    if (mem.u32(::bmh::layout::CompetitionSelection) == 2) request = 0;
    if (modern::day_competitions(g, calendar_entry) == 0x8000 &&
        national_friendly(g, no_team(), 500, int16_t(calendar_entry), g.season_day()))
        request = 4;

    // MatchdayNeedsManagerDesks assigns local_44 only to actual participants. League results
    // must still be 0xcc; setup has already changed postponed 0xce to 0xcd.
    // Cup descriptors instead obtain the context from their entry's +0x18.
    for (const auto& group : groups) {
        const uint32_t competition = rd32(group.desc, 0xcc);
        for (size_t fixture = 0; fixture < size_t(group.matches()); ++fixture) {
            if (competition < 0x14) {
                const size_t result = size_t(competition) * 0x17c +
                    size_t(g.league_rounds.at(competition)) * 10 + fixture;
                if (g.results.at(result) != 0xcc) continue;
            } else if (int8_t(mem.u8(competition + 0x18) + 5) >= 0x1e) {
                continue;
            }
            if (g.manager_of(group.fixtures.at(fixture * 2)) >= 0 ||
                g.manager_of(group.fixtures.at(fixture * 2 + 1)) >= 0)
                request = 1;
        }
    }
    return (mem.has(::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField) && mem.u8(::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField) != 0) ||
        (request != 0 && (request == 4 || !groups.empty() || closing || opening));
}

std::vector<MatchGroup> setup_matchday(ModernState& g, uint32_t competitions, const MatchdayContext& ctx,
                                       GameRandom& game, WatcomRandom& rand, MinuteReport& report) {
    std::vector<MatchGroup> groups;
    // AdvanceTurn first: after the first half of the Bundesliga season (17 rounds) the
    // indoor tournament (competition 0xb) is drawn once, unless switched off (CalendarState_FormatError_RuntimeField)
    if (g.league_rounds.at(0) == 0x11 && g.memory.u8(::bmh::layout::CalendarState_FormatError_RuntimeField) == 0 && add_competition(g, 0xb)) {
        g.memory.set8(::bmh::layout::Cup_AdvanceCup_RuntimeField4, 0);  // Cup_AdvanceCup_RuntimeField4: the first tournament of the season
        turn_step = "setup: indoor tournament draw";
        modern::indoor_tournament_draw(g, game, rand);
    }
    // AdvanceTurn keeps the day's competition bits (DayCompetitions of the calendar entry) in
    // SavedCompetition; the savegame carries the value of the last prepared matchday.
    g.memory.set32(::bmh::layout::SavedCompetition, competitions);
    // the Bundestrainer: on an international match day (competition bits 0x1e00) or a friendly
    // day of the national team (local_28 == 4), not on season day 0 and not after the season
    // end, he takes the national team over (SwapManagerSlots2) unless he already has; on any other
    // day a take-over in effect ends (NationalCoachHandBack). (ActiveManager at this point: what the day
    // step's manager loop left, the number of managers.)
    if (!ctx.national_team_day) {
        const bool national_day =
            (competitions & 0x1e00) ||
            (competitions == 0x8000 &&
             national_friendly(g, no_team(), 500, int16_t(g.memory.u16(::bmh::layout::CompetitionCalendar + uint32_t(ctx.season_day) * 2)), ctx.season_day));
        if (national_day && !(g.memory.has(::bmh::layout::CareerSession_RuntimeError_RuntimeField4) && g.memory.u8(::bmh::layout::CareerSession_RuntimeError_RuntimeField4)) && ctx.season_day != 0) {
            if (!modern::national_coach_active(g))
                modern::national_coach_take_over(g, int(g.manager_teams.size()), uint16_t(ctx.season_day));
        } else if (modern::national_coach_active(g)) {
            modern::national_coach_hand_back(g);
        }
    }
    g.indoor = false;
    for (uint8_t& r : *ctx.referees) r &= 0x7F;  // Range(0)
    for (int bit = 0; bit < 0x13; bit++) {
        if (!((competitions >> bit) & 1)) continue;
        if (bit >= 5) {
            turn_step = "setup: cup groups";
            modern::setup_cup_groups(g, bit - 5, groups, report);  // SetupCupGroups(bit - 5)
            continue;
        }
        const int from = bit, to = bit < 4 ? bit + 1 : 8;
        for (int k = from; k < to; k++) {
            // FixturesFromGenerateFixtures(k)
            for (int league = kGroupLeague[k]; league < kGroupLeague[k] + kGroupLeagues[k]; league++) {
                // Modern: leagues of different sizes share a calendar group; a league whose rounds
                // are all played sits out the remaining dates of the group (docs/new_engine).
                if (g.league_rounds.at(size_t(league)) >= league_matchdays(league)) continue;
                turn_step = "setup: winter postponements";
                winter_postponements(g, ctx.season_day, league, game, rand);
                const int round = g.league_rounds.at(size_t(league));
                const int size = kLeagueSizes[league];
                MatchGroup grp;
                grp.desc = modern::group_descriptor(g, groups.size());
                for (const Fixture& f : generate_fixtures(league_table_teams(league), round + 1, league_matchdays(league) / 2, league_first_team(league))) {
                    grp.fixtures.push_back(f.home);
                    grp.fixtures.push_back(f.away);
                }
                const size_t at = size_t(league) * 0x17C + size_t(round) * 10;
                for (int m = 0; m < size / 2; m++) {
                    uint8_t& r = g.results.at(at + size_t(m));
                    if ((r & 0xCF) == 0xCE) r = 0xCD;  // postponed -> to be replayed later
                }
                grp.desc.at(0) = 0;  // league
                grp.desc.at(1) = 2;
                grp.desc.at(2) = uint8_t(size / 2);
                put32(grp.desc, 0xcc, uint32_t(league));
                // MatchdayNeedsManagerDesks: a human manager's club notes its side (record +0x28a)
                for (int i = 0; i < size; i++)
                    for (size_t mgr = 0; mgr < g.manager_teams.size(); mgr++)
                        if (g.manager_teams[mgr] == grp.fixtures[size_t(i)]) g.manager_records.at(mgr * kManagerRecord + 0x28a) = uint8_t(i % 2);
                groups.push_back(grp);
            }
            // before the 17th Bundesliga matchday every manager is reminded that the balance
            // sheets go to the DFB at the end of the first half of the season (MatchdaySetup_SetupMatchday_TextPointer)
            if (k == 0 && g.league_rounds.at(0) == 0x10) post_text(g, -1, text_id(::bmh::layout::MatchdaySetup_SetupMatchday_TextPointer));
        }
    }
    turn_step = "setup: teams";
    for (MatchGroup& grp : groups) {
        const int cls = kCompetitionClass[grp.desc.at(0)];
        for (int i = 0; i < grp.matches() * 2; i++) {
            if (i < grp.matches()) modern::match_weather(grp, i, ctx, game, rand);
            const int team = grp.fixtures.at(size_t(i));
            turn_team = team;
            turn_step = "setup: prepare team";
            modern::prepare_team(g, team, cls, i);
            try {
                turn_step = "setup: line-up";
                auto_lineup(g, team, 100);
            } catch (const std::invalid_argument& e) {
                report.fail(e.what());
            }
        }
    }
    for (int team = 0; team < league_clubs(); team++)
        if (g.team_flags.at(size_t(team)) & 1) auto_lineup(g, team, 100);
    turn_step = "setup: options";
    modern::cycle_manager_options(g);
    modern::store_group_descriptors(g, groups);
    return groups;
}

int match_mode_bits(const ModernState& g, const MatchGroup& grp, const MatchdayContext& ctx, MinuteReport& report) {
    const int type = grp.desc.at(0);
    uint8_t bits = kTypeInterest[type];
    if (type == 0) {
        const int league = int(le32(grp.desc, 0xcc));
        const int matchdays = league_matchdays(league), rounds = g.league_rounds.at(size_t(league));
        // the last four rounds draw more spectators
        if (rounds > matchdays - 4) bits = uint8_t(bits + uint8_t(0xF - uint8_t((matchdays - rounds) * 3)));
    } else if (type != 9 && type != 0xb) {
        // cups: more interest in the last round of a stage, even more in the final
        const uint32_t e = le32(grp.desc, 0xcc);
        if (g.memory.u8(e + 0x1d) + 1 >= g.memory.u8(e + 0x1b)) {
            bits = uint8_t(bits + 5);
            if (g.memory.u8(e + 0x16) == 0) bits = uint8_t(bits + 10);
        }
    }
    int result = bits;
    if (ctx.weekday == 4 && type == 10) result |= 0x20;
    if (!(grp.desc.at(1) & 2)) result |= 0x80;
    if (type == 1) result |= 0x100;
    if (type > 1 && type < 5) result |= 0x200;
    return result;
}

int attendance(ModernState& g, int home, int away, int bits, int temperature, int rain, int competition_type,
               GameRandom& game, WatcomRandom& rand, MinuteReport& report) {
    const int team[2] = {home, away};
    int strength[2] = {0, 0}, position[2] = {0, 0}, league[2] = {0, 0};  // local_6c[2..3], local_7c
    int best_half_sum = 0;                                                 // CareerSession_FormatError_ExecutableField3 (home side)
    for (int i = 1; i >= 0; i--) {
        const TeamAverage avg = team_average(g, team[i], 0x1f7, &rand);
        strength[i] = uint16_t(avg.value);
        best_half_sum = avg.best_half_sum;
        position[i] = modern::table_position(g, team[i]);
        if (position[i] < 0) position[i] = rand.range(2, 5);
        if (uint16_t(team[i]) >= 0x5a) {
            league[i] = 0;
        } else {
            league[i] = league_of(team[i]);
            if (league[i] > 2) league[i]--;
        }
    }
    const int home_manager = g.manager_of(home), away_manager = g.manager_of(away);  // local_20, local_14
    const size_t rec = size_t(home_manager) * 0xB0C;  // EDI / puVar10: the home manager's record
    auto r8 = [&](size_t off) { return int(g.manager_records.at(rec + off)); };
    auto r16 = [&](size_t off) { return int(g.manager_records.at(rec + off) | g.manager_records.at(rec + off + 1) << 8); };
    auto r32 = [&](size_t off) { return int32_t(rd32(g.manager_records, rec + off)); };
    auto w32 = [&](size_t off, int32_t v) { wr32(g.manager_records, rec + off, uint32_t(v)); };

    int32_t n;  // ESI: the attendance
    int price;  // local_38: ticket price
    if ((home_manager < 0 || home != no_team()) && !(bits & 0x80)) {
        const int lh = league[0], la = league[1];
        n = table_u32(::bmh::layout::MatchdaySetup_Attendance_ExecutableField2, lh);
        int prestige = 0x32, performance = 0x226;  // local_24, local_2c
        if (home_manager < 0) {
            price = table_u8(::bmh::layout::MatchdaySetup_Attendance_ExecutableField6, lh);
        } else {
            if (strength[0] < best_half_sum - 10) n += 2000;  // a team with stars draws more
            prestige = r8(0x211);
            performance = r16(0x8aa);
            price = r8(0x26e);
        }
        int sum = 0, sum_above = 0;  // local_50, local_30
        for (int i = 0; i < 2; i++) {
            sum += strength[i];
            int16_t above = int16_t(strength[i] - table_u8(::bmh::layout::MatchdaySetup_Attendance_ExecutableField4, league[i]));
            if (above < 0) above = 0;
            if (above > 30) above = 30;
            sum_above += above;
        }
        const int32_t from_strength = sum_above * table_u32(::bmh::layout::MatchdaySetup_Attendance_ExecutableField3, lh);
        int x = (40 - position[0] - position[1]) * 2;  // local_5c
        if (lh + la == 0 && rand.range(60, 66) < x) x = x * rand.range(110, 125) / 100;
        prestige = uint8_t(prestige / (lh + 1));
        performance = uint16_t(performance / 5 / (lh + 1));
        x += (performance >> 2) + (prestige >> 2);
        n = (x + 80) * (n + from_strength) / 100;
        if (la < lh)
            n += lh * -2000 + la * -500 + 10000;
        else if (lh < la)
            n += (4 - la) * -4000;
        int capacity;  // local_3c
        if (home_manager < 0) {
            const int a = table_u32(::bmh::layout::MatchdaySetup_Attendance_ExecutableField, lh);
            capacity = rand.range(a - (a >> 2), a + (a >> 2)) / 500 * 500;
            if ((bits & 0x180) == 0x180) capacity = 76000;
        } else {
            // the manager's stadium: facilities, size steps, roofs against the rain
            int covered = 0;  // local_64: covered share of the stands
            const int facilities = modern::facility_rating(g, home_manager, &covered, &rand) - table_u8(::bmh::layout::MatchdaySetup_Attendance_ExecutableField7, lh);  // local_44
            static const int32_t kStadiumSteps[10] = {0, 30000, 30000, 25000, 20000, 23000, 10000, 15000, 20000, 25000};
            for (int k = 0; k < 10; k++) {
                const int v = r8(size_t(k));
                // MatchdaySetupRuntimeField6[0] is overwritten with the step of the stadium size
                const int32_t step = k == 0 ? int32_t(v / 0x55 * 25000) : kStadiumSteps[k];
                if (step < n && (v == 0 || k == 0)) n -= n / rand.range(10, 20);
            }
            const int wet = int(uint32_t(100 - covered) * uint32_t(uint8_t(rain)) * 8) / 100;
            n = (((n >> 1) + (100 - wet) * (n >> 1) / 100) * (int8_t(temperature) + 0x5a)) / 100 >> 1;
            n += n * rand.range(facilities + 0x5a, facilities + 0x6e) / 100;
            capacity = r8(0x5c) * 1000;
        }
        const int level_bonus = (9 - g.level) * 2;  // Range3 returns 1 in this version
        const int form = rand.range(level_bonus + 0x55, level_bonus + 0x5f);
        int interest = bits & 0x1f;
        if (interest == 0x1f) {
            interest = -10;
            bits = 0;
        }
        if (interest > 20) interest = 20;
        int demand = sum * ((bits & 0x1f) + 100) / 100;
        if (demand < 0) demand = 0;
        if (demand > 180) demand = 180;
        int price_index = price * 100 / table_u8(::bmh::layout::MatchdaySetup_Attendance_ExecutableField6, lh);
        int demand_index = demand * 100 / table_u8(::bmh::layout::MatchdaySetup_Attendance_ExecutableField5, lh);
        if (price_index < demand_index) price_index += price_index >> 3;
        if (demand_index < 30) demand_index = 30;
        if (demand_index > 180) demand_index = 180;
        n = ((interest * 3 + 100) * (n * form / 100) / 100) * (demand_index + 100 - price_index) / 100;
        int standing = 1, seated = 1;  // local_4c, local_48: places of the open stands
        if (home_manager >= 0) {
            // stands that differ much from an eighth of the stadium put people off
            int unevenness = 0;  // local_60
            int unit = r8(0x5c) >> 3;
            if (unit == 0) unit = 1;
            for (int k = 0; k < 8; k++) {
                const int places = r8(0x34 + size_t(k)) == 0 ? r8(0x4c + size_t(k)) : 0;
                unevenness += std::abs(100 - places * 100 / unit);
                if (r8(0x3c + size_t(k)) & 1)
                    seated += places;
                else
                    standing += places;
            }
            unevenness >>= 3;
            if (rand.range(0x8c, 0xb4) < unevenness) {
                unevenness = unevenness < 0x96 ? 0x96 : unevenness > 0xfa ? 0xfa : unevenness;
                unevenness -= 0x96;
                n -= (unevenness >> 1) * n / 100;
            }
        }
        if (bits & 0x20) n = rand.range(n - (n >> 2), n + (n >> 2));
        if (n < 100) n = rand.range(100, 500);
        n = (n + 100) / 100 * 100;
        if (n > capacity) n = capacity;
        if (home_manager >= 0 && capacity * rand.range(0x4b, 100) / 100 < n && rand.range(0, r8(0x210) + 1) == 0) {
            // a full house: few seats make riots likely (record +0x289 then costs the team at
            // kick-off, see rate_side); the damage is paid by the club
            const int share = seated * 100 / standing;
            g.manager_records.at(rec + 0x289) = share < 0x1e;
            if (share < game.range(0x50, 0x78) && r8(0x43) < 3 && rand.range(0, r8(0x43) << 2) == 0) {
                const int32_t damage = rand.range(n / 0x1e, n / 10) * 100;
                post_composed(g, home_manager, message_text(g, ::bmh::layout::MatchdaySetup_Attendance_TextPointer) + format_amount(g, damage) + message_text(g, ::bmh::layout::CareerSession_Int32T_TextPointer));
                modern::book_money(g, home_manager, damage, true, true);
            }
        }
    } else {
        n = (rand.range(3, 10) + (bits & 0x1f)) * 2500;
        if ((bits & 0x40) && n > 74000) n = 74000;
        price = 0x16;
    }
    int32_t income = price * n;  // local_5c: ticket income
    if (bits & 0x180) income >>= 1;
    if (home_manager + away_manager != -2 && (bits & 0x100)) {
        // cup matches with a human club: TV and advertising rights raise the (shared) gate
        // by W(12, 15) / 10; both human managers get the message "... erhöhten sich Ihre
        // Einnahmen auf <new> (+ <extra>)." (MatchdaySetup_Attendance_TextPointer2, MatchdaySetupRuntimeField7 " (+ ", MatchdaySetupRuntimeField8 ").")
        const int32_t raised = income * rand.range(12, 15) / 10;  // MatchdaySetupRuntimeField9, MatchdaySetupRuntimeField10
        const std::string text = message_text(g, ::bmh::layout::MatchdaySetup_Attendance_TextPointer2) + format_amount(g, raised) + " (+ " +
                                 format_amount(g, raised - income) + ").";
        int64_t mask = 0x80000000LL;
        if (home_manager >= 0) mask |= 1LL << home_manager;
        if (away_manager >= 0) mask |= 1LL << away_manager;
        post_composed(g, mask, text);
        income = raised;
    }
    int kind = ((bits & 0x200) ? 2 : 0) + ((bits & 0x100) ? 1 : 0);  // 0 league, 1 cup, 2 other
    if (kind > 2) {
        report.fail("attendance: invalid competition bits");
        kind = 2;
    }
    if (home_manager >= 0 && r16(0x290) != no_team()) {
        modern::book_money(g, home_manager, -income, true);
        w32(0x868, r32(0x868) + income);
        const int32_t catering = catering_income(g, home_manager, n, rand);  // BookMoney
        modern::book_money(g, home_manager, -catering, true);
        w32(0x38c + 2 * 4, catering);  // indexed by a stale loop counter (2) in the original
        w32(0x86c, r32(0x86c) + catering);
        if (kind != 0 || competition_type == 0 || competition_type == 0xe || competition_type == 0xa) {
            g.manager_records.at(rec + 0x28c + size_t(kind))++;
            w32(0x748 + size_t(kind) * 4, r32(0x748 + size_t(kind) * 4) + n);
            w32(0x754 + size_t(kind) * 4, r32(0x754 + size_t(kind) * 4) + income);
            if (n > r32(0x718 + size_t(kind) * 8)) {  // record attendance
                w32(0x718 + size_t(kind) * 8, n);
                w32(0x71c + size_t(kind) * 8, int32_t(g.date));
                wr16(g.manager_records, rec + 0x8c0 + size_t(kind) * 2, g.club_ids.at(size_t(away)));
            }
            if (income > r32(0x730 + size_t(kind) * 8)) {  // record income
                w32(0x730 + size_t(kind) * 8, income);
                w32(0x734 + size_t(kind) * 8,
                    int32_t(uint32_t(g.season_day()) + uint32_t(g.memory.u16(::bmh::layout::CalendarState_FormatError_RuntimeField3))));
                wr16(g.manager_records, rec + 0x8c6 + size_t(kind) * 2, g.club_ids.at(size_t(away)));
            }
            if (kind == 0) {
                // the last 19 league home matches: attendance (+0x340) and ticket price (+0x270)
                auto& m = g.manager_records;
                int k = int8_t(r8(0x283));
                if (k < 0x13) {
                    m.at(rec + 0x283) = uint8_t(k + 1);
                } else {
                    std::copy(m.begin() + long(rec + 0x344), m.begin() + long(rec + 0x344 + 0x48), m.begin() + long(rec + 0x340));
                    std::copy(m.begin() + long(rec + 0x271), m.begin() + long(rec + 0x271 + 0x12), m.begin() + long(rec + 0x270));
                    // (only 0x12 bytes of the u32 list +0x38c are moved)
                    std::copy(m.begin() + long(rec + 0x390), m.begin() + long(rec + 0x390 + 0x12), m.begin() + long(rec + 0x38c));
                    k = 0x12;
                }
                w32(0x340 + size_t(k) * 4, n);
                m.at(rec + 0x270 + size_t(k)) = uint8_t(price);
                // season ticket holders (+0x2ac) have paid already: 70 % of their tickets
                // are taken back from the gate income
                const int holders = std::min(r16(0x2ac), int(n));
                const int32_t refund = price * (holders * 0x46 / 100);
                modern::book_money(g, home_manager, refund, true);
                w32(0x754, r32(0x754) - refund);
                w32(0x868, r32(0x868) - refund);
            }
        }
    }
    if (away_manager >= 0 && (bits & 0x180)) {
        // a shared gate (cups, bits 7/8): the human away club gets the same half and counts
        // the match in its attendance statistics (no catering, no records)
        const size_t away_rec = size_t(away_manager) * 0xB0C;
        auto& m = g.manager_records;
        if ((m.at(away_rec + 0x290) | m.at(away_rec + 0x291) << 8) != no_team()) {
            modern::book_money(g, away_manager, -income, true);
            wr32(m, away_rec + 0x868, rd32(m, away_rec + 0x868) + uint32_t(income));
            m.at(away_rec + 0x28c + size_t(kind))++;
            wr32(m, away_rec + 0x748 + size_t(kind) * 4, rd32(m, away_rec + 0x748 + size_t(kind) * 4) + uint32_t(n));
            wr32(m, away_rec + 0x754 + size_t(kind) * 4, rd32(m, away_rec + 0x754 + size_t(kind) * 4) + uint32_t(income));
        }
    }
    return n;
}

namespace {

// AutoLineup: a match with a human team (morale byte bit 7) is shown live, unless the
// option LiveDeskDisabled is set or 4 matches are already chosen. Returns true if chosen (the
// original also loads the club colours then, which is display only).
bool select_live_match(ModernState& g, int home, int away) {
    if (!(g.team_morale.at(size_t(home)) & 0x80) && !(g.team_morale.at(size_t(away)) & 0x80)) return false;
    for (int t : g.live_matches)
        if (t == home) return false;
    const bool no_live = g.memory.has(::bmh::layout::LiveDeskDisabled) && g.memory.u8(::bmh::layout::LiveDeskDisabled) != 0;
    if (no_live || g.live_matches.size() >= 4) return false;
    g.live_matches.push_back(home);
    return true;
}

// KickOff(own manager, own team, other manager, other team, 50, rate): man marking.
// The other team's midfielders and forwards in the line-up are ranked (forwards first,
// then strength, depth preference, shooting skill and experience); each of them is marked
// with a probability of 50 % (+30/20/10 % for the best three). The marker is drawn from the
// own defenders and midfielders not marking yet, ranked by strength, tackling skill and
// how close their board places are; each candidate in turn takes the job with `rate` %.
// Human managers record the pairs on their tactics boards (slot +0x44 = the player the
// slot marks, +0x46 = the player marking the slot); computer teams just draw. A human
// manager only takes part with option byte 3 (MatchdaySetupRuntimeField11) set; his rate comes from
// record +0x8ac / 16 + 40.
void man_marking(ModernState& g, int own_manager, int own, int other_manager, int other, int chance, int rate,
                 WatcomRandom& rand) {
    const int kLeagueTeams = league_clubs();  // CareerSession_DispatchLineup_RuntimeField
    if (g.indoor || own >= kLeagueTeams || other >= kLeagueTeams || (own_manager < 0 && other_manager < 0)) return;
    if (other_manager >= 0)
        for (size_t s = 0; s < 24; s++) {
            const size_t at = size_t(other_manager) * 0x840 + s * 0x58 + 0x46;
            g.tactics.at(at) = 0xFF, g.tactics.at(at + 1) = 0x0F;
        }
    if (own_manager >= 0) {
        if (!g.manager_option(own_manager, 3)) return;
        for (size_t s = 0; s < 24; s++) {
            const size_t at = size_t(own_manager) * 0x840 + s * 0x58 + 0x44;
            g.tactics.at(at) = 0xFF, g.tactics.at(at + 1) = 0x0F;
        }
        const size_t rec = size_t(own_manager) * 0xB0C + 0x8ac;
        rate = uint8_t((int16_t(g.manager_records.at(rec) | g.manager_records.at(rec + 1) << 8) >> 4) + 0x28);
    }
    bool marking[24] = {};  // local_70: own slots already marking
    const SquadRange own_range = squad_range(own), other_range = squad_range(other);
    int16_t targets[24];
    uint32_t target_key[24];
    int n = 0;  // local_38
    for (size_t s = 0; s < other_range.count; s++) {
        const uint16_t slot = g.squad_slots.at(other_range.first + s);
        if (!(slot & kSlotInLineup)) continue;
        const Player& p = g.players.at(slot & 0x0FFF);
        const int position = int(p.w364 & 3);
        if (position < 2) continue;
        targets[n] = int16_t(s);
        target_key[n] = uint32_t(((position - 2) << 6) + int16_t(p.strength(false)) + int(p.w35c >> 21 & 7) * 8 +
                                 int(p.w368 >> 18 & 0x3F) * 4 + int(p.w364 >> 2 & 0x3F) * 8);
        n++;
    }
    if (n == 0) return;
    quicksort_by_key(targets, target_key, n);
    for (int k = 0; k < n; k++) {
        const int bonus = k < 3 ? (3 - k) * 10 : 0;
        if (!(100 - chance < rand.range(0, 100) + bonus)) continue;
        const int target = targets[k];  // local_34
        int tx = 0, ty = 0;
        g.formation_position(other, target, tx, ty);
        int16_t candidates[24];
        uint32_t candidate_key[24];
        int m = 0;  // local_20
        for (size_t s = 0; s < own_range.count; s++) {
            const uint16_t slot = g.squad_slots.at(own_range.first + s);
            if (marking[s] || !(slot & kSlotInLineup)) continue;
            const Player& p = g.players.at(slot & 0x0FFF);
            const int position = int(p.w364 & 3);
            if (position != 1 && position != 2) continue;
            int ox = 0, oy = 0;
            g.formation_position(own, int(s), ox, oy);
            candidates[m] = int16_t(s);
            candidate_key[m] = uint32_t(int16_t(p.strength(false)) + int(p.w368 >> 8 & 3) * 0x10 +
                                        int16_t((0x19 - std::abs(tx - ox)) + std::abs(ty - oy)) * 0x10);
            m++;
        }
        if (m == 0) return;
        quicksort_by_key(candidates, candidate_key, m);
        for (int j = 0; j < m; j++) {
            if (!(rand.range(0, 100) < rate)) continue;
            const int marker = candidates[j];
            if (other_manager >= 0) {
                const uint16_t id = g.squad_slots.at(own_range.first + size_t(marker)) & 0x0FFF;
                const size_t at = size_t(other_manager) * 0x840 + size_t(target) * 0x58 + 0x46;
                g.tactics.at(at) = uint8_t(id), g.tactics.at(at + 1) = uint8_t(id >> 8);
            }
            if (own_manager >= 0) {
                const uint16_t id = g.squad_slots.at(other_range.first + size_t(target)) & 0x0FFF;
                const size_t at = size_t(own_manager) * 0x840 + size_t(marker) * 0x58 + 0x44;
                g.tactics.at(at) = uint8_t(id), g.tactics.at(at + 1) = uint8_t(id >> 8);
            }
            marking[marker] = true;
            break;
        }
    }
}

}  // namespace

void kick_off(ModernState& g, std::vector<MatchGroup>& groups, const MatchdayContext& ctx, WatcomRandom& rand,
              GameRandom& game, MinuteReport& report, bool select_sportschau, uint16_t club_threshold) {
    // LiveSceneVisible = 1 - CareerSession_DispatchLiveTactic_RuntimeField3, set by the caller of DayCompetitions2 right after the
    // matchday setup; MatchdaySetupRuntimeField12 = 0 (no live match chosen yet).
    g.live_scene = uint8_t(1 - (g.memory.has(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField3) ? g.memory.u8(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField3) : 0)) != 0;
    g.live_matches.clear();
    g.sportschau_selection_available = false;
    g.sportschau_club_threshold = club_threshold;
    if (select_sportschau) reset_sportschau_selection(g);
    for (size_t s = 0; s < transfer_list_slot() && s < g.squad_slots.size(); s++) {
        const uint16_t slot = g.squad_slots[s];
        if ((slot & 0x0FFF) != 0x0FFF) g.players.at(slot & 0x0FFF).w364 &= ~0x1000u;  // not played yet
    }
    // tactics board byte 0x10: 0 starts, 0x7f on the bench, 0x7e not in the squad
    for (size_t k = 0; k < g.manager_teams.size(); k++) {
        const SquadRange range = squad_range(g.manager_teams[k]);
        for (size_t s = range.first; s < range.first + range.count; s++) {
            const uint16_t slot = g.squad_slots.at(s);
            if ((slot & 0x0FFF) == 0x0FFF) break;
            g.tactics.at(k * 0x840 + (s - range.first) * 0x58 + 0x10) = slot & 0x8000 ? 0 : slot & 0x4000 ? 0x7f : 0x7e;
        }
    }
    for (MatchGroup& grp : groups) {
        const int n = grp.matches();
        const int league = int(le32(grp.desc, 0xcc));
        put32(grp.desc, 0xb8, 0);
        for (size_t i = 0xbc; i < 0xcc; i++) grp.desc.at(i) = 0;  // bookings, sendings-off, crowd, goals (Assign)
        grp.records.assign(size_t(n) * 18, 0);
        grp.end_minute.assign(size_t(n), 0);
        grp.spectators.assign(size_t(n), 0);
        uint32_t void_mask = 0;
        if (grp.desc.at(0) != 0) {
            // cups: no league result bytes; the results of this leg start at 0:0
            grp.results.clear();
            grp.cup_results.assign(size_t(n), 0);
        } else {
        grp.results.assign(size_t(n), 0);
        const size_t at = size_t(league) * 0x17C + size_t(g.league_rounds.at(size_t(league))) * 10;
        for (int m = 0; m < n; m++) {
            uint8_t& r = g.results.at(at + size_t(m));
            if (r == 0xCC)
                r = 0;  // 0:0 before kick-off
            else
                void_mask |= 1u << m;
            grp.results[size_t(m)] = r;
        }
        }
        put32(grp.desc, 0xb8, void_mask);
        grp.kickoff_void_mask = void_mask;
        for (int m = 0; m < n; m++) {
            if (void_mask & (1u << m)) continue;
            const int home = grp.fixtures.at(size_t(m) * 2), away = grp.fixtures.at(size_t(m) * 2 + 1);
            const bool live = select_live_match(g, home, away);
            if (!live && select_sportschau) consider_sportschau_fixture(g, grp, home, away, rand);
            int forfeit_side = -1;
            for (int side = 0; side < 2; side++) {
                // the original swaps home and away after each pass
                const int t = side ? away : home, opponent = side ? home : away;
                const int own_manager = g.manager_of(t), other_manager = g.manager_of(opponent);
                if ((own_manager >= 0 || other_manager >= 0) && (other_manager < 0 || g.manager_option(other_manager, 3))) {
                    const int rate = rand.range(0x46, 0x50);
                    man_marking(g, own_manager, t, other_manager, opponent, 0x32, rate, rand);
                }
                const TeamAverage fit = team_average(g, t, 0x1f1, &rand);
                if ((t < league_clubs() || t == no_team()) && (fit.players & 0xFF) < 8) {
                    // FinishEvent sets this fixture's local void bit and BuildSideRecords writes
                    // the awarded 0:2/2:0 score before continuing to the next fixture.
                    forfeit_side = side;
                    const int manager = g.manager_of(t);
                    if (manager >= 0) modern::book_money(g, manager, 200000, true, true);  // BookMoney2
                    if (g.memory.has(::bmh::layout::MatchdaySetup_KickOff_RuntimeField + uint32_t(t) * 2))
                        g.memory.set8(::bmh::layout::MatchdaySetup_KickOff_RuntimeField + uint32_t(t) * 2,
                                      uint8_t(g.memory.u8(::bmh::layout::MatchdaySetup_KickOff_RuntimeField + uint32_t(t) * 2) + 1));
                }
            }
            if (forfeit_side >= 0) {
                restore_forfeit_squad(g, home);
                restore_forfeit_squad(g, away);
                void_mask |= 1u << m;
                grp.kickoff_void_mask |= 1u << m;
                put32(grp.desc, 0xb8, void_mask);
                const int home_goals = forfeit_side == 0 ? 0 : 2;
                const int away_goals = forfeit_side == 0 ? 2 : 0;
                if (grp.desc.at(0) == 0) {
                    const size_t at = size_t(league) * 0x17C +
                                      size_t(g.league_rounds.at(size_t(league))) * 10 + size_t(m);
                    const uint8_t score = uint8_t((home_goals << 4) | away_goals);
                    g.results.at(at) = score;
                    grp.results.at(size_t(m)) = score;
                } else {
                    grp.cup_results.at(size_t(m)) = uint16_t((home_goals << 5) | away_goals);
                }
                continue;
            }
            const int bits = modern::match_mode_bits(g, grp, ctx, report);
            int spectators;
            if (g.indoor)
                spectators = 10000;
            else
                spectators = modern::attendance(g, home, away, int16_t(bits), int8_t(grp.desc.at(0x30 + size_t(m))),
                                        grp.desc.at(0x50 + size_t(m)), grp.desc.at(0), game, rand, report);
            put32(grp.desc, 0xc4, le32(grp.desc, 0xc4) + uint32_t(spectators));
            grp.spectators[size_t(m)] = uint32_t(spectators);
            MatchSetup setup;
            setup.home = home;
            setup.away = away;
            setup.match_index = m;
            setup.climate_effect = (grp.desc.at(1) & 2) != 0;
            setup.temperature = int8_t(grp.desc.at(0x30 + size_t(m)));
            setup.spectators = spectators;
            setup.month = ctx.month;
            const std::array<SideRecord, 2> sides = build_side_records(g, setup, rand, &game);
            for (int s = 0; s < 2; s++)
                for (int b = 0; b < 9; b++) grp.records[size_t(m) * 18 + size_t(s) * 9 + size_t(b)] = sides[size_t(s)].b[size_t(b)];
        }
    }
    modern::store_group_descriptors(g, groups);
}

}  // namespace bmh::modern

namespace bmh::modern {

std::vector<int> shuffle_manager_order(ModernState& g, GameRandom& game) {
    constexpr uint32_t kOrder = ::bmh::layout::NextDeskManager;  // u16 per manager
    const int n = int(g.manager_teams.size());
    std::vector<int> order(static_cast<size_t>(n));
    for (int i = 0; i < n; i++) order[size_t(i)] = g.memory.has(kOrder) ? g.memory.u16(kOrder + uint32_t(i) * 2) : i;
    for (int i = 0; i < n; i++) std::swap(order[size_t(i)], order[size_t(game.range(0, n - 1))]);
    if (g.memory.has(kOrder))
        for (int i = 0; i < n; i++) g.memory.set16(kOrder + uint32_t(i) * 2, uint16_t(order[size_t(i)]));
    return order;
}

}  // namespace bmh::modern

namespace bmh::modern {

Bytes group_descriptor(const ModernState& g, size_t index) {
    constexpr uint32_t kGroups = ::bmh::layout::MatchdayGroupDescriptors;
    Bytes d(kDescriptorSize, 0);
    const uint32_t at = kGroups + uint32_t(index) * kDescriptorSize;
    if (g.memory.has(at))
        for (size_t i = 0; i < kDescriptorSize; i++) d[i] = g.memory.u8(at + uint32_t(i));
    return d;
}

void store_group_descriptors(ModernState& g, const std::vector<MatchGroup>& groups, bool with_log) {
    constexpr uint32_t kGroups = ::bmh::layout::MatchdayGroupDescriptors;
    for (size_t gi = 0; gi < groups.size() && gi < 6; gi++) {
        const uint32_t at = kGroups + uint32_t(gi) * kDescriptorSize;
        if (!g.memory.has(at)) continue;
        const Bytes& d = groups[gi].desc;
        auto copy = [&](size_t from, size_t to) {
            for (size_t i = from; i < to; i++) g.memory.set8(at + uint32_t(i), d.at(i));
        };
        copy(0, 4);        // type, flags, matches
        copy(0x10, 0x70);  // referees, temperatures, rain
        copy(0xb8, 0xcc);  // void mask, attendance
        copy(0x94, 0x96);  // event log entries
        // cup groups keep their fixtures in the descriptor (SetupCupGroups)
        if (d.at(0) != 0 && !(d.at(1) & 4)) copy(0x88, 0x88 + size_t(d.at(2)) * 4);
        if (with_log) g.memory.set16(at + 0x94, uint16_t(groups[gi].log.size()));
        // league number, or the guest pointer to the cup table entry
        const uint32_t v = le32(d, 0xcc);
        g.memory.set32(at + 0xcc, d.at(0) == 0 ? v : v + kGuestDataBase);
    }
}

}  // namespace bmh::modern

namespace bmh::modern {

namespace {

// MatchScore (read): the current score of match m as the original reads it, including the
// "not played" markers (0xCC -> 12:12, 0xCCCC -> 6:12).
void raw_score(const ModernState& g, const MatchGroup& grp, int m, int& home, int& away) {
    if (grp.desc.at(0) == 0) {
        const int league = int(le32(grp.desc, 0xcc));
        const uint8_t r = g.results.at(size_t(league) * 0x17C + size_t(g.league_rounds.at(size_t(league))) * 10 + size_t(m));
        home = r >> 4, away = r & 0xF;
    } else {
        const uint16_t r = grp.cup_results.at(size_t(m));
        home = (r >> 5) & 0x1F, away = r & 0x1F;
    }
}

}  // namespace

void build_pools_coupon(ModernState& g, const std::vector<MatchGroup>& groups, WatcomRandom* rand) {
    g.pools_open = true;  // PoolsOpenFlag = 0xcc
    int n = 0;
    for (const MatchGroup& grp : groups) {
        for (int m = 0; m < grp.matches() && n != 0x16; m++) {
            int home = 0, away = 0;
            raw_score(g, grp, m, home, away);
            if (!((home == 0xc || home == 6) && away == 0xc)) continue;
            const int a = uint16_t(team_average(g, grp.fixtures.at(size_t(m) * 2), 0x1ff, rand).value);
            const int b = uint16_t(team_average(g, grp.fixtures.at(size_t(m) * 2 + 1), 0x1ff, rand).value);
            if (std::abs(a - b) >= 0x12) continue;
            for (int s = 0; s < 2; s++) g.memory.set16(::bmh::layout::DeskActions_SubmitPoolsCoupon_RuntimeField + uint32_t(n++) * 2, uint16_t(grp.fixtures.at(size_t(m) * 2 + size_t(s))));
        }
        if (n == 0x16) break;
    }
    g.memory.set8(::bmh::layout::CareerSession_DispatchToto_RuntimeField, uint8_t(n >> 1));  // CareerSession_DispatchToto_RuntimeField: matches on the coupon
}

void evaluate_pools_coupon(ModernState& g, const std::vector<MatchGroup>& groups) {
    if (!g.pools_open) return;  // (PoolsOpenFlag != 0xcc: evaluated already)
    g.pools_open = false;
    int k = 0;
    for (const MatchGroup& grp : groups) {
        for (int m = 0; m < grp.matches() && k < 0xb; m++) {
            if (grp.fixtures.at(size_t(m) * 2) != int(g.memory.u16(::bmh::layout::DeskActions_SubmitPoolsCoupon_RuntimeField + uint32_t(k) * 4))) continue;
            int home = 0, away = 0;
            modern::match_score(grp, m, home, away);
            g.memory.set8(::bmh::layout::MatchdaySetup_EvaluatePoolsCoupon_RuntimeField + uint32_t(k), uint8_t(away < home ? 1 : home == away ? 0 : 2));
            k++;
        }
        if (k >= 0xb) break;
    }
    g.memory.set8(::bmh::layout::CareerSession_DispatchToto_RuntimeField, uint8_t(k));  // CareerSession_DispatchToto_RuntimeField: matches evaluated
}

void settle_pools_coupon(ModernState& g, const std::vector<MatchGroup>& groups) {
    StaticMemory& mem = g.memory;
    if (!mem.has(::bmh::layout::Coupon) || !mem.has(::bmh::layout::DeskActions_SubmitPoolsCoupon_RuntimeField)) return;
    // the coupon's matches among today's (in the order of the coupon, home team at +0)
    int k = 0;
    for (const MatchGroup& grp : groups) {
        for (int m = 0; m < grp.matches() && k < 0xb; m++)
            if (grp.fixtures.at(size_t(m) * 2) == int(mem.u16(::bmh::layout::DeskActions_SubmitPoolsCoupon_RuntimeField + uint32_t(k) * 4))) k++;
        if (k >= 0xb) break;
    }
    if (mem.has(::bmh::layout::CareerSession_DispatchToto_RuntimeField)) mem.set8(::bmh::layout::CareerSession_DispatchToto_RuntimeField, uint8_t(k));
    if (k <= 10) return;
    static constexpr uint8_t kQuota[12] = {11, 10, 0, 20, 11, 10, 0, 1, 0, 10, 20, 50};  // MatchdaySetupRuntimeField13
    if (g.pools.size() < g.manager_teams.size()) g.pools.resize(g.manager_teams.size());
    for (size_t m = 0; m < g.manager_teams.size(); m++) {
        const uint32_t at = ::bmh::layout::Coupon + uint32_t(m) * 0x14;
        auto& extra = g.pools[m].extra;
        if (mem.u8(at + 3) & 0x80) mem.set32(at, 0), extra.clear();
        const uint32_t stake = mem.u32(at);
        if ((stake & 0x7fffffff) == 0) continue;
        // Modern: every column costs the stake, every one beyond the first 10 % more
        // (docs/new_engine/toto.md); with one column this is the original booking.
        const uint32_t cost = pools_cost(stake, 1 + extra.size());
        if (!modern::book_money(g, int(m), int32_t(cost), false, true)) {
            post_composed(g, int(m), message_text(g, ::bmh::layout::MatchdaySetup_SettlePoolsCoupon_TextPointer));
        } else {
            auto hits_of = [&](auto tip) {
                int hits = 0;
                for (uint32_t i = 0; i < 0xb; i++)
                    if (tip(i) == mem.u8(::bmh::layout::MatchdaySetup_EvaluatePoolsCoupon_RuntimeField + i)) hits++;
                return hits;
            };
            const int hits = hits_of([&](uint32_t i) { return mem.u8(at + 8 + i); });
            uint32_t win = hits > 8 ? stake * kQuota[hits] : 0;
            std::vector<uint8_t> column_hits{uint8_t(hits)};
            for (const auto& column : extra) {
                const int h = hits_of([&](uint32_t i) { return column[i]; });
                column_hits.push_back(uint8_t(h));
                if (h > 8) win += stake * kQuota[h];
            }
            mem.set32(at + 4, win);
            mem.set8(at + 0x13, uint8_t(hits));
            g.pools[m].hits = std::move(column_hits);
            g.pools[m].paid = cost, g.pools[m].won = win;
            modern::book_money(g, int(m), -int32_t(win), true, true);
        }
        mem.set32(at, 0);  // (also clears the "settled" bit 7 of byte 3 just set)
        extra.clear();
    }
}

}  // namespace bmh::modern
