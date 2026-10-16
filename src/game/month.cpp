// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/month.hpp"
#include "game/trace.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "game/lineup.hpp"
#include "game/matchday.hpp"
#include "game/messages.hpp"
#include "game/prediction.hpp"
#include "game/schedule.hpp"
#include "game/season.hpp"
#include "game/weekly.hpp"
#include "game/human_buyer_sale.hpp"

namespace bmh {

namespace {

constexpr size_t kRecord = 0xB0C;       // manager record
constexpr size_t kBoard = 0x840;        // tactics board of a manager
constexpr size_t kBoardSlot = 0x58;
constexpr uint32_t kTransferList = ::bmh::layout::EditorList;
constexpr uint32_t kListEntry = 0x58;
constexpr int kPoolFirst = 0xb70;       // CareerRuntimeField: the transfer list (40 slots + 4)
constexpr int kFreeAgents = 0xb9c;      // CareerRuntimeField2: free agents (80 slots)
constexpr int kForeignAgents = 0xbec;   // MonthRuntimeField: foreign free agents (20 slots)
constexpr int kPoolEnd = 0xc00;         // LargeSquadClubCount
constexpr int kBigSquadClubs = 90;      // TeamsWithSquads
constexpr int kLeagueClubs = 0x8a;      // CareerSession_DispatchLineup_RuntimeField

// MonthRuntimeField2: development steps by strength / 10; the ten bytes after it (PlayerCard_PlayerCardMarketValue_RuntimeField)
// are the extra value above 500 points.
constexpr uint8_t kDevelopment[20] = {6, 6, 6, 5, 5, 5, 4, 4, 3, 1, 10, 15, 20, 30, 40, 45, 55, 75, 85, 95};
// BalancePlanRuntimeField5: value of the facilities (maintenance per month), u16 per facility.
constexpr uint16_t kFacilityValue[22] = {2000, 3000, 3000, 8000, 4000, 3000, 2500, 2500, 2500, 2500, 2600,
                                         2600, 2600, 2600, 13000, 9000, 9000, 8000, 1000, 1500, 1500, 2000};
// BalancePlanRuntimeField6: monthly income of facilities 0-5 per level.
constexpr uint16_t kFacilityIncome[6] = {0, 5000, 5000, 4800, 2400, 8000};
// MonthRuntimeField3: share of the new list entries per kind (free agents, big clubs, foreign
// agents, small clubs), percent.
constexpr int16_t kListShare[4] = {47, 35, 10, 8};
// MonthRuntimeField4 / MonthRuntimeField5: players a club wants per position (big / small squads).
constexpr int8_t kWantedBig[4] = {2, 6, 7, 5}, kWantedSmall[4] = {2, 4, 5, 4};

uint32_t rd32(const std::vector<uint8_t>& v, size_t o) {
    return uint32_t(v.at(o) | v.at(o + 1) << 8 | v.at(o + 2) << 16) | uint32_t(v.at(o + 3)) << 24;
}
void wr32(std::vector<uint8_t>& v, size_t o, uint32_t x) {
    for (int i = 0; i < 4; i++) v.at(o + size_t(i)) = uint8_t(x >> (8 * i));
}
void add32(std::vector<uint8_t>& v, size_t o, int32_t x) { wr32(v, o, rd32(v, o) + uint32_t(x)); }

// BoughtEntry: index + 1 of a player in the table of players bought for later / on loan
// (Career_RemoveJoinLoans_RuntimeField), 0 if none.
int bought_entry(const GameState& g, int player) {
    for (int k = 0; k < 200; k++) {
        const uint32_t e = g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + uint32_t(k) * 4);
        if (e != 0 && int(e & 0xFFF) == player) return k + 1;
    }
    return 0;
}

// PlayerOnTransferListFirstPoolSlots: index + 1 of the list entry of the player in an absolute squad slot (the
// first 44 pool slots), 0 if he is not on the list.
int list_index(const GameState& g, int slot) {
    const int player = g.squad_slots.at(size_t(slot)) & 0xFFF;
    for (int s = kPoolFirst; s < kPoolFirst + 0x2c; s++)
        if ((g.squad_slots[size_t(s)] & 0xFFF) == player) return s - kPoolFirst + 1;
    return 0;
}

// FirstFreeSlotSquad(start, count): first empty squad slot from `start`, -1 if none.
int free_slot(const GameState& g, int start, int count) {
    for (int s = start; s < start + count && s < int(g.squad_slots.size()); s++)
        if (g.squad_slots[size_t(s)] == 0xFFF) return s;
    return -1;
}

// ListPlayer: puts the player in an absolute squad slot on the transfer list: the first
// free list slot, asking price and demand percentages, 5-8 weeks, the market value; human
// managers with a matching search profile are told.
bool list_player(GameState& g, int slot, WatcomRandom& rand, MinuteReport& report) {
    StaticMemory& mem = g.memory;
    const int player = g.squad_slots.at(size_t(slot)) & 0xFFF;
    if (bought_entry(g, player) != 0) {
        report.fail("transfer list: the player is bought for later (internal error 0x38)");
        return false;
    }
    if (list_index(g, slot) != 0) {
        report.fail("transfer list: the player is listed already (ListPlayer message box)");
        return false;
    }
    // (the original passes 0xb70 + 0x28 as the count: the search runs on past the list)
    const int free = free_slot(g, kPoolFirst, kPoolFirst + 0x28);
    if (free == -1) return false;  // message box MonthRuntimeField6
    if (free >= kPoolFirst + 0x2c) {
        report.fail("transfer list: the free slot lies beyond the list (the original overwrites memory)");
        return false;
    }
    g.squad_slots[size_t(free)] = uint16_t(player);
    const uint32_t e = kTransferList + uint32_t(free - kPoolFirst) * kListEntry;
    if (slot < kPoolFirst) {
        const int seller = slot_team(slot);
        uint16_t field = uint16_t(seller);
        mem.set16(e + 8, field);
        if (g.manager_of(seller) >= 0) mem.set8(e + 9, uint8_t(mem.u8(e + 9) | 0x80));
        mem.set8(e + 0x54, uint8_t(rand.range(0x4b, 0x7a)));
        mem.set8(e + 0x55, uint8_t(rand.range(0x5c, 100)));
        if (seller >= kBigSquadClubs) mem.set8(e + 0x54, uint8_t(mem.u8(e + 0x54) + rand.range(5, 0xf)));
    } else {
        mem.set16(e + 8, 0xFFF);
        mem.set8(e + 0x54, uint8_t(rand.range(0x46, 0x69)));
        mem.set8(e + 0x55, uint8_t(rand.range(0x3c, 0x60)));
    }
    mem.set8(e + 0x56, uint8_t(rand.range(5, 8)));
    uint32_t fee = 0, salary = 0;
    market_value(g, slot, 0, mem.u8(e + 0x54), fee, salary, 1, 0, rand);
    mem.set32(e + 0x2c, fee);
    mem.set32(e + 0x30, salary);
    for (int b = 0; b < 4; b++) mem.set16(e + uint32_t(b) * 2, 0xFFFF);
    for (size_t m = 0; m < g.manager_teams.size(); m++) search_profile_news(g, int(m), e, g.squad_slots[size_t(free)]);
    return true;
}

// PickFreeAgent(from, to): a random free agent in the pool slots from..to-1 who is not on
// the list and not bought for later.
int pick_free_agent(const GameState& g, int from, int to, WatcomRandom& rand, MinuteReport& report) {
    std::vector<int> slots;
    for (int s = from; s < to; s++) {
        const int p = g.squad_slots.at(size_t(s)) & 0xFFF;
        if (p != 0xFFF && list_index(g, s) == 0 && bought_entry(g, p) == 0) slots.push_back(s);
    }
    if (slots.empty()) {
        report.fail("transfer list: no free agent (internal error 0x26)");
        return -1;
    }
    return slots[size_t(rand.range(0, int(slots.size()) - 1))];
}

// InPseudoSquad: the player of a slot also plays in the squad of the pseudo club 0x165.
bool in_pseudo_squad(const GameState& g, int slot) {
    const int player = g.squad_slots.at(size_t(slot)) & 0xFFF;
    const SquadRange r = squad_range(kNoTeam);
    for (size_t s = r.first; s < r.first + r.count; s++)
        if ((g.squad_slots[s] & 0xFFF) == player) return true;
    return false;
}

// Int5(first, last): a computer club (every second one, starting alternately at the
// first or second, MonthRuntimeField7) with too many goalkeepers or too many players in a line
// puts one of them on the list. With no club found in first..last the search is repeated
// over the big clubs.
void list_club_player(GameState& g, int first, int last, WatcomRandom& rand, MinuteReport& report) {
    const int8_t* wanted = first < kBigSquadClubs ? kWantedBig : kWantedSmall;  // Attempt
    for (;;) {
        int16_t teams[138];
        uint32_t keys[138];
        int n = 0;  // local_14
        g.list_toggle = g.list_toggle + 1 == 2 ? 0 : g.list_toggle + 1;
        int best_line = 0;  // local_1c, keeps its value between the clubs
        for (int team = first + g.list_toggle; team < last; team += 2) {
            if (g.manager_of(team) >= 0) continue;
            const SquadRange range = squad_range(team);
            int8_t count[4] = {0, 0, 0, 0};  // local_50
            const int players = int16_t(squad_count(g, team, 3));
            for (int s = 0; s < players; s++) {
                const int p = g.squad_slots.at(range.first + size_t(s)) & 0xFFF;
                if (p != 0xFFF) count[g.players.at(size_t(p)).w364 & 3]++;
            }
            int key_base = 0, line = 0;
            bool add = false;
            if (rand.range(0, 1) + 2 < count[0]) {
                // three or four goalkeepers
                line = 0;
                key_base = count[0] + 100;
                add = true;
            } else if (players > 0xd) {
                const int flags = (g.team_flags.at(size_t(team)) >> 0xc) & 3;
                if (rand.range(0, flags * 2 + 2) != 0) {
                    int8_t most = 0;  // local_28
                    for (int k = 1; k < 4; k++) {
                        const int8_t over = int8_t(count[k] - wanted[k]);
                        if (most < over) most = over, best_line = k;
                    }
                    int chosen = best_line;
                    if (most == 0) chosen = rand.range(1, 3);
                    key_base = most * 8;
                    const int8_t room = int8_t(range.count - ((team < kBigSquadClubs ? 1 : 0) + 2));
                    if (room < players) key_base += (players - room) * 4;
                    line = chosen;
                    add = true;
                }
            }
            if (add) {
                const int a = rand.range(0, 5), b = rand.range(0, 5);
                keys[n] = uint32_t((key_base + a - b) * 4 + int8_t(line));
                teams[n] = int16_t(team);
                n++;
            }
        }
        int pick = 0x8a;  // sVar11
        int slot = -1;    // unaff_EDI: the slot tried last
        if (n != 0) {
            quicksort_by_key(teams, keys, n);
            for (pick = 0; pick < n; pick++) {
                const int team = teams[pick];
                const SquadRange range = squad_range(team);
                const int players = int16_t(squad_count(g, team, 2));
                const int line = int(keys[pick] & 3);
                int tries = 0;
                bool again;
                do {
                    slot = int(range.first) + rand.range(0, players - 1);
                    const int p = g.squad_slots.at(size_t(slot)) & 0xFFF;
                    const bool taken = bought_entry(g, p) != 0 || list_index(g, slot) != 0;
                    tries++;
                    again = int(g.players.at(size_t(p)).w364 & 3) != line || in_pseudo_squad(g, slot) || taken;
                } while (tries < 0x23 && again);
                if (tries < 0x23) break;
            }
            // (when no club had a fitting player the slot tried last is listed all the same)
        }
        if (pick < 0x8a) {
            list_player(g, slot, rand, report);
            return;
        }
        if (first == 0) {
            report.fail("transfer list: no club to take a player from (internal error 0x28)");
            return;
        }
        first = 0;
        last = kBigSquadClubs;
    }
}

// CareerFirstSteps: new players on the transfer list. The number depends on how full the list
// is (at most 37 entries); free agents of the pool are preferred while there are many.
void new_list_entries(GameState& g, WatcomRandom& rand, MinuteReport& report) {
    uint8_t count[5] = {0, 0, 0, 0, 0};  // local_18: listed, free agents, foreign, listed of both
    for (int s = kPoolFirst; s < kPoolEnd; s++) {
        if (g.squad_slots.at(size_t(s)) == 0xFFF) continue;
        if (!(s < kPoolFirst + 0x28 || kPoolFirst + 0x2c <= s)) continue;
        const int cls = s < kFreeAgents ? 0 : s < kForeignAgents ? 1 : 2;
        count[cls]++;
        if (cls != 0 && list_index(g, s) != 0) count[cls + 2]++;
    }
    uint8_t add[4] = {0, 0, 0, 0};  // local_10
    const int r = rand.range(2, 0x16);
    uint8_t n = uint8_t(r);
    if (count[0] + int8_t(r) >= 0x26) {
        n = uint8_t(0x25 - count[0]);
        if (int8_t(n) <= 0) return;
    }
    count[1] = uint8_t(count[1] - count[3]);
    count[2] = uint8_t(count[2] - count[4]);
    for (int16_t u = count[1]; n != 0 && u > 0x36; u--) n--, add[0]++;
    for (int16_t u = count[2]; n != 0 && u > 0xe; u--) n--, add[2]++;
    for (int k = 0; k < 4; k++) add[k] = uint8_t(add[k] + int8_t(kListShare[k] * int8_t(n) / 100));
    if (count[1] < add[0]) {
        add[1] = uint8_t(add[1] + ((add[0] - count[1]) >> 1));
        add[0] = count[1];
    }
    if (count[2] < add[2]) {
        add[3] = uint8_t(add[3] + ((add[2] - count[2]) >> 1));
        add[2] = count[2];
    }
    int8_t total = 0;
    for (int k = 0; k < 4; k++) total = int8_t(total + add[k]);
    if (total < 4) add[1] = uint8_t(add[1] + rand.range(2, 4));
    if (add[3] == 0 && add[1] != 0 && rand.range(0, 10) == 5) add[3]++, add[1]--;
    // (MonthRuntimeField8, a debug switch for club players only, is 0)
    for (int k = 0; k < 4 && !report.unsupported; k++) {
        uint8_t left = add[k];
        if (left == 0) continue;
        switch (k) {
            case 0:
            case 2:
                while (left != 0 && !report.unsupported) {
                    const int slot = k == 0 ? pick_free_agent(g, kFreeAgents, kForeignAgents, rand, report)
                                            : pick_free_agent(g, kForeignAgents, kPoolEnd, rand, report);
                    if (slot < 0) return;
                    if (list_player(g, slot, rand, report)) left--;
                }
                break;
            case 1:
                while (left-- != 0 && !report.unsupported) list_club_player(g, 0, kBigSquadClubs, rand, report);
                break;
            case 3:
                while (left-- != 0 && !report.unsupported) list_club_player(g, kBigSquadClubs, kLeagueClubs, rand, report);
                break;
        }
    }
}

// Loaned players whose time is up go back to their clubs (part of DayStepRest2).
void return_loans(GameState& g, WatcomRandom& rand, GameRandom* game, MinuteReport& report) {
    StaticMemory& mem = g.memory;
    for (int s = 0; s < kPoolFirst && !report.unsupported; s++) {
        const int player = g.squad_slots.at(size_t(s)) & 0xFFF;
        if (player == 0xFFF) continue;
        const int k = bought_entry(g, player) - 1;
        if (k < 0) continue;
        const uint32_t e = mem.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + uint32_t(k) * 4);
        const int owner = int(e >> 12 & 0x3FF), club = int(e >> 22 & 0x3FF);  // local_30, local_28
        const int owner_manager = g.manager_of(owner);
        const int borrower_manager = g.manager_of(club);
        // Human owners have a saved tactics-board row in ClubInfo_FormatError_RuntimeField and a matching
        // player ID/manager in Career_CareerManagersEnd_RuntimeField/ClubInfo_FormatError_RuntimeField2 (Fail). Restore it on expiry;
        // AI owners have no snapshot. Stage the complete return so malformed state
        // or a failed lineup cannot lose the loan ledger first.
        if (owner_manager >= 0 || borrower_manager >= 0) {
            const uint8_t remaining = mem.u8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField + uint32_t(k));
            if (remaining != 1) {
                // Source decrements an unsigned byte and returns only when the
                // decremented byte is zero (DayStepRest2). A malformed/stale zero
                // therefore wraps to 0xff instead of expiring immediately.
                mem.set8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField + uint32_t(k), uint8_t(remaining - 1));
                continue;
            }
            if ((owner_manager >= 4) || (owner_manager >= 0 &&
                (!mem.has(::bmh::layout::Career_CareerManagersEnd_RuntimeField + 0x17) || !mem.has(::bmh::layout::ClubInfo_FormatError_RuntimeField + 0x41f) ||
                 !mem.has(::bmh::layout::ClubInfo_FormatError_RuntimeField2 + 0x0b) || !mem.has(::bmh::layout::DeskActions_Unsupported_RuntimeField + uint32_t(owner_manager))))) {
                report.fail("month change: human-owner loan record is incomplete");
                return;
            }
            int snapshot_slot = -1;
            if (owner_manager >= 0) {
                for (int i = 0; i < 12; ++i) {
                    if (mem.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField + uint32_t(i) * 2) == uint16_t(player) &&
                        mem.u8(::bmh::layout::ClubInfo_FormatError_RuntimeField2 + uint32_t(i)) == uint8_t(owner_manager)) {
                        snapshot_slot = i;
                        break;
                    }
                }
            }
            const SquadRange owner_range = squad_range(owner);
            const int owner_slot = free_slot(g, int(owner_range.first), int(owner_range.count));
            const SquadRange borrower_range = squad_range(club);
            bool borrower_has_player = false;
            for (size_t i = 0; i < borrower_range.count; ++i)
                if ((g.squad_slots.at(borrower_range.first + i) & 0x0fff) == player) borrower_has_player = true;
            if ((owner_manager >= 0 && snapshot_slot < 0) || owner_slot < 0 || !borrower_has_player ||
                (owner_manager >= 0 && size_t(owner_manager) * kBoard +
                    size_t(owner_range.count) * kBoardSlot > g.tactics.size())) {
                report.fail("month change: human-owner loan cannot be restored safely");
                return;
            }
            if (borrower_manager >= 0 && !g.loan_return_screen) {
                report.fail("month change: expiring human-borrower loan needs its Source return dialog owner");
                return;
            }
            GameState staged = g;
            try {
                staged.memory.set8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField + uint32_t(k), 0);
                staged.memory.set32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + uint32_t(k) * 4, 0);
                staged.squad_slots.at(size_t(owner_slot)) = uint16_t(player);
                if (!remove_from_seller(staged, club, player, report)) return;
                if (owner_manager >= 0) {
                    const size_t board = size_t(owner_manager) * kBoard +
                                         size_t(owner_slot - int(owner_range.first)) * kBoardSlot;
                    const uint32_t snapshot = ::bmh::layout::ClubInfo_FormatError_RuntimeField + uint32_t(snapshot_slot) * uint32_t(kBoardSlot);
                    for (size_t b = 0; b < kBoardSlot; ++b)
                        staged.tactics.at(board + b) = staged.memory.u8(snapshot + uint32_t(b));
                    staged.memory.set16(::bmh::layout::Career_CareerManagersEnd_RuntimeField + uint32_t(snapshot_slot) * 2, 0x0fff);
                    const uint8_t count = staged.memory.u8(::bmh::layout::DeskActions_Unsupported_RuntimeField + uint32_t(owner_manager));
                    if (count == 0) {
                        report.fail("month change: human-owner loan count is inconsistent");
                        return;
                    }
                    staged.memory.set8(::bmh::layout::DeskActions_Unsupported_RuntimeField + uint32_t(owner_manager), uint8_t(count - 1));
                }
                // DayStepRest2 posts the return notice to whichever owner/borrower
                // managers exist.
                const std::string notice = message_text(staged, ::bmh::layout::Month_ReturnLoans_TextPointer) + staged.player_name(player) +
                    message_text(staged, ::bmh::layout::Month_ReturnLoans_TextPointer2) + message_text(staged, ::bmh::layout::Month_ReturnLoans_TextPointer3) + staged.club_name(club) +
                    message_text(staged, ::bmh::layout::Month_ReturnLoans_TextPointer4) + staged.club_name(owner) + message_text(staged, ::bmh::layout::Month_ReturnLoans_TextPointer5);
                if (borrower_manager >= 0) {
                    const int keep = staged.loan_return_screen(staged, borrower_manager,
                        notice + message_text(staged, ::bmh::layout::Month_ReturnLoans_TextPointer6));
                    if (keep < 0) {
                        report.fail("month change: human-borrower loan return dialog stopped");
                        return;
                    }
                    if (keep != 0) {
                        // DayStepRest2 makes the borrower the active manager before
                        // RowWhose/Range2. The two W(90,100) draws occur even
                        // when all four special offer slots are occupied.
                        staged.memory.set8(::bmh::layout::ActiveManager, uint8_t(borrower_manager));
                        int offer = 0;
                        while (offer < 4 && staged.memory.u32(::bmh::layout::OfferSlots + uint32_t(offer) * 4) != 0xffffffffu)
                            ++offer;
                        if (offer < 4 && (!game || !staged.loan_return_offer_screen)) {
                            report.fail("month change: OpenTransferEditor return-offer editor has no typed runtime owner");
                            return;
                        }
                        WatcomRandom offer_rand = rand;
                        std::optional<GameRandom> offer_game;
                        if (game) offer_game = *game;
                        const int percent = offer_rand.range(90, 100);
                        const int demand = offer_rand.range(90, 100);
                        if (offer < 4) {
                            const uint32_t entry = kTransferList + uint32_t(40 + offer) * kListEntry;
                            uint16_t team_word = uint16_t(owner);
                            if (owner_manager >= 0) team_word |= 0x8000;
                            staged.memory.set16(entry + 8, team_word);
                            for (int bid = 0; bid < 4; ++bid)
                                staged.memory.set16(entry + uint32_t(bid) * 2, 0xffff);
                            staged.memory.set8(entry + 0x54, uint8_t(percent));
                            staged.memory.set8(entry + 0x55, uint8_t(demand));
                            uint32_t fee = 0, salary = 0;
                            market_value(staged, owner_slot, 0, uint8_t(percent), fee, salary, 1, 0, offer_rand);
                            staged.memory.set32(entry + 0x2c, fee);
                            staged.memory.set32(entry + 0x30, salary);
                            const int source_slot = kPoolFirst + 40 + offer;
                            staged.squad_slots.at(size_t(source_slot)) = uint16_t(player);
                            staged.memory.set32(::bmh::layout::OfferSlots + uint32_t(offer) * 4,
                                uint32_t(player) * 0x400 + uint32_t(borrower_manager));
                            // A modal may replace staged (and its hook) while paused.
                            // Own the active callable until the synchronous invocation returns.
                            const auto present_return_offer = staged.loan_return_offer_screen;
                            const int submitted = present_return_offer(staged, *offer_game, offer_rand,
                                borrower_manager, source_slot, 40 + offer, HumanBuyerSaleHooks{});
                            if (submitted < 0) {
                                report.fail("month change: OpenTransferEditor return-offer editor stopped");
                                return;
                            }
                            // The original month caller performs this cleanup only
                            // after OpenTransferEditor has returned from the actual editor.
                            staged.squad_slots.at(size_t(kPoolFirst + 40 + offer)) = 0x0fff;
                            staged.memory.set32(::bmh::layout::OfferSlots + uint32_t(offer) * 4, 0xffffffffu);
                        }
                        rand = offer_rand;
                        if (game && offer_game) *game = *offer_game;
                    }
                }
                std::string return_notice = notice;
                if (borrower_manager >= 0) {
                    const SquadRange returned_borrower_range = squad_range(club);
                    for (size_t i = 0; i < returned_borrower_range.count; ++i) {
                        if ((staged.squad_slots.at(returned_borrower_range.first + i) & 0x0fff) == player) {
                            return_notice += message_text(staged, ::bmh::layout::Month_ReturnLoans_TextPointer7) + staged.club_name(club) +
                                             message_text(staged, ::bmh::layout::Month_ReturnLoans_TextPointer8);
                            break;
                        }
                    }
                }
                uint32_t managers = 0x80000000u;
                if (borrower_manager >= 0) managers = 1u << borrower_manager;
                if (owner_manager >= 0) managers |= 1u << owner_manager;
                post_composed(staged, int64_t(managers), return_notice);
                auto_lineup(staged, owner, owner_manager >= 0 ? 0xe4 : 100);
                auto_lineup(staged, club, borrower_manager >= 0 ? 0xe4 : 100);
            } catch (const std::exception&) {
                report.fail("month change: human-owner loan cannot be restored safely");
                return;
            }
            g = std::move(staged);
            continue;
        }
        const uint8_t months = uint8_t(mem.u8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField + uint32_t(k)) - 1);
        mem.set8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField + uint32_t(k), months);
        if (months != 0) continue;
        mem.set32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + uint32_t(k) * 4, 0);
        const SquadRange range = squad_range(owner);
        const int slot = free_slot(g, int(range.first), int(range.count));
        if (slot == -1) {
            report.fail("month change: no room for a returning player (internal error 0x2e)");
            return;
        }
        g.squad_slots[size_t(slot)] = uint16_t(player);
        if (!remove_from_seller(g, club, player, report)) return;
        lineup_after_transfer(g, owner);
        lineup_after_transfer(g, club);
    }
}

