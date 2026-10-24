// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "modern/game/modern_state.hpp"
#include "modern/game/environment.hpp"
#include "modern/game/season.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

#include "modern/game/career.hpp"
#include "modern/game/matchday.hpp"
#include "modern/game/month.hpp"
#include "modern/game/national.hpp"
#include "modern/game/cup.hpp"
#include "game/lineup.hpp"
#include "game/schedule.hpp"
#include "game/table.hpp"
#include "game/messages.hpp"
#include "modern/game/prediction.hpp"
#include "modern/game/week.hpp"
#include "modern/game/weekly.hpp"

namespace bmh::modern {

namespace {

constexpr uint32_t kTransferList = ::bmh::layout::EditorList;  // 44 entries of 0x58 bytes
constexpr uint32_t kListEntry = 0x58;
constexpr uint32_t kOffers = ::bmh::layout::OfferSlots;        // OfferSlots: 4 offers, -1 = free
constexpr size_t kBoard = 0x840;             // tactics board of a manager
constexpr size_t kBoardSlot = 0x58;

uint32_t board32(const ModernState& g, size_t at) {
    return uint32_t(g.tactics.at(at) | g.tactics.at(at + 1) << 8 | g.tactics.at(at + 2) << 16) |
           uint32_t(g.tactics.at(at + 3)) << 24;
}

}  // namespace

void answer_offers(ModernState& g, int manager, bool accept, WatcomRandom& rand, MinuteReport& report, GameRandom* game) {
    StaticMemory& mem = g.memory;
    const int team = g.manager_teams.at(size_t(manager));
    const SquadRange range = squad_range(team);
    for (uint32_t k = 0; k < 4; k++) {
        const uint32_t offer = mem.u32(kOffers + k * 4);
        if (offer == 0xFFFFFFFFu || int(offer & 0x1f) != manager) continue;
        const int player = int(offer >> 10) & 0xFFF;
        size_t slot = range.first;
        while (slot < range.first + range.count && (g.squad_slots.at(slot) & 0xFFF) != player) slot++;
        if (slot != range.first + range.count) {
            const uint32_t e = kTransferList + (0x28 + k) * kListEntry;
            const size_t board = size_t(manager) * kBoard + (slot - range.first) * kBoardSlot;
            if (!(offer & 0x80000000u)) {
                if (!game) {
                    report.fail("an offer for a player: the transfer (IncomingTransferScreen)");
                    return;
                }
                // Opening the negotiation restarts the game generator and picks the negotiator.
                game->set_seed(int32_t(mem.u32(::bmh::layout::CareerSession_Invalid_RuntimeField2)));
                game->range(0x5c, 0x6c);
                // The fee offer is decided on its own (GameState::transfer_decision); `accept`
                // answers the salary demands below. Without a controller the manager refuses
                // the release, as in all recordings of the original.
                const int offered_by = int16_t(mem.u16(e));
                const bool sell = g.transfer_decision &&
                                  g.transfer_decision(g, manager, player, offered_by, int32_t(mem.u32(e + 0x1c)),
                                                      int32_t(mem.u32(e + 0x0c)));
                if (sell) {
                    const int buyer = offered_by;
                    if (buyer >= 0) {
                        const SquadRange target = squad_range(buyer);
                        size_t free = target.first;
                        while (free < target.first + target.count && g.squad_slots.at(free) != 0x0fff) free++;
                        if (free < target.first + target.count) {
                            if (!modern::remove_from_seller(g, team, player, report)) return;
                            g.squad_slots.at(free) = uint16_t(player);
                            modern::book_money(g, manager, -int32_t(mem.u32(e + 0x1c)), true, true);
                            modern::lineup_after_transfer(g, team);
                            modern::lineup_after_transfer(g, buyer);
                        }
                    }
                }
                g.squad_slots.at(transfer_list_slot() + 0x28 + k) = 0x0FFF;
                mem.set32(kOffers + k * 4, 0xFFFFFFFFu);
                continue;
            }
            // "<player> fordert ein Gehalt von X EU - sonst nutzt er seine Ausstiegsklausel!"
            if (!accept) {
                if (!game) {
                    report.fail("a salary demand declined: the player leaves (IncomingTransferScreen)");
                    return;
                }
                // "NEIN": IncomingTransferScreen opens the negotiation ("Entscheidung") like for a fee
                // offer - the game generator restarts, the negotiator is drawn - and the
                // manager refuses the release (right click): the offer is gone, the player
                // stays (confirmed by the game 1 and 2P recordings against the original)
                game->set_seed(int32_t(mem.u32(::bmh::layout::CareerSession_Invalid_RuntimeField2)));
                game->range(0x5c, 0x6c);
                g.squad_slots.at(transfer_list_slot() + 0x28 + k) = 0x0FFF;
                mem.set32(kOffers + k * 4, 0xFFFFFFFFu);
                continue;
            }
            if (!(offer & 0x20)) {
                // the new salary and its percentage of the market salary (AddToSquad with
                // 98 % and the contract years left)
                uint32_t fee = 0, salary = 0;
                modern::market_value(g, int(slot), 0, 0x62, fee, salary, (g.tactics.at(board + 0xc) + 6) / 0xc, 0, rand);
                const uint32_t offered = mem.u32(e + 0xc);
                for (int i = 0; i < 4; i++) g.tactics.at(board + 0x50 + size_t(i)) = uint8_t(offered >> (8 * i));
                // Deliberate deviation (#40): the original stores the percentage in the mood
                // byte without a limit, so a raise to more than 255 % of the market salary
                // (a rival's offer against few months left) wraps it to almost 0 - the player
                // then asks for next to nothing at his renewal (60.900 -> 8.500 in the QA).
                // Purchase and renewal clamp the same byte to 255; here too.
                g.tactics.at(board + 0xd) = uint8_t(std::min<uint32_t>(offered * 100 / salary, 0xff));
            }
        }
        g.squad_slots.at(transfer_list_slot() + 0x28 + k) = 0x0FFF;
        mem.set32(kOffers + k * 4, 0xFFFFFFFFu);
    }
    (void)board32;
}

void free_offer_slots(ModernState& g) {
    for (size_t k = 0; k < 4; k++) g.squad_slots.at(transfer_list_slot() + 0x28 + k) = 0x0FFF;
    for (uint32_t k = 0; k < 4; k++) g.memory.set32(kOffers + k * 4, 0xFFFFFFFFu);
}

void reset_club_moods(ModernState& g, GameRandom& game, WatcomRandom& rand, bool new_career) {
    const StaticMemory& mem = g.memory;
    // the title holders (cup table entries 0, 15, 19 and 21, +0x22) start at the top
    const uint16_t holders[4] = {mem.u16(::bmh::layout::Season_CareerSeasonSetup_RuntimeField4), mem.u16(::bmh::layout::Season_CareerSeasonSetup_RuntimeField), mem.u16(::bmh::layout::Season_CareerSeasonSetup_RuntimeField6), mem.u16(::bmh::layout::Season_CareerSeasonSetup_RuntimeField5)};
    rand.range(0, 2);  // local_18, a counter that is not used
    for (size_t team = 0; team < g.team_flags.size(); team++) {
        g.team_morale.at(team) = uint8_t((g.team_morale.at(team) & 0xc0) | rand.range(0x14, 0x1e));
        g.team_flags.at(team) = uint16_t(game.range(0, 2) * 2);
        int high = 3, low = 0;  // iVar7 (bits 14-15), iVar8 (bits 12-13)
        if (new_career) {
            high = 1, low = 1;  // their initial values, never changed in this mode
        } else if (std::find(std::begin(holders), std::end(holders), uint16_t(team)) == std::end(holders)) {
            const int pos = int16_t(modern::table_position(g, int(team)));  // TablePosition
            high = std::max(3 - pos / 6, 0);
            if (rand.range(0, 4) == 0) high = rand.range(0, 3);
            low = std::min(pos / 5, 3);
            if (rand.range(0, 6) == 0) low = rand.range(0, 3);
        }
        g.team_flags.at(team) = uint16_t(g.team_flags.at(team) | low << 12 | high << 14);
        modern::club_balance(g, int(team), rand);  // CareerSeasonSetup4
    }
}

void club_balance(ModernState& g, int team, WatcomRandom& rand) {
    // condition against technique of the whole squad, defence against attack, each shaken a
    // little and cut to 51..149 %, give bits 8-9 and 10-11 of the club flags
    const int a1 = team_average(g, team, 0xf1, &rand).value;
    const int a2 = team_average(g, team, 0xf2, &rand).value;
    const int a3 = team_average(g, team, 0x2f, &rand).value;
    const int a4 = team_average(g, team, 0x8f, &rand).value;
    uint32_t c = uint32_t(a1 * 100) / uint32_t(a2);
    uint32_t d = uint32_t(a3 * 100) / uint32_t(a4);
    c = (c & 0xffff) - uint32_t(int(c & 0xffff) / rand.range(2, 7));
    d = uint32_t(int(d & 0xffff) / rand.range(2, 6)) + (d & 0xffff);
    const int cc = std::clamp(int(int16_t(c)), 0x33, 0x95);  // ClampInteger
    const int dd = std::clamp(int(int16_t(d)), 0x33, 0x95);
    uint16_t f = uint16_t(g.team_flags.at(size_t(team)) & 0xf0ff);
    f = uint16_t(f | ((cc - 0x32) / 0x19) << 8 | ((dd - 0x32) / 0x19) << 10);
    g.team_flags.at(size_t(team)) = f;
}

namespace {

// TeamLeague2: the league of a club; ids above 0x164 (national teams) count as league 0
int club_league(int team) { return team > 0x164 ? 0 : team_league(team); }

// The European entrants (Career_CompetitionPointersEnglish_ExecutableField25 UEFA Cup, Career_CompetitionPointersEnglish_RuntimeField14 Champions League,
// Career_CompetitionPointersEnglish_ExecutableField19 Cup Winners' Cup) and their counts (Career_EuropeanClub_ExecutableField / Career_EuropeanClub_ExecutableField2 / Career_EuropeanClub_ExecutableField3).
struct Entrants {
    StaticMemory& mem;
    static constexpr uint32_t kList[3] = {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField25, ::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField14, ::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField19};
    static constexpr uint32_t kCount[3] = {::bmh::layout::Career_EuropeanClub_ExecutableField, ::bmh::layout::Career_EuropeanClub_ExecutableField2, ::bmh::layout::Career_EuropeanClub_ExecutableField3};
    // EuropeanClub: 1 UEFA Cup, 2 Champions League, 3 Cup Winners' Cup, 0 none
    int find(int team) const {
        for (int l = 0; l < 3; l++)
            for (int i = 0; i < int(int8_t(mem.u8(kCount[l]))); i++)
                if (mem.u16(kList[l] + uint32_t(i) * 2) == uint16_t(team)) return l + 1;
        return 0;
    }
    void add(int l, int team) {
        const int n = int8_t(mem.u8(kCount[l]));
        mem.set16(kList[l] + uint32_t(n) * 2, uint16_t(team));
        mem.set8(kCount[l], uint8_t(n + 1));
    }
};

// OnTableJust(league, rank, ranks): the club with that final rank (0 if none)
int club_at_rank(int league, int rank, const uint8_t* ranks) {
    for (int i = 0; i < kLeagueSizes[league]; i++)
        if (ranks[i] == rank) return league_first_team(league) + i;
    return 0;
}

// LeagueEntrants(league, cup winner, cup finalist, uefa places): the European places of a
// league - its champion (and the Champions League holder if he plays there, else the
// runner-up), the cup winner (or the finalist) and the cup winners' cup holder, the UEFA
// Cup holder and `uefa` more clubs by rank. Human managers note where they play.
void league_entrants(ModernState& g, int league, int winner, int finalist, int uefa) {
    StaticMemory& mem = g.memory;
    Entrants en{mem};
    const LeagueTable t = compute_league_table(g, league, 0, league_matchdays(league), 7, 1);
    uint8_t ranks[24] = {};
    for (int i = 0; i < t.size; i++) ranks[i] = uint8_t(t.rank(i));
    en.add(1, club_at_rank(league, 0, ranks));
    const int cl_holder = mem.u16(::bmh::layout::Season_CareerSeasonSetup_RuntimeField5);
    if (club_league(cl_holder) == league) en.add(1, en.find(cl_holder) == 0 ? cl_holder : club_at_rank(league, 1, ranks));
    if (en.find(winner) == 0) en.add(2, winner);
    else if (en.find(finalist) == 0) en.add(2, finalist);
    const int cw_holder = mem.u16(::bmh::layout::Season_CareerSeasonSetup_RuntimeField6);
    if (club_league(cw_holder) == league) {
        if (en.find(cw_holder) == 0) en.add(2, cw_holder);
        else if (en.find(finalist) == 0) en.add(2, finalist);
    }
    const int uefa_holder = mem.u16(::bmh::layout::Season_CareerSeasonSetup_RuntimeField4);
    if (club_league(uefa_holder) == league) {
        if (en.find(uefa_holder) == 0) en.add(0, uefa_holder);
        else uefa++;
    }
    for (int rank = 1; uefa != 0; rank++) {
        const int team = club_at_rank(league, rank, ranks);
        if (en.find(team) == 0) {
            en.add(0, team);
            uefa--;
        }
    }
    static const uint8_t kRound[3] = {1, 3, 2};  // SeasonRuntimeField
    for (size_t m = 0; m < g.manager_teams.size(); m++) {
        const int kind = en.find(g.manager_teams[m]);
        if (kind == 0) continue;
        // ManagerOf(manager, 2, ...): the European campaign of this season
        const size_t at = m * 0xB0C + 0xda + 2 + size_t(std::min<int>(g.season, 0x31)) * 3;
        g.manager_records.at(at) = uint8_t(kRound[kind - 1] + (kind - 1) * 0x20);
    }
}

// CareerSeasonSetup(1) up to the German entrants: the cup tables restart, the international
// tournament of the year (competition 7 - season % 4, national teams from id 357) is drawn
// and the European entrants are found (SeasonEntrantsFromGame).
void observe_initial_international_groups(ModernState& g, WatcomRandom& rand, int competition,
                                         const SeasonCupDrawHook& hook) {
    StaticMemory& mem = g.memory;
    const int first = modern::find_cup(mem, competition);
    int group_ordinal = 0;
    for (int index = first; index >= 0;) {
        const uint32_t entry = modern::cup_entry_address(index);
        SeasonCupDrawEvent event;
        event.entry_address = entry;
        event.competition = competition;
        event.flags = mem.u8(entry + 0x14);
        event.group_stage = (event.flags & 4) == 0;
        event.group_index = group_ordinal;
        event.group_count = mem.u8(entry + 0x1e) & 0xf;
        event.round = modern::cup_round(mem, index);
        event.offset = 0;
        event.count = mem.u8(entry + 0x1a);
        const uint32_t list = mem.pointer(entry);
        event.participants.reserve(event.count);
        for (size_t i = 0; i < event.count; ++i) event.participants.push_back(mem.u16(list + uint32_t(i) * 2));
        event.phase = SeasonCupDrawPhase::draw;
        if (hook) hook(g, event);
        event.phase = SeasonCupDrawPhase::overview;
        modern::cup_group_overview_effects(g, event.group_index);
        if(event.group_stage) {
            const int schedule_teams=modern::cup_group_schedule_teams(mem,entry);
            const int rounds=mem.u8(entry+0x1b)/((event.flags&1)+1);
            const uint32_t teams=mem.pointer(entry);
            for(const Fixture& fixture:generate_fixtures(schedule_teams,event.round+1,rounds,0)) {
                for(int position:{fixture.home,fixture.away}) {
                    const int team=mem.u16(teams+uint32_t(position)*2);
                    const bool manager_national=mem.has(::bmh::layout::CupDraw_FormatError_RuntimeField2)&&mem.has(::bmh::layout::CupDraw_FormatError_RuntimeField3)&&
                        mem.u16(::bmh::layout::CupDraw_FormatError_RuntimeField2)==no_team()&&mem.u16(::bmh::layout::CupDraw_FormatError_RuntimeField3)==team;
                    const bool human_owned=g.manager_of(team)>=0||manager_national;
                    if(team<first_foreign_cup()||team==no_team()||!human_owned)continue;
                    SeasonCupDrawEvent::GroupAverageSnapshot snapshot;
                    snapshot.runtime_id=uint16_t(team);
                    snapshot.values={team_average(g,team,0x0f1,&rand).value,
                                     team_average(g,team,0x0f2,&rand).value,
                                     team_average(g,team,0x0f4,&rand).value,
                                     team_average(g,team,0x0f8,&rand).value,
                                     team_average(g,team,0x0ff,&rand).value};
                    event.group_average_snapshots.push_back(snapshot);
                }
            }
        }
        if (hook) hook(g, event);
        if (int8_t(mem.u8(entry + 0x17)) == 0) break;
        ++index;  // CupDrawFromGame advances to the adjacent 0x28-byte record
        ++group_ordinal;
    }
}

void european_setup(ModernState& g, GameRandom& game, WatcomRandom& rand, const SeasonCupDrawHook& cup_draw) {
    StaticMemory& mem = g.memory;
    for (uint32_t i = 0; i < 0x24; i++) mem.set8(::bmh::layout::MatchdaySetup_IndoorTournamentReset_RuntimeField + i * 0x28, 0);  // played counters
    const std::pair<uint32_t, uint32_t> blocks[8] = {{::bmh::layout::Career_CompetitionPointers_ExecutableField, 0x7e}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField22, 0x80}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField23, 0x80}, {::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField17, 0x9a},
                                                     {::bmh::layout::Savestate_Layout_ExecutableField4, 0x62}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField26, 0x40}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField21, 0x40}, {::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField13, 0x68}};
    for (const auto& [at, size] : blocks)
        for (uint32_t i = 0; i < size; i++)
            if (mem.has(at + i)) mem.set8(at + i, 0xcc);
    if (mem.has(::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField31)) mem.set16(::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField31, 0xcccc);
    for (size_t m = 0; m < g.manager_teams.size(); m++)
        for (size_t i = 0; i < 2; i++) g.manager_records.at(m * 0xB0C + 0xdb + size_t(g.season) * 3 + i) = 0;
    // the international tournament: its host (or last winner and finalist) and more teams
    // drawn from four pools, none twice and none of the last field
    static const uint8_t kPoolFixed[16] = {2, 0, 0, 0, 7, 0, 0, 0, 3, 0, 0, 0, 10, 4, 8, 0};  // SeasonRuntimeField2
    static const uint8_t kField[4] = {4, 8, 6, 24};                                        // SeasonRuntimeField3
    static const uint8_t kPoolFrom[4] = {0x00, 0x0f, 0x20, 0x2f};                           // SeasonRuntimeField4
    static const uint8_t kPoolTo[4] = {0x0f, 0x20, 0x2f, 0x3e};                             // SeasonRuntimeField5
    constexpr uint32_t kField0 = ::bmh::layout::Field0;
    if (!mem.has(kField0)) mem.add_block(kField0, 0x30);
    const int s = g.season % 4;
    const int base = no_team();  // ClubPlayerDetail_ClubPlayerDetailFromGame_RuntimeField3 + CareerSession_DispatchLineup_RuntimeField + CareerSession_InstallLeagueSizes_RuntimeField6
    int n = 1;
    std::vector<uint16_t> last;
    if (s % 2 == 0) {
        mem.set16(kField0, uint16_t(base));
    } else {
        const uint32_t e = modern::cup_entry_address(0xe - s / 2);
        const int count = mem.u8(e + 0x1a);
        for (int i = 0; i < count; i++) last.push_back(mem.u16(kField0 + uint32_t(i) * 2));
        mem.set16(kField0, mem.u16(e + 0x22));
        if (mem.u8(e + 0x1f) == 2) mem.set16(kField0 + 2, mem.u16(e + 0x24)), n = 2;
    }
    for (int k = 0; k < 4; k++) {
        const int fixed = kPoolFixed[k + s * 4];
        int left = fixed != 0 ? rand.range(fixed - 2, fixed) : kField[s] - n;
        while (int16_t(left) != 0) {
            const uint16_t t = uint16_t(rand.range(base + kPoolFrom[k], kPoolTo[k] + base - 1));
            mem.set16(kField0 + uint32_t(n) * 2, t);
            bool twice = false;
            for (int i = 0; i < n && !twice; i++) twice = mem.u16(kField0 + uint32_t(i) * 2) == t;
            if (!twice && std::find(last.begin(), last.end(), t) == last.end()) n++, left--;
        }
    }
    for (uint32_t i = 0; i < kField[s]; i++) {  // PrepareEvent
        const uint32_t j = uint32_t(game.range(0, kField[s] - 1));
        const uint16_t a = mem.u16(kField0 + i * 2);
        mem.set16(kField0 + i * 2, mem.u16(kField0 + j * 2));
        mem.set16(kField0 + j * 2, a);
    }
    // CupDrawFromGame(entry of competition 7 - s): the draw of the tournament's first round,
    // shown unless switched off
    const int shown_type = 7 - s;
    if (mem.u8(::bmh::layout::LeaguePresentationOptions + uint32_t(shown_type + 5) * 9) == 0)
        observe_initial_international_groups(g, rand, shown_type, cup_draw);
    // SeasonEntrantsFromGame: the entrants of the first division (cup winner and finalist) and of
    // the four foreign leagues (two random clubs), then the holders not placed yet
    Entrants en{mem};
    mem.set8(::bmh::layout::Career_EuropeanClub_ExecutableField, 0), mem.set8(::bmh::layout::Career_EuropeanClub_ExecutableField3, 0), mem.set8(::bmh::layout::Career_EuropeanClub_ExecutableField2, 0);
    static const uint8_t kLeague[5] = {0, 5, 6, 7, 8};  // SeasonRuntimeField6
    static const uint8_t kUefa[5] = {4, 4, 4, 4, 3};    // SeasonRuntimeField7
    for (int u = 0; u < 5; u++) {
        int a = 0, b = 0;
        if (u == 0) {
            // the national cup winner and finalist (+0x22/+0x24): entry 0, in the English
            // structure the FA Cup proper, entry 28 (U16: SeasonRuntimeField8/83c)
            const uint32_t cup = kEnglishCompetitions ? ::bmh::layout::CupTable + 28 * 0x28 : ::bmh::layout::CupTable;
            a = mem.u16(cup + 0x22), b = mem.u16(cup + 0x24);
            if (mem.has(::bmh::layout::Season_CareerSeasonSetup_RuntimeField7)) mem.set16(::bmh::layout::Season_CareerSeasonSetup_RuntimeField7, uint16_t(a));
        } else {
            a = rand.range(0, kLeagueSizes[kLeague[u]] - 1);
            do b = rand.range(0, kLeagueSizes[kLeague[u]] - 1); while (b == a);
        }
        const int first = kLeague[u] == 0 ? 0 : league_first_team(kLeague[u]);
        league_entrants(g, kLeague[u], uint8_t(a + first), uint8_t(b + first), kUefa[u]);
    }
    const int holders[3][2] = {{0, mem.u16(::bmh::layout::Season_CareerSeasonSetup_RuntimeField4)}, {1, mem.u16(::bmh::layout::Season_CareerSeasonSetup_RuntimeField5)}, {2, mem.u16(::bmh::layout::Season_CareerSeasonSetup_RuntimeField6)}};
    for (const auto& h : holders)
        if (en.find(h[1]) == 0) en.add(h[0], h[1]);
    // CompetitionWinnerScreen(1, 2..4, list, count): the German clubs in each competition are shown
    // (the first pause of the recording)
}

