// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/national.hpp"

#include <algorithm>
#include <cstring>

#include "game/lineup.hpp"
#include "game/match_engine.hpp"
#include "game/month.hpp"
#include "game/weekly.hpp"

namespace bmh {

namespace {

constexpr size_t kManagerRecord = 0xb0c;
constexpr size_t kBoard = 0x840;          // one manager's tactics board (24 x 0x58)
constexpr size_t kBoardSlot = 0x58;
constexpr uint32_t kDesktops = ::bmh::layout::Desktops;   // desktop layouts, 0x360 per manager slot
constexpr size_t kDesktop = 0x360;
constexpr size_t kPoolSlots = 0xb70;      // CareerRuntimeField: the clubs' squads end here
constexpr uint8_t kNominated[4] = {3, 5, 7, 5};  // NationalRuntimeField: keepers, defence, midfield, attack

// the manager's team (record +0x290) as the port caches it
void refresh_manager_teams(GameState& g) {
    for (size_t m = 0; m < g.manager_teams.size(); m++) {
        const size_t at = m * kManagerRecord + 0x290;
        g.manager_teams[m] = int16_t(g.manager_records.at(at) | g.manager_records.at(at + 1) << 8);
    }
}

// a byte of the original's static data, created on first use (not every one is saved)
uint8_t& static_byte(GameState& g, uint32_t address) {
    if (!g.memory.has(address)) g.memory.add_block(address, 1);
    return g.memory.at(address);
}

}  // namespace

void swap_manager_slots(GameState& g, int a, int b) {
    if (a == b) return;
    std::swap_ranges(g.manager_records.begin() + long(size_t(a) * kManagerRecord),
                     g.manager_records.begin() + long(size_t(a + 1) * kManagerRecord),
                     g.manager_records.begin() + long(size_t(b) * kManagerRecord));
    std::swap_ranges(g.tactics.begin() + long(size_t(a) * kBoard), g.tactics.begin() + long(size_t(a + 1) * kBoard),
                     g.tactics.begin() + long(size_t(b) * kBoard));
    if (g.memory.has(kDesktops))
        for (size_t i = 0; i < kDesktop; i++) {
            const uint32_t pa = kDesktops + uint32_t(size_t(a) * kDesktop + i);
            const uint32_t pb = kDesktops + uint32_t(size_t(b) * kDesktop + i);
            const uint8_t t = g.memory.u8(pa);
            g.memory.set8(pa, g.memory.u8(pb));
            g.memory.set8(pb, t);
        }
    refresh_manager_teams(g);
}

int add_to_squad(GameState& g, int manager, int from_slot, int squad_start, WatcomRandom& rand) {
    // FirstFreeSlotSquad(start, 0x18): the first free slot of the squad
    int slot = -1;
    for (int s = squad_start; s < squad_start + 24; s++)
        if (g.squad_slots.at(size_t(s)) == kEmptySlot) {
            slot = s;
            break;
        }
    if (slot < 0) return -1;  // (the original shows the message PendingOffer_FormatError_ExecutableField)
    g.squad_slots.at(size_t(slot)) = g.squad_slots.at(size_t(from_slot));
    new_board_entry(g, manager, slot - squad_start, slot, rand);
    return slot;
}

void new_board_entry(GameState& g, int manager, int index, int slot, WatcomRandom& rand) {
    uint8_t* e = &g.tactics.at(size_t(manager) * kBoard + size_t(index) * kBoardSlot);
    std::memset(e + 7, 0, 3);
    std::memset(e + 0x19, 0, 0x24);
    e[10] = 2;
    for (int k = 0; k < 4; k++) e[2 + k] = index < 2 ? 1 : uint8_t(rand.range(2, 0x27));
    e[0x3f] = 0xff;
    e[0x46] = 0xff, e[0x47] = 0x0f;  // u16 0xfff
    e[0x44] = 0xff, e[0x45] = 0x0f;
    e[0] = 10;
    e[1] = 4;
    e[0xe] = 0x59;
    e[0xf] = uint8_t(rand.range(0x28, 0x3c));
    e[0x11] = 0;
    e[0x43] = 0;
    e[0x4e] = 0, e[0x4f] = 0;
    e[0x3d] = index < 11 ? 0 : index <= 0x10 ? 1 : 2;
    e[0xc] = 0x18;
    // AddToSquad(slot, 0, W(95, 105), &fee, &salary (+0x50), 2 years, no extra)
    const int percent = rand.range(0x5f, 0x69);
    uint32_t fee = 0, salary = 0;
    market_value(g, slot, 0, percent, fee, salary, 2, 0, rand);
    for (int k = 0; k < 4; k++) e[0x50 + k] = uint8_t(salary >> (8 * k));
    std::memset(e + 0x12, 0, 7);
}

bool national_team_day(GameState& g, WatcomRandom& rand, MinuteReport& report) {
    StaticMemory& mem = g.memory;
    const SquadRange national = squad_range(kNoTeam);
    // NationalSquadEmptied: the national squad is emptied
    for (size_t s = national.first; s < national.first + national.count; s++) g.squad_slots.at(s) = kEmptySlot;

    // SwapManagerSlots2 (called whether or not a take-over is already in effect): a manager flagged
    // as Bundestrainer takes the national team over in slot 0. ActiveManager is what the day
    // step's manager loop (DayStepRest) leaves: the number of managers.
    (void)mem;
    (void)report;
    national_coach_take_over(g, int(g.manager_teams.size()), uint16_t(g.date - g.season_start));
    const uint8_t coach = static_byte(g, ::bmh::layout::CoachActive);  // CoachActive
    if (coach == 0) swap_manager_slots(g, 4, 0);  // slot 0 becomes the spare slot 4

    // the strongest available German players per position (NationalRuntimeField of each), kept in
    // ascending order: a stronger one pushes the weakest out
    int16_t best[4][7] = {}, slots[4][7] = {};
    for (size_t s = 0; s < kPoolSlots; s++) {
        const int id = g.squad_slots.at(s) & 0xfff;
        if (id == 0xfff) continue;
        const Player& p = g.players.at(size_t(id));
        const bool fit = (p.w35c >> 27) == 0 || ((p.w364 >> 16) & 0x1f) == 0;
        if (!fit || nationality(g, id) != 0) continue;
        const int pos = int(p.w364 & 3);
        int16_t strength = int16_t(p.strength(false));  // Strength(w35c, 0)
        if (pos > 1) strength = int16_t(strength + ((p.w364 >> 2) & 0x3f) + (((p.w368 >> 18) & 0x3f) >> 1));
        if (s > 0x86f) strength = int16_t((strength * 0xb) / 10);  // (the clubs from slot 0x870 on)
        int i = 0;
        // NationalTeamDay caps the insertion index at the positional quota. Guard
        // before indexing: the full seven-player row has no C++ eighth element.
        while (i < kNominated[pos] && strength > best[pos][i]) i++;
        if (i == 0) continue;
        const int j = i - 1;
        // Int16T: entries 1..j move down one place, the new one takes place j
        for (int k = 0; k < j; k++) best[pos][k] = best[pos][k + 1], slots[pos][k] = slots[pos][k + 1];
        best[pos][j] = strength;
        slots[pos][j] = int16_t(s);
    }
    // PendingOfferAnswer for each, weakest first, into the board of slot 0
    for (int pos = 0; pos < 4; pos++)
        for (int k = 0; k < kNominated[pos]; k++) add_to_squad(g, 0, slots[pos][k], int(national.first), rand);
    // Int2(0x165, 0xe4) with manager 0's option byte (ManagerAutomation) at 1
    const uint8_t option = g.manager_options.at(0);
    g.manager_options.at(0) = 1;
    try {
        auto_lineup(g, kNoTeam, 0xe4);
    } catch (const std::exception& ex) {
        report.fail(std::string("national team line-up: ") + ex.what());
    }
    if (coach == 0) swap_manager_slots(g, 4, 0);
    g.manager_options.at(0) = option;
    return coach != 0;  // (with a Bundestrainer his desk follows: AdvanceTurn's local_28 = 4)
}


// ---------------------------------------------------------------------------------------
// The Bundestrainer (national coach). A manager flagged in his record (+0x21c), every
// manager in the world game without leagues (CompetitionSelection == 2) and the only manager of a
// one-manager game coaches the national team on international days: SwapManagerSlots2 moves his
// slot 0 to the spare slot 4 and puts the national team's record (kept in slot 4 between the
// international periods) into slot 0; NationalCoachHandBack swaps them back. CoachActive tells
// whether the swap is in effect, OriginalManagerCount keeps the number of managers meanwhile.

namespace {

void set_manager_count(GameState& g, int managers) {
    static_byte(g, ::bmh::layout::ManagerCount) = uint8_t(managers);  // ManagerCount
    g.manager_teams.assign(size_t(managers), 0);  // (the typed copy, one entry per manager)
}

}  // namespace

void national_coach_take_over(GameState& g, int active_manager, uint16_t source_season_day) {
    StaticMemory& mem = g.memory;
    int managers = int(g.manager_teams.size());
    static_byte(g, ::bmh::layout::OriginalManagerCount) = uint8_t(managers);                  // OriginalManagerCount = ManagerCount
    static_byte(g, ::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField3) = uint8_t(active_manager);            // ClubPlayerDetail_InvalidArgument_RuntimeField3 = ActiveManager
    // DaysIntoSeason == 1 (the first day after the saved season origin): the national team's
    // mood byte loses its top bit before coach eligibility is checked.
    const bool first_day = source_season_day == 1;
    if (first_day) g.team_morale.at(kNoTeam) &= 0x7f;
    const bool world_only = mem.u32(::bmh::layout::CompetitionSelection) == 2;
    // (the loop bound is ManagerCount itself, which a take-over sets to 1)
    for (int m = 0; m < managers; m++) {
        uint8_t* rec = &g.manager_records.at(size_t(m) * kManagerRecord);
        if (rec[0x21c] == 0 && !world_only && managers != 1) continue;
        // the national team's record (slot 4) gets the coach's portrait (+0x221) and name
        // (+0xa8, StrcpySoHallReplacesItsCurrentTournament = strcpy)
        uint8_t* spare = &g.manager_records.at(4 * kManagerRecord);
        spare[0x221] = rec[0x221];
        for (size_t i = 0;; i++) {
            spare[0xa8 + i] = rec[0xa8 + i];
            if (rec[0xa8 + i] == 0) break;
        }
        managers = 1;
        set_manager_count(g, 1);
        swap_manager_slots(g, 4, 0);                               // SwapManagerSlots(4, 0)
        uint8_t* coach = &g.manager_records.at(0);
        coach[0x290] = uint8_t(kNoTeam & 0xff), coach[0x291] = uint8_t(kNoTeam >> 8);  // CupDraw_FormatError_RuntimeField2 = 0x165
        coach[0x218] = g.manager_records.at(4 * kManagerRecord + 0x218);       // NationalRuntimeField2 = NationalRuntimeField3
        g.manager_teams.at(0) = kNoTeam;
        auto_lineup(g, kNoTeam, 0xe4);                             // Int2(0x165, 0xe4)
        static_byte(g, ::bmh::layout::CoachActive) = 1;
        if (first_day) g.team_morale.at(kNoTeam) |= 0x80;
    }
}

void national_coach_hand_back(GameState& g) {
    if (static_byte(g, ::bmh::layout::CoachActive) == 0) return;
    const int managers = static_byte(g, ::bmh::layout::OriginalManagerCount);
    set_manager_count(g, managers);  // ManagerCount = OriginalManagerCount
    swap_manager_slots(g, 4, 0);     // SwapManagerSlots(4, 0) (refreshes the typed teams)
    static_byte(g, ::bmh::layout::CoachActive) = 0;
}

bool national_coach_active(const GameState& g) { return !g.manager_teams.empty() && g.manager_teams[0] == kNoTeam; }

void national_coach_after_load(GameState& g) {
    // NationalCoachAfterLoad's end, loading as well as saving: the take-over is done again
    if (g.memory.has(::bmh::layout::CoachActive) && g.memory.u8(::bmh::layout::CoachActive) != 0)
        national_coach_take_over(g, g.memory.has(::bmh::layout::ActiveManager) ? g.memory.u8(::bmh::layout::ActiveManager) : 0,
                                 uint16_t(g.date - g.season_start));
}

GameState national_coach_save_form(const GameState& g) {
    // NationalCoachAfterLoad's start: the slots swapped back, CoachActive kept at 1
    GameState s = g;
    if (s.memory.has(::bmh::layout::CoachActive) && s.memory.u8(::bmh::layout::CoachActive) != 0) {
        national_coach_hand_back(s);
        s.memory.set8(::bmh::layout::CoachActive, 1);
    }
    return s;
}

}  // namespace bmh