// OpenBMH deviates from the original: omits the two discontinued youth-group cost categories from monthly accounts.
// OriginalFinancePreview(1): the month's expenses of a human manager: facility maintenance
// (BMHExeXa), record +0x854/+0x858, the wages of the squad (MonthlyPay with flags
// 14), the six running costs of Spending (the youth work capped per league by the
// supported rule profile patch MonthlyExpenses, which also drops the youth groups 0 and 2) and the debt
// service (+0x179 x 1000); each booked in the statistics.
int32_t monthly_expenses(GameState& g, int m) {
    std::vector<uint8_t>& r = g.manager_records;
    const size_t rec = size_t(m) * kRecord;
    int32_t sum = 0;
    for (int i = 0; i < 0xe; i++) sum += kFacilityValue[i];
    for (int i = 0xe; i < 0x16; i++) sum += (r.at(rec + 0x2e + size_t(i)) & 7) * kFacilityValue[i];
    add32(r, rec + 0x87c, sum);
    for (int k = 1; k < 3; k++) {
        const int32_t v = int32_t(rd32(r, rec + 0x850 + size_t(k) * 4));
        add32(r, rec + 0x890 + size_t(k) * 4, v);
        sum += v;
    }
    // MonthlyPay(manager, 14): salaries (+0x50) and bonus bases (+0x4e) of the board
    int32_t wages = 0;
    uint32_t bonus = 0;
    for (size_t s = 0; s < 24; s++) {
        const size_t slot = size_t(m) * kBoard + s * kBoardSlot;
        if (g.tactics.at(slot + 0x3d) == 0xFF) continue;
        bonus = uint32_t(g.tactics.at(slot + 0x4e) | (g.tactics.at(slot + 0x4f) & 0x7F) << 8);
        const int32_t salary = int32_t(rd32(g.tactics, slot + 0x50));
        add32(r, rec + 0x338, salary);
        add32(r, rec + 0x33c, int32_t(bonus));
        wages += salary + int32_t(bonus);
    }
    sum += wages;
    for (int k = 0; k < 6; k++) {
        int32_t v = int32_t(g.memory.u32(::bmh::layout::Spending + uint32_t(m) * 0x24 + uint32_t(k) * 4));
        // OpenBMH deviates from the original: omits the two discontinued youth-group cost categories from monthly accounts.
// MonthlyExpenses (supported rule profile patch): the two other youth groups (items 0 and 2) cost
        // nothing any more, whatever the saved value
        if (k == 0 || k == 2) v = 0;
        if (k == 1 && v != 0) {
            // TeamLeague3: the limit of the league (1.5 M, 1 M, 580000, 280000); the full
            // limit is paid while +0x880 is below it, nothing after
            const int league = team_league(g.manager_teams.at(size_t(m)));
            const uint32_t limit = league == 0 ? 1500000 : league == 1 ? 1000000 : league <= 3 ? 580000 : 280000;
            v = rd32(r, rec + 0x880) < limit ? int32_t(limit) : 0;
        }
        if (k >= 3 && k <= 5) add32(r, rec + 0x878 + size_t(k) * 4, v);
        else add32(r, rec + 0x880, v);
        sum += v;
    }
    const int debt = r.at(rec + 0x179);
    if (debt != 0) {
        add32(r, rec + 0x8a0, debt * 1000);
        sum += debt * 1000;
    }
    return sum < 0 ? 0 : sum;
}