// The rest of CareerSeasonSetup(1): the three European fields are filled up with foreign
// clubs (from id 168 on, one of every block of seven, none twice), shuffled
// (PrepareEvent) and their first rounds shown (CupDrawFromGame, no draws while switched off):
// Cup Winners' Cup to SeasonRuntimeField9 (32), Champions League to 24, UEFA Cup to
// SeasonRuntimeField10 (64, two per block after the first 16 blocks).
void european_fields(ModernState& g, GameRandom& game, WatcomRandom& rand) {
    StaticMemory& mem = g.memory;
    Entrants en{mem};
    const int pool = first_foreign_cup();  // CareerSession_DispatchLineup_RuntimeField + CareerSession_InstallLeagueSizes_RuntimeField6: the first foreign club
    auto pick = [&](int from) {
        int t;
        do t = rand.range(from, from + 6); while (en.find(t) != 0);
        return t;
    };
    auto shuffle = [&](uint32_t list, int n) {  // PrepareEvent
        for (uint32_t i = 0; i < uint32_t(n); i++) {
            const uint32_t j = uint32_t(game.range(0, n - 1));
            const uint16_t a = mem.u16(list + i * 2);
            mem.set16(list + i * 2, mem.u16(list + j * 2));
            mem.set16(list + j * 2, a);
        }
    };
    int from = pool;
    for (int i = int8_t(mem.u8(::bmh::layout::Career_EuropeanClub_ExecutableField3)); i < 0x20; i++, from += 7) en.add(2, pick(from));
    shuffle(::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField19, 0x20);
    from = pool;
    for (int i = int8_t(mem.u8(::bmh::layout::Career_EuropeanClub_ExecutableField2)); i < 0x18; i++, from += 7) en.add(1, pick(from));
    shuffle(mem.pointer(::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField5), 0x10);  // (the team list of cup entry 16, Career_CompetitionPointersEnglish_ExecutableField5)
    from = pool;
    const int start = int8_t(mem.u8(::bmh::layout::Career_EuropeanClub_ExecutableField));
    int per = 2;
    for (int i = start; i < 0x40; i++, from += 7) {
        for (int k = 0; k < per; k++) en.add(0, pick(from));
        if (i - start == 0x10) per = 1;
    }
    shuffle(::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField25, 0x40);
    mem.set8(::bmh::layout::Career_EuropeanClub_ExecutableField, 0x40), mem.set8(::bmh::layout::Career_EuropeanClub_ExecutableField2, 0x18), mem.set8(::bmh::layout::Career_EuropeanClub_ExecutableField3, 0x20);
}

int32_t rec32(const ModernState& g, int m, size_t o) {
    const size_t at = size_t(m) * 0xB0C + o;
    return int32_t(uint32_t(g.manager_records.at(at) | g.manager_records.at(at + 1) << 8 | g.manager_records.at(at + 2) << 16) |
                   uint32_t(g.manager_records.at(at + 3)) << 24);
}
void set_rec32(ModernState& g, int m, size_t o, int32_t v) {
    for (size_t i = 0; i < 4; i++) g.manager_records.at(size_t(m) * 0xB0C + o + i) = uint8_t(uint32_t(v) >> (8 * i));
}
uint8_t& rec8(ModernState& g, int m, size_t o) { return g.manager_records.at(size_t(m) * 0xB0C + o); }
uint16_t mem_u16(const ModernState& g, uint32_t a) { return g.memory.u16(a); }

// HighscoreScore(mask, 0, 0xff): bit 0 the account (+0x314), 1 the deposit (+0x318), 2 the
// fixed deposits (Advertising_OriginalShareAdjustment_RuntimeField x FundBlock), 3 the shares (block StockExchange), 4 the credits: with
// mask 0x10 alone what is still owed (Analytics_SourceAssetMaskE_RuntimeField - bd720), otherwise minus the amounts
int32_t assets(const ModernState& g, int m, int mask) {
    const StaticMemory& mem = g.memory;
    int32_t sum = 0;
    if (mask & 1) sum += rec32(g, m, 0x314);
    if (mask & 2) sum += rec32(g, m, 0x318);
    if (mask & 4)
        for (uint32_t k = 0; k < 6; k++)
            sum += int32_t(mem.u16(::bmh::layout::Advertising_OriginalShareAdjustment_RuntimeField + uint32_t(m) * 2 + k * 0x1c) * mem.u32(::bmh::layout::FundBlock + k * 0x1c));
    if (mask & 8)
        for (size_t k = 0; k < 12; k++)
            sum += int32_t(uint32_t(g.stocks.at(k * 0x5c + 0x50 + size_t(m) * 2) | g.stocks.at(k * 0x5c + 0x51 + size_t(m) * 2) << 8) *
                           uint32_t(g.stocks.at(k * 0x5c + 0x4e) | g.stocks.at(k * 0x5c + 0x4f) << 8));
    if (mask & 0x10)
        for (uint32_t b = 0; b < 5; b++) {
            const uint32_t at = ::bmh::layout::Analytics_SourceAssetMaskE_RuntimeField + uint32_t(m) * 0x78 + b * 0x18;
            sum += mask == 0x10 ? int32_t(mem.u32(at) - mem.u32(at + 0x10)) : -int32_t(mem.u32(at));
        }
    return sum;
}

// ClubAttraction(club, pay): how popular a club is (0..100) - its European campaign, the
// championship and the cups it holds, its strength and position. With `pay` the
// manager (ActiveManager) gets the premiums for the titles (messages, record +0x874).
int club_popularity(ModernState& g, int m, int team, bool pay) {
    StaticMemory& mem = g.memory;
    Entrants en{mem};
    static const uint8_t kKind[4] = {0, 1, 3, 2};                      // SeasonRuntimeField11
    static const uint8_t kEntry[4] = {0x00, 0x0f, 0x13, 0x15};         // SeasonRuntimeField12
    static const uint8_t kPlus[4] = {0x14, 0x1e, 0x14, 0x0f};          // SeasonRuntimeField13
    static const int32_t kPremium[4] = {500000, 1500000, 2500000, 1500000};  // SeasonRuntimeField14
    int8_t p = int8_t(kKind[en.find(team)] * 10 + 10);
    if (team == mem.u16(::bmh::layout::Cup_AdvanceCup_RuntimeField6)) {
        if (pay) {
            post_composed(g, m, kChampionshipBonusText);  // ChampionshipBonusText
            modern::book_money(g, m, -700000, true);
            set_rec32(g, m, 0x874, rec32(g, m, 0x874) + 1000000);
        }
        p = int8_t(p + 10);
    }
    for (int k = 0; k < 4; k++) {
        if (team != mem.u16(::bmh::layout::Season_CareerSeasonSetup_RuntimeField + uint32_t(kEntry[k]) * 0x28)) continue;
        p = int8_t(p + kPlus[k]);
        if (pay) {
            post_composed(g, m, message_text(g, ::bmh::layout::Season_ClubPopularity_TextPointer) + message_text(g, ::bmh::layout::CupDraw_FormatError_ExecutableField6 + uint32_t(k) * 4) +
                                    message_text(g, ::bmh::layout::Season_ClubPopularity_TextPointer2) + format_amount(g, kPremium[k]) + ".");
            modern::book_money(g, m, -kPremium[k], true);
            set_rec32(g, m, 0x874, rec32(g, m, 0x874) + kPremium[k]);
        }
    }
    const int avg = team_average(g, team, 0xff).value;
    int pos = int8_t(modern::table_position(g, team));
    if (pos == -1) pos = 10;
    int league = club_league(team);
    if (int8_t(league) > 2) league--;
    int v = int16_t((int(p) << 2) / 5) + int16_t(uint32_t(uint16_t((4 - int8_t(league)) * 0x16 + (0x14 - int8_t(pos)))) * 0x28 / 0x6c) +
            int16_t(uint32_t(uint16_t(avg)) * 2 / 10);
    return std::clamp(int(int16_t(v)), 0, 100);  // ClampInteger
}

// CareerSeasonSetup2(manager): the season tickets - popularity above the league's base plus
// W(0, 10) in steps of 300 on top of the base number (SeasonRuntimeField15), less 5 % per game
// level, sold at the league's price (SeasonRuntimeField16); record +0x2ac, +0x754, +0x868
void season_tickets(ModernState& g, int m, WatcomRandom& rand) {
    static const uint8_t kBase[5] = {50, 40, 30, 30, 20};                  // SeasonRuntimeField17
    static const uint16_t kTickets[5] = {5000, 2000, 1000, 1000, 300};     // SeasonRuntimeField15
    static const uint16_t kPrice[5] = {330, 270, 240, 240, 225};           // SeasonRuntimeField16
    const int team = g.manager_teams.at(size_t(m));
    const int league = std::min(club_league(team), 4);
    const int pop = club_popularity(g, m, team, true);
    const int low = std::max(pop - kBase[league], 0);
    const int r = rand.range(low, low + 10);
    const int sold = ((100 - g.level * 5) * (kTickets[league] + r * 300)) / 100;
    g.manager_records.at(size_t(m) * 0xB0C + 0x2ac) = uint8_t(sold);
    g.manager_records.at(size_t(m) * 0xB0C + 0x2ad) = uint8_t(sold >> 8);
    const int32_t income = int32_t(kPrice[league]) * int16_t(sold);
    modern::book_money(g, m, -income, true);
    set_rec32(g, m, 0x754, rec32(g, m, 0x754) + income);
    set_rec32(g, m, 0x868, rec32(g, m, 0x868) + income);
    post_composed(g, m, message_text(g, ::bmh::layout::Season_SeasonTickets_TextPointer) + format_number(g, int16_t(sold), 1) + message_text(g, ::bmh::layout::Season_SeasonTickets_TextPointer2) +
                            format_amount(g, income) + message_text(g, ::bmh::layout::Season_SeasonTickets_TextPointer3));
}

// SeasonEvent: a random event of the summer (a bonus, a fine, ...) for a manager whose
// finances went badly; otherwise only its draw is spent
void season_event(ModernState& g, int m, GameRandom& game, WatcomRandom& rand, MinuteReport& report) {
    const int32_t worth = assets(g, m, 0xf) - assets(g, m, 0x10) + game.range(500000, 1000000);
    // Int64T: bankrupt, or out of credit
    const uint8_t cut = rec8(g, m, 0x220);
    const int32_t total = int32_t(rec8(g, m, 0x26f)) * 80000 + (assets(g, m, 0xf) - assets(g, m, 0x10));
    if (total < -6000000 || ((cut >> 5) == 2 && int32_t(cut & 0x1f) * 100000 <= rec32(g, m, 0x330))) return;
    if (g.memory.u8(::bmh::layout::GameMode) < 3) set_rec32(g, m, 0x320, rec32(g, m, 0x314) - rec32(g, m, 0x324));
    bool poor = false;
    int32_t w = worth;
    if (rec32(g, m, 0x314) - rec32(g, m, 0x324) < rec32(g, m, 0x320) - 1000000) {
        poor = true;
        if (rec8(g, m, 0x26f) < 0x3c) w -= 1500000;
    }
    if ((!poor || rand.range(0, 1) == 0) && w + int32_t(rec8(g, m, 0x26f)) * 80000 > -3000001) return;
    (void)report;
    // The board's sanction (kind G(0, 3)): 0 and 2 last n weeks/points, 1 cuts the credit line
    // to n * 100000, 3 is a fine paid at once. Ranges SeasonRuntimeField18 / SeasonRuntimeField19, factor
    // SeasonRuntimeField20 (above 1: thousands of money).
    static const int kMin[4] = {4, 8, 2, 4}, kMax[4] = {8, 15, 6, 10}, kFactor[4] = {1, 100, 1, 50};
    const int kind = game.range(0, 3);
    std::string text = kind == 0 || kind == 3 ? message_text(g, ::bmh::layout::Season_SeasonEvent_TextPointer3) : std::string();
    int n = game.range(kMin[kind], kMax[kind]);
    int32_t value = kFactor[kind] * n;
    if (kFactor[kind] > 1) value *= 1000;
    text += message_text(g, ::bmh::layout::Season_SeasonEvent_TextPointer + uint32_t(kind) * 4);  // Season_SeasonEvent_TextPointer[kind]
    // FormatNumber: the number (an amount of money for kinds 1 and 3), then Season_SeasonEvent_TextPointer2[kind]
    text += format_number(g, value, kind == 1 || kind == 3 ? 0x101 : 1) + message_text(g, ::bmh::layout::Season_SeasonEvent_TextPointer2 + uint32_t(kind) * 4);
    if (kind == 0) n--;
    if (kind == 3) {
        modern::book_money(g, m, value, true, true);  // FinanceStatisticsEnabled = 1: into the statistics
    } else {
        // Season_SeasonAccounts_RuntimeField[manager]: becomes the record's sanction byte +0x220 at the season change
        if (!g.memory.has(::bmh::layout::Season_SeasonAccounts_RuntimeField)) g.memory.add_block(::bmh::layout::Season_SeasonAccounts_RuntimeField, 4);
        g.memory.set8(::bmh::layout::Season_SeasonAccounts_RuntimeField + uint32_t(m), uint8_t(n | (kind + 1) << 5));
    }
    post_composed(g, m, text);  // PostMessage
}

}  // namespace

int32_t season_profit_tax(int32_t profit) {
    // GameEndTaxAnimationScreen/SeasonProfitTax: six progressive bands. The original rounds the
    // annual result to 100 EU before this point, divides each band by ten first,
    // then multiplies by its 0..5 rate. Preserve that integer order.
    if (profit <= 100000) return 0;
    int32_t remaining = profit;
    int32_t tax = 0;
    for (int band = 0; band < 6 && remaining > 0; ++band) {
        int32_t taxable = remaining;
        if (band < 5 && taxable > 100000) taxable = 100000;
        tax += (taxable / 10) * band;
        remaining -= taxable;
    }
    return tax;
}

int32_t season_profit_tax_refund(int32_t profit, int32_t previous_taxable_profit,
                                 int32_t previous_tax_paid) {
    if (profit >= 100000 || previous_taxable_profit <= 0 || previous_tax_paid <= 0 ||
        previous_taxable_profit / 100 == 0)
        return 0;
    // Keep GameEndTaxAnimationScreen's division and multiplication order. A malformed saved
    // denominator is safely ignored; valid source values are rounded to 100 EU.
    int32_t base = profit + previous_taxable_profit;
    if (base < 200000) base = 200000;
    int32_t retained_tax = (((previous_tax_paid / 100) * ((base - 200000) / 1000)) /
                            (previous_taxable_profit / 100)) * 1000;
    if (retained_tax < 0) retained_tax = 0;
    return std::max(previous_tax_paid - retained_tax, 0);
}

void season_apply_profit_tax(ModernState& g, int manager, int32_t profit,
                             GameRandom& game, WatcomRandom& rand,
                             const SeasonTaxHooks& hooks, SeasonTaxState* continuation) {
    if (profit <= 100000) return;
    SeasonTaxEvent event;
    event.manager_slot = manager;
    event.team = g.manager_teams.at(size_t(manager));
    event.profit = profit;
    event.legal_tax = modern::season_profit_tax(profit);
    event.paid_tax = event.legal_tax;
    event.fine = continuation ? continuation->carried_fine : 0;
    // SystemIdentity is an acknowledgement, not PostMessage mailbox delivery.
    event.text = message_text(g, ::bmh::layout::Season_SeasonApplyProfitTax_TextPointer) + format_amount(g, profit) +
                 message_text(g, ::bmh::layout::Season_SeasonApplyProfitTax_TextPointer2) + format_amount(g, event.legal_tax) + ")."; // SeasonRuntimeField21
    if (hooks.observe) hooks.observe(g, event);
    event.kind = SeasonTaxEventKind::question;
    event.text = message_text(g, ::bmh::layout::Season_SeasonApplyProfitTax_TextPointer3);
    const bool adjust = hooks.choose && hooks.choose(g, event);
    if (adjust) {
        event.caught = game.range(1, 10) >= 10;
        event.animation_variant = event.caught ? 4 : rand.range(1, 3);
        event.kind = SeasonTaxEventKind::animation;
        event.text.clear();
        if (hooks.observe) hooks.observe(g, event);
        if (!event.caught) {
            event.paid_tax = int32_t(uint32_t(event.legal_tax) * uint32_t(game.range(6, 9))) / 10;
            event.text = message_text(g, ::bmh::layout::Season_SeasonApplyProfitTax_TextPointer6) + format_amount(g, event.legal_tax) +
                         message_text(g, ::bmh::layout::Season_SeasonApplyProfitTax_TextPointer7) + format_amount(g, event.paid_tax) +
                         message_text(g, ::bmh::layout::Season_SeasonApplyProfitTax_TextPointer8);
        } else {
            event.fine = rand.range(1, 3) * 100000;
            if (continuation) continuation->carried_fine = event.fine;
            event.text = message_text(g, ::bmh::layout::Season_SeasonApplyProfitTax_TextPointer4) + format_amount(g, event.fine) +
                         message_text(g, ::bmh::layout::Season_SeasonApplyProfitTax_TextPointer5);
            // Source SeasonRuntimeField22: the last ten competition-4 ratings of this manager.
            for (size_t i = 0; i < 10; ++i)
                g.manager_ratings.at(size_t(manager) * 0x3c + 0x28 + i) = 0;
            rec8(g, manager, 0x26f) = uint8_t(std::clamp(int(rec8(g, manager, 0x26f)) - 10, 0, 100));
        }
        event.kind = SeasonTaxEventKind::outcome;
        if (hooks.observe) hooks.observe(g, event);
    }
    // Both Source carries describe the tax, while the forced debit includes the fine.
    set_rec32(g, manager, 0x328, event.paid_tax);
    set_rec32(g, manager, 0x32c, profit);
    modern::book_money(g, manager, event.paid_tax + event.fine, true);
}