// FinanceTotalsFromGame3(1): the month's income: facilities (FacilityIncome) and the 16 advertising
// contracts (record +0xa34), booked in the statistics.
int32_t monthly_income(GameState& g, int m) {
    std::vector<uint8_t>& r = g.manager_records;
    const size_t rec = size_t(m) * kRecord;
    int32_t sum = 0;
    for (int k = 0; k < 6; k++) {
        const uint8_t v = r.at(rec + size_t(k));
        if (v == 0) continue;
        const int level = k == 0 ? ((v - 1) / 0x55 + 1) & 0xFF : 1;  // Add32
        sum += level * kFacilityIncome[k];
    }
    add32(r, rec + 0x86c, sum);
    for (int k = 0; k < 16; k++) {
        const int32_t v = int32_t(rd32(r, rec + 0xa34 + size_t(k) * 0xc));
        add32(r, rec + (k < 14 ? 0x860 : k == 14 ? 0x85c : 0x864), v);
        sum += v;
    }
    return sum;
}

// The monthly bookkeeping of the players on a human manager's tactics board.
void board_month(GameState& g, int m, WatcomRandom& rand, MinuteReport& report, GameRandom* game) {
    std::vector<uint8_t>& r = g.manager_records;
    const size_t rec = size_t(m) * kRecord;
    const SquadRange range = squad_range(g.manager_teams.at(size_t(m)));
    auto board = [&](int s) { return size_t(m) * kBoard + size_t(s) * kBoardSlot; };
    // the value history has 12 months; when it is full it moves on by one
    int month = r.at(rec + 0x286);  // local_24
    if (month < 0xc) {
        r.at(rec + 0x286) = uint8_t(month + 1);
    } else {
        for (int s = 0; s < 24; s++) {
            if (g.squad_slots.at(range.first + size_t(s)) == 0xFFF) continue;
            for (int k = 0; k < 3; k++) {
                const size_t at = board(s) + 0x19 + size_t(k) * 0xc;
                std::copy(g.tactics.begin() + long(at + 1), g.tactics.begin() + long(at + 0xc), g.tactics.begin() + long(at));
            }
        }
        month = 0xb;
    }
    for (int s = 0; s < 24; s++) {
        // (a player who leaves closes the gap: the same row is looked at again, or after the
        //  contracts screen all rows from the first one - those done are marked 0x80)
    next_player:
        const size_t b = board(s);
        const int player = g.squad_slots.at(range.first + size_t(s)) & 0xFFF;
        if (player == 0xFFF || (g.tactics.at(b + 0xb) & 0x80)) continue;
        g.tactics.at(b + 0xb) |= 0x80;
        if (rand.range(0, 0xb) == 0) {
            const uint32_t limit = uint32_t(rand.range(0x11, 0x17));
            if (limit < (g.players.at(size_t(player)).w364 >> 21 & 0x1F) && !(g.tactics.at(b + 0xb) & 0x20)) {
                // he wants to end his career at the end of his contract
                g.tactics.at(b + 0xb) |= 0x20;
                post_composed(g, m, g.player_name(player) + message_text(g, ::bmh::layout::Month_BoardMonth_TextPointer));
            }
        }
        g.tactics.at(b + 0x11)++;
        if (rand.range(0, 6) == 0) {
            // the mood (+0x0d) drifts back towards 100
            const int d = std::abs(100 - g.tactics.at(b + 0xd));
            if (int16_t(d) != 0) {
                int16_t step = int16_t(d) < 0xb ? 1 : 10;
                if (g.tactics.at(b + 0xd) > 100) step = int16_t(-step);
                g.tactics.at(b + 0xd) = uint8_t(std::clamp(step + g.tactics.at(b + 0xd), 0, 0xff));
            }
        }
        if (g.tactics.at(b + 0x43) != 0) {
            g.tactics.at(b + 0x43)--;
            if ((g.tactics.at(b + 0x43) & 0x3f) == 0) g.tactics.at(b + 0x43) = 0;
        }
        if (g.tactics.at(b + 0xc) != 0xFF) g.tactics.at(b + 0xc)--;  // contract months (BalancePlan_BalanceFromGame_RuntimeField is 0)
        if (g.tactics.at(b + 0xc) <= 1) {
            if (g.tactics.at(b + 0xc) != 0) {
                // "'s Vertrag läuft in einem Monat ab. ..." (string at ContractExpiryWarningText)
                post_composed(g, m, g.player_name(player) +
                                        kContractEndsSoonText);
            } else if (!(g.tactics.at(b + 0xb) & 0x20)) {
                // the contract has ended: "... Vertrag ist ausgelaufen" (Month_BoardMonth_TextPointer4) and the
                // contracts screen; without a renewal the message is posted and he goes
                const std::string text = g.player_name(player) + message_text(g, ::bmh::layout::Month_BoardMonth_TextPointer4);
                const int renewed = g.contract_screen ? g.contract_screen(g, m, s) : -2;
                if (renewed == -2) {
                    report.fail("month change: a contract ends (the contracts screen InstallDefaultHooks)");
                    return;
                }
                if (renewed < 0) return;
                if (renewed == 0) {
                    post_composed(g, m, text);
                    if (!game) {
                        report.fail("month change: a player goes (MonthChange) without the game generator");
                        return;
                    }
                    if (!release_player(g, int(range.first) + s, *game, rand, report)) return;
                    s = 0;
                    goto next_player;
                }
            } else {
                // he ends his career (flag 0x20, see above): Month_BoardMonth_TextPointer2, then his club gets
                // a compensation, Month_BoardMonth_TextPointer3 + the amount (AddToSquad with W(15, 30)
                // percent, 1 year, rounded down to 10000)
                post_composed(g, m, g.player_name(player) + message_text(g, ::bmh::layout::Month_BoardMonth_TextPointer2));
                const int percent = rand.range(0xf, 0x1e);
                uint32_t fee = 0, salary = 0;
                market_value(g, int(range.first) + s, 0, percent, fee, salary, 1, 0, rand);
                fee = fee / 10000 * 10000;
                post_composed(g, m, message_text(g, ::bmh::layout::Month_BoardMonth_TextPointer3) + format_amount(g, int32_t(fee)) + ".");
                add32(r, rec + 0x314, int32_t(fee));  // MonthRuntimeField9: the balance, not booked
                if (!game) {
                    report.fail("month change: a player retires (MonthChange) without the game generator");
                    return;
                }
                if (!release_player(g, int(range.first) + s, *game, rand, report)) return;
                goto next_player;
            }
        }
        // the value history of the month
        uint32_t fee = 0, salary = 0;
        market_value(g, int(range.first) + s, 0, 100, fee, salary, 0, 0, rand);
        const size_t h = b + size_t(month);
        g.tactics.at(h + 0x19) = uint8_t(std::min<uint32_t>(fee / 8000, 0xff));
        const uint32_t w = g.players.at(size_t(player)).w35c;
        g.tactics.at(h + 0x25) = uint8_t((((((w >> 14) & 0x7F) + ((w >> 7) & 0x7F)) * 3 >> 1) * 0xff) / 300);
        g.tactics.at(h + 0x31) = uint8_t(std::min<uint32_t>(((w & 0x7F) * 0x8c) / 0x5a, 0xff));
    }
    for (int s = 0; s < 24; s++) g.tactics.at(board(s) + 0xb) &= 0x7F;
}