namespace {

}  // namespace

// OpenBMH deviates from the original: retains the enhanced academy player-generation strength bands with monthly youth budgets.
// The middle youth budget enables the existing enhanced academy recruitment.
// Old lump-sum contracts remain eligible after their saved season payment.
// Player search, strength bands, age and board setup retain the shared rules.
bool youth_academy_player(ModernState& g, int m, GameRandom& game, WatcomRandom& rand, MinuteReport& report) {
    const int team = g.manager_teams.at(size_t(m));
    const int league = club_league(team);
    if (!modern::environment_academy_active(g, m)) return false;
    if (modern::squad_count(g, team, 3) > 0x16) return false;
    const auto budget = g.memory.u32(::bmh::layout::Spending + uint32_t(m) * 0x24 + kYouthAcademy * 4);
    if (budget <= uint32_t(kEnvironmentStep[kYouthAcademy] * 6) &&
        g.memory.u8(::bmh::layout::CareerDuration) != 1 &&
        int(g.memory.u8(::bmh::layout::EnvironmentYouthTeams + uint32_t(m) * 0x24 + 2)) <
            team_average(g, team, 0xf3).value - 25)
        return false;
    const SquadRange own = squad_range(team);
    if (game.range(0, 0) != 0) return false;
    for (int t = 0xf;; --t) {
        for (int s = 0x86f; s >= 0; --s) {
            const int club = s / 24;
            if (g.manager_of(modern::slot_team(s)) >= 0) {
                s -= 0x17;  // the whole squad of a human manager
                continue;
            }
            const int player = g.squad_slots.at(size_t(s)) & 0xfff;
            if (player == 0xfff) continue;
            bool loan = false;  // BoughtEntry
            for (int k = 0; k < 200 && !loan; k++) {
                const uint32_t e = g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + uint32_t(k) * 4);
                loan = e != 0 && int(e & 0xfff) == player;
            }
            if (loan) continue;
            const int age = int((g.players.at(size_t(player)).w364 >> 21) & 0x1f);
            if (age > game.range(t, t + 3)) continue;
            if (!modern::remove_from_seller(g, club, player, report)) return false;
            static const int kLevel[4][2] = {{0x4f, 0x5f}, {0x45, 0x55}, {0x31, 0x44}, {0x1d, 0x30}};
            const int band = league == 0 ? 0 : league == 1 ? 1 : league < 4 ? 2 : 3;
            const int level = game.range(kLevel[band][0], kLevel[band][1]);
            modern::generate_player(g, player, level | 0x3000, game, rand);
            modern::new_player_identity(g, player);  // the youth player is a new person (almanac)
            // PendingOfferAnswer(manager, player | 0xf000, -1, squad start)
            int slot = -1;
            for (size_t q = own.first; q < own.first + 24 && slot < 0; q++)
                if (g.squad_slots.at(q) == 0xfff) slot = int(q);
            if (slot < 0) return false;  // (the original shows PendingOffer_FormatError_ExecutableField)
            g.squad_slots.at(size_t(slot)) = uint16_t(player);
            modern::new_board_entry(g, m, slot - int(own.first), slot, rand);
            g.tactics.at(size_t(m) * 0x840 + size_t(slot - int(own.first)) * 0x58 + 0xd) = 0x69;
            uint32_t& w = g.players.at(size_t(player)).w364;
            w = (w & 0xff00ffffu) | uint32_t(uint8_t(rand.range(0, 4) << 5)) << 16;
            return true;
        }
        if (t == 0) return false;
    }
}

namespace {

// GameEndTaxAnimationScreen for one manager (SeasonProfitTax repeats it for the others): the summer
// event, the players' season bonuses, the championship mark, the new credit line from
// the season's result, taxes on a profit and refunds, then the season tickets
void season_accounts(ModernState& g, int m, GameRandom& game, WatcomRandom& rand, MinuteReport& report,
                     const SeasonTaxHooks& tax_hooks, SeasonTaxState& tax_state) {
    for (size_t i = 0; i < 0xc; i++) rec8(g, m, 0x754 + i) = 0;
    modern::finish_youth_season(g, m);
    if (!g.memory.has(::bmh::layout::Season_SeasonAccounts_RuntimeField)) g.memory.add_block(::bmh::layout::Season_SeasonAccounts_RuntimeField, 4);  // (runtime only, not saved)
    g.memory.set8(::bmh::layout::Season_SeasonAccounts_RuntimeField + uint32_t(m), 0);
    season_event(g, m, game, rand, report);
    if (report.unsupported) return;
    const int team = g.manager_teams.at(size_t(m));
    rec8(g, m, 0x21f) = 0;
    if (g.manager_teams.size() > 1) rec8(g, m, 0x21c) = 0;
    const int pos = int16_t(modern::table_position(g, team));
    const int league = club_league(team);
    const SquadRange range = squad_range(team);
    for (size_t s = 0; s < 24; s++)
        if (g.squad_slots.at(range.first + s) != 0x0FFF) modern::season_bonus(g, m, int(s), uint8_t(pos), true);
    // OpenBMH deviates from the original: retains the enhanced academy player-generation strength bands with monthly youth budgets.
    // Academy recruitment is posted after the youth season has settled.
    if (modern::youth_academy_player(g, m, game, rand, report)) post_text(g, m, text_id(::bmh::layout::Season_SeasonAccounts_TextPointer));
    if (report.unsupported) return;
    if (league == 0 && pos < 100) rec8(g, m, 0x21c) = 1;  // the best human club of the first division
    set_rec32(g, m, 0x324, rec32(g, m, 0x324) + rec32(g, m, 0x30c));
    set_rec32(g, m, 0x30c, 0);
    const int32_t result = ((rec32(g, m, 0x314) - rec32(g, m, 0x324)) / 100) * 100;
    const int32_t before = rec32(g, m, 0x320);
    const int16_t step = int16_t(int64_t(std::abs(result - before)) / 200000);
    int16_t credit = 0;
    if (result < 0 && result + 1000000 + (10 - g.level) * 100000 < before) credit = int16_t(-step);
    if (result > 0 && std::abs(result + 800000 - before) < 1500000) credit = int16_t(result / 250000 - step);
    credit = int16_t(std::clamp(int(int16_t(credit + rec8(g, m, 0x26f))), 0, 100));
    set_rec32(g, m, 0x31c, credit * 20000);
    post_composed(g, m, message_text(g, ::bmh::layout::Season_SeasonAccounts_TextPointer2) + format_amount(g, credit * 20000) + message_text(g, ::bmh::layout::Season_SeasonAccounts_TextPointer3));
    rec8(g, m, 0x26f) = uint8_t(credit);
    if (result > 100000) {
        modern::season_apply_profit_tax(g, m, result, game, rand, tax_hooks, &tax_state);
    }
    const int32_t taxed = rec32(g, m, 0x32c), tax = rec32(g, m, 0x328);
    if (result < 100000 && taxed != 0 && tax != 0) {
        const int32_t back = modern::season_profit_tax_refund(result, taxed, tax);
        if (back > 0) {
            post_composed(g, m, message_text(g, ::bmh::layout::Season_SeasonAccounts_TextPointer4) + format_amount(g, back) + ".");
            set_rec32(g, m, 0x314, rec32(g, m, 0x314) + back);
        }
    }
    if (result < 0x186a1) set_rec32(g, m, 0x32c, 0);
    set_rec32(g, m, 0x320, 0);
    set_rec32(g, m, 0x324, rec32(g, m, 0x314));
    for (size_t i = 0; i < 0x48; i++) rec8(g, m, 0x85c + i) = 0;
    season_tickets(g, m, rand);  // ChangePlaces -> CareerSeasonSetup2
}

// ClubsChangePlaces(a, b): two clubs (ids below 168) change places - every table per club id
// swaps its entries, lists of club ids replace the one by the other. Squads, position
// histories and loans only move between league clubs (ids below 138); the table *ClubAuxiliaryBytes
// swaps twice for them (no change), once otherwise.
void change_places(ModernState& g, int a, int b) {
    StaticMemory& mem = g.memory;
    if (a >= first_foreign_cup() || b >= first_foreign_cup()) return;
    auto swap_bytes = [](std::vector<uint8_t>& v, size_t x, size_t y, size_t n) {
        for (size_t i = 0; i < n; i++)
            if (x + i < v.size() && y + i < v.size()) std::swap(v[x + i], v[y + i]);
    };
    auto swap_mem8 = [&](uint32_t base) {
        const uint8_t t = mem.u8(base + uint32_t(a));
        mem.set8(base + uint32_t(a), mem.u8(base + uint32_t(b)));
        mem.set8(base + uint32_t(b), t);
    };
    auto replace = [&](uint32_t at, int n) {  // U163
        for (int i = 0; i < n; i++) {
            const uint16_t v = mem.u16(at + uint32_t(i) * 2);
            if (v == uint16_t(a)) mem.set16(at + uint32_t(i) * 2, uint16_t(b));
            else if (v == uint16_t(b)) mem.set16(at + uint32_t(i) * 2, uint16_t(a));
        }
    };
    const bool both_league_clubs = a < league_clubs() && b < league_clubs();
    if (both_league_clubs)
        for (uint32_t k = 0; k < 200; k++) {  // loans: seller and buyer
            const uint32_t e = mem.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + k * 4);
            if (e == 0) continue;
            int seller = int(e >> 12) & 0x3ff, buyer = int(e >> 22) & 0x3ff;
            seller = seller == a ? b : seller == b ? a : seller;
            buyer = buyer == a ? b : buyer == b ? a : buyer;
            mem.set32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + k * 4, (e & 0xfff) | uint32_t(seller) << 12 | uint32_t(buyer) << 22);
        }
    if (!g.club_table.empty() && a < first_foreign_cup() && b < first_foreign_cup()) swap_bytes(g.club_table, size_t(a) * 0x4a, size_t(b) * 0x4a, 0x4a);
    std::swap(g.club_ids.at(size_t(a)), g.club_ids.at(size_t(b)));
    // Modern world names and identities move with the club (empty with Classic data).
    for (auto* names : {&g.world_club_id, &g.world_club_name, &g.world_club_short})
        if (size_t(std::max(a, b)) < names->size()) std::swap((*names)[size_t(a)], (*names)[size_t(b)]);
    swap_bytes(g.club_heap_bc, size_t(a) * 2, size_t(b) * 2, 2);
    std::swap(g.team_morale.at(size_t(a)), g.team_morale.at(size_t(b)));
    swap_mem8(::bmh::layout::ClubShortNameOffsets);
    swap_mem8(::bmh::layout::ClubKitSelectors);
    swap_bytes(g.club_heap_78, size_t(a) * 2, size_t(b) * 2, 2);
    std::swap(g.team_flags.at(size_t(a)), g.team_flags.at(size_t(b)));
    if (both_league_clubs) {
        swap_bytes(g.club_heap_b8, size_t(a) * 2, size_t(b) * 2, 2);
        swap_bytes(g.position_history, size_t(a) * 0x1a, size_t(b) * 0x1a, 0x1a);
        const SquadRange ra = squad_range(a), rb = squad_range(b);
        for (size_t i = 0; i < ra.count; i++) std::swap(g.squad_slots.at(ra.first + i), g.squad_slots.at(rb.first + i));
    }
    swap_bytes(g.club_heap_b8, size_t(a) * 2, size_t(b) * 2, 2);
    replace(::bmh::layout::Titles, 0x1c2);  // title holders
    replace(::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField28, 2);
    if (!mem.has(::bmh::layout::CareerSession_FormatError_RuntimeField6) || mem.u8(::bmh::layout::CareerSession_FormatError_RuntimeField6) == 0) {
        replace(::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField14, 0x20);
        replace(::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField19, 0x20);
        replace(::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField25, 0x40);
        // (the English national cup field: 68 clubs at layout field, InitializeNationalCup(0x44, ...))
        if (kEnglishCompetitions) {
            if (mem.has(kEnglishFaField)) replace(kEnglishFaField, 0x44);
        } else {
            replace(::bmh::layout::Field, 0x40);
        }
        replace(::bmh::layout::Cup_AdvanceCup_RuntimeField6, 3);
    }
    for (size_t m = 0; m < g.manager_teams.size(); m++) {
        auto& t = g.manager_teams[m];
        const int old = t;
        if (old == a) t = b;
        else if (old == b) t = a;
        if (t != old) {
            g.manager_records.at(m * 0xB0C + 0x290) = uint8_t(t);
            g.manager_records.at(m * 0xB0C + 0x291) = uint8_t(uint16_t(t) >> 8);
        }
        for (size_t i = 0; i < 12; i++) {  // record +0x292
            const size_t at = m * 0xB0C + 0x292 + i * 2;
            const uint16_t v = uint16_t(g.manager_records.at(at) | g.manager_records.at(at + 1) << 8);
            const uint16_t w = v == uint16_t(a) ? uint16_t(b) : v == uint16_t(b) ? uint16_t(a) : v;
            g.manager_records.at(at) = uint8_t(w), g.manager_records.at(at + 1) = uint8_t(w >> 8);
        }
        swap_bytes(g.manager_results, size_t(a) * 10 + m * manager_history_stride(), size_t(b) * 10 + m * manager_history_stride(), 10);
    }
}

// Getenv(teams, offset): the national cup's ties from `offset` on (list of cup
// entry 0): a lower club (from the third division on, SeasonRuntimeField23) gets home advantage,
// then the ties are ordered by the leagues' weight (SeasonRuntimeField24), heaviest first.
void order_cup_ties(ModernState& g, int teams, int offset) {
    StaticMemory& mem = g.memory;
    uint32_t list = mem.pointer(::bmh::layout::CupTable) + uint32_t(offset) * 2;
    if (kEnglishCompetitions) {
        // the Football Limited CupField: the FA Cup stage decides the range - qualifying
        // round 1 (48 clubs from 0), round 2 (24 from 48), the proper rounds (entry 28);
        // with 0x80 the League Cup (entry 25)
        if ((teams & 0x80) == 0) {
            const uint8_t played = mem.u8(::bmh::layout::CupTable + 0x1d);
            if (played == 0) teams = 0x30, offset = 0;
            if (played == 1) teams = 0x18, offset = 0x30;
            const uint32_t base = played == 3 ? mem.pointer(::bmh::layout::CupTable + 28 * 0x28) : mem.pointer(::bmh::layout::CupTable);
            list = base + uint32_t(offset) * 2;
        } else {
            teams ^= 0x80;
            list = mem.pointer(::bmh::layout::CupTable + 25 * 0x28) + uint32_t(offset) * 2;
        }
    }
    static const uint8_t kWeight[10] = {3, 1, 0, 0, 2, 0, 0, 0, 7, 0};  // SeasonRuntimeField24
    const int lower = league_first_team(2);
    std::vector<uint8_t> w;
    for (int i = 0; i < teams; i += 2) {
        const uint32_t at = list + uint32_t(i) * 2;
        if (int16_t(mem.u16(at + 2)) >= lower && int16_t(mem.u16(at)) < lower) {
            const uint16_t t = mem.u16(at);
            mem.set16(at, mem.u16(at + 2));
            mem.set16(at + 2, t);
        }
        w.push_back(uint8_t(kWeight[club_league(mem.u16(at))] + kWeight[club_league(mem.u16(at + 2))]));
    }
    for (int i = 0; i < teams / 2 - 1; i++)
        for (int j = i + 1; j < teams / 2; j++)
            if (w[size_t(i)] < w[size_t(j)]) {
                std::swap(w[size_t(i)], w[size_t(j)]);
                for (uint32_t k = 0; k < 2; k++) {
                    const uint32_t x = list + uint32_t(i) * 4 + k * 2, y = list + uint32_t(j) * 4 + k * 2;
                    const uint16_t t = mem.u16(x);
                    mem.set16(x, mem.u16(y));
                    mem.set16(y, t);
                }
            }
}

}  // namespace

void clubs_change_places(ModernState& g, int a, int b) { change_places(g, a, b); }

// The side effects of the cup overview screen CupGroupOverviewEffects (otherwise display only): slot 4
// of the running competitions is this year's international tournament (7 - season % 4),
// the screen state of the manager (CareerSession_DispatchOtherGames_RuntimeField) is reset, and the friendlies
// (competition 0xc) run while their entry (cup entry 30) has teams.
void cup_overview_effects(ModernState& g) {
    StaticMemory& mem = g.memory;
    if (mem.has(::bmh::layout::CareerSession_FormatError_RuntimeField3)) mem.set8(::bmh::layout::CareerSession_FormatError_RuntimeField3, uint8_t(7 - uint16_t(g.season) % 4));
    // CupGroupOverviewEffects normalizes the active manager byte before selecting that manager's
    // screen-state slot. This is a real Source mutation (and is serialized where present).
    uint8_t active = mem.has(::bmh::layout::ActiveManager) ? mem.u8(::bmh::layout::ActiveManager) : 0;
    if (active > 3) {
        active = 0;
        if (mem.has(::bmh::layout::ActiveManager)) mem.set8(::bmh::layout::ActiveManager, active);
    }
    if (mem.has(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField + active)) mem.set8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField + active, 0x80);
    if (mem.u8(::bmh::layout::FriendlyPlanCount) == 0) {
        modern::end_competition(mem, 0xc);
    } else {
        for (uint32_t i = 0; i < 8; i++) {  // AddCompetition(0xc)
            if (int8_t(mem.u8(::bmh::layout::ActiveSelectors + i)) == 0xc) break;
            if (mem.u8(::bmh::layout::ActiveSelectors + i) == 0x14) {
                mem.set8(::bmh::layout::ActiveSelectors + i, 0xc);
                break;
            }
        }
    }
}

void cup_group_overview_effects(ModernState& g, int group_index) {
    // This Source traversal cursor is runtime state, absent from native FBMs.
    // Own it at the overview boundary even after loading a saved career.
    if (!g.memory.has(::bmh::layout::CareerSession_FormatError_RuntimeField8)) g.memory.add_block(::bmh::layout::CareerSession_FormatError_RuntimeField8, 1);
    g.memory.set8(::bmh::layout::CareerSession_FormatError_RuntimeField8, uint8_t(group_index));
    modern::cup_overview_effects(g);
}

namespace {

// Getenv2: every club of every league changes places with a random other club of the
// same league (ClubsChangePlaces) - this mixes the fixture positions of the new season
void shuffle_leagues(ModernState& g, WatcomRandom& rand) {
    int first = 0;
    for (int l = 0; l < kLeagueCount; l++) {
        const int end = first + kLeagueSizes[l];
        for (int t = first; t < end; t++) {
            int r;
            do r = rand.range(first, end - 1); while (uint8_t(r) == uint8_t(t));
            change_places(g, t, r);
        }
        first = end;
    }
}

// CareerSeasonSetup(2): the national cup's field - the first 18 of the first two divisions and
// the first 14 of the two third divisions by the final tables, shuffled (PrepareEvent).
// Human managers note the cup in their season record (ManagerOf(m, 1, 1)). Its first
// round is shown (CupDrawFromGame on cup entry 0) unless switched off; then Getenv gives
// the lower club home advantage and sorts the ties by the leagues' weight (SeasonRuntimeField24).
void observe_cup_draw(const ModernState& g, SeasonCupDrawPhase phase,
                      const SeasonCupDrawHook& hook) {
    if (!hook) return;
    const StaticMemory& mem = g.memory;
    constexpr uint32_t entry = kCupTable;
    SeasonCupDrawEvent event;
    event.phase = phase;
    event.entry_address = entry;
    event.competition = mem.u8(entry + 0x18);
    event.flags = mem.u8(entry + 0x14);
    event.group_stage = (event.flags & 4) == 0;
    event.group_index = mem.u8(entry + 0x1e) >> 4;
    event.group_count = mem.u8(entry + 0x1e) & 0xf;
    event.round = modern::cup_round(mem, 0);
    event.offset = modern::cup_round_offset(mem, 0, event.round);
    event.count = size_t(mem.u8(entry + 0x1a)) >> event.round;
    const uint32_t field = mem.pointer(entry) + uint32_t(event.offset) * 2;
    event.participants.reserve(event.count);
    for (size_t i = 0; i < event.count; ++i)
        event.participants.push_back(mem.u16(field + uint32_t(i) * 2));
    hook(g, event);
}

// The English national cups (the Football Limited CupFieldsEnglish(2)): the FA Cup field of 68
// clubs - the first three divisions complete and the best 8 of the fourth - whose 20 Premier
// League clubs and 48 others are shuffled apart (the 48 play the qualifying stage of entry
// 0), and the League Cup field of 44 - the first two divisions and the best 4 of the third,
// 20 and 24 shuffled apart (entry 25) - whose ties are ordered (CupField(0x98, 0)).
void cup_fields_english(ModernState& g, GameRandom& game, const uint8_t (&ranks)[4][24],
                        const std::function<void(const char*)>& pause, const SeasonCupDrawHook& cup_draw) {
    StaticMemory& mem = g.memory;
    modern::english_competition_blocks(mem);
    auto shuffle = [&](uint32_t list, uint32_t n) {  // Uint32T2 = PrepareEvent
        for (uint32_t i = 0; i < n; i++) {
            const uint32_t j = uint32_t(game.range(0, int(n) - 1));
            const uint16_t a = mem.u16(list + i * 2);
            mem.set16(list + i * 2, mem.u16(list + j * 2));
            mem.set16(list + j * 2, a);
        }
    };
    uint32_t n = 0;
    for (int l = 0; l < 4; l++)
        for (int i = 0; i < kLeagueSizes[l]; i++) {
            if (l >= 3 && i >= 8) continue;
            const int team = club_at_rank(l, i, ranks[l]);
            mem.set16(kEnglishFaField + n++ * 2, uint16_t(team));
            if (const int m = g.manager_of(team); m >= 0)
                g.manager_records.at(size_t(m) * 0xB0C + 0xda + 1 + size_t(std::min<int>(g.season, 0x31)) * 3) = 1;
        }
    shuffle(kEnglishFaField, 0x14);
    shuffle(kEnglishFaQualifyingField, 0x30);
    if (mem.u8(::bmh::layout::LeaguePresentationOptions + 5 * 9) == 0) {  // CupOverviewEffects(entry 0) unless the draw display is off
        observe_cup_draw(g, SeasonCupDrawPhase::draw, cup_draw);
        if (pause) pause("the FA Cup draw (CupOverviewEffects)");
        order_cup_ties(g, 0x30, 0);  // (CupOverviewEffects orders the ties: CupField, stage 0)
        modern::cup_overview_effects(g);
        observe_cup_draw(g, SeasonCupDrawPhase::overview, cup_draw);
    }
    n = 0;
    for (int l = 0; l < 3; l++)
        for (int i = 0; i < kLeagueSizes[l]; i++) {
            if (l >= 2 && i >= 4) continue;
            mem.set16(kEnglishLeagueCupField + n++ * 2, uint16_t(club_at_rank(l, i, ranks[l])));
        }
    shuffle(kEnglishLeagueCupField, 0x14);
    shuffle(kEnglishLeagueCupField + 0x28, n - 0x14);
    order_cup_ties(g, 0x98, 0);  // CupField(0x98, 0): the League Cup ties
}

void cup_field(ModernState& g, GameRandom& game, const std::function<void(const char*)>& pause,
               const SeasonCupDrawHook& cup_draw) {
    StaticMemory& mem = g.memory;
    uint8_t ranks[4][24] = {};
    for (int l = 0; l < 4; l++) {
        const LeagueTable t = compute_league_table(g, l, 0, league_matchdays(l), 7, 1);
        for (int i = 0; i < t.size; i++) ranks[l][i] = uint8_t(t.rank(i));
    }
    if (kEnglishCompetitions) {
        cup_fields_english(g, game, ranks, pause, cup_draw);
        return;
    }
    constexpr uint32_t kField = ::bmh::layout::Field;
    uint32_t n = 0;
    for (int l = 0; l < 4; l++)
        for (int i = 0; i < kLeagueSizes[l]; i++) {
            if (l >= 2 && i >= 0xe) continue;
            const int team = club_at_rank(l, i, ranks[l]);
            mem.set16(kField + n++ * 2, uint16_t(team));
            if (const int m = g.manager_of(team); m >= 0)
                g.manager_records.at(size_t(m) * 0xB0C + 0xda + 1 + size_t(std::min<int>(g.season, 0x31)) * 3) = 1;
        }
    for (uint32_t i = 0; i < 0x40; i++) {  // PrepareEvent
        const uint32_t j = uint32_t(game.range(0, 0x3f));
        const uint16_t a = mem.u16(kField + i * 2);
        mem.set16(kField + i * 2, mem.u16(kField + j * 2));
        mem.set16(kField + j * 2, a);
    }
    if (mem.u8(::bmh::layout::LeaguePresentationOptions + 5 * 9) != 0) return;  // the draw display is switched off
    observe_cup_draw(g, SeasonCupDrawPhase::draw, cup_draw);
    if (pause) pause("the national cup draw (CupDrawFromGame)");
    order_cup_ties(g, mem.u8(::bmh::layout::CupTable + 0x1a), 0);  // (round 0 of cup entry 0)
    modern::cup_overview_effects(g);  // CupGroupOverviewEffects lists the ties
    observe_cup_draw(g, SeasonCupDrawPhase::overview, cup_draw);
}

// GeneratePlayer: see generate_player (career.hpp)
void regenerate_player(ModernState& g, int player, int strength, GameRandom& game, WatcomRandom& rand) {
    modern::generate_player(g, player, strength, game, rand);
}

// SetAge(player, age): the age (w364 bits 21-25); bits 2-12 are cleared as well
void set_age(ModernState& g, int player, int age) {
    Player& p = g.players.at(size_t(player));
    p.w364 = (p.w364 & 0x1fe003u) | uint32_t(age) << 21;
}

}  // namespace