// MonthChange2: share prices grow by 2.5 %, dividends, loans between the managers.
void shares_and_loans(GameState& g) {
    StaticMemory& mem = g.memory;
    std::vector<uint8_t>& r = g.manager_records;
    static const int kShareFactor[6] = {15, 18, 21, 24, 27, 30};  // MonthRuntimeField10
    const int managers = int(g.manager_teams.size());
    for (int k = 0; k < 6; k++) {
        const uint32_t at = ::bmh::layout::FundBlock + uint32_t(k) * 0x1c;
        uint32_t price = mem.u32(at);
        price += ((price / 10000) * 0x19 / 10) * 10;
        mem.set32(at, price);
        // (a bug of the original: only the last manager's dividend is kept and it is booked
        //  for the manager after the last one)
        int32_t dividend = 0;
        for (int m = 0; m < managers; m++)
            dividend = int32_t(mem.u16(::bmh::layout::Advertising_OriginalShareAdjustment_RuntimeField + uint32_t(m) * 2 + uint32_t(k) * 0x1c) *
                               ((price * uint32_t(kShareFactor[k])) / 10000));
        book_money(g, managers, -dividend, true);
        add32(r, size_t(managers) * kRecord + 0x870, dividend);
    }
    for (int m = 0; m < managers; m++)
        for (int l = 0; l <= managers; l++) {
            const uint32_t e = ::bmh::layout::Loans + uint32_t(l) * 0x18 + uint32_t(m) * 0x78;
            const int32_t rate = int32_t(mem.u32(e + 8));
            if (rate == 0) continue;
            book_money(g, m, rate, true);
            add32(r, size_t(m) * kRecord + 0x89c, rate);
            if (l != 0) {
                add32(r, size_t(l - 1) * kRecord + 0x870, rate);
                book_money(g, l - 1, -rate, true);
            }
            mem.set32(e + 0x14, mem.u32(e + 0x14) + uint32_t(rate));
            mem.set32(e + 0x10, mem.u32(e + 0x10) + 1);
            if (int32_t(mem.u32(e + 0xc)) <= int32_t(mem.u32(e + 0x10)))
                for (uint32_t i = 0; i < 0x18; i++) mem.set8(e + i, 0);
        }
}

}  // namespace

bool list_transfer_player(GameState& g, int slot, WatcomRandom& rand, MinuteReport& report) {
    return list_player(g, slot, rand, report);
}

void new_transfer_list_entries(GameState& g, WatcomRandom& rand, MinuteReport& report) { new_list_entries(g, rand, report); }

void market_value(const GameState& g, int slot, int loan, int percent, uint32_t& fee, uint32_t& salary, int years,
                  int extra, WatcomRandom& rand) {
    if (years > 4) years = 4;
    const int player = g.squad_slots.at(size_t(slot)) & 0xFFF;
    const Player& p = g.players.at(size_t(player));
    int age = int((p.w364 >> 21) & 0x1F) + 1;  // local_24
    if (age > 0x14) age = 0x14;
    const int position = int(p.w364 & 3);
    uint16_t value[2] = {uint16_t((p.w35c >> 7) & 0x7F), uint16_t((p.w35c >> 14) & 0x7F)};  // technique, condition
    uint8_t growth = 100;  // local_14
    if (age > 10) growth = uint8_t(age * -10 - 0x38);
    for (int k = 0; k < 2; k++)
        for (int s = 0; s < years - 1; s++)
            value[k] = uint16_t((uint32_t(growth) * kDevelopment[int16_t(value[k]) / 10]) / 100 + value[k]);
    const int ad = std::abs(0xb - age) & 0xFF;
    int points = ((int16_t(value[0]) + int16_t(value[1])) * 5) / 2 + 0x96 - ad * 2;  // local_2c
    points = std::clamp(points, 0xc, 0x26c);
    if (points > 500) points += points < 600 ? kDevelopment[10 + (points - 500) / 10] : 100;
    points = (((points >> 2) + 1) * points * ((points >> 3) + 100)) / 10;
    int16_t factor = int16_t(((p.w364 >> 26) & 0x3F) >> 2);  // experience
    if (position == 0) {
        factor = int16_t(factor + 0x6e);
    } else {
        const int16_t skill = int16_t((int((p.w364 >> 10) & 0x3F) * 3 + int((p.w368 >> 7) & 0x3F)) >> 1);
        const int t = (position * skill) / 3;
        factor = int16_t(factor + 100 + (int16_t(t) >> 1));
    }
    fee = uint32_t((percent * ((factor * points) / 100)) / 100);
    if (fee < 50000) fee = uint32_t(rand.range(50000, 80000));
    salary = fee / 0x28;
    // (local_24 holds 11 - age by now, the variable of the age difference above: younger
    //  players ask for more per extra year, older ones for less)
    salary = ((uint32_t(extra * (0xb - age) + 100) * salary) / 10000) * 100;
    if (loan == 1) fee >>= 2;
    // BMH_MARKET_TRACE=1: the factors of one valuation (diagnosis of #40; no state, no draws)
    if (const char* trace = std::getenv("BMH_MARKET_TRACE"); trace && trace[0] == '1')
        std::fprintf(stderr, "[market_value player %d (%s) pos %d age_raw %d TE %d KO %d -> points %d factor %d percent %d years %d extra %d: fee %u salary %u]\n",
                     player, g.player_name(player).c_str(), position, age, int((p.w35c >> 7) & 0x7F), int((p.w35c >> 14) & 0x7F),
                     points, int(factor), percent, years, extra, fee, salary);
}