// MonthChange(slot): a player leaves his club for the free agents (foreigners to their own
// pool): his loan entry and youth mark go, he gets new values (RegeneratePlayer: all but w35c),
// an age W(4, 15) and no injury; the club closes the gap (ReleasePlayer)
bool release_player(ModernState& g, int slot, GameRandom& game, WatcomRandom& rand, MinuteReport& report) {
    StaticMemory& mem = g.memory;
    const int player = g.squad_slots.at(size_t(slot)) & 0xFFF;
    for (uint32_t k = 0; k < 200; k++) {
        const uint32_t e = mem.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + k * 4);
        if (e == 0 || int(e & 0xFFF) != player) continue;
        mem.set32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + k * 4, 0);
        for (uint32_t i = 0; i < 12; i++)
            if (mem.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField + i * 2) == uint16_t(player)) {
                mem.set16(::bmh::layout::Career_CareerManagersEnd_RuntimeField + i * 2, 0xFFF);
                const uint32_t owner = ::bmh::layout::DeskActions_Unsupported_RuntimeField + uint32_t(int8_t(mem.u8(::bmh::layout::ClubInfo_FormatError_RuntimeField2 + i)));
                mem.set8(owner, uint8_t(mem.u8(owner) - 1));
            }
        break;
    }
    const int team = modern::slot_team(slot);
    const bool foreign = modern::nationality(g, player) != 0;
    const int start = foreign ? int(foreign_offer_slot()) : int(free_agent_slot()), count = foreign ? 0x14 : 0x50;  // FirstFreeSlotSquad
    int free = -1;
    for (int s = start; s < start + count && free < 0; s++)
        if (g.squad_slots.at(size_t(s)) == 0x0FFF) free = s;
    const uint32_t skills = g.players.at(size_t(player)).w35c;  // RegeneratePlayer
    regenerate_player(g, player, 0x32, game, rand);
    modern::new_player_identity(g, player);  // a new person for the almanac
    Player& p = g.players.at(size_t(player));
    p.w35c = skills;
    g.player_bonus.at(size_t(player)) &= 0x80;
    p.w368 &= 0xf8003fffu;
    p.w364 &= 3;
    p.w364 |= uint32_t(rand.range(4, 0xf)) << 21;
    p.w35c &= 0x07ffffffu;
    if (free < 0) {
        // no room among the free agents: a club takes him (TransferBid(slot, 0, 0, -1))
        modern::transfer_bid(g, slot, 0, false, -1, game, rand, report);
        if (report.unsupported) return false;
    } else {
        g.squad_slots.at(size_t(free)) = uint16_t(player);
    }
    if (team >= 0) return modern::remove_from_seller(g, team, player, report);
    return true;
}

namespace {

// CareerSeasonSetup3: the players' new season. Records and boards of the managers restart
// (with new advertising offers, TransferListCountdown2); then every squad and pool slot: players
// out for their career recover now and then with a human club's good doctor, else leave;
// everybody grows a year older (w364 bits 21-25), and at a computer club an old player
// retires (unless the squad is small: then he is replaced by a young one) - the first old
// one of every club always goes (a quirk: the counter starts at 0xFF). Last the rounds,
// cards, position histories and results of the old season are cleared.
void season_players(ModernState& g, GameRandom& game, WatcomRandom& rand, MinuteReport& report) {
    StaticMemory& mem = g.memory;
    auto fill8 = [&](uint32_t at, uint32_t n, uint8_t v) {
        for (uint32_t i = 0; i < n; i++)
            if (mem.has(at + i)) mem.set8(at + i, v);
    };
    auto fill16 = [&](uint32_t at, uint32_t n, uint16_t v) {
        for (uint32_t i = 0; i < n; i++)
            if (mem.has(at + i * 2)) mem.set16(at + i * 2, v);
    };
    // Fill8: desk and screen states
    fill8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField, 4, 0x80), fill8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField, 4, 1);
    fill16(::bmh::layout::LeagueTableDisplayModes, 4, 0x11), fill16(::bmh::layout::ManagerSettings, 4, 0x1ff), fill16(::bmh::layout::CareerSession_AnyOf_ExecutableField, 4, 0x15), fill16(::bmh::layout::Savestate_Layout_ExecutableField3, 4, 0xdf0);
    fill8(::bmh::layout::Flags, 0xc, 0x40);
    for (size_t m = 0; m < g.manager_teams.size(); m++)
        for (size_t i = 0; i < 10; i++) rec8(g, int(m), 0x8b6 + i) = 0xff;
    modern::end_competition(mem, 0xd);  // EndCompetition
    fill8(::bmh::layout::Season_SeasonPlayers_ExecutableField, 1, 0);
    for (size_t mi = 0; mi < g.manager_teams.size() && !report.unsupported; mi++) {
        const int m = int(mi);
        // CareerSeasonSetup3 invokes StartingSponsors for each manager before resetting
        // seasonal records and constructing new advertising offers.
        modern::starting_sponsors(g, m);
        rec8(g, m, 0x284) = 0, rec8(g, m, 0x21e) = 0, rec8(g, m, 0x283) = 0;
        set_rec32(g, m, 0x330, 0);
        rec8(g, m, 0x28b) = 0;
        for (size_t i = 0; i < 3; i++) rec8(g, m, 0x28c + i) = 0;
        for (size_t i = 0; i < 10; i++) rec8(g, m, 0x170 + i) = 0;
        for (size_t i = 0; i < 0x48; i++) rec8(g, m, 0x1ae + i) = 0;
        for (size_t i = 0; i < 0x30; i++) rec8(g, m, 0x718 + i) = 0;
        for (size_t i = 0; i < 0xc; i++) rec8(g, m, 0x8c0 + i) = 0, rec8(g, m, 0x748 + i) = 0;
        modern::advertising_offers(g, m, rand, report);  // TransferListCountdown2
        if (report.unsupported) return;
        set_rec32(g, m, 0x33c, 0), set_rec32(g, m, 0x338, 0), set_rec32(g, m, 0x334, 0);
        for (size_t s = 0; s < 24; s++) {
            const size_t at = mi * 0x840 + s * 0x58;
            g.tactics.at(at + 6) = 0;
            g.tactics.at(at + 0xf) = uint8_t(rand.range(0x14, 0x32));
        }
        for (uint32_t i = 0; i < 12; i++)
            if (mem.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField + i * 2) != 0xfff && int8_t(mem.u8(::bmh::layout::ClubInfo_FormatError_RuntimeField2 + i)) == m) {
                mem.set8(::bmh::layout::Season_SeasonPlayers_RuntimeField + i * 0x58, 0);
                mem.set8(::bmh::layout::Season_SeasonPlayers_RuntimeField2 + i * 0x58, uint8_t(rand.range(0x14, 0x32)));
            }
    }
    if (g.date > 0) {
        g.lineups_frozen = true;  // LineupRuntimeField
        uint8_t loans[0x1d3] = {};
        for (uint32_t k = 0; k < 200; k++) {
            const uint32_t e = mem.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + k * 4);
            if (e != 0) loans[(e >> 12) & 0x3ff]++;
        }
        uint16_t last_team = 0xffff;  // local_30
        uint8_t count = 0xff;         // local_20
        int8_t owner = -1;            // local_24: the club's human manager
        size_t board_first = 0;       // local_3c
        for (int s = 0; s < int(pool_end_slot()) && !report.unsupported; s++) {
            for (;;) {
                const int player = g.squad_slots.at(size_t(s)) & 0xFFF;
                if (player == 0xFFF) break;
                const int team = modern::slot_team(s);
                if (team != int(last_team)) {  // (-1 for the pool never equals 0xFFFF)
                    count = 0xff;
                    const int mm = g.manager_of(team);
                    owner = int8_t(mm >= 0 ? mm : -1);
                    if (mm >= 0) board_first = squad_range(team).first;
                }
                Player& p = g.players.at(size_t(player));
                bool leaves = false;
                if (s < int(transfer_list_slot()) && (p.w35c >> 27) == 0x1f) {
                    // out for his career
                    bool gone = true;
                    if (owner != -1) {
                        std::string text;
                        const int doctor = int16_t(g.manager_records.at(size_t(owner) * 0xB0C + 0x8ae) |
                                                   g.manager_records.at(size_t(owner) * 0xB0C + 0x8af) << 8);
                        if (doctor > rand.range(700, 0x4b0)) {
                            text = message_text(g, ::bmh::layout::Season_SeasonPlayers_TextPointer) + g.player_name(player) + message_text(g, ::bmh::layout::Season_SeasonPlayers_TextPointer2);
                            gone = false;
                            p.w35c &= 0x07ffffffu;
                            p.w364 &= ~(0x1fu << 16);
                        } else {
                            text = g.player_name(player) + message_text(g, ::bmh::layout::Season_SeasonPlayers_TextPointer3);
                            const size_t board = size_t(owner) * 0x840 + size_t(s - int(board_first)) * 0x58;
                            const int8_t insured = int8_t(g.tactics.at(board + 0x43));
                            if (insured != 0) {
                                const int k = (insured >> 6) & 3;
                                const int32_t pay = int32_t(uint32_t(g.tactics.at(board + 0x50) | g.tactics.at(board + 0x51) << 8 |
                                                                     g.tactics.at(board + 0x52) << 16) |
                                                            uint32_t(g.tactics.at(board + 0x53)) << 24) *
                                                    int8_t(k * 3 + 3);
                                modern::book_money(g, owner, -pay, true);
                                text += message_text(g, ::bmh::layout::Season_SeasonPlayers_TextPointer4) + format_amount(g, pay) + message_text(g, ::bmh::layout::Season_SeasonPlayers_TextPointer5);
                            }
                        }
                        post_composed(g, owner, text);
                    }
                    if (gone) {
                        if (count != 0xff) count = uint8_t(modern::squad_count(g, team, 2) + loans[team]);
                        if (owner == -1 && count <= 0xd) {
                            set_age(g, player, rand.range(0x12, 0x19) - 0x10);
                        } else {
                            count--;
                            leaves = true;
                            // a loaned player: his club's manager is told
                            for (uint32_t k = 0; k < 200; k++) {
                                const uint32_t e = mem.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + k * 4);
                                if (e == 0 || int(e & 0xFFF) != player) continue;
                                const int mm = g.manager_of(int(e >> 12) & 0x3ff);
                                owner = int8_t(mm >= 0 ? mm : -1);
                                if (mm >= 0)
                                    post_composed(g, mm, message_text(g, ::bmh::layout::Season_SeasonPlayers_TextPointer6) + g.player_name(player) + message_text(g, ::bmh::layout::Season_SeasonPlayers_TextPointer7));
                                break;
                            }
                        }
                    }
                }
                if (!leaves) {
                    // a year older
                    p.w368 &= 0xf8003fffu;
                    int age = int16_t(((p.w364 >> 21) & 0x1f) + 1);
                    if (s < int(transfer_list_slot()) && rand.range(0x10, 0x16) < age && owner == -1) {
                        if (count != 0xff) count = uint8_t(modern::squad_count(g, team, 2) + loans[team]);
                        if (count > 0xd) {
                            count--;
                            leaves = true;
                        } else {
                            age = rand.range(0x12, 0x19) - 0x10;
                        }
                    }
                    if (!leaves) {
                        set_age(g, player, age);  // SetAge
                        last_team = uint16_t(team);
                        break;
                    }
                }
                if (std::getenv("BMH_TRACE"))
                    std::printf("  [release slot %d player %d team %d age %u count %u]\n", s, player, team, (p.w364 >> 21) & 0x1f, count);
                if (!modern::release_player(g, s, game, rand, report)) return;
            }
        }
        g.lineups_frozen = false;
    }
    for (size_t l = 0; l < g.league_rounds.size() && l < 9; l++) g.league_rounds[l] = 0;
    fill8(::bmh::layout::DeskRecords_Teams_RuntimeField, 0x114, 0);
    for (size_t i = 0; i < size_t(league_clubs()) * 0x1a && i < g.position_history.size(); i++) g.position_history[i] = 0;
    std::fill(g.results.begin(), g.results.end(), uint8_t(0xcc));
}