std::array<uint32_t, 4> contract_demands(GameState& g, int manager, int row, bool from_desk, WatcomRandom& rand) {
    const SquadRange range = squad_range(g.manager_teams.at(size_t(manager)));
    const int slot = int(range.first) + row;
    const size_t b = size_t(manager) * kBoard + size_t(row) * kBoardSlot;
    const int player = g.squad_slots.at(size_t(slot)) & 0xFFF;
    const int age = int((g.players.at(size_t(player)).w364 >> 21) & 0x1F) + 1;  // local_24
    // Difficulty (Range3 always returns 1, so the level is used)
    const int percent = g.level + 100;
    std::array<uint32_t, 4> demand{};
    for (int k = 0; k < 4; k++) {
        // per year beyond the current contract the salary changes by 11 - age percent
        const int extra = int8_t(uint8_t(k - (g.tactics.at(b + 0xc) + 6) / 0xc + 1));
        uint32_t fee = 0, salary = 0;
        market_value(g, slot, 0, percent, fee, salary, k + 1, extra, rand);
        salary = (g.tactics.at(b + 0xd) * salary) / 100;  // his mood
        const bool refuses = age + k >= 0x24 || (g.tactics.at(b + 0xb) & 0x60);
        if (const char* trace = std::getenv("BMH_MARKET_TRACE"); trace && trace[0] == '1')
            std::fprintf(stderr, "[contract_demands player %d years %d: mood +0x0d %d, flags +0x0b %02x, refuses %d -> %u]\n",
                         player, k + 1, int(g.tactics.at(b + 0xd)), int(g.tactics.at(b + 0xb)), int(refuses), salary);
        demand[size_t(k)] = refuses && from_desk ? 0xFFFFFFFFu : salary;
    }
    return demand;
}

bool contract_offer(GameState& g, int manager, int row, int column, uint32_t offer, uint32_t demand, bool request,
                    bool screen_row, WatcomRandom& rand, const std::array<uint8_t, 7>* proposed_bonuses) {
    const int team = g.manager_teams.at(size_t(manager));
    const SquadRange range = squad_range(team);
    const int slot = int(range.first) + row;
    const size_t b = size_t(manager) * kBoard + size_t(row) * kBoardSlot;
    const int player = g.squad_slots.at(size_t(slot)) & 0xFFF;
    const size_t bonus_at = b + 0x12;
    std::array<uint8_t, 7> current_bonuses{};
    for (size_t i = 0; i < current_bonuses.size(); ++i) {
        current_bonuses[i] = g.tactics.at(bonus_at + i);
        if (proposed_bonuses && (*proposed_bonuses)[i] > (i < 3 ? 20 : 200)) return false;
    }
    // the longer he is at the club (+0x11, months), the less he insists
    int loyalty = g.tactics.at(b + 0x11) / rand.range(8, 0xc);  // local_3c
    if (int16_t(loyalty) > 6) loyalty = 6;
    // the offer counts with 80 % of the bonus he can expect (ContractOffer with the fitness
    // correction on a copy of his board row whose salary is the offer: its +0x50 is the
    // offer variable local_f0 that follows the copy on the stack)
    std::array<uint8_t, 24> buf;
    const int predicted = table_prediction(g, team, 4, buf);
    const int rank = predicted == 0 ? 0xB : predicted + 1;
    uint8_t saved[4];
    std::copy(g.tactics.begin() + long(b + 0x50), g.tactics.begin() + long(b + 0x54), saved);
    for (int i = 0; i < 4; i++) g.tactics.at(b + 0x50 + size_t(i)) = uint8_t(offer >> (8 * i));
    if (proposed_bonuses)
        std::copy(proposed_bonuses->begin(), proposed_bonuses->end(), g.tactics.begin() + long(bonus_at));
    const uint32_t bonus = expected_bonus(g, manager, rank, row, player, true);
    std::copy(saved, saved + 4, g.tactics.begin() + long(b + 0x50));
    if (proposed_bonuses)
        std::copy(current_bonuses.begin(), current_bonuses.end(), g.tactics.begin() + long(bonus_at));
    const uint32_t worth = offer + (bonus * 0x50) / 100;  // uVar12
    if (std::getenv("BMH_TRACE"))
        std::printf("  [offer %u: loyalty %d, rank %d, bonus %u, worth %u, demand %u]\n", offer, loyalty, rank, bonus, worth,
                    demand);
    // what he wants: W(80, 100) - 2 x loyalty percent of his demand (a request of his own:
    // the whole demand)
    const uint32_t wanted = (request ? 100u : uint32_t(rand.range(0x50, 100) - 2 * int16_t(loyalty))) * demand;
    if (std::getenv("BMH_TRACE")) std::printf("  [wanted %u / 100]\n", wanted);
    if ((offer * 100u) / demand < 0x3d || worth < wanted / 100) {
        // "Der Spieler lehnt ab" (CareerSession_Reject_TextPointer6, a message box): he is offended (+0x4f bit 7)
        // and in a worse mood
        g.tactics.at(b + 0x4f) |= 0x80;
        if (g.tactics.at(b + 0xd) < 0xfb) g.tactics.at(b + 0xd) = uint8_t(g.tactics.at(b + 0xd) + 5);
        if (screen_row) g.tactics.at(b + 0xb) |= 0x40;  // he will not talk again
        return false;
    }
    // accepted (CareerSession_Reject_TextPointer5): the new salary and term; his mood is what the offer is worth
    // against his market salary for that term
    for (int i = 0; i < 4; i++) g.tactics.at(b + 0x50 + size_t(i)) = uint8_t(offer >> (8 * i));
    g.tactics.at(b + 0xc) = uint8_t((column + 1) * 0xc);
    uint32_t fee = 0, salary = 0;
    market_value(g, slot, 0, 100, fee, salary, column, 0, rand);
    g.tactics.at(b + 0xd) = uint8_t(std::clamp<uint32_t>((worth * 100) / salary, 100, 0xff));
    if (proposed_bonuses)
        std::copy(proposed_bonuses->begin(), proposed_bonuses->end(), g.tactics.begin() + long(bonus_at));
    return true;
}

bool contract_bonuses(GameState& g, int manager, int row, const std::array<uint8_t, 7>& rates) {
    if (manager < 0 || size_t(manager) >= g.manager_teams.size()) return false;
    const int team = g.manager_teams[size_t(manager)];
    if (team == kNoTeam) return false;
    const SquadRange range = squad_range(team);
    if (row < -1 || (row >= 0 && size_t(row) >= range.count) || range.first + range.count > g.squad_slots.size())
        return false;
    for (size_t i = 0; i < rates.size(); ++i)
        if (rates[i] > (i < 3 ? 20 : 200)) return false;
    const size_t board = size_t(manager) * 0x840;
    if (board + size_t(range.count) * 0x58 > g.tactics.size()) return false;
    bool changed = false;
    const size_t first_row = row < 0 ? 0 : size_t(row);
    const size_t end_row = row < 0 ? range.count : size_t(row) + 1;
    // The original checks the whole selected scope before committing any of it.
    for (size_t r = first_row; r < end_row; ++r) {
        const int player = g.squad_slots.at(range.first + r) & 0x0fff;
        if (player == 0x0fff) continue;
        if (size_t(player) >= g.players.size()) return false;
        const size_t b = board + r * 0x58;
        for (size_t i = 0; i < rates.size(); ++i) {
            const uint8_t old = g.tactics.at(b + 0x12 + i);
            changed |= rates[i] > old;
        }
    }
    for (size_t i = 0; i < rates.size(); ++i) {
        bool found = false;
        uint8_t minimum = 0xff;
        for (size_t r = first_row; r < end_row; ++r) {
            if ((g.squad_slots.at(range.first + r) & 0x0fff) == 0x0fff) continue;
            found = true;
            minimum = std::min(minimum, g.tactics.at(board + r * 0x58 + 0x12 + i));
        }
        if (found && rates[i] < minimum) return false;
    }
    if (!changed) return false;
    for (size_t r = first_row; r < end_row; ++r) {
        const int player = g.squad_slots.at(range.first + r) & 0x0fff;
        if (player == 0x0fff) continue;
        const size_t b = board + r * 0x58;
        for (size_t i = 0; i < rates.size(); ++i)
            if (rates[i] > g.tactics.at(b + 0x12 + i)) g.tactics.at(b + 0x12 + i) = rates[i];
    }
    return true;
}