// The end of the CareerSession_RuntimeError_RuntimeField4 branch of DailyInterest: histories beyond 50 seasons shift
// (SeasonEndSteps), the managers' season record starts (ManagerOf(m, 0, -2)), the
// limited game length counts down (CareerDuration), the days pass (DayStepRest) up to the
// first Wednesday, new players come onto the transfer list (CareerFirstSteps), and the new
// season starts on that day (DaysIntoSeason = 0) with its calendar cleared of replay days.
SeasonTransitionOutcome season_end_steps(ModernState& g, GameRandom& game, WatcomRandom& rand, MinuteReport& report,
                                         const SeasonHighscoreHook& highscore,
                                         TransferAllocatorContext allocator) {
    StaticMemory& mem = g.memory;
    if (g.season > 0x31) {  // SeasonEndSteps
        for (uint32_t k = 0; k < 9; k++) {
            for (uint32_t i = 0; i < 0x62; i++) mem.set8(::bmh::layout::Titles + k * 100 + i, mem.u8(::bmh::layout::Season_SeasonEndSteps_RuntimeField + k * 100 + i));
            mem.set16(::bmh::layout::Season_SeasonEndSteps_RuntimeField2 + k * 100, 0xffff);
        }
        for (size_t m = 0; m < g.manager_teams.size(); m++) {
            for (size_t i = 0; i < 0x93; i++) rec8(g, int(m), 0xda + i) = rec8(g, int(m), 0xdd + i);
            const size_t at = m * 0xB0C + 0x2aa;
            const uint16_t n = uint16_t(g.manager_records.at(at) | g.manager_records.at(at + 1) << 8) + 1;
            g.manager_records.at(at) = uint8_t(n), g.manager_records.at(at + 1) = uint8_t(n >> 8);
        }
    }
    for (size_t m = 0; m < g.manager_teams.size(); m++)
        rec8(g, int(m), 0xda + size_t(std::min<int>(g.season, 0x31)) * 3) = 0xfe;
    // GameEndAlternateScoreScreen(0): update the external highscore table before decrement.
    if (highscore) highscore(g, false);
    if (mem.has(::bmh::layout::CareerDuration)) {
        uint8_t left = mem.u8(::bmh::layout::CareerDuration);
        if (left < 5) mem.set8(::bmh::layout::CareerDuration, --left);
        if (left == 0) {
            // DailyInterest returns here before first-Wednesday day steps.
            if (highscore) highscore(g, true);
            return SeasonTransitionOutcome::CareerComplete;
        }
    }
    const bool full = (mem.u8(::bmh::layout::CompetitionSelection) & 1) != 0;
    while (modern::weekday_code(g.date) != 2) {  // DayStepRest
        g.date++;
        modern::daily_interest(g, rand);
        if (std::getenv("BMH_TRACE"))
            std::printf("  [day %u (weekday %d): game %llu, Watcom %llu]\n", g.date, modern::weekday_code(g.date), (unsigned long long)game.calls(),
                        (unsigned long long)rand.calls());
        const int wd = modern::weekday_code(g.date - 1);
        if (wd == 2 || wd == 6) {
            if (full && modern::weekday_code(g.date) == 3) {
                modern::weekly_processing(g, game, rand, report, allocator);
                if (report.unsupported) return SeasonTransitionOutcome::Failed;
            }
            modern::familiarity_decay(g, game, rand);
            for (size_t m = 0; m < g.manager_teams.size(); m++) modern::train_squad(g, int(m), game, rand);
        }
        if (full && modern::month_code(g.date - 1) != modern::month_code(g.date)) {
            modern::month_change(g, rand, report, &game);
            if (report.unsupported) return SeasonTransitionOutcome::Failed;
        }
    }
    if (std::getenv("BMH_TRACE"))
        std::printf("  [before CareerFirstSteps: game %llu, Watcom %llu]\n", (unsigned long long)game.calls(), (unsigned long long)rand.calls());
    modern::new_transfer_list_entries(g, rand, report);  // CareerFirstSteps
    if (report.unsupported) return SeasonTransitionOutcome::Failed;
    if (mem.has(::bmh::layout::CalendarState_FormatError_RuntimeField3)) mem.set16(::bmh::layout::CalendarState_FormatError_RuntimeField3, uint16_t(g.date));  // (a 16-bit copy of the date)
    g.season_start = g.date;  // DaysIntoSeason = 0
    mem.set8(::bmh::layout::Cup_AdvanceCup_RuntimeField4, 0);
    for (uint32_t d = 0; d < 0x16d; d++)
        if ((mem.u16(::bmh::layout::CompetitionCalendar + d * 2) & 0x4f0e) == 0x4000) mem.set16(::bmh::layout::CompetitionCalendar + d * 2, 0);
    return SeasonTransitionOutcome::NextSeason;
}

// LicenceWithdrawals(league, ...): clubs losing their licence - a bankrupt human club placed
// below 6th (its manager's league marker was set to 0x1e before), and with fewer than six
// managers now and then (G(0, 12 - 7 * league) == 0) a computer club of the first two
// divisions placed 10th-15th. They go down first; `places` counts down.
void licence_withdrawals(ModernState& g, int league, std::vector<uint16_t>& down, int& places, const uint8_t* ranks,
                         GameRandom& game, WatcomRandom& rand) {
    const int first = league_first_team(league);
    for (size_t m = 0; m < g.manager_teams.size(); m++) {
        const int team = g.manager_teams[m];
        if (team < first || team >= first + kLeagueSizes[league] || ranks[team - first] <= 5) continue;
        static const uint8_t kDownPlaces[kLeagueCount] = {3, 4, 2, 2, 3, 0, 0, 0, 0};  // CareerSession_InstallLeagueSizes_RuntimeField2
        if (kDownPlaces[league] == 0) continue;
        const uint8_t cut = rec8(g, int(m), 0x220);
        const int32_t total = int32_t(rec8(g, int(m), 0x26f)) * 80000 + (assets(g, int(m), 0xf) - assets(g, int(m), 0x10));
        const bool broke = total < -6000000 || ((cut >> 5) == 2 && int32_t(cut & 0x1f) * 100000 <= rec32(g, int(m), 0x330));
        if (broke && rec8(g, int(m), 0x210) == 0x1e) {
            rec8(g, int(m), 0x210) = 0;
            post_text(g, int(m), text_id(::bmh::layout::Season_LicenceWithdrawals_TextPointer));
            down.push_back(uint16_t(team));
            if (places != 0) places--;
        }
    }
    if (g.manager_teams.size() < 6 && league < 2 && game.range(0, 12 - 7 * league) == 0) {
        int t = 0, r = 0;
        do {
            do {
                r = rand.range(0, kLeagueSizes[league] - 1);
                t = first + r;
            } while (g.manager_of(t) >= 0);
        } while (ranks[r] < 10 || ranks[r] > 15);
        down.push_back(uint16_t(t));
        post_composed(g, -1, message_text(g, ::bmh::layout::Season_LicenceWithdrawals_TextPointer2) + g.club_name(t) + message_text(g, ::bmh::layout::Season_LicenceWithdrawals_TextPointer3));
        places--;
    }
}

// A real club name has a letter; the English database fills unused club ids with blanks and
// punctuation (",    ", " :").
bool club_has_name(const std::string& name) {
    return std::any_of(name.begin(), name.end(), [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); });
}

// Void: settle transfers and compute each group's local lists. Source
// displays them with SeasonPromotionScreen immediately before that group's club swaps.
void promotion(ModernState& g, GameRandom& game, WatcomRandom& rand, MinuteReport& report,
               const std::function<void(const char*)>& pause,
               const SeasonPromotionHook& promotion_hook, TransferAllocatorContext allocator) {
    modern::transfer_list_countdown(g, game, rand, report, true, allocator);  // TransferListCountdown(1)
    if (report.unsupported) return;
    std::vector<uint8_t> was;  // the managers' leagues before (record +0x210)
    for (size_t m = 0; m < g.manager_teams.size(); m++) was.push_back(rec8(g, int(m), 0x210)), rec8(g, int(m), 0x210) = 0x1e;
    uint8_t ranks[5][24] = {};
    for (int l = 0; l < 5; l++) {
        const LeagueTable t = compute_league_table(g, l, 0, league_matchdays(l), 7, 1);
        for (int i = 0; i < t.size; i++) ranks[l][i] = uint8_t(t.rank(i));
    }
    // The league ladder is data of the loaded game (schedule.hpp, installed from the EXE):
    // Void reads the German tables, the English ClubName its own copies -
    // first league / number of leagues of each promotion group (kGroupLeague/s), relegation
    // places (kLeagueRelegation), the league(s) below (kGroupBelow/Count). German:
    // 0 | 1 | 2+3 (Regionalliga A/B) | 4; English: 0 | 1 | 2 | 3.
    const uint8_t* kFirst = kGroupLeague;
    const uint8_t* kCount = kGroupLeagues;
    const uint8_t* kDown = kLeagueRelegation;
    const uint8_t* kBelow = kGroupBelow;
    const uint8_t* kBelowCount = kGroupBelowCount;
    // The clubs that refill the lowest league: kNewcomerCount clubs from kNewcomerFirst
    // (German: the 30 amateur clubs 138-167). The English game draws from its 12 further
    // English clubs (ClubName: range(CareerSession_DispatchLineup_RuntimeField, CareerSession_DispatchLineup_RuntimeField + 11)); its later ids have no name.
    // The span ends at the last named club, which keeps the German draws unchanged.
    // (OpenBMH deviates from the English original here: its extra coin flip per draw is not
    // ported.)
    const int newcomer_first = kNewcomerFirst;
    int newcomer_count = kNewcomerCount;
    while (newcomer_count > 1) {
        const std::string name = g.club_name(newcomer_first + newcomer_count - 1);
        if (club_has_name(name)) break;
        newcomer_count--;
    }
    for (int grp = 0; grp < 4; grp++) {
        std::vector<uint16_t> down, up;
        for (int l = kFirst[grp]; l < kFirst[grp] + kCount[grp]; l++) {
            int places = kDown[l];
            licence_withdrawals(g, l, down, places, ranks[l], game, rand);
            for (int rank = kLeagueSizes[l] - 1; places != 0; rank--) {
                const int team = club_at_rank(l, rank, ranks[l]);
                if (std::find(down.begin(), down.end(), uint16_t(team)) == down.end()) down.push_back(uint16_t(team)), places--;
            }
        }
        size_t count = down.size();
        if (grp < 3) {
            // the top of the division(s) below; from the two third divisions their
            // champions and the play-off winners (Season_Promotion_ExecutableField2 / Season_Promotion_ExecutableField), without a play-off
            // only the champions
            size_t per = count / kBelowCount[grp];
            for (int l = kBelow[grp]; l < kBelow[grp] + kBelowCount[grp]; l++) {
                if (kBelowCount[grp] > 1) {
                    if (mem_u16(g, ::bmh::layout::Season_Promotion_ExecutableField) == 0) count = 2;
                    else up.push_back(l > kBelow[grp] ? mem_u16(g, ::bmh::layout::Season_Promotion_ExecutableField) : mem_u16(g, ::bmh::layout::Season_Promotion_ExecutableField2));
                    per = 1;
                }
                for (size_t i = 0; i < per; i++) up.push_back(uint16_t(club_at_rank(l, int(i), ranks[l])));
            }
        } else {
            // into the fourth division come amateur clubs (ids 138-167) at random
            for (size_t i = 0; i < count; i++) {
                uint16_t t;
                do t = uint16_t(rand.range(newcomer_first, newcomer_first + newcomer_count - 1));
                // (never a club without a name: the English range has gaps; German draws are
                // unchanged, all thirty amateur clubs have names)
                while (std::find(up.begin(), up.end(), t) != up.end() ||
                       !club_has_name(g.club_name(t)));
                up.push_back(t);
            }
        }
        static const char* kScreen[4] = {"promotion and relegation of the first division (SeasonPromotionScreen)",
                                         "promotion and relegation of the second division (SeasonPromotionScreen)",
                                         "promotion and relegation of the third divisions (SeasonPromotionScreen)",
                                         "promotion and relegation of the fourth division (SeasonPromotionScreen)"};
        if (std::getenv("BMH_TRACE")) {
            std::printf("  [group %d: count %zu down", grp, count);
            for (uint16_t t : down) std::printf(" %d", t);
            std::printf(" up");
            for (uint16_t t : up) std::printf(" %d", t);
            std::printf(" a3204 %d a3206 %d]\n", mem_u16(g, ::bmh::layout::Season_Promotion_ExecutableField), mem_u16(g, ::bmh::layout::Season_Promotion_ExecutableField2));
        }
        if (promotion_hook) promotion_hook(g, SeasonPromotionEvent{grp, count, down, up});
        if (pause) pause(kScreen[grp]);
        for (size_t i = 0; i < count && i < up.size() && i < down.size(); i++) {
            if (grp == 3 && g.manager_of(down[i]) >= 0) continue;
            const int m = g.manager_of(up[i]);
            if (m >= 0) {
                // a promoted human club: its advertising contracts are marked, and a message
                for (size_t k = 0; k < 0x12; k++) rec8(g, m, 0xa3a + k * 0xc) |= 0x80;
                post_text(g, m, text_id(::bmh::layout::Season_Promotion_TextPointer));
            }
            change_places(g, up[i], down[i]);
        }
    }
    // the managers' leagues (record +0x210) and the desk buttons of leagues they left or
    // joined (block LeaguePresentationOptions, 9 per league)
    StaticMemory& mem = g.memory;
    bool left[5] = {}, joined[5] = {};
    for (size_t m = 0; m < g.manager_teams.size(); m++) {
        const uint8_t before = uint8_t(was[m]);
        const int now = club_league(g.manager_teams[m]);
        rec8(g, int(m), 0x210) = uint8_t(now);
        if (now < 5) joined[now] = true;
        if (before < 5) left[before] = true;
        for (uint32_t k = 0; k < 4; k++)
            if (mem.u8(::bmh::layout::LeaguePresentationOptions + uint32_t(before) * 9 + k) == 0) mem.set8(::bmh::layout::LeaguePresentationOptions + uint32_t(now) * 9 + k, 0);
    }
    for (int l = 0; l < 5; l++)
        if (!joined[l] && left[l])
            for (uint32_t k = 0; k < 4; k++) mem.set8(::bmh::layout::LeaguePresentationOptions + uint32_t(l) * 9 + k, 1);
}

}  // namespace