ContractInsuranceResult contract_insurance(GameState& g, int manager, int row, int insurance_type,
                                           uint8_t months) {
    ContractInsuranceResult out;
    if (manager < 0 || size_t(manager) >= g.manager_teams.size() || insurance_type < 0 || insurance_type >= 4 ||
        months < 1 || months > 60) return out;
    const int team = g.manager_teams[size_t(manager)];
    if (team == kNoTeam) return out;
    const SquadRange range = squad_range(team);
    if (row < 0 || size_t(row) >= range.count || range.first + size_t(row) >= g.squad_slots.size()) return out;
    const int player = g.squad_slots.at(range.first + size_t(row)) & 0x0fff;
    if (player == 0x0fff || size_t(player) >= g.players.size()) return out;
    const size_t b = size_t(manager) * 0x840 + size_t(row) * 0x58;
    if (b + 0x54 > g.tactics.size() || (g.tactics.at(b + 0x0b) & 0x60) != 0 ||
        g.tactics.at(b + 0x43) != 0 || (g.players[size_t(player)].w35c >> 27) != 0) return out;
    const uint32_t salary = uint32_t(g.tactics.at(b + 0x50)) |
        (uint32_t(g.tactics.at(b + 0x51)) << 8) | (uint32_t(g.tactics.at(b + 0x52)) << 16) |
        (uint32_t(g.tactics.at(b + 0x53)) << 24);
    const uint32_t fee = (((uint32_t(insurance_type * 10 + 10) * salary) / 1000) * 10);
    const uint16_t old_fee = uint16_t(g.tactics.at(b + 0x4e) | (uint16_t(g.tactics.at(b + 0x4f)) << 8));
    g.tactics.at(b + 0x43) = uint8_t(months | uint8_t(insurance_type << 6));
    // The original clears the old amount but preserves the high flag before adding the
    // term's monthly premium into the 16-bit field.
    const uint16_t base = uint16_t(old_fee & 0x8000);
    const uint16_t stored = uint16_t(base + uint16_t(fee));
    g.tactics.at(b + 0x4e) = uint8_t(stored);
    g.tactics.at(b + 0x4f) = uint8_t(stored >> 8);
    out.accepted = true;
    out.monthly_fee = uint16_t(fee);
    return out;
}

void search_profile_news(GameState& g, int manager, uint32_t list, int player) {
    const StaticMemory& mem = g.memory;
    const size_t rec = size_t(manager) * kRecord;
    auto b = [&](size_t off) { return int(g.manager_records.at(rec + off)); };
    const int team = g.manager_teams.at(size_t(manager));
    player &= 0xFFF;
    if (team == kNoTeam || (mem.u16(list + 8) & 0x7FFF) == team || (b(0x1f6) & 0x40)) return;
    const uint32_t w364 = g.players.at(size_t(player)).w364, w35c = g.players.at(size_t(player)).w35c;
    // position wanted (or none selected) and age within the range
    if (b(0x1f6 + (w364 & 3)) == 0 && b(0x1f9) + b(0x1f8) + b(0x1f7) + b(0x1f6) != 0) return;
    const int age = int((w364 >> 21) & 0x1F);
    if (age < b(0x1fa) || age > b(0x1fb)) return;
    // strength by the selected attributes (condition, technique, form; none = all three)
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
    if (strength < b(0x1fc) || strength > b(0x1fd)) return;
    // left / right foot and wing preferences (w35c bits 21-23 and 24-26)
    const int lane = int((w35c >> 21) & 7), foot = int((w35c >> 24) & 7);
    bool low_foot = b(0x201) || b(0x203), high_foot = b(0x202) || b(0x204);
    if (low_foot && high_foot) low_foot = high_foot = false;
    bool low_lane = b(0x201) || b(0x202), high_lane = b(0x203) || b(0x204);
    if (low_lane && high_lane) low_lane = high_lane = false;
    if ((low_lane && lane >= 3) || (high_lane && lane <= 4) || (low_foot && foot >= 3) || (high_foot && foot <= 4)) return;
    // the price class (asking price 70..130 %) against the classes wanted
    int8_t price_class = int8_t((mem.u8(list + 0x54) - 0x46) / 0x14);
    price_class = std::clamp<int8_t>(price_class, 0, 2);
    const int8_t classes = int8_t(b(0x207) + b(0x205) + b(0x206));
    int highest = 2;
    while (highest >= 0 && b(0x205 + size_t(highest)) == 0) highest--;
    if (classes == 0 || price_class <= highest)
        post_composed(g, manager, message_text(g, ::bmh::layout::Month_SearchProfileNews_TextPointer) + g.player_name(player) + message_text(g, ::bmh::layout::Month_SearchProfileNews_TextPointer2));
}

void month_change(GameState& g, WatcomRandom& rand, MinuteReport& report, GameRandom* game) {
    StaticMemory& mem = g.memory;
    std::vector<uint8_t>& r = g.manager_records;
    const int managers = int(g.manager_teams.size());
    // 1. advertising contracts
    turn_step = "month_change: advertising";
    if (mem.u8(::bmh::layout::GameMode) != 1 && mem.u8(::bmh::layout::CareerDuration) != 1)
        for (int m = 0; m < managers; m++)
            for (int k = 0; k < 0x12; k++) {
                const size_t c = size_t(m) * kRecord + 0xa34 + size_t(k) * 0xc;
                if (r.at(c + 5) == 0 && (r.at(c + 6) & 0x80)) {
                    wr32(r, c, 0);
                    r.at(c + 6) = 0;
                }
                if (r.at(c + 5) != 0 && --r.at(c + 5) == 0) {
                    if ((r.at(c + 6) & 0x7F) == 0) wr32(r, c, 0);
                    else turn_step = "month_change: advertising text", post_text(g, m, text_id(::bmh::layout::Month_MonthChange_TextPointer));  // the contract can be renewed
                }
            }
    // 2. new players on the transfer list
    turn_step = "month_change: new list entries";
    new_list_entries(g, rand, report);
    if (report.unsupported) return;
    // 3. loans
    turn_step = "month_change: loans";
    return_loans(g, rand, game, report);
    if (report.unsupported) return;
    // 4. the human managers
    turn_step = "month_change: managers";
    for (int m = 0; m < managers && !report.unsupported; m++) {
        const size_t rec = size_t(m) * kRecord;
        for (int k = 0; k < 3; k++)
            rate_manager(g, m, uint8_t(int8_t(int32_t(mem.u32(::bmh::layout::Month_MonthChange_RuntimeField + uint32_t(k) * 4 + uint32_t(m) * 0x24)) / 0x96)),
                         k + 1);
        const int32_t expenses = monthly_expenses(g, m);
        const int32_t income = monthly_income(g, m);
        book_money(g, m, expenses - income, true);
        for (int k = 0; k < 2; k++)
            rate_manager(g, m, uint8_t(int8_t(((rd32(r, rec + 0x854 + size_t(k) * 4) / 5000) * 200) / 7)), k + 4);
        board_month(g, m, rand, report, game);
    }
    if (report.unsupported) return;
    // 5. shares and loans
    turn_step = "month_change: shares";
    shares_and_loans(g);
}

}  // namespace bmh