// LeagueName(buf, league): the name of a league (< 5): the texts from Achievements_FormatError_TextPointer3 on - a
// league whose suffix letter (CareerSession_InstallLeagueSizes_RuntimeField3, two bytes per league) is set shares the text of
// the one before when its letter is from 'B' on (LeagueName2) - with " " + the letter.
std::string league_name(const ModernState& g, int league) {
    // CareerSession_InstallLeagueSizes_RuntimeField3 of the loaded game (schedule.hpp: German A/B for the two Regionalligen,
    // English none - its four divisions are one ladder)
    const char* kSuffix = kLeagueSuffix;
    int index = 0;
    for (int l = 0; l < league; l++)
        if (uint8_t(kSuffix[l + 1]) < 0x42) index++;
    std::string name = message_text(g, ::bmh::layout::Achievements_FormatError_TextPointer3 + uint32_t(index) * 4);
    if (kSuffix[league] != 0) {
        name += message_text(g, ::bmh::layout::Manager_FormatError_TextPointer2);
        if (!name.empty()) name.back() = kSuffix[league];
    }
    return name;
}

namespace {

// TopScorersFromGame(league, flags): the ten best scorers of a league into Season_List_RuntimeField (10 x
// slot, team, goals, assists): goals (w364 bits 2-7, flag 2) plus assists (w368 bits 18-23,
// flag 4); flag 1 only counts human clubs. A player goes before the first one with fewer
// points (equal ones keep the earlier club / squad slot first). Returns the team of the best.
int best_scorer_team(ModernState& g, int league, int flags) {
    std::vector<uint16_t> list(40, 0);
    for (int i = 0; i < 40; i += 4) list[size_t(i)] = 0xfff, list[size_t(i) + 1] = 0xfff;
    int16_t best[10] = {};
    const int first = league_first_team(league);
    for (int team = first; team < first + kLeagueSizes[league]; team++) {
        if (g.manager_of(team) < 0 && (flags & 1)) continue;
        const SquadRange range = squad_range(team);
        for (size_t s = range.first; s < range.first + range.count; s++) {
            const int player = g.squad_slots.at(s) & 0xfff;
            if (player == 0xfff) continue;
            const Player& p = g.players.at(size_t(player));
            const uint16_t goals = (flags & 2) ? uint16_t((p.w364 >> 2) & 0x3f) : 0;
            const uint16_t assists = (flags & 4) ? uint16_t((p.w368 >> 18) & 0x3f) : 0;
            const uint8_t points = uint8_t(goals + assists);
            if (points == 0) continue;
            for (int i = 0; i < 10; i++) {
                if (best[i] >= int16_t(points)) continue;
                for (int k = 9; k > i; k--) {
                    best[k] = best[k - 1];
                    for (int j = 0; j < 4; j++) list[size_t(k * 4 + j)] = list[size_t((k - 1) * 4 + j)];
                }
                list[size_t(i * 4)] = uint16_t(s), list[size_t(i * 4 + 1)] = uint16_t(team);
                list[size_t(i * 4 + 2)] = goals, list[size_t(i * 4 + 3)] = assists;
                best[i] = int16_t(points);
                break;
            }
        }
    }
    if (g.memory.has(::bmh::layout::Season_List_RuntimeField))
        for (uint32_t i = 0; i < 40; i++) g.memory.set16(::bmh::layout::Season_List_RuntimeField + i * 2, list[i]);
    return list[1];
}

// FairPlayStateFromGame(league, 0): the fair play table - 4 points per red card (high byte of the
// club's card counter DeskRecords_Teams_RuntimeField) and 1 per yellow, sorted ascending (Games).
// Returns the club with the fewest points.
int fair_play_team(const ModernState& g, int league) {
    const int first = league_first_team(league), count = kLeagueSizes[league];
    std::vector<int16_t> teams(static_cast<size_t>(count));
    std::vector<uint32_t> points(static_cast<size_t>(count));
    for (int i = 0; i < count; i++) {
        teams[size_t(i)] = int16_t(first + i);
        const uint16_t cards = g.memory.u16(::bmh::layout::DeskRecords_Teams_RuntimeField + uint32_t(first + i) * 2);
        points[size_t(i)] = uint32_t((int16_t(cards) >> 8) * 4 + (cards & 0xff));
    }
    quicksort_by_key(teams.data(), points.data(), count, true);
    return teams[0];
}

}  // namespace

void season_awards(ModernState& g) {
    static const uint8_t kFlags[2] = {2, 6};  // SeasonRuntimeField25: top scorer, best scorer (+ assists)
    // SeasonRuntimeField26: the prizes in 10000s per league (top scorer, best scorer, fair play)
    static const uint8_t kPrize[5][3] = {{30, 20, 25}, {20, 15, 17}, {15, 10, 12}, {15, 10, 12}, {10, 7, 9}};
    for (int kind = 0; kind < 3; kind++)
        for (int league = 0; league < 5; league++) {
            const int team = kind < 2 ? best_scorer_team(g, league, kFlags[kind]) : fair_play_team(g, league);
            for (size_t m = 0; m < g.manager_teams.size(); m++) {
                if (g.manager_teams[m] != team) continue;
                const int32_t prize = int32_t(kPrize[league][kind]) * 10000;
                // "Ihr Spieler / Ihre Mannschaft gewinnt den ... der <league>. Prämie: <amount>."
                std::string text = message_text(g, ::bmh::layout::Season_SeasonAwards_TextPointer3) + message_text(g, ::bmh::layout::Season_SeasonAwards_TextPointer2 + (kind == 2 ? 4u : 0u)) +
                                   message_text(g, ::bmh::layout::Season_SeasonAwards_TextPointer4) + message_text(g, ::bmh::layout::Season_SeasonAwards_TextPointer + uint32_t(kind) * 4) +
                                   message_text(g, ::bmh::layout::Season_SeasonAwards_TextPointer5) + modern::league_name(g, league) + message_text(g, ::bmh::layout::Season_SeasonAwards_TextPointer6) +
                                   format_amount(g, prize) + ".";
                post_composed(g, int(m), text);
                modern::book_money(g, int(m), -prize, true, true);  // BookMoney2(team, -prize, 1), FinanceStatisticsEnabled
            }
        }
}

SeasonTransitionOutcome season_transition(ModernState& g, GameRandom& game, WatcomRandom& rand, MinuteReport& report,
                       const std::function<void(const char*)>& pause,
                       const SeasonHighscoreHook& highscore,
                       const SeasonTaxHooks& tax_hooks,
                       const SeasonPromotionHook& promotion_hook,
                       const SeasonCupDrawHook& cup_draw,
                       TransferAllocatorContext allocator) {
    if (g.memory.has(::bmh::layout::CareerDuration) && g.memory.u8(::bmh::layout::CareerDuration) == 0) {
        report.fail("season transition: career already completed");
        return SeasonTransitionOutcome::Failed;
    }
    g.memory.set8(::bmh::layout::CareerSession_RuntimeError_RuntimeField4, 0);
    if (g.memory.has(::bmh::layout::Season_SeasonTransition_RuntimeField)) g.memory.set8(::bmh::layout::Season_SeasonTransition_RuntimeField, 0);
    modern::almanac_final_tables(g);  // final tables and champions of the season that ended
    g.season = int16_t(g.season + 1);  // SeasonIndex
    modern::reset_club_moods(g, game, rand);
    european_setup(g, game, rand, cup_draw);
    if (report.unsupported) return SeasonTransitionOutcome::Failed;
    if (pause) pause("the German entrants (CompetitionWinnerScreen)");
    european_fields(g, game, rand);
    SeasonTaxState tax_state;
    for (size_t m = 0; m < g.manager_teams.size() && !report.unsupported; m++)
        season_accounts(g, int(m), game, rand, report, tax_hooks, tax_state);
    if (report.unsupported) return SeasonTransitionOutcome::Failed;
    promotion(g, game, rand, report, pause, promotion_hook, allocator);  // Void
    if (report.unsupported) return SeasonTransitionOutcome::Failed;
    // DailyInterest: the summer sanctions (SeasonEvent, Season_SeasonAccounts_RuntimeField) take effect
    if (g.memory.has(::bmh::layout::Season_SeasonAccounts_RuntimeField))
        for (size_t m = 0; m < g.manager_teams.size(); m++) rec8(g, int(m), 0x220) = g.memory.u8(::bmh::layout::Season_SeasonAccounts_RuntimeField + uint32_t(m));
    cup_field(g, game, pause, cup_draw);  // CareerSeasonSetup(2)
    // Getenv2: every club changes places with a random one of its league (the new
    // fixtures), then the cup's ties are ordered again (Getenv(0x40, 0))
    auto trace = [&](const char* what) {
        if (std::getenv("BMH_TRACE"))
            std::printf("  [%s: game %llu, Watcom %llu draws so far]\n", what, (unsigned long long)game.calls(),
                        (unsigned long long)rand.calls());
    };
    trace("after the cup draw");
    shuffle_leagues(g, rand);
    trace("after Getenv2");
    order_cup_ties(g, 0x40, 0);
    season_players(g, game, rand, report);  // CareerSeasonSetup3
    trace("after CareerSeasonSetup3");
    if (report.unsupported) return SeasonTransitionOutcome::Failed;
    const auto outcome = season_end_steps(g, game, rand, report, highscore, allocator);
    trace("after the day steps");
    return outcome;
}

}  // namespace bmh::modern

namespace bmh::modern {

bool career_season_setup(ModernState& g, GameRandom& game, WatcomRandom& rand, MinuteReport& report,
                         const std::function<void(const char*)>& pause,
                         const SeasonCupDrawHook& cup_draw) {
    StaticMemory& mem = g.memory;
    g.team_morale.at(size_t(no_team())) |= 0x80;
    for (int team = 0; team < league_clubs(); team++) {
        modern::club_balance(g, team, rand);
        auto_lineup(g, team, g.manager_of(team) >= 0 ? 0xe4 : 100);  // (byte 100 - 0x80 for humans)
    }
    // the title holders of the cup entries (+0x22): none yet, the placeholder clubs
    const uint16_t first_small = 90;  // TeamsWithSquads
    const uint16_t foreign = no_team();  // ClubPlayerDetail_ClubPlayerDetailFromGame_RuntimeField3 + CareerSession_DispatchLineup_RuntimeField + CareerSession_InstallLeagueSizes_RuntimeField6 (0x165)
    mem.set16(::bmh::layout::Season_CareerSeasonSetup_RuntimeField2, foreign);
    mem.set16(::bmh::layout::Season_CareerSeasonSetup_RuntimeField6, uint16_t(first_small + 7));
    mem.set16(::bmh::layout::Season_CareerSeasonSetup_RuntimeField, 7);
    if (kEnglishCompetitions) mem.set16(::bmh::layout::CupTable + 28 * 0x28 + 0x22, 7);  // (EN: the FA Cup proper, SeasonRuntimeField8)
    mem.set16(::bmh::layout::Season_CareerSeasonSetup_RuntimeField5, uint16_t(first_small + 0x1f));
    mem.set16(::bmh::layout::Season_CareerSeasonSetup_RuntimeField4, uint16_t(first_small + 0x13));
    mem.set16(::bmh::layout::Season_CareerSeasonSetup_RuntimeField3, uint16_t(foreign + 1));
    mem.set16(::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField28, 0);
    mem.set16(::bmh::layout::Cup_AdvanceCup_RuntimeField6, 0);
    mem.set16(::bmh::layout::Cup_AdvanceCup_RuntimeField5, 7);
    mem.set16(::bmh::layout::Season_CareerSeasonSetup_RuntimeField7, 7);
    european_setup(g, game, rand, cup_draw);  // CareerSeasonSetup(3)
    if (report.unsupported) return false;
    if (pause) pause("the German entrants (CompetitionWinnerScreen)");
    european_fields(g, game, rand);
    cup_field(g, game, pause, cup_draw);
    for (size_t m = 0; m < g.manager_teams.size(); m++) season_tickets(g, int(m), rand);  // CareerSeasonSetup2
    order_cup_ties(g, 0x40, 0);
    season_players(g, game, rand, report);  // CareerSeasonSetup3
    return !report.unsupported;
}

}  // namespace bmh::modern

namespace bmh::modern {

bool career_first_steps(ModernState& g, GameRandom& game, WatcomRandom& rand, MinuteReport& report) {
    g.memory.set32(::bmh::layout::CareerSession_Invalid_RuntimeField2, uint32_t(game.seed()));  // CareerFirstSteps2: CareerSession_Invalid_RuntimeField2 = CareerSession_Save_RuntimeField
    g.memory.set8(::bmh::layout::ActiveManager, 4);
    g.autosave_month = 7;  // DailyInterest(0): SeasonRuntimeField27
    shuffle_leagues(g, rand);   // Getenv2
    modern::new_transfer_list_entries(g, rand, report);  // CareerFirstSteps
    return !report.unsupported;
}

}  // namespace bmh::modern
