// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/weekly.hpp"
#include "game/pending_offer.hpp"
#include "game/human_buyer_sale.hpp"
#include "game/transfer_bid.hpp"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "game/lineup.hpp"
#include "game/match.hpp"
#include "game/messages.hpp"
#include "game/month.hpp"
#include "game/prediction.hpp"
#include "game/matchday.hpp"
#include "game/matchday_setup.hpp"
#include "game/schedule.hpp"
#include "game/table.hpp"
#include "game/national.hpp"
#include "game/bank_credit.hpp"

namespace bmh {

namespace {

constexpr size_t kRecord = 0xB0C;       // manager record
constexpr size_t kBoard = 0x840;        // tactics board of a manager
constexpr size_t kBoardSlot = 0x58;
constexpr uint32_t kTransferList = ::bmh::layout::EditorList;  // 44 entries of 0x58 bytes, one per pool slot
constexpr uint32_t kListEntry = 0x58;
const int& kPoolSlots = runtime_dimensions.pool_first;    // CareerRuntimeField: first squad slot of the transfer pool
const int& kLeagueClubs = runtime_dimensions.league_teams;      // CareerSession_DispatchLineup_RuntimeField
constexpr int kBigSquadClubs = 90;      // TeamsWithSquads

// Offers and incidents (Bool): 1 in WeeklyRuntimeField[kind] is only possible while the
// league is not in its last rounds.
constexpr uint8_t kOfferLate[16] = {0, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0};  // WeeklyRuntimeField
constexpr int kShareFactor[6] = {15, 18, 21, 24, 27, 30};                           // MonthRuntimeField10
// Facilities (30 entries of 5 bytes at layout field): [0] weight in the club rating,
// [2] / [3] the range of the months a renewed facility lasts (x 4 weeks).
constexpr uint8_t kFacility[152] = {
    0x02, 0x2d, 0x0f, 0x1e, 0x00, 0x03, 0x1e, 0x0f, 0x16, 0x00, 0x03, 0x1c, 0x0f, 0x16, 0x00, 0x07, 0x28, 0x16, 0x1e,
    0x00, 0x06, 0x1e, 0x0d, 0x13, 0x00, 0x02, 0x14, 0x0f, 0x16, 0x00, 0x01, 0x06, 0x07, 0x0c, 0x00, 0x01, 0x06, 0x07,
    0x0c, 0x00, 0x01, 0x06, 0x07, 0x0c, 0x00, 0x01, 0x06, 0x07, 0x0c, 0x00, 0x01, 0x05, 0x07, 0x0c, 0x00, 0x01, 0x05,
    0x07, 0x0c, 0x00, 0x01, 0x05, 0x07, 0x0c, 0x00, 0x01, 0x05, 0x07, 0x0c, 0x00, 0x02, 0x0f, 0x0c, 0x10, 0x00, 0x01,
    0x0c, 0x0c, 0x10, 0x00, 0x02, 0x19, 0x12, 0x16, 0x00, 0x01, 0x14, 0x12, 0x16, 0x00, 0x01, 0x04, 0x03, 0x06, 0x00,
    0x02, 0x07, 0x04, 0x07, 0x00, 0x02, 0x09, 0x04, 0x07, 0x00, 0x03, 0x05, 0x05, 0x07, 0x00, 0x00, 0x1e, 0x1e, 0x32,
    0x00, 0x00, 0x25, 0x1e, 0x32, 0x00, 0x00, 0x1e, 0x1e, 0x32, 0x00, 0x00, 0x19, 0x1e, 0x32, 0x00, 0x00, 0x1e, 0x1e,
    0x32, 0x00, 0x00, 0x25, 0x1e, 0x32, 0x00, 0x00, 0x1e, 0x1e, 0x32, 0x00, 0x00, 0x19, 0x1e, 0x32, 0x00, 0x02, 0x06};
// Season ticket compensation per league (CareerSession_FormatError_RuntimeField5, int; leagues above 4 read on).
constexpr uint8_t kTicketRefund[36] = {0x14, 0, 0, 0, 0x12, 0, 0, 0, 0x10, 0, 0, 0, 0x10, 0, 0, 0, 0x0f, 0, 0, 0,
                                       0x02, 0x7d, 0xe0, 0xcd, 0x00, 0x8f, 0x00, 0x11, 0x01, 0xc9, 0x00, 0xf3,
                                       0x00, 0x6d, 0x00, 0x22};
// Transfer interest: required strength per league (LeagueStrengthRanges, every second byte) and by
// table position (WeeklyRuntimeField2).
constexpr uint8_t kLeagueLevel[20] = {0x47, 0x5d, 0x31, 0x47, 0x1c, 0x31, 0x1c, 0x31, 0x0a, 0x1c,
                                      0x55, 0x5f, 0x5a, 0x5f, 0x4b, 0x5f, 0x50, 0x5f, 0x5a, 0x00};
constexpr uint8_t kPositionLevel[24] = {0x17, 0x16, 0x15, 0x13, 0x12, 0x11, 0x10, 0x0e, 0x0d, 0x0c, 0x0b, 0x0a,
                                        0x09, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x00, 0xe6, 0xee, 0xf3, 0xf9};
// Players a club wants per position (MonthRuntimeField4 big squads, MonthRuntimeField5 small squads).
constexpr int8_t kWantedBig[4] = {2, 6, 7, 5}, kWantedSmall[4] = {2, 4, 5, 4};
// Advertising: European cup bonus by EuropeanClub (SeasonRuntimeField11) and cup winners
// (SeasonRuntimeField12 entries of the cup table, bonus SeasonRuntimeField13, premium SeasonRuntimeField14).
constexpr int8_t kEuropeBonus[4] = {0, 1, 3, 2};
constexpr uint8_t kWinnerEntry[4] = {0x00, 0x0f, 0x13, 0x15};
constexpr int8_t kWinnerBonus[4] = {0x14, 0x1e, 0x14, 0x0f};
constexpr int32_t kWinnerPremium[4] = {500000, 1500000, 2500000, 1500000};
// Statistics: value of the facilities (BalancePlanRuntimeField5, u16 per facility).
constexpr uint16_t kFacilityValue[22] = {2000, 3000, 3000, 8000, 4000, 3000, 2500, 2500, 2500, 2500, 2600,
                                         2600, 2600, 2600, 13000, 9000, 9000, 8000, 1000, 1500, 1500, 2000};

struct Record {
    std::vector<uint8_t>& r;
    size_t base;
    uint8_t& b(size_t o) { return r.at(base + o); }
    int16_t i16(size_t o) const { return int16_t(r.at(base + o) | r.at(base + o + 1) << 8); }
    uint16_t u16(size_t o) const { return uint16_t(r.at(base + o) | r.at(base + o + 1) << 8); }
    int32_t i32(size_t o) const { return int32_t(uint32_t(u16(o)) | uint32_t(u16(o + 2)) << 16); }
    uint32_t u32(size_t o) const { return uint32_t(u16(o)) | uint32_t(u16(o + 2)) << 16; }
    void set16(size_t o, uint16_t v) { r.at(base + o) = uint8_t(v), r.at(base + o + 1) = uint8_t(v >> 8); }
    void set32(size_t o, uint32_t v) { set16(o, uint16_t(v)), set16(o + 2, uint16_t(v >> 16)); }
};
Record record(GameState& g, int manager) { return Record{g.manager_records, size_t(manager) * kRecord}; }

// The special post-desk adapter retains FindManagerForClub's active-count lookup:
// inactive original records1..3 are AI clubs, while the coach's parked club is
// recognized as native record4. Ordinary/legacy callers keep their old lookup.
int pending_native_manager_of(const GameState& g, int team, bool native_takeover) {
    const int active = g.manager_of(team);
    if (active >= 0 || !native_takeover || g.manager_teams.size() != 1 ||
        g.manager_teams[0] != kNoTeam || 4*kRecord+0x292 > g.manager_records.size())
        return active;
    const size_t base = 4*kRecord+0x290;
    const int parked = int(g.manager_records[base]) | (int(g.manager_records[base+1])<<8);
    return parked == team ? 4 : -1;
}

// Validate the opt-in against the actual AdvanceTurn/DispatchClubPlayerPendingOffers coordinates. It
// cannot turn an ordinary/day-step/poach request into a national pending sale.
bool source_pending_owner_valid(const GameState& g,const SourcePendingOwner& owner,int entry,size_t slot) {
    const auto& mem=g.memory;
    if(entry<40 || entry>=44 || slot!=kPoolSlots+size_t(entry) || slot>=g.squad_slots.size() ||
        g.manager_teams!=std::vector<int>{kNoTeam} || g.manager_records.size()<5*kRecord ||
        g.tactics.size()<5*kBoard || !mem.has(::bmh::layout::CoachActive) || !mem.u8(::bmh::layout::CoachActive) ||
        !mem.has(::bmh::layout::OriginalManagerCount) || mem.u8(::bmh::layout::OriginalManagerCount)<1 || mem.u8(::bmh::layout::OriginalManagerCount)>4 ||
        !mem.has(::bmh::layout::ManagerCount) || mem.u8(::bmh::layout::ManagerCount)!=1 || !mem.has(::bmh::layout::PendingOffer_PendingOfferOwnerTeam_RuntimeField) ||
        !mem.has(::bmh::layout::DeskPosition) || mem.u8(::bmh::layout::DeskPosition)!=0 || !mem.has(::bmh::layout::ActiveManager) ||
        owner.manager<0 || owner.manager>=mem.u8(::bmh::layout::OriginalManagerCount) ||
        mem.u16(::bmh::layout::NextDeskManager)!=owner.manager || mem.u8(::bmh::layout::ActiveManager)!=owner.manager ||
        !mem.has(::bmh::layout::OfferSlots+uint32_t(entry-40)*4+3))return false;
    const auto team=[&](int manager){const size_t at=size_t(manager)*kRecord+0x290;
        return int(g.manager_records[at]) | int(g.manager_records[at+1])<<8;};
    if(team(0)!=kNoTeam || team(4)<0 || team(4)>=kLeagueClubs ||
        team(owner.manager)!=owner.team ||
        (owner.manager==0 ? owner.team!=kNoTeam : owner.team<0 || owner.team>=kLeagueClubs))return false;
    const uint32_t list=kTransferList+uint32_t(entry)*kListEntry;
    if(!mem.has(list+kListEntry-1) || mem.u16(list+2)!=0xffff ||
        mem.u16(list+4)!=0xffff || mem.u16(list+6)!=0xffff)return false;
    const uint32_t word=mem.u32(::bmh::layout::OfferSlots+uint32_t(entry-40)*4);
    return word!=0xffffffffu && int(word&31)==owner.manager &&
        int((word>>10)&0x0fff)==int(g.squad_slots[slot]&0x0fff);
}

int league_of(int team) { return team > (kNoTeam - 1) ? 0 : team_league(team); }  // TeamLeague2

}  // namespace

// Nationality: nationality of a player (w368 bits 27-31, +32 for the flag in *PlayerBonuses).
int nationality(const GameState& g, int player) {
    player &= 0xFFF;
    return int(g.players.at(size_t(player)).w368 >> 27) + ((g.player_bonus.at(size_t(player)) & 0x80) >> 2);
}

// SquadCount(team, flags): players of a club (bit 1: the injured too) plus, with bit 0,
// the players it has bought for later (table Career_RemoveJoinLoans_RuntimeField, 200 entries).
int squad_count(const GameState& g, int team, int flags) {
    const SquadRange range = squad_range(team);
    int n = 0;
    for (size_t i = 0; i < range.count; i++) {
        const int p = g.squad_slots.at(range.first + i) & 0xFFF;
        if (p != 0xFFF && ((flags & 2) || ((g.players.at(size_t(p)).w364 >> 16) & 0x1F) == 0)) n++;
    }
    if (flags & 1)
        for (int k = 0; k < 200; k++) {
            const uint32_t e = g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + uint32_t(k) * 4);
            if (e != 0 && ((e >> 12) & 0x3FF) == uint32_t(team & 0xFFFF)) n++;
        }
    return n & 0xFF;
}

// SlotTeam: club of an absolute squad slot (-1 for the pool).
int slot_team(int slot) {
    const int large=runtime_dimensions.big_squad_teams;
    if (slot < large * 24) return slot / 24;
    if (slot < int(kPoolSlots)) return (slot - large * 24) / 16 + large;
    return -1;
}

// Add32 / FacilityRating2: the club's rating of its facilities.
int facility_rating(GameState& g, int manager, int* second, WatcomRandom* rand) {
    Record rec = record(g, manager);
    int sum = 0x14, first = 0, closed = 0;
    *second = 0;
    if (g.memory.u8(::bmh::layout::GameMode) == 1) {
        int league = rec.b(0x210);
        if (league > 2) league--;
        return 0x50 - league * 0x14;
    }
    for (int i = 0; i < 0x16; i++) {
        const uint8_t v = rec.b(size_t(i));
        if (v == 0) continue;
        int level;
        if (i < 0xe || i == 0x10 || i == 0x11) level = i == 0 ? (v - 1) / 0x55 + 1 : 1;
        else level = rec.b(size_t(i) + 0x2e) & 7;
        sum += (level & 0xFF) * kFacility[i * 5];
    }
    for (int i = 0; i < 8; i++) {
        if (rec.b(0x34 + size_t(i)) != 0) {
            // a stand under construction costs 10-12 % (only drawn for the attendance)
            if (rand) closed += rand->range(10, 12);
            continue;
        }
        if (rec.b(0x44 + size_t(i)) & 1) first += rec.b(0x4c + size_t(i));
        if (rec.b(0x44 + size_t(i)) & 2) *second += rec.b(0x4c + size_t(i));
    }
    const int seats = rec.b(0x5c);
    if (seats == 0) {
        *second = 0;
        first = 0;
    } else {
        first = first * 100 / seats;
        *second = *second * 100 / seats;
    }
    return (((100 - closed) * sum / 100 + first / 4 + (*second >> 4)) * 0x5a) / 0x72 + 0x18;
}

void manager_offers(GameState& g, int manager, WatcomRandom& rand, MinuteReport& report) {
    if (g.memory.u32(::bmh::layout::CompetitionSelection) == 2) return;
    Record rec = record(g, manager);
    const bool events_off = false;  // OpenBMH deviates from the original: uses the supported weekly-event configuration, with its rule switch disabled.
// WeeklyEventsDisabled (0 in the supported rule profile)
    const uint8_t weeks = g.memory.u8(::bmh::layout::CareerDuration);
    for (int kind = 0; kind < 16; kind++) {
        if ((kind == 11 || kind == 12) && weeks == 1) continue;
        if (kind == 7) continue;
        if (kind == 9) {
            if (rand.range(0, 1) != 0 && !events_off) continue;
            if (g.memory.u32(::bmh::layout::Loans + uint32_t(manager) * 0x78) == 0) continue;
        }
        if ((kind == 13 || kind == 15) && rand.range(0, 1) != 0 && !events_off) continue;
        int sum = 0;  // ESI (kind 14: dividends)
        if (kind == 14) {
            for (int k = 0; k < 6; k++) {
                const uint32_t held = g.memory.u16(::bmh::layout::Advertising_OriginalShareAdjustment_RuntimeField + uint32_t(manager) * 2 + uint32_t(k) * 0x1c);
                const uint32_t value = uint32_t(g.memory.u32(::bmh::layout::FundBlock + uint32_t(k) * 0x1c) * uint32_t(kShareFactor[k])) / 10000;
                sum += int(uint32_t(rand.range(10, 20)) * (held * value));
            }
            if (sum == 0) continue;
        }
        if (kind <= 7 && rec.b(0x170 + size_t(kind)) != 0) continue;
        if (!events_off && rand.range(0, (0x39 - g.level) * 0xd - kOfferLate[kind] * 0x16c) != 0) continue;
        if (kOfferLate[kind] != 0 && g.league_rounds.at(rec.b(0x210)) >= 0x14) continue;
        int league = rec.b(0x210);
        if (league > 2) league--;
        uint8_t value = 0;  // local_18
        // the message: the offer's text (pointer table Weekly_ManagerOffers_TextPointer6) plus details
        std::string msg = kind < 15 ? message_text(g, ::bmh::layout::Weekly_ManagerOffers_TextPointer6 + uint32_t(kind) * 4) : std::string();
        switch (kind) {
            case 0: {
                // the main sponsor pays a part of a transfer: text + position + amount limits
                const int text = rand.range(0, 3);
                msg += message_text(g, ::bmh::layout::Weekly_ManagerOffers_TextPointer) + message_text(g, ::bmh::layout::IncomingTransfer_RenderAtElapsed_TextPointer2 + uint32_t(text) * 4) + message_text(g, ::bmh::layout::Weekly_ManagerOffers_TextPointer2);
                const int a = rand.range(1, 7), b = rand.range(1, 7);
                value = uint8_t(text + uint8_t(a << 2) + uint8_t(b << 5));
                break;
            }
            case 1:
            case 2: {
                const int a = rand.range(1, 7), b = rand.range(1, 7);
                value = uint8_t(uint8_t(a << 2) + uint8_t(b << 5));
                break;
            }
            case 3:
                value = uint8_t(rand.range(10, 20));
                msg += format_number(g, value * (4 - league) * 1000, 0x101) + message_text(g, ::bmh::layout::Weekly_ManagerOffers_TextPointer3);
                break;
            case 4:
                value = uint8_t(rand.range(4, 20));
                msg += format_number(g, value * 1000, 0x101) + message_text(g, ::bmh::layout::Weekly_ManagerOffers_TextPointer4);
                break;
            case 5: {
                const int seats = rec.b(0x5c) * 1000;
                const int r = rand.range(0x46, 100);
                sum = ((0x14 - league * 2) * (seats * r / 100)) / 10000 * 10000;
                value = uint8_t(sum / 10000);
                break;
            }
            case 6:
                sum = (4 - league) * 50000;
                value = uint8_t(rand.range(5, 10));
                msg += format_number(g, sum, 0x101) + message_text(g, ::bmh::layout::Desk_SaveGameDraws_TextPointer) + format_number(g, value, 1) + message_text(g, ::bmh::layout::Weekly_ManagerOffers_TextPointer5);
                break;
            case 8:
                value = uint8_t(rand.range(5, (4 - league) * 5 + 10));
                rec.b(0x170 + 8) = value;
                break;
            case 9:
                value = uint8_t(rand.range(1, 3) * 10);
                if (!change_bank_credit_interest(g, manager, value)) {
                    report.fail("weekly offer: unavailable credit factors (ChangeBankCreditInterest)");
                    return;
                }
                break;
            default: break;
        }
        if (kind < 10 && kind != 5 && kind != 8) {
            rec.b(0x170 + size_t(kind)) = value;
            post_composed(g, manager, msg);  // PostMessage
            continue;
        }
        // The other offers ask the manager (PresentManagerDialog with JA/NEIN, a message box only).
        // GameState::offer_answer decides (the replays decline unless told otherwise).
        if (!g.offer_answer) {
            if (kind == 14) {
                // declined dividends: with 1 in 2 the payment comes anyway, twice (WeeklyRuntimeField3),
                // else WeeklyRuntimeField4 - both message boxes only
                if (rand.range(0, 1) == 0) book_money(g, manager, sum * 2, true);
            }
            continue;
        }
        switch (kind) {
            case 5: rec.b(0x170 + 5) = value; break;
            case 8:
                rec.b(0x170 + 8) = 0;
                book_money(g, manager, int32_t(value) * 10000, true);
                break;
            case 10:
                // insurance for every tactics board slot without one (slot byte 0x43)
                for (size_t s = 0; s < 0x18; s++) {
                    uint8_t& cover = g.tactics.at(size_t(manager) * 0x840 + s * 0x58 + 0x43);
                    if (cover == 0) cover = 0x4c;
                }
                break;
            case 11:
                // the advertising contracts (record +0xa34, 18 of 12 bytes: amount, +5 months
                // left) are extended by a year, up to 36 months
                for (size_t c = 0; c < 0x12; c++) {
                    const size_t at = 0xa34 + c * 0xc;
                    if (rec.i32(at) != 0 && int8_t(rec.b(at + 5)) < 0x24) rec.b(at + 5) = uint8_t(rec.b(at + 5) + 0xc);
                }
                break;
            case 12:
                // the advertising contracts are renegotiated: W(50, 150) % each
                for (size_t c = 0; c < 0x12; c++) {
                    const size_t at = 0xa34 + c * 0xc;
                    if (rec.i32(at) != 0) rec.set32(at, uint32_t(rec.i32(at) * rand.range(0x32, 0x96) / 100));
                }
                break;
            case 13: book_money(g, manager, 4, true); break;  // (4 EU, as in the original)
            case 14: book_money(g, manager, sum, true); break;
            default: break;
        }
    }
}

void season_ticket_compensation_source(GameState& g, int manager,
                                      const SeasonTicketCompensationNoticeHook& notice) {
    Record rec = record(g, manager);
    rec.b(0x5c) = 0;
    for (int i = 0; i < 8; i++)
        if (rec.b(0x34 + size_t(i)) == 0) rec.b(0x5c) = uint8_t(rec.b(0x5c) + rec.b(0x4c + size_t(i)));
    int missing = int(rec.u16(0x2ac)) - rec.b(0x5c) * 1000;
    if (missing <= 0) return;
    const int league = league_of(int16_t(rec.u16(0x290)));
    int weeks = 0x11 - g.league_rounds.at(size_t(league));
    if (weeks > 0xf) weeks = 0xf;
    const int32_t per = int32_t(kTicketRefund[league * 4] | kTicketRefund[league * 4 + 1] << 8 |
                                kTicketRefund[league * 4 + 2] << 16 | uint32_t(kTicketRefund[league * 4 + 3]) << 24);
    // Source uses 32-bit IMULs, then passes the signed dword to BookMoney2.
    // Unsigned intermediates make the wrap explicit without C++ signed UB.
    const uint32_t week_rate = uint32_t(weeks) * uint32_t(per);
    const int32_t amount = std::bit_cast<int32_t>(uint32_t(missing) * week_rate);
    if (notice) notice(g, {manager, missing, int(rec.b(0x5c)) * 1000, weeks, amount});
    book_money(g, manager, amount, true, true);
    rec.set16(0x2ac, uint16_t(rec.b(0x5c) * 1000));
}

void season_ticket_compensation(GameState& g, int manager) {
    season_ticket_compensation_source(g, manager);
}

void facility_ageing(GameState& g, int manager, GameRandom& game, WatcomRandom& rand, MinuteReport& report) {
    (void)report;
    Record rec = record(g, manager);
    uint8_t& weeks = g.memory.at(::bmh::layout::Savestate_Layout_ExecutableField);
    if (manager == 0) weeks++;
    if (weeks == 0xc) weeks = 0;
    if (weeks % (rand.range(0, 1) + 3) == 0) g.memory.set32(::bmh::layout::CareerSession_Invalid_RuntimeField2, uint32_t(game.seed()));  // the game generator seed is saved
    // "Ihr Sparkurs laesst Ihr Stadion langsam verfallen ... BETROFFEN: <facility>" for a
    // facility that decays (local_c is set once per call and never cleared: after the first
    // message every further decay of the call is reported, even one that would not be)
    bool message = false;
    for (int i = 0; i < 0x1e; i++) {
        if (weeks == 0 && rec.b(size_t(i)) != 0 && rec.b(0x1e + size_t(i)) == 0) {
            uint8_t& v = rec.b(size_t(i));
            v--;
            if ((i == 0 && v % 0x55 == 0) || rec.b(0x1e + size_t(i)) + v == 0) {
                if (i < 0xe) {
                    message = true;
                } else if (i < 0x16) {
                    uint8_t& n = rec.b(0x2e + size_t(i));
                    if (n > 1) {
                        message = true;
                        if (--n != 0) v = uint8_t(kFacility[i * 5 + 2] << 2);
                    }
                } else {
                    const size_t k = 0x4c + size_t(i - 0x16);
                    if (rec.b(0x5c) != 0 && rec.b(k) != 0) {
                        message = true;
                        rec.b(k) = uint8_t(rec.b(k) / 3);
                        if (rec.b(k) != 0) v = uint8_t(kFacility[i * 5 + 2] << 2);
                    }
                }
                if (message) post_composed(g, manager, message_text(g, ::bmh::layout::Weekly_FacilityAgeing_TextPointer) + message_text(g, ::bmh::layout::Stadium_RenderPanorama_TextPointer + uint32_t(i) * 4));
            }
        }
        uint8_t& build = rec.b(0x1e + size_t(i));
        if (build != 0 && --build == 0) {
            uint8_t& v = rec.b(size_t(i));
            if (i == 0) v = uint8_t((v == 0 ? 1 : (v - 1) / 0x55 + 2) * 0x55);
            else v = uint8_t(rand.range(kFacility[i * 5 + 2], kFacility[i * 5 + 3]) << 2);
            if (i > 0xd) {
                rec.b(0x2e + size_t(i)) = uint8_t((rec.b(0x2e + size_t(i)) >> 4) & 7);
                if (i > 0x15) {
                    rec.b(0x36 + size_t(i)) = rec.b(0x3e + size_t(i));
                    rec.b(0x3e + size_t(i)) = 0;
                }
            }
        }
    }
    season_ticket_compensation(g, manager);
}

void facility_damage(GameState& g, int manager, GameRandom& game, WatcomRandom& rand) {
    Record rec = record(g, manager);
    bool forced = false;  // BH: an announced damage (record +0x178)
    const int level = g.level;
    if (rec.b(0x178) != 0 && rand.range(0, 0x34) == 0) forced = true;
    if (!forced) {
        if (!(level > rand.range(0, 0xaf))) return season_ticket_compensation(g, manager);
        const int league = rec.b(0x210);
        if (!(rand.range(1, 3) > league)) return season_ticket_compensation(g, manager);
    }
    // The message is composed from pointer tables of TEXTD.BLK: articles Weekly_FacilityDamage_TextPointer2 (the
    // index tables WeeklyRuntimeField5/WeeklyRuntimeField6/WeeklyRuntimeField7 pick the right one), facility names Stadium_RenderPanorama_TextPointer,
    // causes Weekly_FacilityDamage_TextPointer (0-4 at random, 5 for an announced damage).
    static constexpr uint8_t kCauseArticle[6] = {0, 1, 7, 3, 1, 0};         // WeeklyRuntimeField5
    static constexpr uint8_t kCauseArticleBy[6] = {6, 2, 8, 3, 9, 6};       // WeeklyRuntimeField6
    static constexpr uint8_t kFacilityArticle[0x1e] = {0, 5, 4, 4, 4, 4, 5, 5, 5, 5, 4, 4, 4, 4, 5,  // WeeklyRuntimeField7
                                                       5, 5, 5, 4, 4, 4, 5, 5, 5, 5, 4, 5, 5, 5, 4};
    auto article = [&](int k) { return message_text(g, ::bmh::layout::Weekly_FacilityDamage_TextPointer2 + uint32_t(k) * 4); };
    auto facility = [&](int k) { return message_text(g, ::bmh::layout::Stadium_RenderPanorama_TextPointer + uint32_t(k) * 4); };
    auto cause = [&](int k) { return message_text(g, ::bmh::layout::Weekly_FacilityDamage_TextPointer + uint32_t(k) * 4); };
    std::string text;
    int which = game.range(0, 0x1d);
    if (forced) which = game.range(1, 0x1d);
    if (which == 0) {
        for (int k = 0; k < 6; k++) {
            const int i = rand.range(8, 0xf);
            if (rec.b(0x3c + size_t(i)) & 2) {
                rec.b(0x3c + size_t(i)) &= 0xFD;
                post_composed(g, manager, message_text(g, ::bmh::layout::Weekly_FacilityDamage_TextPointer4) + message_text(g, ::bmh::layout::Weekly_FacilityDamage_TextPointer3 + uint32_t(i) * 4) + message_text(g, ::bmh::layout::Weekly_FacilityDamage_TextPointer5));
                break;
            }
        }
        return season_ticket_compensation(g, manager);
    }
    const size_t i = size_t(which);
    if (rec.b(i) == 0 || rec.b(0x1e + i) != 0) return season_ticket_compensation(g, manager);
    int half = rand.range(0, (which < 0xe) + 1);
    if (rec.b(i) < 2) half = 0;
    if (forced) rec.b(0x178) = 0;
    if (half == 0) {
        rec.b(i) = 0;
        if (which < 0xe) {
            const int c = forced ? 5 : rand.range(0, 4);
            text = article(kFacilityArticle[i]) + facility(which) + message_text(g, ::bmh::layout::Weekly_SeasonTicketCompensation_TextPointer) + article(kCauseArticle[c]) + cause(c) +
                   message_text(g, ::bmh::layout::Weekly_SeasonTicketCompensation_TextPointer2);
        } else if (which < 0x16) {
            uint8_t& lvl = rec.b(0x2e + i);
            if (lvl > 1 || (lvl == 2 && (which == 0x10 || which == 0x11))) {
                if (rand.range(0, 2) == 0) lvl = 1;
                else lvl--;
                text = article(kFacilityArticle[i]) + facility(which) + message_text(g, ::bmh::layout::Weekly_SeasonTicketCompensation_TextPointer3);
            }
        } else if (rec.b(0x5c) != 0 && int(rec.b(0x42)) < rand.range(1, 3)) {
            int k;
            do {
                k = rand.range(0, 7);
            } while (rec.b(0x4c + size_t(k)) == 0);
            text = message_text(g, ::bmh::layout::Weekly_SeasonTicketCompensation_TextPointer4) + facility(k + 0x16) + message_text(g, ::bmh::layout::Weekly_SeasonTicketCompensation_TextPointer5);
            rec.b(0x4c + size_t(k)) = 0;
        }
    } else {
        rec.b(i) = uint8_t(rec.b(i) >> 1);
        const int c = forced ? 5 : rand.range(0, 4);
        text = article(kFacilityArticle[i]) + facility(which) + message_text(g, ::bmh::layout::Weekly_SeasonTicketCompensation_TextPointer6) + article(kCauseArticleBy[c]) + cause(c) +
               message_text(g, which > 0x11 && which < 0x16 ? ::bmh::layout::Weekly_SeasonTicketCompensation_TextPointer7 : ::bmh::layout::Weekly_SeasonTicketCompensation_TextPointer8);
    }
    if (!text.empty()) post_composed(g, manager, text);  // PostMessage
    season_ticket_compensation(g, manager);
}

// ClubAttraction(team, 0): the attraction of a club for advertisers (0-100).
int club_attraction(const GameState& g, int team) {
    // EuropeanClub: which European cup the club still plays in
    int europe = 0;
    const struct { uint32_t list, count; } cups[3] = {{::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField25, ::bmh::layout::Career_EuropeanClub_ExecutableField}, {::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField14, ::bmh::layout::Career_EuropeanClub_ExecutableField2}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField19, ::bmh::layout::Career_EuropeanClub_ExecutableField3}};
    for (int c = 0; c < 3 && europe == 0; c++)
        for (int k = 0; k < int(int8_t(g.memory.u8(cups[c].count))); k++)
            if (g.memory.u16(cups[c].list + uint32_t(k) * 2) == uint16_t(team)) {
                europe = c + 1;
                break;
            }
    int8_t bonus = int8_t(kEuropeBonus[europe] * 10 + 10);
    if (team == int(g.memory.u16(::bmh::layout::Cup_AdvanceCup_RuntimeField6))) bonus = int8_t(bonus + 10);
    for (int k = 0; k < 4; k++)
        if (g.memory.u16(::bmh::layout::Season_CareerSeasonSetup_RuntimeField + uint32_t(kWinnerEntry[k]) * 0x28) == uint16_t(team)) bonus = int8_t(bonus + kWinnerBonus[k]);
    const int strength = uint16_t(team_average(g, team, 0xff).value);
    int position = int8_t(table_position(g, team));
    if (position == -1) position = 10;
    int league = int8_t(league_of(team));
    if (league > 2) league--;
    const int place = uint16_t((4 - league) * 0x16 + (0x14 - position));
    int v = int16_t(place * 40 / 108 + strength * 2 / 10 + bonus * 4 / 5);
    return std::clamp(v, 0, 100);
}

void advertising_offers(GameState& g, int manager, WatcomRandom& rand, MinuteReport& report) {
    (void)report;
    if (g.memory.u8(::bmh::layout::CareerDuration) == 1) return;
    Record rec = record(g, manager);
    if (manager == 0) {
        // "Es gibt neue Werbeangebote!" to every manager with a free advertising slot
        uint32_t mask = 0x80000000u;
        for (size_t m = 0; m < g.manager_teams.size(); m++)
            for (int k = 0; k < 0x10; k++)
                if (g.manager_records.at(m * kRecord + 0xa39 + size_t(k) * 0xc) == 0) mask |= 1u << m;
        if (mask != 0x80000000u) post_text(g, int64_t(mask), text_id(::bmh::layout::Weekly_AdvertisingOffers_TextPointer));
    }
    int score = int16_t(int8_t(club_attraction(g, int16_t(rec.u16(0x290)))));
    if (g.memory.u8(::bmh::layout::GameMode) == 3 && g.league_rounds.at(size_t(league_of(int16_t(rec.u16(0x290))))) > 0x11) {
        int t = rec.i32(0x320) / 100000;
        t = std::clamp(t, -10, 10);
        score = std::clamp(int(int16_t(score + t)), 0, 100);
    }
    const int fans = rec.i16(0x8b4) * 100 / 0x226 - 100;
    score = int16_t(score) + fans / 5;
    if (int16_t(score) < 0) score = 0;
    score = int16_t(score) * 100 / 0x78;
    int base = (int16_t(score) * 60000 + 1000000) / 0xc;
    base = ((0xb4 - g.level * 5) * base) / 100;
    for (int kind = 0; kind < 3; kind++)
        for (int k = 0; k < 10; k++) {
            const size_t o = 0x8cc + size_t(kind) * 0x78 + size_t(k) * 0xc;
            rec.b(o + 7) = uint8_t(k);
            const int r = rand.range(0, (k * 2) / 3 + 5 - rec.i16(0x8b4) / 0x14);
            if (int16_t(r) != 0) continue;
            if ((k >= 5 && kind == 2) || rand.range(0, 0xb) <= k) {
                rec.set32(o, 0);
                continue;
            }
            int part, percent = 0;
            if (kind == 0) {
                part = (base / 2) / 8;
                percent = k * 6 + 0x46;
            } else {
                part = base >> 2;
                percent = kind == 1 ? k * 4 + 0x50 : k * 4 + 0x5a;
            }
            const int v = percent * part / 100;
            rec.set32(o, uint32_t(rand.range(v - (v >> 2), v + (v >> 2))));
            if (kind == 0 && rand.range(0, 5) == 0) rec.b(o + 4) = uint8_t(rand.range(2, 3));
            else rec.b(o + 4) = 1;
            rec.b(o + 5) = uint8_t(rand.range(1, 2));
            if (rand.range(0, 5) != 0) rec.b(o + 6) = 0;
            else rec.b(o + 6) = uint8_t(rand.range(1, 3 - int8_t(rec.b(o + 5))) * 0xc);
            rec.b(o + 5) = uint8_t(rec.b(o + 5) * 0xc);
            rec.set16(o + 8, uint16_t(score));
        }
}

void transfer_bid(GameState& g, int slot, uint32_t list, bool high, int months, GameRandom& game, WatcomRandom& rand,
                  MinuteReport& report) {
    StaticMemory& mem = g.memory;
    const int own = slot_team(slot);
    int free_bid = 0;  // local_14
    if (list != 0) {
        while (free_bid < 4 && int16_t(mem.u16(list + uint32_t(free_bid) * 2)) != -1) free_bid++;
        if (free_bid == 4) return;
    }
    const int player = g.squad_slots.at(size_t(slot)) & 0xFFF;
    const int nat = nationality(g, player) & 0xFF;
    const uint32_t w35c = g.players.at(size_t(player)).w35c;
    const int position = g.players.at(size_t(player)).w364 & 3;
    const int half = int(((w35c >> 14) & 0x7F) + ((w35c >> 7) & 0x7F)) >> 1;  // local_5c
    bool first_pass = true;  // local_34
    uint8_t foreign_skip = 0;  // local_1c
    int bonus_need = 0;  // local_4c: the value of the last club rated (not of the chosen one)
    for (;;) {
        std::vector<int16_t> teams;
        std::vector<uint32_t> keys;
        std::vector<int> needs;
        int region = 0;  // local_20 (not reset between the clubs)
        for (int team = 0; team < kLeagueClubs; team++) {
            if (team == own || g.manager_of(team) >= 0) continue;
            if (team >= kBigSquadClubs) region = team_region(team) & 0xFF;
            if (own >= 0 && g.manager_of(own) >= 0 && list != 0 && (w35c >> 27) != 0 && game.range(0, 0xf) != 0) continue;
            if (first_pass && nat < 5 && nat != region && foreign_skip++ % 10 == 0) continue;
            const int players = squad_count(g, team, 3);
            if (region == 0 && players > 0x15) continue;
            if (region != 0 && players > 0xf) continue;
            int avg[4];
            int count[4];
            for (int p = 0; p < 4; p++) {
                const TeamAverage a = team_average(g, team, uint16_t((1 << (p + 4)) + 3));
                avg[p] = int16_t(a.value);
                count[p] = a.players;
            }
            int score = (region == nat || nat > 4) ? 0x46 : 0;
            int16_t strength = 0;
            for (int p = 0; p < 4; p++) strength = int16_t(strength + avg[p]);
            strength = int16_t(strength >> 2);
            const int league = league_of(team);
            int need = 0;  // local_4c
            if (g.league_rounds.at(size_t(league)) > 10) {
                const int pos = int8_t(table_position(g, team));
                const int16_t wanted = int16_t(kLeagueLevel[league * 2] + kPositionLevel[pos]);
                if (strength < wanted && strength < int16_t(half)) need = wanted - strength;
            }
            const int diff = std::abs(half - strength);  // Abs2
            const int r = rand.range(int16_t(need) + 3, int16_t(need) + 0xd);
            if (int16_t(diff) < r) score += int(int16_t(need + 0xe - diff)) * 16 + 100;
            else score -= int16_t(diff) * 8;
            const int8_t* wanted = team < runtime_dimensions.big_squad_teams ? kWantedBig : kWantedSmall;
            int16_t d = int16_t(wanted[position] - int8_t(count[position]));
            if (d < 0) {
                score += int(d) << 8;
            } else {
                d = int16_t(d + 1);
                if (position == 0 && int8_t(count[0]) < 2) d = int16_t(d + (2 - int8_t(count[0])) * 4);
                score += int(d) << 6;
                if (score > 1) {
                    if (strength > rand.range(0x5a, 0x62) && int16_t(diff) < rand.range(0, 0x14)) score += rand.range(10, 0x28);
                    score += ((g.team_flags.at(size_t(team)) >> 14) & 3) << 4;
                }
            }
            int16_t limit = int16_t((region == 0 ? 3 : 0) + 0xe);
            int cnt = players;
            if (int16_t(cnt) < limit) {
                // (a bug of the original: it counts the club among the bidders of entry 40)
                int8_t twice = 0;
                for (int b = 0; b < 4; b++)
                    if (mem.u16(kTransferList + 40 * kListEntry + uint32_t(b) * 2) == uint16_t(team)) twice++;
                cnt = uint8_t(cnt + twice / 2);
                limit = int16_t(limit - cnt);
                if (limit > 0 && int16_t(diff) < limit * 5 + 10) score += int(limit) << 7;
            }
            keys.push_back(uint32_t(score + 100000));
            teams.push_back(int16_t(team));
            needs.push_back(need);
            bonus_need = need;
        }
        int chosen = -1, index = -1;
        const int n = int(teams.size());
        if (n != 0) {
            // Games sorts both lists; the need of the chosen club is kept separately
            std::vector<int16_t> order(static_cast<size_t>(n));
            for (int i = 0; i < n; i++) order[size_t(i)] = int16_t(i);
            std::vector<uint32_t> sorted = keys;
            quicksort_by_key(order.data(), sorted.data(), n);
            for (index = 0; index < n; index++) {
                const int team = teams[size_t(order[size_t(index)])];
                int slots = int(squad_range(team).count);
                if (list != 0)
                    for (int b = 0; b < 4; b++)
                        if (mem.u16(list + uint32_t(b) * 2) == uint16_t(team)) {
                            slots = 0;
                            break;
                        }
                const bool reject = g.manager_of(team) >= 0 || (list != 0 && team == (mem.u16(list + 8) & 0x7FFF)) || slots == 0;
                if (!reject) {
                    chosen = team;
                    break;
                }
            }
            // The key of the chosen club is read with the club id as index (a bug: MOV EAX,
            // [ESP + club * 4] at 00017ecd, the key array holds 138 entries). Beyond the
            // candidates of this call the slot keeps whatever an earlier call left on the stack;
            // it only matters through clamp(key / 0x50, 0, 4) below, and any bid key of an
            // earlier call (score + 100000) gives the top level 4. The number of draws is the
            // same either way, only the size of the bid could differ.
            // (Known gap: the stale slot lies 0x1cc-0x218 bytes below the caller's stack
            // pointer and holds whatever the functions DayStepRest3 called before left there.
            // Level 4 matches WEEK21->22; in the summer of WEEK42->43 the original had level 0
            // for a bid of day 337, so only the amounts of such bids may differ. Season 2 shows
            // level 0 as well (WEEK57, WEEK65: the salary factor gives r = W(90, 95)), so the
            // slot then holds no bid key at all - a key would always give level 4 - but data
            // of another function at that stack depth; WEEK42/44/59/63 fit neither 0 nor 4.
            // A different amount can change which bid wins later (WEEK149, day 1275: another
            // club got the player, the line-ups and then the injury rolls went apart).)
            if (chosen >= 0) keys[0] = chosen < n ? sorted[size_t(chosen)] : 4 * 0x50;
        }
        if (chosen < 0) {
            if (list != 0) return;
            if (first_pass) {
                first_pass = false;
                continue;
            }
            report.fail("transfer: no club found (internal error 0x2a)");
            return;
        }
        if (list == 0) {
            // no list entry: the player moves straight to the club (its first empty squad
            // slot, FirstFreeSlotSquad), which gets a new line-up (Int2(club, 100))
            const SquadRange r = squad_range(chosen);
            int free = -1;
            for (size_t s = r.first; s < r.first + r.count && free < 0; s++)
                if (g.squad_slots.at(s) == 0x0FFF) free = int(s);
            if (free < 0) {
                report.fail("transfer: no room in the club's squad (internal error 0x2c)");
                return;
            }
            g.squad_slots.at(size_t(free)) = uint16_t(player);
            try {
                auto_lineup(g, chosen, 100);
            } catch (const std::exception& ex) {
                report.fail(std::string("line-up after a transfer: ") + ex.what());
            }
            return;
        }
        mem.set16(list + uint32_t(free_bid) * 2, uint16_t(chosen));
        if (mem.u16(list + 8) == 0xffe) {
            mem.set8(list + 0x34 + uint32_t(free_bid), uint8_t(uint8_t(months) * 0xc));
        } else {
            bool loan = rand.range(0, 6) == 4;
            if (mem.u16(list + 8) == 0xfff) loan = false;
            const int m = rand.range(1, 4 - (loan ? 3 : 0));
            mem.set8(list + 0x34 + uint32_t(free_bid), uint8_t(uint8_t(m * 0xc) | (loan ? 0x80 : 0)));
            if (loan && rand.range(0, 10) > 7) mem.set8(list + 0x34 + uint32_t(free_bid), 0x98);
        }
        // RateTransferBidSource: fee and salary bases of the offer
        const uint8_t term = mem.u8(list + 0x34 + uint32_t(free_bid));
        uint32_t fee_base = (mem.u32(list + 0x2c) / 10000) * 0x30d4;
        if (term & 0x80) fee_base >>= 2;
        const int years = ((term & 0x7F) - 1) / 0xc;
        const uint32_t salary_base = ((uint32_t(years * 0x14 + 100) * mem.u32(list + 0x30)) / 10000) * 100;
        uint32_t k = keys[0] / 0x50;
        int16_t level = int16_t(std::clamp(int(int16_t(k)), 0, 4));
        level = int16_t(level + ((g.team_flags.at(size_t(chosen)) >> 14) & 3));
        const uint32_t r = uint32_t(rand.range(0x5a, level * 5 + 0x5f));
        uint32_t factor = (r * uint32_t(int16_t(bonus_need) + 100)) / 100;
        if (high) factor = uint32_t(rand.range(0x78, 0x96));
        mem.set32(list + 0x1c + uint32_t(free_bid) * 4, ((factor * fee_base) / 1000000) * 10000);
        mem.set32(list + 0x0c + uint32_t(free_bid) * 4, ((salary_base * r) / 10000) * 100);
        for (int i = 0; i < 7; i++) mem.set8(list + 0x38 + uint32_t(free_bid) * 7 + uint32_t(i), 0);
        return;
    }
}

namespace {

// RateTransferBidSource: fee and salary bases of bid `bid` of a list entry (-1: none, a one-year
// term): the fee base is the market value / 10000 * 12500 (a quarter for a loan), the
// salary base the salary * (100 + 20 per extra contract year) / 100.
void offer_bases(const StaticMemory& mem, uint32_t list, int bid, uint32_t& fee_base, uint32_t& salary_base) {
    bool loan = false;
    uint8_t years = 1;
    if (bid != -1) {
        const uint8_t term = mem.u8(list + 0x34 + uint32_t(bid));
        loan = (term & 0x80) != 0;
        years = uint8_t(((term & 0x7F) - 1) / 0xc);
    }
    fee_base = (mem.u32(list + 0x2c) / 10000) * 0x30d4;
    if (loan) fee_base >>= 2;
    salary_base = ((uint32_t(years * 0x14 + 100) * mem.u32(list + 0x30)) / 10000) * 100;
}

// ProjectTransferOfferList: how good bid `bid` is, as percentages of the bases (fee, salary); a fee
// below W(90, 100) % or a salary below the player's demand (entry +0x55) counts 0.
bool evaluate_bid(GameState& g, uint32_t list, int bid, uint32_t& fee_pct, uint32_t& salary_pct, WatcomRandom& rand,
                  MinuteReport& report) {
    StaticMemory& mem = g.memory;
    uint32_t fee_base = 0, salary_base = 0;
    offer_bases(mem, list, bid, fee_base, salary_base);
    if (fee_base == 0 || salary_base == 0) {
        report.fail("transfer list: bid bases 0 (the original divides by zero)");
        return false;
    }
    fee_pct = uint32_t(int32_t(mem.u32(list + 0x1c + uint32_t(bid) * 4)) * 100) / fee_base;
    const int32_t salary = int32_t(mem.u32(list + 0x0c + uint32_t(bid) * 4));
    uint64_t expected_salary = uint32_t(salary);
    const int bidder = int16_t(mem.u16(list + uint32_t(bid) * 2));
    const int manager = g.manager_of(bidder);
    if (manager >= 0) {
        const int position = int(g.players.at(size_t(g.squad_slots.at(kPoolSlots + size_t(list - kTransferList) / kListEntry) & 0xfff)).w364 & 3);
        static constexpr uint8_t goals[4] = {0, 2, 4, 8};
        const uint64_t annual = uint32_t(salary);
        expected_salary += annual * mem.u8(list + 0x38 + uint32_t(bid) * 7) * goals[position] / 100 / 12;
        expected_salary += annual * mem.u8(list + 0x39 + uint32_t(bid) * 7) * 34 / 100 / 12;
        expected_salary += annual * mem.u8(list + 0x3a + uint32_t(bid) * 7) * 25 / 100 / 12;
    }
    salary_pct = uint32_t(expected_salary * 100 / salary_base);
    if (fee_pct < uint32_t(rand.range(0x5a, 100))) fee_pct = 0;
    if (salary_pct < mem.u8(list + 0x55)) salary_pct = 0;
    return true;
}

// Pending callers supply their explicit ordinary/special-list-derived Source
// pool index. The shared ProjectTransferOfferList primitive never performs this mapping itself.
bool evaluate_pending_human_bid(GameState& g, uint32_t list, int bid,
                                uint32_t& fee_pct, uint32_t& salary_pct,
                                WatcomRandom& rand, MinuteReport& report,
                                const SourcePendingOwner* owner = nullptr) {
    if (list < kTransferList || (list - kTransferList) % kListEntry != 0 ||
        (list - kTransferList) / kListEntry >= 44 || bid < 0 || bid >= 4 ||
        !g.memory.has(list + kListEntry - 1)) {
        report.fail("ProjectTransferOfferList: invalid pending caller list/bid coordinates");
        return false;
    }
    const int entry = int((list - kTransferList) / kListEntry);
    const size_t slot = kPoolSlots + size_t(entry);
    if (slot >= g.squad_slots.size()) {
        report.fail("ProjectTransferOfferList: pending caller source slot outside native storage");
        return false;
    }
    TransferBidRatingRequest request{int(slot), entry, bid,
        int(int16_t(g.memory.u16(list + uint32_t(bid) * 2))),
        int(g.squad_slots.at(slot) & 0x0fff)};
    if(owner)request.source_pending_owner=*owner;
    const auto rating = rate_transfer_bid_source(g, request, rand, report);
    fee_pct = rating.fee_pct;
    salary_pct = rating.salary_pct;
    return rating.supported;
}

// Foreigners2: players of a club (squad slots start..start+count, plus the ones bought
// for later, table Career_RemoveJoinLoans_RuntimeField) whose nationality differs from `nation`.
int foreigners(const GameState& g, int team, size_t start, size_t count, int nation) {
    int n = 0;
    for (size_t s = start; s < start + count; s++) {
        const int p = g.squad_slots.at(s) & 0xFFF;
        if (p != 0xFFF && (nationality(g, p) & 0xFFFF) != nation) n++;
    }
    for (int k = 0; k < 200; k++) {
        const uint32_t e = g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + uint32_t(k) * 4);
        if (e != 0 && ((e >> 12) & 0x3FF) == uint32_t(team) && (nationality(g, int(e & 0xFFF)) & 0xFFFF) != nation) n++;
    }
    return int8_t(n);
}

// InterestedManagers(seller, buyer): human managers interested in a transfer: those in the league
// of the seller or the buyer whose club is within 5 table places of one of them (only with
// the option Weekly_InterestedManagers_RuntimeField off and a real seller).
uint32_t interested_managers(const GameState& g, int seller, int buyer) {
    uint32_t mask = 0;
    if (g.memory.has(::bmh::layout::Weekly_InterestedManagers_RuntimeField) && g.memory.u8(::bmh::layout::Weekly_InterestedManagers_RuntimeField) != 0) return 0;
    for (size_t m = 0; m < g.manager_teams.size(); m++) {
        if (seller == 0xFFF) continue;
        const int league = g.manager_records.at(m * kRecord + 0x210);
        if (league_of(seller) != league && league_of(buyer) != league) continue;
        const int own = int8_t(table_position(g, g.manager_teams[m]));
        if (std::abs(own - int16_t(table_position(g, seller))) > 5 && std::abs(own - int16_t(table_position(g, buyer))) > 5)
            continue;
        mask |= 1u << m;
    }
    return mask;
}

}  // namespace

TransferBidRating rate_transfer_bid_source(GameState& g, const TransferBidRatingRequest& request,
                                           WatcomRandom& rand, MinuteReport& report) {
    if (report.unsupported) return {};
    const auto fail = [&](const std::string& why) {
        report.fail("ProjectTransferOfferList: " + why);
        return TransferBidRating{};
    };
    if (request.list_entry < 0 || request.list_entry >= 44 ||
        request.bid_slot < 0 || request.bid_slot >= 4 || request.source_slot < 0 ||
        size_t(request.source_slot) >= g.squad_slots.size())
        return fail("invalid explicit source/list/bid coordinates");
    const uint32_t list = kTransferList + uint32_t(request.list_entry) * kListEntry;
    if (!g.memory.has(list + kListEntry - 1))
        return fail("missing native transfer list storage");
    const int player = int(g.squad_slots.at(size_t(request.source_slot)) & 0x0fff);
    const int bidder = int(int16_t(g.memory.u16(list + uint32_t(request.bid_slot) * 2)));
    if (player == 0x0fff || size_t(player) >= g.players.size() ||
        player != request.expected_player || bidder != request.expected_bidder ||
        bidder < 0 || bidder >= kLeagueClubs)
        return fail("source player/bidder identity mismatch");
    uint32_t fee_base = 0, salary_base = 0;
    offer_bases(g.memory, list, request.bid_slot, fee_base, salary_base);
    if (fee_base == 0 || salary_base == 0)
        return fail("zero Source denominator (original divide-by-zero boundary)");
    const bool native_takeover=request.source_pending_owner.has_value();
    if(native_takeover && (request.bid_slot!=0 ||
        !source_pending_owner_valid(g,*request.source_pending_owner,request.list_entry,size_t(request.source_slot))))
        return fail("invalid DispatchClubPlayerPendingOffers native pending owner context");
    const int manager = pending_native_manager_of(g,bidder,native_takeover);
    if (manager >= 0) {
        const bool parked=manager==4 && native_takeover;
        if ((!parked && (manager >= 4 || size_t(manager) >= g.manager_teams.size() ||
            std::count(g.manager_teams.begin(), g.manager_teams.end(), bidder) != 1)) ||
            size_t(manager) * kRecord + 0x292 > g.manager_records.size() ||
            size_t(manager) * kBoard + kBoardSlot > g.tactics.size())
            return fail("unsupported native human bidder record/board");
        const size_t rec = size_t(manager) * kRecord;
        const int record_team = int(g.manager_records.at(rec + 0x290)) |
                                (int(g.manager_records.at(rec + 0x291)) << 8);
        if (record_team != bidder)
            return fail("bidder manager record and runtime team disagree");
        const auto range = squad_range(bidder);
        if (range.count == 0 || range.first + range.count > g.squad_slots.size())
            return fail("bidder fitness squad outside native storage");
        for (size_t slot = range.first; slot < range.first + range.count; ++slot) {
            const uint16_t word = g.squad_slots.at(slot);
            if (word != 0x0fff && (word & kSlotInLineup) &&
                size_t(word & 0x0fff) >= g.players.size())
                return fail("bidder lineup references an unavailable fitness player");
        }
    }
    try {
        // Void5 mode4 has real divisions by each recent table's games.
        // Keep that native error boundary instead of accepting per_game's
        // generic zero fallback on an incomplete controlled/malformed season.
        if (manager >= 0) {
            const int league = team_league(bidder);
            if (league >= kLeagueCount || size_t(league) >= g.league_rounds.size())
                return fail("missing bidder prediction league");
            const int rounds = int(int8_t(g.league_rounds.at(size_t(league))));
            if (rounds >= 2 && rounds < league_matchdays(league)) {
                const auto recent = compute_league_table(g, league, std::max(0, rounds - 5), rounds, 3, 0);
                for (int club = 0; club < recent.size; ++club)
                    if (recent.games(club) == 0)
                        return fail("zero recent games in Source rank prediction");
            }
        }
        TransferBidRating rating;
        rating.fee_pct = (g.memory.u32(list + 0x1c + uint32_t(request.bid_slot) * 4) * 100u) / fee_base;
        uint32_t total_salary = g.memory.u32(list + 0x0c + uint32_t(request.bid_slot) * 4);
        if (manager >= 0) {
            const size_t board = size_t(manager) * kBoard;
            std::array<uint8_t, kBoardSlot> saved{};
            std::copy_n(g.tactics.begin() + std::ptrdiff_t(board), kBoardSlot, saved.begin());
            struct RestoreBoard {
                GameState& state; size_t offset;
                const std::array<uint8_t, kBoardSlot>& saved;
                ~RestoreBoard() {
                    std::copy(saved.begin(), saved.end(), state.tactics.begin() + std::ptrdiff_t(offset));
                }
            } restore{g, board, saved};
            // ProjectTransferOfferList's stack board reads only salary+50 and seven bonuses
            // +12. No seller board, actual bidder row, mood or 80% weighting.
            for (int b = 0; b < 4; ++b)
                g.tactics.at(board + 0x50 + size_t(b)) = uint8_t(total_salary >> (8 * b));
            for (int b = 0; b < 7; ++b)
                g.tactics.at(board + 0x12 + size_t(b)) =
                    g.memory.u8(list + 0x38 + uint32_t(request.bid_slot) * 7 + uint32_t(b));
            total_salary += manager==4
                ? expected_bonus_parked_club_source(g,0xff,0,player,true)
                : expected_bonus(g, manager, 0xff, 0, player, true);
        } // Restore the full borrowed board before threshold W or any caller.
        rating.salary_pct = (total_salary * 100u) / salary_base;
        if (rating.fee_pct < uint32_t(rand.range(90, 100))) rating.fee_pct = 0;
        if (rating.salary_pct < g.memory.u8(list + 0x55)) rating.salary_pct = 0;
        rating.supported = true;
        return rating;
    } catch (const std::exception& e) {
        return fail(e.what());
    }
}

// Int2(team, 100) for the clubs of a transfer (only clubs with a squad).
static void lineup_after_transfer_impl(GameState& g, int team, bool native_takeover) {
    if (pending_native_manager_of(g,team,native_takeover)==4)
        auto_lineup_parked_club_source(g,team,100);
    else if (team < kLeagueClubs || team == kNoTeam) auto_lineup(g, team, 100);
}

void lineup_after_transfer(GameState& g, int team) { lineup_after_transfer_impl(g,team,false); }

// ReleasePlayer closes the seller's squad and native human board gap.
static bool remove_from_seller_impl(GameState& g, int seller, int player, MinuteReport& report,
                                    bool native_takeover) {
    player &= 0xFFF;
    if (seller == 0xFFF || seller == 0xFFFF) {
        for (size_t s = kPoolSlots; s < runtime_dimensions.national_slots_first; s++)  // CareerRuntimeField .. LargeSquadClubCount
            if ((g.squad_slots[s] & 0xFFF) == player) g.squad_slots[s] = 0xFFF;
        return true;
    }
    const SquadRange range = squad_range(seller);
    size_t at = range.first;
    while (at < range.first + range.count && (g.squad_slots[at] & 0xFFF) != player) at++;
    if (at == range.first + range.count) {
        report.fail("transfer: the player is not in the seller's squad (internal error 0x21)");
        return false;
    }
    for (size_t s = kPoolSlots; s < runtime_dimensions.free_first; s++)  // CareerRuntimeField .. CareerRuntimeField2
        if ((g.squad_slots[s] & 0xFFF) == player) g.squad_slots[s] = 0xFFF;
    for (size_t s = at; s + 1 < range.first + range.count; s++) g.squad_slots[s] = g.squad_slots[s + 1];
    g.squad_slots[range.first + range.count - 1] = 0xFFF;
    // a human club's tactics board rows (0x58 bytes each) move up with the squad; the last
    // row keeps its old content (a copy of the row above), as in the original
    const int manager = pending_native_manager_of(g, seller, native_takeover);
    const size_t row = at - range.first;
    if (manager >= 0 && row != 0x17) {
        const size_t base = size_t(manager) * 0x840;
        std::copy(g.tactics.begin() + long(base + (row + 1) * 0x58), g.tactics.begin() + long(base + 0x18 * 0x58),
                  g.tactics.begin() + long(base + row * 0x58));
    }
    lineup_after_transfer_impl(g, seller, native_takeover);
    return true;
}

bool remove_from_seller(GameState& g, int seller, int player, MinuteReport& report) {
    return remove_from_seller_impl(g,seller,player,report,false);
}

namespace {

// Fail(seller, pool slot, buyer, list entry, bid): the player moves to a computer
// club (first free squad slot); a loan is recorded in Career_RemoveJoinLoans_RuntimeField / ClubPlayerDetail_InvalidArgument_RuntimeField.
void move_player(GameState& g, int seller, size_t pool_slot, int buyer, uint32_t list, int bid, WatcomRandom& rand,
                 MinuteReport& report, TransferAllocatorContext allocator, bool native_takeover = false) {
    // The ordinary accepted human-buyer loan route is settled by the shared helper in
    // execute_transfer. Keep this fallback guarded for legacy callers without that owner.
    (void)allocator;
    StaticMemory& mem = g.memory;
    const int player = g.squad_slots.at(pool_slot) & 0xFFF;
    for (int k = 0; k < 200; k++) {
        const uint32_t e = mem.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + uint32_t(k) * 4);
        if (e != 0 && int(e & 0xFFF) == player) {
            report.fail("transfer: the player is already bought for later (internal error 0x30)");
            return;
        }
    }
    if (player == 0xFFF) {
        report.fail("transfer: no player (internal error 0x31)");
        return;
    }
    const uint8_t term = mem.u8(list + 0x34 + uint32_t(bid));
    const bool loan = term > 0x7F;
    const int buyer_manager = pending_native_manager_of(g, buyer, native_takeover);
    const int seller_manager = pending_native_manager_of(g, seller, native_takeover);
    // Deliberate Original bug fix (#105): record4 parks club manager0 only.
    // Fail uses DeskActions_Unsupported_RuntimeField[4], corrupting graphics pointer bbccc, and saves
    // snapshot owner4 which cannot match the club after NationalCoachHandBack handback.
    // Preserve the actual board4 below, but persist its real club owner0 in
    // the existing four-byte counter/snapshot format. Never invent counter4.
    const int loan_owner_manager = native_takeover && seller_manager == 4 ? 0 : seller_manager;
    int loan_slot = -1;
    int owner_snapshot_slot = -1;
    int owner_squad_row = -1;
    std::array<uint8_t, 0x58> owner_board_snapshot{};
    if (loan) {
        if (buyer_manager >= 0) {
            report.fail("transfer: loans to a human manager are not supported yet");
            return;
        }
        // EntryLoanLedgerMustHaveSlotBefore's 200-entry loan ledger must have a slot before any squad/account
        // mutation. Keep the existing AI-only path's layout and draw order unchanged.
        for (int k = 0; k < 200; ++k) {
            if (mem.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + uint32_t(k) * 4) == 0) { loan_slot = k; break; }
        }
        if (loan_slot < 0) {
            report.fail("transfer: loan table full (internal error 0x36)");
            return;
        }
        if (seller_manager >= 0) {
            // Fail allows a human owner to lend to a computer club only while
            // below the three-loan limit and with a free Source snapshot record.
            if (loan_owner_manager >= 4 || !mem.has(::bmh::layout::DeskActions_Unsupported_RuntimeField + uint32_t(loan_owner_manager)) ||
                mem.u8(::bmh::layout::DeskActions_Unsupported_RuntimeField + uint32_t(loan_owner_manager)) >= 3 ||
                !mem.has(::bmh::layout::Career_CareerManagersEnd_RuntimeField + 0x17) || !mem.has(::bmh::layout::ClubInfo_FormatError_RuntimeField + 0x41f) ||
                !mem.has(::bmh::layout::ClubInfo_FormatError_RuntimeField2 + 0x0b)) {
                report.fail("transfer: no available human-owner loan record");
                return;
            }
            for (int k = 0; k < 12; ++k) {
                if (mem.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField + uint32_t(k) * 2) == uint16_t(player)) {
                    report.fail("transfer: player already has a human-owner loan record");
                    return;
                }
                if (owner_snapshot_slot < 0 && mem.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField + uint32_t(k) * 2) == 0x0fff)
                    owner_snapshot_slot = k;
            }
            if (owner_snapshot_slot < 0) {
                report.fail("transfer: human-owner loan snapshots full (internal error 0x34)");
                return;
            }
            const SquadRange owner_range = squad_range(seller);
            for (size_t row = 0; row < owner_range.count; ++row) {
                if ((g.squad_slots.at(owner_range.first + row) & 0x0fff) == player) {
                    owner_squad_row = int(row);
                    break;
                }
            }
            const size_t board = size_t(seller_manager) * 0x840 +
                                 size_t(std::max(owner_squad_row, 0)) * 0x58;
            if (owner_squad_row < 0 || board + owner_board_snapshot.size() > g.tactics.size()) {
                report.fail("transfer: human-owner loan player has no board row");
                return;
            }
            std::copy_n(g.tactics.begin() + std::ptrdiff_t(board), owner_board_snapshot.size(),
                        owner_board_snapshot.begin());
        }
    }
    // FirstFreeSlotSquad: first free slot of the buyer's squad
    const SquadRange range = squad_range(buyer);
    size_t slot = range.first;
    while (slot < range.first + range.count && g.squad_slots[slot] != 0xFFF) slot++;
    if (slot == range.first + range.count) return;  // squad full (message box PendingOffer_FormatError_ExecutableField)
    g.squad_slots[slot] = uint16_t(player);
    if (buyer_manager >= 0) {
        const size_t row = slot - range.first;
        new_board_entry(g, buyer_manager, int(row), int(slot), rand);
        const size_t board = size_t(buyer_manager) * 0x840 + row * 0x58;
        const uint32_t salary = mem.u32(list + 0x0c + uint32_t(bid) * 4);
        for (int b = 0; b < 4; ++b) g.tactics.at(board + 0x50 + size_t(b)) = uint8_t(salary >> (b * 8));
        g.tactics.at(board + 0x0c) = uint8_t((term & 0x7f) == 0 ? 0x18 : (term & 0x7f));
        // Fail (human buyer): the market salary for the term (AddToSquad at 100 %,
        // the term in months as its year count, as the original passes it) ...
        uint32_t reference_fee = 0, reference_salary = 0;
        market_value(g, int(pool_slot), 0, 100, reference_fee, reference_salary, int(term & 0x7f), 0, rand);
        for (int b = 0; b < 7; ++b) g.tactics.at(board + 0x12 + size_t(b)) = mem.u8(list + 0x38 + uint32_t(bid) * 7 + uint32_t(b));
        // ... and his mood (+0x0d): what the contract is worth against it, salary plus 80 % of
        // the expected bonus (ContractOffer), 0-255 (ClampInteger). The port left it out, so a
        // player bought this way kept the 0 of the new board row and asked for nothing at his
        // renewal (#40: HENDERSON 60.900 -> 0, later 8.580). +0x54 is the price paid.
        const uint32_t bonus = expected_bonus(g, buyer_manager, 0xff, int(row), player, true);
        const int32_t worth = int32_t(salary) + int32_t(bonus * 0x50u) / 100;
        const int32_t ratio = reference_salary == 0 ? 0xff : int32_t(uint32_t(worth) * 100u / reference_salary);
        g.tactics.at(board + 0x0d) = uint8_t(std::clamp(ratio, 0, 0xff));
        const uint32_t price = mem.u32(list + 0x1c + uint32_t(bid) * 4);
        for (int b = 0; b < 4; ++b) g.tactics.at(board + 0x54 + size_t(b)) = uint8_t(price >> (b * 8));
        if (const char* trace = std::getenv("BMH_MARKET_TRACE"); trace && trace[0] == '1')
            std::fprintf(stderr, "[move_player mood player %d: salary %u bonus %u reference %u -> %d]\n", player, salary, bonus,
                         reference_salary, int(g.tactics.at(board + 0x0d)));
        book_money(g, buyer_manager, int32_t(mem.u32(list + 0x1c + uint32_t(bid) * 4)), true, true);
    }
    if (!remove_from_seller_impl(g, seller, player, report, native_takeover)) return;
    lineup_after_transfer_impl(g, seller, native_takeover);
    lineup_after_transfer_impl(g, buyer, native_takeover);
    if (loan) {
        // Fail stores the owner's original manager-board row before loaning
        // a human-owned player to an AI club. Snapshot bytes and manager mapping stay
        // intact after return; Source only frees the player-ID slot and decrements count.
        if (seller_manager >= 0) {
            const uint32_t snapshot = ::bmh::layout::ClubInfo_FormatError_RuntimeField + uint32_t(owner_snapshot_slot) * 0x58;
            for (size_t b = 0; b < owner_board_snapshot.size(); ++b)
                mem.set8(snapshot + uint32_t(b), owner_board_snapshot[b]);
            mem.set16(::bmh::layout::Career_CareerManagersEnd_RuntimeField + uint32_t(owner_snapshot_slot) * 2, uint16_t(player));
            mem.set8(::bmh::layout::ClubInfo_FormatError_RuntimeField2 + uint32_t(owner_snapshot_slot), uint8_t(loan_owner_manager));
            mem.set8(::bmh::layout::DeskActions_Unsupported_RuntimeField + uint32_t(loan_owner_manager), uint8_t(mem.u8(::bmh::layout::DeskActions_Unsupported_RuntimeField + uint32_t(loan_owner_manager)) + 1));
        }
        // EntryLoanLedgerMustHaveSlotBefore: the player returns to the seller after `term & 0x7f` months.
        mem.set8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField + uint32_t(loan_slot), uint8_t(term & 0x7F));
        mem.set32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + uint32_t(loan_slot) * 4, uint32_t(player + seller * 0x1000 + buyer * 0x400000));
    }
}

// Validate the extra state needed by Fail before execute_transfer emits news or
// books the human owner's fee. move_player repeats the checks immediately before its
// mutations; the caller invokes this first so an unavailable Source loan record cannot
// produce a half-completed transfer.
bool preflight_human_owner_loan(const GameState& g, int seller, size_t pool_slot, int buyer,
                               MinuteReport& report, bool require_squad_room = true, bool native_takeover = false) {
    const int owner_manager = pending_native_manager_of(g, seller, native_takeover);
    // Match move_player's explicit #105 correction: board4 belongs to the
    // persistent club manager0; bbccc is a graphics pointer, never a counter.
    const int loan_owner_manager = native_takeover && owner_manager == 4 ? 0 : owner_manager;
    const auto& mem = g.memory;
    const int player = g.squad_slots.at(pool_slot) & 0x0fff;
    if (seller == 0x0fff) {
        report.fail("transfer: Source free-agent loan return destination is not represented");
        return false;
    }
    if (buyer < 0 || buyer >= kLeagueClubs) {
        report.fail("transfer: loan buyer has no squad");
        return false;
    }
    if (!mem.has(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + 199u * 4u + 3u)) {
        report.fail("transfer: loan ledger storage is truncated");
        return false;
    }
    if (owner_manager >= (native_takeover ? 5 : 4) || (owner_manager >= 0 && (!mem.has(::bmh::layout::DeskActions_Unsupported_RuntimeField + uint32_t(loan_owner_manager)) ||
        mem.u8(::bmh::layout::DeskActions_Unsupported_RuntimeField + uint32_t(loan_owner_manager)) >= 3 || !mem.has(::bmh::layout::Career_CareerManagersEnd_RuntimeField + 0x17) ||
        !mem.has(::bmh::layout::ClubInfo_FormatError_RuntimeField + 0x41f) || !mem.has(::bmh::layout::ClubInfo_FormatError_RuntimeField2 + 0x0b)))) {
        report.fail("transfer: no available human-owner loan record");
        return false;
    }
    int snapshot_slot = -1;
    int loan_slot = -1;
    for (int i = 0; i < 200; ++i) {
        const uint32_t e = mem.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + uint32_t(i) * 4);
        if (e == 0 && loan_slot < 0) loan_slot = i;
        else if (e != 0 && int(e & 0x0fff) == player) {
            report.fail("transfer: the player is already bought for later (internal error 0x30)");
            return false;
        }
    }
    if (loan_slot < 0) {
        report.fail("transfer: loan table full (internal error 0x36)");
        return false;
    }
    if (owner_manager >= 0) for (int i = 0; i < 12; ++i) {
        const uint16_t id = mem.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField + uint32_t(i) * 2);
        if (id == uint16_t(player)) {
            report.fail("transfer: player already has a human-owner loan record");
            return false;
        }
        if (snapshot_slot < 0 && id == 0x0fff) snapshot_slot = i;
    }
    if (owner_manager >= 0 && snapshot_slot < 0) {
        report.fail("transfer: human-owner loan snapshots full (internal error 0x34)");
        return false;
    }
    const SquadRange seller_range = squad_range(seller);
    bool owner_has_player = false;
    for (size_t i = 0; i < seller_range.count; ++i)
        if ((g.squad_slots.at(seller_range.first + i) & 0x0fff) == player) owner_has_player = true;
    if (!owner_has_player) {
        report.fail("transfer: the player is not in the seller's squad (internal error 0x21)");
        return false;
    }
    const size_t board = owner_manager >= 0 ? size_t(owner_manager) * 0x840 : 0;
    if (owner_manager >= 0 && (g.tactics.size() < board + 0x18 * 0x58 || !seller_range.count)) {
        report.fail("transfer: human-owner loan player has no board row");
        return false;
    }
    const SquadRange buyer_range = squad_range(buyer);
    bool room = false;
    for (size_t i = 0; i < buyer_range.count; ++i)
        if (g.squad_slots.at(buyer_range.first + i) == 0x0fff) { room = true; break; }
    if (require_squad_room && !room) {
        report.fail("transfer: buyer squad is full");
        return false;
    }
    return true;
}

// The weekly transfer itself (Void2): news for the interested human
// managers (all of them with the option Weekly_ExecuteTransfer_RuntimeField off), then the move.
void execute_transfer(GameState& g, int k, uint32_t list, int bid, WatcomRandom& rand, MinuteReport& report,
                      TransferAllocatorContext allocator) {
    StaticMemory& mem = g.memory;
    const int seller = mem.u16(list + 8) & 0x7FFF;  // Canonical free-player seller is 0x0fff; FFFF is not its alias.
    const int buyer = int16_t(mem.u16(list + uint32_t(bid) * 2));
    const uint8_t term = mem.u8(list + 0x34 + uint32_t(bid));
    const int buyer_manager = g.manager_of(buyer);
    // SettleHumanBuyerSale books fees for human managers through the shared settlement.
    uint32_t mask = 0x80000000u;
    if (!(mem.has(::bmh::layout::Weekly_ExecuteTransfer_RuntimeField) && mem.u8(::bmh::layout::Weekly_ExecuteTransfer_RuntimeField) != 0)) mask = 0xFFFFFFFFu;
    const uint32_t interested = interested_managers(g, seller, buyer);
    const size_t pool_slot = kPoolSlots + size_t(k);
    if (term > 0x7f &&
        !preflight_human_owner_loan(g, seller, pool_slot, buyer, report, buyer_manager < 0)) return;
    // SettleHumanBuyerSale/Fail owns ordinary purchases and loans for human 24-slot buyers.
    // Require the explicit runtime allocator observation for either contract;
    // AI buyers retain the original legacy path.
    if (buyer_manager >= 0) {
        HumanBuyerSaleHooks hooks;
        switch (allocator) {
        case TransferAllocatorContext::zero: hooks.allocator_gate = HumanBuyerSaleAllocatorGate::zero; break;
        case TransferAllocatorContext::blocked: hooks.allocator_gate = HumanBuyerSaleAllocatorGate::blocked; break;
        default:
            report.fail("PendingOfferAnswer: general weekly human-buyer sale has no observed allocator gate");
            return;
        }
        hooks.direct_mode = HumanBuyerSaleDirectMode::ordinary_ai_purchase;
        hooks.notice = [&](const GameState&, const HumanBuyerSaleNotice& notice) {
            post_composed(g, notice.manager, notice.original_message);
        };
        hooks.after_paid_accounts = [&](GameState& state, const HumanBuyerSalePaid& paid) {
            uint32_t message_mask = 0x80000000u;
            if (paid.seller_manager >= 0) message_mask |= 1u << paid.seller_manager;
            if (!(state.memory.has(::bmh::layout::Weekly_ExecuteTransfer_RuntimeField) && state.memory.u8(::bmh::layout::Weekly_ExecuteTransfer_RuntimeField) != 0))
                message_mask = 0xffffffffu;
            else
                message_mask |= 1u << paid.buyer_manager;
            const uint32_t interested = interested_managers(state, paid.seller, paid.buyer);
            if ((message_mask | interested) == 0x80000000u) return;
            const std::string message = message_text(state, ::bmh::layout::Month_SearchProfileNews_TextPointer) + state.player_name(paid.player) +
                message_text(state, ::bmh::layout::Weekly_ExecuteTransfer_TextPointer) + state.club_name(paid.seller) +
                message_text(state, ::bmh::layout::Weekly_ExecuteTransfer_TextPointer2) + format_number(state, int32_t(paid.raw_fee), 0x101) +
                message_text(state, ::bmh::layout::ClubPlayerDetail_RenderCard_TextPointer7) + state.club_name(paid.buyer) +
                message_text(state, (paid.term & 0x80) ? ::bmh::layout::Weekly_ExecuteTransfer_TextPointer3 : ::bmh::layout::Weekly_ExecuteTransfer_TextPointer4);
            post_composed(state, int64_t(message_mask | interested), message);
        };
        HumanBuyerSaleRequest request;
        request.list_entry = k;
        request.bid_slot = bid;
        request.list_address = list;
        request.source_pool_slot = pool_slot;
        request.expected_seller = seller;
        request.expected_buyer = buyer;
        request.expected_active_manager = mem.has(::bmh::layout::ActiveManager) ? int(mem.u8(::bmh::layout::ActiveManager)) : -1;
        request.day_step = true;  // (a bid of manager 1 may be decided on manager 2's turn)
        const auto settled = settle_human_buyer_sale(g, request, hooks, rand, report);
        (void)settled; // Void2 clears the completed list for refused/blocked outcomes too.
        return;
    }
    if ((mask | interested) != 0x80000000u) {
        const int32_t fee = int32_t(mem.u32(list + 0x1c + uint32_t(bid) * 4));
        std::string msg = message_text(g, ::bmh::layout::Month_SearchProfileNews_TextPointer) + g.player_name(g.squad_slots.at(pool_slot)) + message_text(g, ::bmh::layout::Weekly_ExecuteTransfer_TextPointer) +
                          g.club_name(seller) + message_text(g, ::bmh::layout::Weekly_ExecuteTransfer_TextPointer2) + format_number(g, fee, 0x101) +
                          message_text(g, ::bmh::layout::ClubPlayerDetail_RenderCard_TextPointer7) + g.club_name(buyer) +
                          message_text(g, term > 0x7F ? ::bmh::layout::Weekly_ExecuteTransfer_TextPointer3 : ::bmh::layout::Weekly_ExecuteTransfer_TextPointer4);
        post_composed(g, int64_t(mask | interested), msg);
    }
    move_player(g, seller, pool_slot, buyer, list, bid, rand, report, allocator);
    const int seller_manager = g.manager_of(seller);
    if (seller_manager >= 0)
        book_money(g, seller_manager, -int32_t(mem.u32(list + 0x1c + uint32_t(bid) * 4)), true, true);
}

// The bids of an expiring list entry (TransferListCountdown when the countdown reaches 0): every bid
// is rated (fee and salary percentages plus G(0, 10) each, the salary also by the bidder's
// table position), the best one the buyer can afford and has room for gets the player if
// W(90, 110) is below its rating. Returns false when a path is not ported.
bool decide_bids(GameState& g, int k, GameRandom& game, WatcomRandom& rand, MinuteReport& report,
                 TransferAllocatorContext allocator) {
    StaticMemory& mem = g.memory;
    bool preserve_entry = false;
    const uint32_t list = kTransferList + uint32_t(k) * kListEntry;
    const int player = g.squad_slots.at(kPoolSlots + size_t(k)) & 0xFFF;  // local_34
    const int seller = mem.u16(list + 8) & 0x7FFF;
    const int seller_manager = g.manager_of(seller);  // local_24 / local_1c
    int16_t order[4];
    uint32_t score[4];
    int n = 0;
    for (; n < 4; n++) {
        order[n] = int16_t(n);
        if (int16_t(mem.u16(list + uint32_t(n) * 2)) == -1) break;
        uint32_t fee = 0, salary = 0;
        if (!evaluate_bid(g, list, n, fee, salary, rand, report)) return false;
        if (fee == 0 || salary == 0) {
            score[n] = 0;
        } else {
            const int position = int8_t(table_position(g, int16_t(mem.u16(list + uint32_t(n) * 2))));
            salary += uint32_t(game.range(0, 10) + (0x14 - position) / 2);
            fee += uint32_t(game.range(0, 10));
            if (fee > 5) fee -= 5;
            if (salary > 5) salary -= 5;
            score[n] = fee + salary;
        }
    }
    quicksort_by_key(order, score, n);
    int c = 0;
    for (; c < n; c++) {
        const int bid = order[c];
        const int bidder = int16_t(mem.u16(list + uint32_t(bid) * 2));
        const int bidder_manager = g.manager_of(bidder);
        if (bidder_manager >= 0) {
            const int32_t fee = int32_t(mem.u32(list + 0x1c + uint32_t(bid) * 4));
            const size_t rec = size_t(bidder_manager) * 0xb0c;
            const int32_t balance = int32_t(uint32_t(g.manager_records.at(rec + 0x314)) |
                uint32_t(g.manager_records.at(rec + 0x315)) << 8 | uint32_t(g.manager_records.at(rec + 0x316)) << 16 |
                uint32_t(g.manager_records.at(rec + 0x317)) << 24);
            const int32_t credit = int32_t(uint32_t(g.manager_records.at(rec + 0x31c)) |
                uint32_t(g.manager_records.at(rec + 0x31d)) << 8 | uint32_t(g.manager_records.at(rec + 0x31e)) << 16 |
                uint32_t(g.manager_records.at(rec + 0x31f)) << 24);
            if (int64_t(balance) + credit < fee) {
                // the board forbids further talks for financial reasons (Weekly_DecideBids_TextPointer / Weekly_DecideBids_TextPointer2)
                post_composed(g, bidder_manager, message_text(g, ::bmh::layout::Weekly_DecideBids_TextPointer) + g.player_name(player) + message_text(g, ::bmh::layout::Weekly_DecideBids_TextPointer2));
                continue;
            }
        }
        const SquadRange range = squad_range(bidder);
        const int nation = team_region(bidder) & 0xFF;  // Foreigners
        const int others = foreigners(g, bidder, range.first, range.count, nation);
        // The foreigner limit of a computer seller's list (more than three foreigners in the
        // bidder's squad, a player of another nation) only stops computer bidders: a human
        // bidder goes on to the squad check (TransferListCountdown, `local_28 != 0xff`).
        if (seller_manager < 0 && others > 3 && (nationality(g, player) & 0xFFFF) != nation && bidder_manager < 0) continue;
        if (squad_count(g, bidder, 3) < int(range.count) - 1) break;  // room in the squad
        // a full squad: a human bidder hears that the board approves no more transfers (DeskActions_Unsupported_ExecutableField)
        if (bidder_manager >= 0) post_text(g, bidder_manager, text_id(::bmh::layout::DeskActions_Unsupported_ExecutableField));
    }
    if (c == n) {
        // TransferListCountdown sends the expiry notice to the seller and every human bidder.
        uint32_t audience = 0x80000000u;
        if (seller_manager >= 0) audience |= 1u << seller_manager;
        for (int i = 0; i < n; ++i) {
            const int manager = g.manager_of(int16_t(mem.u16(list + uint32_t(i) * 2)));
            if (manager >= 0) audience |= 1u << manager;
        }
        if (audience != 0x80000000u) {
            const std::string msg = message_text(g, ::bmh::layout::Month_SearchProfileNews_TextPointer) + g.player_name(player) + message_text(g, ::bmh::layout::Weekly_DecideBids_TextPointer3);
            post_composed(g, int64_t(audience), msg);
        }
    } else {
        const int bid = order[c];
        const int bidder = int16_t(mem.u16(list + uint32_t(bid) * 2));
        const uint32_t r = uint32_t(rand.range(0x5a, 0x6e));
        bool accept = r < score[c];
        if (seller_manager >= 0) {
            if (g.transfer_offer_selector) {
                TransferOfferListView view = project_transfer_offer_list(g, k);
                if (allocator == TransferAllocatorContext::zero || allocator == TransferAllocatorContext::blocked)
                    for (auto& offer : view.offers)
                        if (offer.present && offer.loan && offer.buyer_manager >= 0)
                            offer.transaction_supported = true;
                view.preferred_bid_index = bid;
                view.preferred_bid_passed_random_check = accept;
                auto refresh_quote = [&]() {
                    const SquadRange seller_range = squad_range(seller);
                    size_t seller_slot = seller_range.first;
                    while (seller_slot < seller_range.first + seller_range.count &&
                           (g.squad_slots.at(seller_slot) & 0x0fff) != player) ++seller_slot;
                    if (seller_slot >= seller_range.first + seller_range.count || seller_manager < 0 ||
                        size_t(seller_manager) * kBoard + (seller_slot - seller_range.first) * kBoardSlot + 0x0c >=
                            g.tactics.size()) return false;
                    const size_t board = size_t(seller_manager) * kBoard +
                                         (seller_slot - seller_range.first) * kBoardSlot;
                    const int months = g.tactics[board + 0x0c];
                    int years = months > 0 ? (months - 1) / 12 + 1 : 1;
                    if (years > 4) years = 4;
                    view.quoted_years = uint8_t(years);
                    const int percent = game.range(0x5c, 0x6c); // OpenTransferEditor: GameRandom
                    view.quoted_fee = 0;
                    view.quoted_monthly_salary = 0;
                    market_value(g, int(kPoolSlots + size_t(k)), 0, percent,
                                 view.quoted_fee, view.quoted_monthly_salary, years, 0, rand);
                    // OpenTransferEditor passes the seller's own bid slot to RateTransferBidSource
                    // when one exists, otherwise -1 for the generic asking reference.
                    int own_bid = -1;
                    for (int row = 0; row < 4; ++row) {
                        const int buyer_id = int16_t(mem.u16(list + uint32_t(row) * 2));
                        if (buyer_id == -1) break;
                        if (buyer_id == seller) { own_bid = row; break; }
                    }
                    offer_bases(mem, list, own_bid, view.asking_fee, view.asking_monthly_salary);
                    return true;
                };
                if (!refresh_quote()) {
                    report.fail("transfer offer: seller player has no source quote row");
                    return false;
                }
                int malformed_responses = 0;
                while (view.offer_count > 0) {
                    const int selected = g.transfer_offer_selector(g, view);
                    if (selected == -1) {
                        accept = false;
                        if (g.transfer_decision_observer && !g.transfer_notice_suppressed) {
                            TransferDecisionEvent event;
                            event.kind = TransferDecisionEventKind::confirmation;
                            event.offers = view;
                            event.selected_bid_index = -1;
                            event.accepted = false;
                            event.viewer_manager = seller_manager;
                            event.result_text_address = ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField; // IncomingTransferScreen explicit right-click decline
                            g.transfer_decision_observer(event);
                        }
                        break;
                    }
                    if (selected < 0 || selected >= 4 || !view.offers[size_t(selected)].present) {
                        // The original mouse loop rejects an empty row without a game
                        // action. The current port cannot complete a human-involved loan
                        // lifecycle; keep that row unavailable rather than enter a known
                        // failing transfer/return path. Bound only malformed replies.
                        view.notice_text_address = 0;
                        if (++malformed_responses >= 8) {
                            preserve_entry = true;
                            break;
                        }
                        continue;
                    }
                    const TransferOfferCandidateView& chosen = view.offers[size_t(selected)];
                    if (chosen.loan && !chosen.loan_permitted) {
                        // IncomingTransferScreen refuses a loan when DeskActions_Unsupported_RuntimeField[seller] is >= 3,
                        // displays PendingOffer_PendingOfferNoticeDialog_ExecutableField2, and returns to the same choice screen.
                        view.rejected_bid_index = selected;
                        view.notice_text_address = ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField2;
                        if (!refresh_quote()) return false;
                        continue;
                    }
                    if (chosen.loan && chosen.buyer_manager >= 0 &&
                        (allocator == TransferAllocatorContext::zero ||
                         allocator == TransferAllocatorContext::blocked))
                        view.offers[size_t(selected)].transaction_supported = true;
                    if (!view.offers[size_t(selected)].transaction_supported) {
                        // A human-involved loan passes the original DeskActions_Unsupported_RuntimeField limit check,
                        // but this port lacks the board-snapshot/return path. Refuse it
                        // inertly instead of reaching move_player's known report.fail.
                        if (++malformed_responses >= 8) {
                            preserve_entry = true;
                            break;
                        }
                        continue;
                    }
                    if (!chosen.affordable) {
                        // OpenTransferEditor shows DeskActions_SubmitTransferOfferEditorSource_RuntimeField and lets the seller choose again.
                        view.rejected_bid_index = selected;
                        view.notice_text_address = ::bmh::layout::DeskActions_SubmitTransferOfferEditorSource_RuntimeField;
                        if (!refresh_quote()) return false;
                        continue;
                    }
                    if (!chosen.has_squad_room || !chosen.passes_foreign_limit) {
                        // Fail reports PendingOffer_FormatError_ExecutableField when the buyer has no free
                        // place. The foreign-player guard is only reached for a free-agent
                        // seller; retain it as a defensive boundary here.
                        view.rejected_bid_index = selected;
                        view.notice_text_address = ::bmh::layout::PendingOffer_FormatError_ExecutableField;
                        if (!refresh_quote()) return false;
                        continue;
                    }
                    const uint32_t salary_at = list + 0x0c + uint32_t(selected) * 4;
                    const uint32_t salary_before = mem.u32(salary_at);
                    mem.set32(salary_at, (salary_before * 10u) >> 3); // temporary 125% for ProjectTransferOfferList
                    uint32_t fee_pct = 0, salary_pct = 0;
                    const bool rated = evaluate_bid(g, list, selected, fee_pct, salary_pct, rand, report);
                    mem.set32(salary_at, salary_before); // Source restores +0x0c before any outcome
                    if (!rated || report.unsupported) return false;
                    if ((seller_manager >= 0 && salary_pct != 0) ||
                        (seller_manager < 0 && fee_pct != 0 && salary_pct != 0)) {
                        const bool moved = accept_transfer_bid_immediately(g, k, selected, rand, report, allocator);
                        if (!moved && !report.unsupported)
                            report.fail("transfer offer: selected bid could not be completed");
                        if (report.unsupported) return false;
                        if (moved && seller_manager >= 0 && g.transfer_decision_observer) {
                            bool buyer_has_player = false;
                            const SquadRange buyer_range = squad_range(chosen.buyer);
                            for (size_t row = buyer_range.first; row < buyer_range.first + buyer_range.count; ++row) {
                                if ((g.squad_slots.at(row) & 0x0fff) == player) {
                                    buyer_has_player = true;
                                    break;
                                }
                            }
                            if (buyer_has_player) {
                                TransferDecisionEvent event;
                                event.kind = TransferDecisionEventKind::confirmation;
                                event.offers = view; // owned snapshot before helper clears the offer rows
                                event.selected_bid_index = selected;
                                event.accepted = true;
                                event.viewer_manager = seller_manager;
                                event.result_text_address = ::bmh::layout::PendingOffer_FormatError_ExecutableField2; // Void2 prefers the human seller
                                g.transfer_decision_observer(event);
                            }
                        }
                        accept = false; // immediate helper has already settled the list/player
                        break;
                    }
                    // IncomingTransferScreen clears just the rejected bidder slot and retries the
                    // same selector while other rows remain.
                    mem.set16(list + uint32_t(selected) * 2, 0xffff);
                    view.offers[size_t(selected)].present = false;
                    view.offers[size_t(selected)].eligible = false;
                    if (view.preferred_bid_index == selected) {
                        view.preferred_bid_index = -1;
                        view.preferred_bid_passed_random_check = false;
                    }
                    --view.offer_count;
                    view.rejected_bid_index = selected;
                    view.notice_text_address = ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField3;
                    if (view.offer_count <= 0) {
                        // IncomingTransferScreen displays the final refusal, then returns when
                        // param_9 reaches zero. It does not redraw OpenTransferEditor.
                        view.preferred_bid_index = -1;
                        view.preferred_bid_passed_random_check = false;
                        (void)g.transfer_offer_selector(g, view); // terminal notice; response ignored
                        accept = false;
                        break;
                    }
                    if (!refresh_quote()) return false;
                }
                if (view.offer_count <= 0 || malformed_responses >= 8) accept = false;
                // A successful selected-bid transfer was finalized above; prevent the
                // legacy top-bid path from executing a second transfer.
                if (accept) execute_transfer(g, k, list, bid, rand, report, allocator);
            } else {
                accept = g.transfer_decision && g.transfer_decision(g, seller_manager, player, bidder,
                    int32_t(mem.u32(list + 0x1c + uint32_t(bid) * 4)),
                    int32_t(mem.u32(list + 0x0c + uint32_t(bid) * 4)));
                if (accept) execute_transfer(g, k, list, order[c], rand, report, allocator);
            }
        } else {
            // Original IncomingTransferScreen enters the incoming-offer presentation when
            // at least one raw bidder is human, even though the seller is AI.
            // OpenTransferEditor computes its one-year reference quote before the
            // accept/reject animation. Compute it on both observed and silent
            // runs so installing a read-only observer does not alter RNG order.
            TransferOfferListView decision_view;
            int viewer_manager = -1;
            bool human_bidder_present = false;
            for (int raw = 0; raw < 4; ++raw) {
                const int raw_buyer = int16_t(mem.u16(list + uint32_t(raw) * 2));
                if (raw_buyer == -1) break;
                const int manager = g.manager_of(raw_buyer);
                if (manager >= 0) {
                    human_bidder_present = true;
                    if (viewer_manager < 0) viewer_manager = manager;
                }
            }
            if (human_bidder_present) {
                decision_view = project_transfer_offer_list(g, k);
                decision_view.preferred_bid_index = bid;
                decision_view.preferred_bid_passed_random_check = accept;
                decision_view.quoted_years = 1; // OpenTransferEditor uses one year for a computer seller
                const int quote_percent = game.range(0x5c, 0x6c); // GameRandom in OpenTransferEditor
                market_value(g, int(kPoolSlots + size_t(k)), 0, quote_percent,
                             decision_view.quoted_fee, decision_view.quoted_monthly_salary, 1, 0, rand);
                offer_bases(mem, list, -1, decision_view.asking_fee,
                            decision_view.asking_monthly_salary); // RateTransferBidSource(..., -1)
                if (mem.has(::bmh::layout::ActiveManager)) viewer_manager = int(mem.u8(::bmh::layout::ActiveManager)); // ActiveManager
                if (g.transfer_decision_observer) {
                    TransferDecisionEvent event;
                    event.kind = TransferDecisionEventKind::evaluation;
                    event.offers = decision_view;
                    event.selected_bid_index = bid;
                    event.accepted = accept;
                    event.viewer_manager = viewer_manager;
                    event.result_text_address = accept ? ::bmh::layout::IncomingTransfer_RenderAtElapsed_TextPointer3 : ::bmh::layout::IncomingTransfer_RenderAtElapsed_TextPointer4;
                    g.transfer_decision_observer(event);
                }
            }
            if (accept) {
                execute_transfer(g, k, list, order[c], rand, report, allocator);
                if (human_bidder_present && !report.unsupported &&
                    g.manager_of(bidder) >= 0 && g.transfer_decision_observer) {
                    bool buyer_has_player = false;
                    const SquadRange buyer_range = squad_range(bidder);
                    for (size_t row = buyer_range.first; row < buyer_range.first + buyer_range.count; ++row) {
                        if ((g.squad_slots.at(row) & 0x0fff) == player) {
                            buyer_has_player = true;
                            break;
                        }
                    }
                    if (buyer_has_player) {
                        TransferDecisionEvent event;
                        event.kind = TransferDecisionEventKind::confirmation;
                        event.offers = decision_view;
                        event.selected_bid_index = bid;
                        event.accepted = true;
                        event.viewer_manager = g.manager_of(bidder);
                        event.result_text_address = ::bmh::layout::PendingOffer_FormatError_ExecutableField2; // Void2
                        g.transfer_decision_observer(event);
                    }
                }
            }
        }
        if (report.unsupported) return false;
    }
    if (preserve_entry) return true;
    for (int b = 0; b < 4; b++) mem.set16(list + uint32_t(b) * 2, 0xffff);
    g.squad_slots.at(kPoolSlots + size_t(k)) = 0x0FFF;
    return true;
}

}  // namespace

static TransferOfferListView project_transfer_offer_list_impl(const GameState& g, int entry, bool pending) {
    TransferOfferListView view;
    if (entry < 0 || entry >= (pending ? 44 : 40) || (pending && entry < 40)) return view;
    const uint32_t list = kTransferList + uint32_t(entry) * kListEntry;
    if (!g.memory.has(list + kListEntry - 1) ||
        kPoolSlots + size_t(entry) >= g.squad_slots.size()) return view;
    const int player = g.squad_slots[kPoolSlots + size_t(entry)] & 0x0fff;
    if (player == 0x0fff || size_t(player) >= g.players.size()) return view;

    view.valid = true;
    view.list_entry = entry;
    view.player = player;
    view.seller = g.memory.u16(list + 8) & 0x7fff;
    view.seller_manager = g.manager_of(view.seller);
    for (int bid = 0; bid < 4; ++bid) view.offers[size_t(bid)].bid_index = bid;
    if (view.seller_manager >= 0 && g.memory.has(::bmh::layout::DeskActions_Unsupported_RuntimeField + uint32_t(view.seller_manager)))
        view.seller_loan_count = g.memory.u8(::bmh::layout::DeskActions_Unsupported_RuntimeField + uint32_t(view.seller_manager));
    for (int bid = 0; bid < 4; ++bid) {
        const int buyer = int16_t(g.memory.u16(list + uint32_t(bid) * 2));
        // TransferListCountdown stops at the first empty slot; later bytes are not offers.
        if (buyer == -1) break;
        TransferOfferCandidateView& offer = view.offers[size_t(bid)];
        offer.present = true;
        offer.buyer = buyer;
        offer.buyer_manager = g.manager_of(buyer);
        offer.fee = g.memory.u32(list + 0x1c + uint32_t(bid) * 4);
        offer.monthly_salary = g.memory.u32(list + 0x0c + uint32_t(bid) * 4);
        const uint8_t term = g.memory.u8(list + 0x34 + uint32_t(bid));
        offer.contract_months = term & 0x7f;
        offer.loan = (term & 0x80) != 0;
        offer_bases(g.memory, list, bid, offer.fee_reference, offer.salary_reference);
        for (int bonus = 0; bonus < 7; ++bonus)
            offer.bonuses[size_t(bonus)] = g.memory.u8(list + 0x38 + uint32_t(bid) * 7 + uint32_t(bonus));

        // These are the non-random preselection guards in TransferListCountdown. The
        // projection deliberately leaves ranking and the later ProjectTransferOfferList
        // salary/fee recheck to the eventual synchronous selector.
        offer.affordable = true;
        if (offer.buyer_manager >= 0) {
            const size_t rec = size_t(offer.buyer_manager) * kRecord;
            if (rec + 0x31f >= g.manager_records.size()) {
                offer.affordable = false;
            } else {
                const auto i32 = [&](size_t at) {
                    return int32_t(uint32_t(g.manager_records[at]) |
                                   uint32_t(g.manager_records[at + 1]) << 8 |
                                   uint32_t(g.manager_records[at + 2]) << 16 |
                                   uint32_t(g.manager_records[at + 3]) << 24);
                };
                offer.affordable = int64_t(i32(rec + 0x314)) + i32(rec + 0x31c) >= offer.fee;
            }
        }
        // Only club IDs have a squad range in the source transfer path. Keep
        // malformed IDs visible for diagnosis but never use them as indexes.
        const SquadRange range = buyer >= 0 && buyer < kLeagueClubs ? squad_range(buyer) : SquadRange{};
        offer.has_squad_room = range.count != 0 && squad_count(g, buyer, 3) < int(range.count) - 1;
        offer.passes_foreign_limit = range.count != 0;
        if (view.seller_manager < 0 && range.count != 0) {
            const int nation = team_region(buyer) & 0xff;
            const int others = foreigners(g, buyer, range.first, range.count, nation);
            if (others > 3 && (nationality(g, player) & 0xffff) != nation)
                offer.passes_foreign_limit = false;
        }
        if (offer.loan) offer.loan_permitted = view.seller_loan_count >= 0 && view.seller_loan_count < 3;
        // The human-owner -> AI-borrower route has its Source snapshot and return path;
        // loans to a human borrower still require the separate Source consent flow.
        // The generic weekly expiry/selector path has no owner for the explicit
        // PendingOfferAnswer runtime allocator observation. The special DispatchClubPlayerPendingOffers pending
        // caller opts in below, where its typed hooks own that observation.
        offer.transaction_supported = !(offer.loan && offer.buyer_manager >= 0);
        offer.eligible = offer.affordable && offer.has_squad_room && offer.passes_foreign_limit;
        ++view.offer_count;
    }
    return view;
}

TransferOfferListView project_transfer_offer_list(const GameState& g, int entry) {
    return project_transfer_offer_list_impl(g, entry, false);
}

namespace {

void pending_notice(const GameState& g, const PendingOfferHooks& hooks, int manager,
                    int raw, uint32_t address, const std::string& composed = {}) {
    if (hooks.notice) hooks.notice(g, {manager, raw, address,
        composed.empty() ? message_text(g, address) : composed});
}

// Only the DispatchClubPlayerPendingOffers adapter admits entries40..43; legacy entry bounds stay40.
struct PendingOwnerContext {
    int manager = -1;
    int team = -1;  // AdvanceTurn reads the raw record, including retained records1..3.
    bool native_takeover = false;
};

TransferOfferListView pending_view(const GameState& g, int raw, int player, MinuteReport& report,
                                   const PendingOwnerContext* owner) {
    auto view = project_transfer_offer_list_impl(g, raw + 40, true);
    if (owner && view.valid) {
        // IncomingTransferScreen receives param8 directly from the offer owner. FindManagerForClub
        // is only used for the buyer and later movement, not this seller context.
        view.seller_manager = owner->manager;
        if (g.memory.has(::bmh::layout::DeskActions_Unsupported_RuntimeField+uint32_t(owner->manager)))
            view.seller_loan_count = g.memory.u8(::bmh::layout::DeskActions_Unsupported_RuntimeField+uint32_t(owner->manager));
        for (auto& offer : view.offers) if (offer.present) {
            if (offer.loan) offer.loan_permitted = view.seller_loan_count >= 0 && view.seller_loan_count < 3;
            if (pending_native_manager_of(g,offer.buyer,owner->native_takeover)==4) {
                offer.buyer_manager=4;
                const size_t rec=4*kRecord;
                const auto i32=[&](size_t at) {
                    uint32_t n=0;
                    for(int i=0;i<4;++i)n|=uint32_t(g.manager_records.at(at+size_t(i)))<<(8*i);
                    return int32_t(n);
                };
                offer.affordable=int64_t(i32(rec+0x314))+i32(rec+0x31c)>=offer.fee;
                offer.eligible=offer.affordable && offer.has_squad_room && offer.passes_foreign_limit;
            }
        }
    }
    if (!view.valid || view.player != player || !view.offers[0].present ||
        view.seller_manager < 0 || view.offer_count != 1 ||
        g.memory.u16(kTransferList+uint32_t(raw+40)*kListEntry+4)!=0xffff ||
        g.memory.u16(kTransferList+uint32_t(raw+40)*kListEntry+6)!=0xffff) {
        report.fail("DispatchClubPlayerPendingOffers: malformed special transfer entry (expected one actual bid)");
        return {};
    }
    // DispatchClubPlayerPendingOffers is the only caller whose staged human-buyer settlement owns
    // the observed allocator gate and payer notices for this loan route.
    if (view.offers[0].loan && view.offers[0].buyer_manager >= 0)
        view.offers[0].transaction_supported = true;
    return view;
}

void record_transfer_history_source(GameState& g, int manager, int other_team, int player,
                                   uint32_t fee, bool loan, bool buyer_direction = false) {
    // PoolString is explicitly inert for parked owner4, even when Fail
    // recognizes that owner and calls it. Only ordinary owners0..3 have history.
    if(manager<0 || manager>=4)return;
    // The manager's two30-entry transfer-history blocks, newest first.
    auto rec = record(g, manager);
    for (int i = 29; i > 0; --i) {
        rec.set32(0x760 + size_t(i)*4, uint32_t(rec.i32(0x760 + size_t(i-1)*4)));
        rec.set32(0x7d8 + size_t(i)*4, uint32_t(rec.i32(0x7d8 + size_t(i-1)*4)));
    }
    rec.set32(0x760, uint32_t(loan)*2 + g.date*4 + uint32_t(buyer_direction) + (fee/10000)*0x80000);
    const uint32_t other_club = other_team == 0x0fff ? 0xffffu
        : uint32_t(g.club_ids.at(size_t(other_team)));
    rec.set32(0x7d8, uint32_t(g.player_name_offsets.at(size_t(player))) + other_club*0x10000);
    if (rec.b(0x21d) < 25) ++rec.b(0x21d);
}

enum class PendingSettleOutcome {
    not_moved,
    buyer_funds_refused,
    buyer_allocator_blocked,
    buyer_no_room,
    moved,
    unsupported
};

PendingSettleOutcome pending_settle_human_buyer(GameState& g, int raw, int bid,
                    bool force, const PendingOfferHooks& hooks, WatcomRandom& rand,
                    MinuteReport& report, int seller, int buyer, uint32_t severance,
                    const PendingOwnerContext* owner) {
    HumanBuyerSaleHooks shared;
    switch (hooks.human_buyer_allocator_gate) {
    case PendingOfferAllocatorGate::zero: shared.allocator_gate = HumanBuyerSaleAllocatorGate::zero; break;
    case PendingOfferAllocatorGate::blocked: shared.allocator_gate = HumanBuyerSaleAllocatorGate::blocked; break;
    default: shared.allocator_gate = HumanBuyerSaleAllocatorGate::unsupported; break;
    }
    if (hooks.notice) shared.notice = [&](const GameState& state, const HumanBuyerSaleNotice& notice) {
        if (hooks.notice) hooks.notice(state, {notice.manager, notice.raw_offer_slot,
                                                notice.source_text_address, notice.original_message});
    };
    shared.after_paid_accounts = [&](GameState& state, const HumanBuyerSalePaid& paid) {
        auto& mem = state.memory;
        // Void2 caller-only work remains between SettleHumanBuyerSale and Fail.
        if (severance != 0) {
            if (mem.has(::bmh::layout::FinanceStatisticsEnabled)) mem.set8(::bmh::layout::FinanceStatisticsEnabled, 1);
            book_money(state, paid.seller_manager, int32_t(severance), true, true);
            if (state.level != 0 && mem.has(::bmh::layout::FinanceStatisticsEnabled)) mem.set8(::bmh::layout::FinanceStatisticsEnabled, 0);
        }
        if (!force && hooks.notice)
            hooks.notice(state, {paid.buyer_manager, paid.raw_offer_slot, ::bmh::layout::PendingOffer_FormatError_ExecutableField2,
                                 message_text(state, ::bmh::layout::PendingOffer_FormatError_ExecutableField2)});
        state.transfer_notice_suppressed = false;
        if (mem.has(::bmh::layout::DeskActions_EditorFail_RuntimeField2)) mem.set8(::bmh::layout::DeskActions_EditorFail_RuntimeField2, 0);
        uint32_t mask = 0x80000000u | (1u << paid.seller_manager);
        if (!(mem.has(::bmh::layout::Weekly_ExecuteTransfer_RuntimeField) && mem.u8(::bmh::layout::Weekly_ExecuteTransfer_RuntimeField) != 0)) mask = 0xffffffffu;
        else mask |= 1u << paid.buyer_manager;
        const uint32_t interested = interested_managers(state, paid.seller, paid.buyer);
        if ((mask | interested) != 0x80000000u) {
            const std::string message = message_text(state, ::bmh::layout::Month_SearchProfileNews_TextPointer) + state.player_name(paid.player) +
                message_text(state, ::bmh::layout::Weekly_ExecuteTransfer_TextPointer) + state.club_name(paid.seller) + message_text(state, ::bmh::layout::Weekly_ExecuteTransfer_TextPointer2) +
                format_amount(state, int32_t(paid.raw_fee)) + message_text(state, ::bmh::layout::ClubPlayerDetail_RenderCard_TextPointer7) +
                state.club_name(paid.buyer) + message_text(state, (paid.term & 0x80) ? ::bmh::layout::Weekly_ExecuteTransfer_TextPointer3 : ::bmh::layout::Weekly_ExecuteTransfer_TextPointer4);
            post_composed(state, int64_t(mask | interested), message);
        }
    };

    HumanBuyerSaleRequest request;
    request.list_entry = raw + 40;
    request.bid_slot = bid;
    request.list_address = kTransferList + uint32_t(request.list_entry) * kListEntry;
    request.source_pool_slot = kPoolSlots + size_t(request.list_entry);
    request.expected_seller = seller;
    request.expected_buyer = buyer;
    request.expected_active_manager = g.memory.has(::bmh::layout::ActiveManager) ? int(g.memory.u8(::bmh::layout::ActiveManager)) : -1;
    request.raw_offer_slot = raw;
    if(owner && owner->native_takeover)
        request.source_pending_owner=SourcePendingOwner{owner->manager,owner->team};
    const auto settled = settle_human_buyer_sale(g, request, shared, rand, report);
    switch (settled.status) {
    case HumanBuyerSaleStatus::funds_refused:
        return PendingSettleOutcome::buyer_funds_refused;
    case HumanBuyerSaleStatus::allocator_blocked:
        if (g.memory.has(::bmh::layout::Weekly_PendingSettleHumanBuyer_RuntimeField)) g.memory.set8(::bmh::layout::Weekly_PendingSettleHumanBuyer_RuntimeField, 1);
        return PendingSettleOutcome::buyer_allocator_blocked;
    case HumanBuyerSaleStatus::no_room:
        if (g.memory.has(::bmh::layout::Weekly_PendingSettleHumanBuyer_RuntimeField)) g.memory.set8(::bmh::layout::Weekly_PendingSettleHumanBuyer_RuntimeField, 1);
        return PendingSettleOutcome::buyer_no_room;
    case HumanBuyerSaleStatus::moved:
        if (g.memory.has(::bmh::layout::Weekly_PendingSettleHumanBuyer_RuntimeField)) g.memory.set8(::bmh::layout::Weekly_PendingSettleHumanBuyer_RuntimeField, 1);
        return PendingSettleOutcome::moved;
    case HumanBuyerSaleStatus::unsupported:
    default:
        return PendingSettleOutcome::unsupported;
    }
}

PendingSettleOutcome pending_settle(GameState& g, int raw, int player, int bid, int clicked_player,
                    int clicked_slot, bool force, const PendingOfferHooks& hooks,
                    WatcomRandom& rand, MinuteReport& report, const PendingOwnerContext* owner) {
    auto& mem = g.memory;
    const uint32_t list = kTransferList + uint32_t(raw+40)*kListEntry;
    const int seller = mem.u16(list+8)&0x7fff;
    const int seller_manager = owner ? owner->manager : g.manager_of(seller);
    const bool native_takeover = owner && owner->native_takeover;
    const int movement_seller = pending_native_manager_of(g, seller, native_takeover);
    const int buyer = int16_t(mem.u16(list+uint32_t(bid)*2));
    const int buyer_manager = pending_native_manager_of(g, buyer, native_takeover);
    if (seller_manager < 0 || seller_manager >= 4 || buyer < 0 || buyer >= kLeagueClubs) {
        report.fail("DispatchClubPlayerPendingOffers: invalid seller/buyer for Void2"); return PendingSettleOutcome::unsupported;
    }
    const uint8_t term = mem.u8(list+0x34+uint32_t(bid));
    const bool loan = term > 0x7f;
    if (buyer_manager >= 0 && (buyer_manager >= (native_takeover?5:4) || buyer == seller ||
        (hooks.human_buyer_allocator_gate != PendingOfferAllocatorGate::zero &&
         hooks.human_buyer_allocator_gate != PendingOfferAllocatorGate::blocked))) {
        report.fail("DispatchClubPlayerPendingOffers: human-buyer loan/same-club/unknown allocator route is unsupported");
        return PendingSettleOutcome::unsupported;
    }
    if (buyer_manager >= 0) {
        const auto buyer_range = buyer >= 0 && buyer < kLeagueClubs ? squad_range(buyer) : SquadRange{};
        if (buyer_range.count != 24) {
            report.fail("DispatchClubPlayerPendingOffers: human buyer uses unsupported non-24-slot squad layout");
            return PendingSettleOutcome::unsupported;
        }
        for (int k = 0; k < 200; ++k) {
            const uint32_t e = mem.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + uint32_t(k) * 4);
            if (e != 0 && int(e & 0xfff) == player) {
                report.fail("Fail: existing return-loan record (error30) is unsupported for human buyer");
                return PendingSettleOutcome::unsupported;
            }
        }
    }
    // Fail reports a full AI squad only after settlement accounting/news,
    // including loans. Keep the legacy preflight's earlier room guard unchanged.
    if (loan && !preflight_human_owner_loan(g,seller,kPoolSlots+size_t(raw+40),buyer,report,false,native_takeover))
        return PendingSettleOutcome::unsupported;
    const auto range = squad_range(seller);
    size_t owner_slot = range.first;
    while (owner_slot < range.first+range.count && (g.squad_slots.at(owner_slot)&0xfff) != player)
        ++owner_slot;
    if (owner_slot == range.first+range.count) {
            report.fail("DispatchClubPlayerPendingOffers: IncomingTransferScreen seller player missing (internal error0x1e)"); return PendingSettleOutcome::unsupported;
    }
    const size_t board = size_t(seller_manager)*kBoard+(owner_slot-range.first)*kBoardSlot;
    if (board+kBoardSlot > g.tactics.size()) {
        report.fail("DispatchClubPlayerPendingOffers: missing seller salary board"); return PendingSettleOutcome::unsupported;
    }
    uint32_t old_salary = 0;
    for (int i=0;i<4;++i) old_salary |= uint32_t(g.tactics[board+0x50+size_t(i)])<<(8*i);
    const uint32_t months = loan ? term&0x7f : g.tactics[board+0x0c];
    const uint32_t old_total = old_salary*months;
    const uint32_t new_total = mem.u32(list+0x0c+uint32_t(bid)*4)*months;
    // Void2 draws this W even if no salary decrease follows.
    const uint32_t threshold = old_total*uint32_t(rand.range(90,100));
    uint32_t severance = 0;
    if (new_total*100u < threshold) {
        severance = std::max(((old_total-new_total)/1000)*1000,1000u);
        PendingOfferQuestion question;
        question.kind=PendingOfferQuestionKind::severance;
        question.manager=seller_manager;question.raw_offer_slot=raw;question.offered_player=player;
        question.clicked_player=clicked_player;question.clicked_source_slot=clicked_slot;
        question.current_clicked_name=g.player_name(g.squad_slots.at(size_t(clicked_slot))&0xfff);
        question.amount=severance;question.source_flags=0x305;
        question.original_message=message_text(g,::bmh::layout::Weekly_PendingSettle_TextPointer)+format_amount(g,int32_t(severance))+".";
        if (!hooks.answer) {
            report.fail("DispatchClubPlayerPendingOffers: missing manual severance answer"); return PendingSettleOutcome::unsupported;
        }
        const auto answer=hooks.answer(g,question);
        if(answer==PendingOfferAnswer::unsupported) {
            report.fail("DispatchClubPlayerPendingOffers: unsupported manual severance answer"); return PendingSettleOutcome::unsupported;
        }
        if(answer==PendingOfferAnswer::no) {
            mem.set8(::bmh::layout::ActiveManager,uint8_t(seller_manager));
            pending_notice(g,hooks,seller_manager,raw,::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField4);
            return PendingSettleOutcome::not_moved;
        }
    }
    const uint32_t fee=mem.u32(list+0x1c+uint32_t(bid)*4);
    if (buyer_manager >= 0)
        return pending_settle_human_buyer(g,raw,bid,force,hooks,rand,report,seller,buyer,severance,owner);
    // SettleHumanBuyerSale: this scoped adapter has an AI buyer (no buyer debit/subsidy).
    // BookMoney2(...,-fee,1), transfer ledger, then severance debit.
    if(mem.has(::bmh::layout::FinanceStatisticsEnabled))mem.set8(::bmh::layout::FinanceStatisticsEnabled,1);
    book_money(g,seller_manager,-int32_t(fee),true,true);
    if(g.level!=0 && mem.has(::bmh::layout::FinanceStatisticsEnabled))mem.set8(::bmh::layout::FinanceStatisticsEnabled,0);
    auto rec=record(g,seller_manager);
    rec.set32(0x330,uint32_t(rec.i32(0x330))-fee);
    if(severance) {
        if(mem.has(::bmh::layout::FinanceStatisticsEnabled))mem.set8(::bmh::layout::FinanceStatisticsEnabled,1);
        book_money(g,seller_manager,int32_t(severance),true,true);
        if(g.level!=0 && mem.has(::bmh::layout::FinanceStatisticsEnabled))mem.set8(::bmh::layout::FinanceStatisticsEnabled,0);
    }
    if(!force)pending_notice(g,hooks,seller_manager,raw,::bmh::layout::PendingOffer_FormatError_ExecutableField2); // Source param10 suppresses forced-sale ACK
    g.transfer_notice_suppressed=false;
    if(mem.has(::bmh::layout::DeskActions_EditorFail_RuntimeField2))mem.set8(::bmh::layout::DeskActions_EditorFail_RuntimeField2,0);
    uint32_t mask=0x80000000u|(1u<<seller_manager);
    if(!(mem.has(::bmh::layout::Weekly_ExecuteTransfer_RuntimeField)&&mem.u8(::bmh::layout::Weekly_ExecuteTransfer_RuntimeField)!=0))mask=0xffffffffu;
    const uint32_t interested=interested_managers(g,seller,buyer);
    if((mask|interested)!=0x80000000u) {
        const std::string message=message_text(g,::bmh::layout::Month_SearchProfileNews_TextPointer)+g.player_name(player)+message_text(g,::bmh::layout::Weekly_ExecuteTransfer_TextPointer)+
            g.club_name(seller)+message_text(g,::bmh::layout::Weekly_ExecuteTransfer_TextPointer2)+format_amount(g,int32_t(fee))+
            message_text(g,::bmh::layout::ClubPlayerDetail_RenderCard_TextPointer7)+g.club_name(buyer)+message_text(g,loan?::bmh::layout::Weekly_ExecuteTransfer_TextPointer3: ::bmh::layout::Weekly_ExecuteTransfer_TextPointer4);
        post_composed(g,int64_t(mask|interested),message);
    }
    // Fail's full-AI-squad notice happens after accounting/confirmation/news.
    const auto target=squad_range(buyer);
    bool room=false;
    for(size_t at=target.first;at<target.first+target.count;++at)
        room |= g.squad_slots.at(at)==0x0fff;
    if(!room) {
        pending_notice(g,hooks,seller_manager,raw,::bmh::layout::PendingOffer_FormatError_ExecutableField);
        if(mem.has(::bmh::layout::Weekly_PendingSettleHumanBuyer_RuntimeField))mem.set8(::bmh::layout::Weekly_PendingSettleHumanBuyer_RuntimeField,1);
        return PendingSettleOutcome::not_moved;
    }
    if(movement_seller>=0) record_transfer_history_source(g,movement_seller,buyer,player,fee,loan);
    move_player(g,seller,kPoolSlots+size_t(raw+40),buyer,list,bid,rand,report,
                TransferAllocatorContext::unsupported,native_takeover);
    if(mem.has(::bmh::layout::Weekly_PendingSettleHumanBuyer_RuntimeField))mem.set8(::bmh::layout::Weekly_PendingSettleHumanBuyer_RuntimeField,1);
    return report.unsupported ? PendingSettleOutcome::unsupported : PendingSettleOutcome::moved;
}

PendingSettleOutcome pending_negotiate(GameState& g, int raw, int player, bool force, int clicked_player,
                       int clicked_slot, GameRandom& game, WatcomRandom& rand,
                       const PendingOfferHooks& hooks, MinuteReport& report, const PendingOwnerContext* owner) {
    auto view=pending_view(g,raw,player,report,owner);
    if(report.unsupported)return PendingSettleOutcome::unsupported;
    if(force) return pending_settle(g,raw,player,0,clicked_player,clicked_slot,true,hooks,rand,report,owner);
    if(!hooks.select_transfer) {
        report.fail("DispatchClubPlayerPendingOffers: missing manual transfer-row chooser"); return PendingSettleOutcome::unsupported;
    }
    const uint32_t list=kTransferList+uint32_t(raw+40)*kListEntry;
    const auto range=squad_range(view.seller);
    size_t owner_slot=range.first;
    while(owner_slot<range.first+range.count && (g.squad_slots.at(owner_slot)&0xfff)!=player)++owner_slot;
    if(owner_slot==range.first+range.count) {
        report.fail("DispatchClubPlayerPendingOffers: IncomingTransferScreen seller player missing");return PendingSettleOutcome::unsupported;
    }
    const size_t board=size_t(view.seller_manager)*kBoard+(owner_slot-range.first)*kBoardSlot;
    for(;;) {
        // OpenTransferEditor restarts G from CareerSession_Invalid_RuntimeField2 on each actual redraw, not just entry.
        game.set_seed(int32_t(g.memory.u32(::bmh::layout::CareerSession_Invalid_RuntimeField2)));
        const int percent=game.range(92,108);
        view.quoted_years=uint8_t((int(g.tactics.at(board+0x0c))-1)/12+1);
        market_value(g,int(kPoolSlots+size_t(raw+40)),0,percent,view.quoted_fee,
            view.quoted_monthly_salary,view.quoted_years,0,rand);
        // MakeRequest -> PlayerCardMarketValue -> 422f4, distinct from salary-JA's raw42307.
        view.quoted_fee=((view.quoted_fee+(view.quoted_fee>>2))/10000)*10000;
        view.quoted_monthly_salary=(view.quoted_monthly_salary/100)*100;
        // OpenTransferEditor prepares its first empty offer row even in presentation mode.
        g.memory.set32(list+0x0c+4,0);g.memory.set32(list+0x1c+4,0);
        g.memory.set8(list+0x34+1,24);
        offer_bases(g.memory,list,1,view.asking_fee,view.asking_monthly_salary);
        const auto choice=hooks.select_transfer(g,view);
        if(choice.kind==PendingOfferTransferChoiceKind::unsupported) {
            report.fail("DispatchClubPlayerPendingOffers: unsupported manual transfer choice");return PendingSettleOutcome::unsupported;
        }
        if(choice.kind==PendingOfferTransferChoiceKind::decline) {
            if(!g.transfer_notice_suppressed &&
               !(g.memory.has(::bmh::layout::DeskActions_EditorFail_RuntimeField2) && g.memory.u8(::bmh::layout::DeskActions_EditorFail_RuntimeField2)!=0))
                pending_notice(g,hooks,view.seller_manager,raw,::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField);
            return PendingSettleOutcome::not_moved;
        }
        if(choice.raw_bid_slot!=0) {
            report.fail("DispatchClubPlayerPendingOffers: invalid special-entry raw bid slot");return PendingSettleOutcome::unsupported;
        }
        if(view.offers[0].loan && !view.offers[0].loan_permitted) {
            pending_notice(g,hooks,view.seller_manager,raw,::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField2);
            view.rejected_bid_index=0;view.notice_text_address=::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField2;
            continue;
        }
        const uint32_t before=g.memory.u32(list+0x0c);
        g.memory.set32(list+0x0c,(before*10u)>>3);
        uint32_t fee_pct=0,salary_pct=0;
        const bool native_takeover=owner && owner->native_takeover;
        const SourcePendingOwner native_owner{owner?owner->manager:-1,owner?owner->team:-1};
        const bool human_buyer = pending_native_manager_of(g,view.offers[0].buyer,native_takeover)>=0;
        const bool rated = human_buyer
            ? evaluate_pending_human_bid(g,list,0,fee_pct,salary_pct,rand,report,native_takeover?&native_owner:nullptr)
            : evaluate_bid(g,list,0,fee_pct,salary_pct,rand,report);
        g.memory.set32(list+0x0c,before);
        if(!rated||report.unsupported)return PendingSettleOutcome::unsupported;
        if(salary_pct!=0)
            return pending_settle(g,raw,player,0,clicked_player,clicked_slot,false,hooks,rand,report,owner);
        pending_notice(g,hooks,view.seller_manager,raw,::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField3);
        g.memory.set16(list,0xffff); // param9==1: final rejection, no extra redraw.
        return PendingSettleOutcome::not_moved;
    }
}

} // namespace

HumanBuyerSaleResult settle_human_buyer_sale(GameState& g, const HumanBuyerSaleRequest& request,
    const HumanBuyerSaleHooks& hooks, WatcomRandom& rand, MinuteReport& report) {
    HumanBuyerSaleResult result;
    if (report.unsupported) return result;
    auto& mem = g.memory;
    const auto fail = [&](const char* why) {
        report.fail(why);
        result.status = HumanBuyerSaleStatus::unsupported;
        return result;
    };

    // Validate the exact caller-owned source coordinates. Ordinary and pending
    // offers share the transaction but never share or synthesize their indices.
    if (request.list_entry < 0 || request.list_entry >= 44 || request.bid_slot < 0 || request.bid_slot >= 4 ||
        request.list_address != kTransferList + uint32_t(request.list_entry) * kListEntry ||
        request.source_pool_slot != kPoolSlots + size_t(request.list_entry) ||
        !mem.has(request.list_address + kListEntry - 1) ||
        request.source_pool_slot >= g.squad_slots.size())
        return fail("SettleHumanBuyerSale/Fail: invalid explicit source list/pool/bid coordinates");
    if (request.raw_offer_slot == -1) {
        const bool return_entry = request.loan_return_entry && !request.poach &&
            request.list_entry >= 40 && request.list_entry < 44 &&
            mem.has(::bmh::layout::OfferSlots + uint32_t(request.list_entry - 40) * 4 + 3) &&
            mem.u32(::bmh::layout::OfferSlots + uint32_t(request.list_entry - 40) * 4) ==
                uint32_t(request.source_pool_slot < g.squad_slots.size()
                    ? (g.squad_slots[request.source_pool_slot] & 0x0fff) : 0x0fff) * 0x400u +
                    uint32_t(request.expected_active_manager);
        if ((request.list_entry >= 40 && !request.poach && !return_entry) ||
            (request.loan_return_entry && !return_entry) ||
            (request.list_entry < 40 && (request.poach || request.loan_return_entry)))
            return fail("SettleHumanBuyerSale/Fail: ordinary sale requires list entry 0..39");
    } else if (request.raw_offer_slot < 0 || request.raw_offer_slot >= 4 ||
               request.list_entry != 40 + request.raw_offer_slot || request.loan_return_entry) {
        return fail("SettleHumanBuyerSale/Fail: pending raw slot does not identify its explicit list entry");
    }
    const bool native_takeover=request.source_pending_owner.has_value();
    if(native_takeover && (request.raw_offer_slot<0 || request.bid_slot!=0 || request.day_step || request.poach || request.loan_return_entry ||
        !source_pending_owner_valid(g,*request.source_pending_owner,request.list_entry,request.source_pool_slot)))
        return fail("SettleHumanBuyerSale/Fail: invalid DispatchClubPlayerPendingOffers native pending owner context");
    if (!request.day_step && (!mem.has(::bmh::layout::ActiveManager) || request.expected_active_manager < 0 ||
        (!native_takeover && size_t(request.expected_active_manager) >= g.manager_teams.size()) ||
        mem.u8(::bmh::layout::ActiveManager) != request.expected_active_manager))
        return fail("SettleHumanBuyerSale/Fail: active manager does not match caller context");
    if (hooks.allocator_gate != HumanBuyerSaleAllocatorGate::zero &&
        hooks.allocator_gate != HumanBuyerSaleAllocatorGate::blocked)
        return fail("PendingOfferAnswer: human-buyer allocator gate was not observed by the runtime owner");
    if (!hooks.notice)
        return fail("SettleHumanBuyerSale/Fail: Source notice presenter is unavailable");

    const uint32_t list = request.list_address;
    const int player = int(g.squad_slots.at(request.source_pool_slot) & 0x0fff);
    const int seller = int(mem.u16(list + 8) & 0x7fff);
    const int buyer = int16_t(mem.u16(list + uint32_t(request.bid_slot) * 2));
    const bool free_agent = seller == 0x0fff;
    if (player == 0x0fff || size_t(player) >= g.players.size() ||
        seller != request.expected_seller || buyer != request.expected_buyer ||
        (!free_agent && (seller < 0 || seller >= kLeagueClubs)) ||
        buyer < 0 || buyer >= kLeagueClubs || seller == buyer)
        return fail("SettleHumanBuyerSale/Fail: source player/seller/buyer identity mismatch");
    // SettleHumanBuyerSale uses param8 for the seller's accounts; Fail separately
    // resolves actual movement ownership through FindManagerForClub.
    const int seller_manager=pending_native_manager_of(g,seller,native_takeover);
    const int account_seller=native_takeover?request.source_pending_owner->manager:seller_manager;
    const int buyer_manager=pending_native_manager_of(g,buyer,native_takeover);
    const bool parked_buyer=native_takeover && buyer_manager==4;
    if (buyer_manager < 0 || buyer_manager >= (native_takeover?5:4) ||
        (!parked_buyer && (size_t(buyer_manager)>=g.manager_teams.size() || g.manager_teams[size_t(buyer_manager)]!=buyer)) ||
        (free_agent && seller_manager >= 0) ||
        (seller_manager >= 0 && (seller_manager >= (native_takeover?5:4) || seller_manager == buyer_manager)))
        return fail("SettleHumanBuyerSale/Fail: unsupported buyer/seller manager identity");
    if(native_takeover) {
        const int expected=request.source_pending_owner->team==kNoTeam
            ? int(g.manager_records[4*kRecord+0x290]) | int(g.manager_records[4*kRecord+0x291])<<8
            : request.source_pending_owner->team;
        if(seller!=expected)return fail("SettleHumanBuyerSale/Fail: invalid native pending seller provenance");
    }
    if (!request.day_step && request.expected_active_manager != buyer_manager && request.expected_active_manager != account_seller)
        return fail("SettleHumanBuyerSale/Fail: active manager is neither the direct buyer nor pending seller context");

    const uint8_t term = mem.u8(list + 0x34 + uint32_t(request.bid_slot));
    const bool loan = term > 0x7f;
    if (loan && free_agent)
        return fail("Fail/DayStepRest2: free-agent loan return destination is not represented");
    const auto buyer_range = squad_range(buyer);
    if (buyer_range.count != 24)
        return fail("PendingOfferAnswer: human buyer uses unsupported non-24-slot squad layout");
    if (!mem.has(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + 199 * 4 + 3))
        return fail("Fail: missing return-loan storage (error30/31 boundary)");
    int loan_slot = -1;
    for (int k = 0; k < 200; ++k) {
        const uint32_t e = mem.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + uint32_t(k) * 4);
        if (e == 0 && loan_slot < 0) loan_slot = k;
        else if (e != 0 && int(e & 0x0fff) == player)
            return fail("Fail: existing return-loan record (error30) is unsupported for human buyer");
    }
    if (loan && loan_slot < 0)
        return fail("Fail: return-loan ledger full (error36)");

    size_t seller_board = 0;
    if (free_agent) {
        // ReleasePlayer's canonical 0FFF seller scans the entire free-player pool,
        // not a fictitious seller squad. FFFF is not an alias for this seller.
        if (g.squad_slots.size() < runtime_dimensions.national_slots_first)
            return fail("ReleasePlayer: free-agent pool is outside native squad storage");
    } else {
        const auto seller_range = squad_range(seller);
        if (seller_range.count == 0 || seller_range.first + seller_range.count > g.squad_slots.size())
            return fail("Fail: seller squad (error32) is outside native squad storage");
        size_t seller_slot = seller_range.first;
        while (seller_slot < seller_range.first + seller_range.count &&
               (g.squad_slots.at(seller_slot) & 0x0fff) != player) ++seller_slot;
        if (seller_slot == seller_range.first + seller_range.count || seller_slot >= g.squad_slots.size())
            return fail("Fail: seller does not own source player (error32)");
        if (seller_manager >= 0)
            seller_board = size_t(seller_manager) * kBoard + (seller_slot - seller_range.first) * kBoardSlot;
    }
    if (seller_manager >= 0 && seller_board + kBoardSlot > g.tactics.size())
        return fail("Fail: seller board row (error32) is outside native tactics storage");
    if (buyer_range.first + buyer_range.count > g.squad_slots.size() ||
        size_t(buyer_manager) * kBoard + 24 * kBoardSlot > g.tactics.size())
        return fail("Fail: unsupported human buyer squad/board layout");

    int owner_snapshot_slot = -1;
    std::array<uint8_t, kBoardSlot> owner_board_snapshot{};
    if (loan && seller_manager >= 0) {
        if (seller_manager >= (native_takeover?5:4) || !mem.has(::bmh::layout::DeskActions_Unsupported_RuntimeField + uint32_t(seller_manager)) ||
            mem.u8(::bmh::layout::DeskActions_Unsupported_RuntimeField + uint32_t(seller_manager)) >= 3 ||
            !mem.has(::bmh::layout::Career_CareerManagersEnd_RuntimeField + 0x17) || !mem.has(::bmh::layout::ClubInfo_FormatError_RuntimeField + 0x41f) || !mem.has(::bmh::layout::ClubInfo_FormatError_RuntimeField2 + 0x0b))
            return fail("Fail: human owner loan count/snapshot storage unavailable");
        for (int i = 0; i < 12; ++i) {
            const uint16_t id = mem.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField + uint32_t(i) * 2);
            if (id == uint16_t(player))
                return fail("Fail: duplicate human-owner loan snapshot");
            if (owner_snapshot_slot < 0 && id == 0x0fff) owner_snapshot_slot = i;
        }
        if (owner_snapshot_slot < 0)
            return fail("Fail: human-owner loan snapshots full (error34)");
        std::copy_n(g.tactics.begin() + std::ptrdiff_t(seller_board), kBoardSlot,
                    owner_board_snapshot.begin());
    }

    const uint32_t fee = mem.u32(list + 0x1c + uint32_t(request.bid_slot) * 4);
    const uint32_t salary = mem.u32(list + 0x0c + uint32_t(request.bid_slot) * 4);
    if (hooks.before_accounts) hooks.before_accounts(g, request);
    auto buyer_record = record(g, buyer_manager);
    const uint8_t subsidy = buyer_record.b(0x170);
    uint32_t adjusted_fee = fee;
    const int position = int(g.players.at(size_t(player)).w364 & 3);
    if (fee <= buyer_record.u32(0x314) && subsidy != 0 && (subsidy & 3) == position) {
        int league = buyer_record.b(0x210);
        if (league > 2) --league;
        const uint32_t threshold = (((uint32_t(subsidy) >> 2) & 7u) * 50000u + 150000u) *
                                   uint32_t(4 - league);
        if (fee < threshold) {
            const uint32_t discount = (((uint32_t(subsidy) >> 5) + 3u) * fee * 10u) / 100u;
            const std::string message = message_text(g, ::bmh::layout::PendingOffer_ConstructionNoticeDialog_TextPointer) +
                format_amount(g, int32_t(discount)) + message_text(g, ::bmh::layout::Weekly_Fail_TextPointer) +
                format_amount(g, int32_t(fee)) + ")."; // EXE literal at a12ca, not a TEXTD pointer.
            hooks.notice(g, {buyer_manager, request.list_entry, request.raw_offer_slot,
                             ::bmh::layout::PendingOffer_ConstructionNoticeDialog_TextPointer, message});
            adjusted_fee = fee - discount;
        } else {
            hooks.notice(g, {buyer_manager, request.list_entry, request.raw_offer_slot,
                             ::bmh::layout::PendingOffer_FormatError_ExecutableField3, message_text(g, ::bmh::layout::PendingOffer_FormatError_ExecutableField3)});
        }
        // Both qualifying Source branches consume the subsidy even if payment fails.
        record(g, buyer_manager).b(0x170) = 0;
    }

    const uint8_t old_active = mem.u8(::bmh::layout::ActiveManager);
    if (mem.has(::bmh::layout::FinanceStatisticsEnabled)) mem.set8(::bmh::layout::FinanceStatisticsEnabled, 1);
    const bool buyer_paid = book_money(g, buyer_manager, int32_t(adjusted_fee), false, true);
    if (!buyer_paid) {
        mem.set8(::bmh::layout::ActiveManager, uint8_t(buyer_manager));
        hooks.notice(g, {buyer_manager, request.list_entry, request.raw_offer_slot,
                         ::bmh::layout::Advertising_FormatError_TextPointer18, message_text(g, ::bmh::layout::Advertising_FormatError_TextPointer18)});
        if (g.level != 0 && mem.has(::bmh::layout::FinanceStatisticsEnabled)) mem.set8(::bmh::layout::FinanceStatisticsEnabled, 0);
        mem.set8(::bmh::layout::ActiveManager, old_active);
        result.status = HumanBuyerSaleStatus::funds_refused;
        result.player = player; result.seller = seller; result.buyer = buyer;
        result.seller_manager = account_seller; result.buyer_manager = buyer_manager;
        result.raw_fee = fee; result.adjusted_fee = adjusted_fee;
        return result;
    }
    if (g.level != 0 && mem.has(::bmh::layout::FinanceStatisticsEnabled)) mem.set8(::bmh::layout::FinanceStatisticsEnabled, 0);
    record(g, buyer_manager).set32(0x330, uint32_t(record(g, buyer_manager).i32(0x330)) + adjusted_fee);
    if (account_seller >= 0) {
        if (mem.has(::bmh::layout::FinanceStatisticsEnabled)) mem.set8(::bmh::layout::FinanceStatisticsEnabled, 1);
        book_money(g, account_seller, int32_t(0u - adjusted_fee), true, true);
        if (g.level != 0 && mem.has(::bmh::layout::FinanceStatisticsEnabled)) mem.set8(::bmh::layout::FinanceStatisticsEnabled, 0);
        auto seller_record = record(g, account_seller);
        seller_record.set32(0x330, uint32_t(seller_record.i32(0x330)) - adjusted_fee);
    }

    HumanBuyerSalePaid paid;
    paid.list_entry = request.list_entry; paid.bid_slot = request.bid_slot;
    paid.raw_offer_slot = request.raw_offer_slot; paid.player = player;
    paid.seller = seller; paid.buyer = buyer;
    paid.seller_manager = account_seller; paid.buyer_manager = buyer_manager;
    paid.raw_fee = fee; paid.adjusted_fee = adjusted_fee; paid.salary = salary; paid.term = term;
    if (hooks.after_paid_accounts) hooks.after_paid_accounts(g, paid);

    result.player = player; result.seller = seller; result.buyer = buyer;
    result.seller_manager = account_seller; result.buyer_manager = buyer_manager;
    result.raw_fee = fee; result.adjusted_fee = adjusted_fee;
    if (hooks.allocator_gate == HumanBuyerSaleAllocatorGate::blocked) {
        result.status = HumanBuyerSaleStatus::allocator_blocked;
        return result;
    }

    size_t buyer_slot = buyer_range.first;
    while (buyer_slot < buyer_range.first + buyer_range.count &&
           g.squad_slots.at(buyer_slot) != 0x0fff) ++buyer_slot;
    if (buyer_slot == buyer_range.first + buyer_range.count) {
        const int current_manager = int(mem.u8(::bmh::layout::ActiveManager));
        hooks.notice(g, {current_manager, request.list_entry, request.raw_offer_slot,
                         ::bmh::layout::PendingOffer_FormatError_ExecutableField, message_text(g, ::bmh::layout::PendingOffer_FormatError_ExecutableField)});
        result.status = HumanBuyerSaleStatus::no_room;
        return result;
    }

    const uint16_t source_word = g.squad_slots.at(request.source_pool_slot);
    g.squad_slots.at(buyer_slot) = source_word;
    const size_t buyer_board = size_t(buyer_manager) * kBoard +
        (buyer_slot - buyer_range.first) * kBoardSlot;
    new_board_entry(g, buyer_manager, int(buyer_slot - buyer_range.first), int(buyer_slot), rand);
    if (seller_manager >= 0)
        std::copy_n(g.tactics.begin() + std::ptrdiff_t(seller_board), kBoardSlot,
                    g.tactics.begin() + std::ptrdiff_t(buyer_board));
    for (int b = 0; b < 4; ++b)
        g.tactics.at(buyer_board + 0x50 + size_t(b)) = uint8_t(salary >> (b * 8));
    uint32_t reference_fee = 0, reference_salary = 0;
    market_value(g, int(request.source_pool_slot), 0, 100, reference_fee, reference_salary,
                 int(term & 0x7f), 0, rand);
    if (reference_salary == 0)
        return fail("Fail: zero reference salary is an unsupported divide-by-zero boundary");
    for (int b = 0; b < 7; ++b)
        g.tactics.at(buyer_board + 0x12 + size_t(b)) =
            mem.u8(list + 0x38 + uint32_t(request.bid_slot) * 7 + uint32_t(b));
    const uint32_t bonus = parked_buyer
        ? expected_bonus_parked_club_source(g,0xff,int(buyer_slot-buyer_range.first),player,true)
        : expected_bonus(g,buyer_manager,0xff,int(buyer_slot-buyer_range.first),player,true);
    const uint32_t bonus_product = bonus * 0x50u;
    const int32_t signed_bonus_product = int32_t(bonus_product);
    const int32_t expected_bonus_component = signed_bonus_product / 100;
    const uint32_t source_total = salary + uint32_t(expected_bonus_component);
    const uint32_t source_numerator = source_total * 100u;
    const uint32_t unsigned_ratio = source_numerator / reference_salary;
    const int32_t signed_ratio = int32_t(unsigned_ratio);
    const uint32_t ratio = uint32_t(std::clamp(signed_ratio, 0, 0xff));
    g.tactics.at(buyer_board + 0x0d) = uint8_t(ratio);
    if (const char* trace = std::getenv("BMH_MARKET_TRACE"); trace && trace[0] == '1')
        std::fprintf(stderr, "[purchase mood player %d: salary %u bonus %u (x0.8 = %d) reference %u (term %d) -> ratio %u]\n", player,
                     salary, bonus, expected_bonus_component, reference_salary, int(term & 0x7f), ratio);
    for (int b = 0; b < 4; ++b)
        g.tactics.at(buyer_board + 0x54 + size_t(b)) = uint8_t(fee >> (b * 8));
    g.tactics.at(buyer_board + 0x0c) = loan ? uint8_t(0xff) : term;

    if (loan) {
        if (seller_manager >= 0) {
            const uint32_t snapshot = ::bmh::layout::ClubInfo_FormatError_RuntimeField + uint32_t(owner_snapshot_slot) * uint32_t(kBoardSlot);
            for (size_t b = 0; b < kBoardSlot; ++b)
                mem.set8(snapshot + uint32_t(b), owner_board_snapshot[b]);
            mem.set16(::bmh::layout::Career_CareerManagersEnd_RuntimeField + uint32_t(owner_snapshot_slot) * 2, uint16_t(player));
            mem.set8(::bmh::layout::ClubInfo_FormatError_RuntimeField2 + uint32_t(owner_snapshot_slot), uint8_t(seller_manager));
            mem.set8(::bmh::layout::DeskActions_Unsupported_RuntimeField + uint32_t(seller_manager), uint8_t(mem.u8(::bmh::layout::DeskActions_Unsupported_RuntimeField + uint32_t(seller_manager)) + 1));
        }
        mem.set8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField + uint32_t(loan_slot), uint8_t(term & 0x7f));
        mem.set32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + uint32_t(loan_slot) * 4,
                  uint32_t(player + seller * 0x1000 + buyer * 0x400000));
    }

    if (seller_manager >= 0)
        record_transfer_history_source(g, seller_manager, buyer, player, fee, loan, false);
    record_transfer_history_source(g, buyer_manager, seller, player, fee, loan, true);
    if (!remove_from_seller_impl(g,seller,player,report,native_takeover)) {
        result.status = HumanBuyerSaleStatus::unsupported;
        return result;
    }
    lineup_after_transfer_impl(g,seller,native_takeover);
    lineup_after_transfer_impl(g,buyer,native_takeover);
    result.status = HumanBuyerSaleStatus::moved;
    return result;
}

namespace {
PendingOfferResult process_pending_batch(GameState& g, int clicked_player, int clicked_slot,
    int clicked_manager, int physical_row, bool human_loan, int human_loan_index,
    GameRandom& game, WatcomRandom& rand, const PendingOfferHooks& hooks, MinuteReport& report,
    const PendingOwnerContext* owner = nullptr) {
    PendingOfferResult result;
    if(report.unsupported)return result;
    if(!g.texts || !g.memory.has(::bmh::layout::ActiveManager) || !g.memory.has(::bmh::layout::OfferSlots+15) ||
       !g.memory.has(::bmh::layout::CareerSession_Invalid_RuntimeField2+3) || kPoolSlots+44>g.squad_slots.size()) {
        report.fail("DispatchClubPlayerPendingOffers: missing native offer state or Original TEXTD");return result;
    }
    // Owned board storage, not a copied board or a remapped offered-player row.
    const size_t board=human_loan ? 0 : size_t(clicked_manager)*kBoard+
        size_t(physical_row)*kBoardSlot;
    const uint32_t loan_board=::bmh::layout::ClubInfo_FormatError_RuntimeField+uint32_t(std::max(human_loan_index,0))*kBoardSlot;
    const auto board_byte=[&](size_t offset) {
        return human_loan ? g.memory.u8(loan_board+uint32_t(offset)) : g.tactics.at(board+offset);
    };
    const auto write_board=[&](size_t offset,uint8_t value) {
        if(human_loan)g.memory.set8(loan_board+uint32_t(offset),value);
        else g.tactics.at(board+offset)=value;
    };
    bool sticky=false;
    for(int raw=0;raw<4;++raw) {
        const uint32_t at=::bmh::layout::OfferSlots+uint32_t(raw)*4;
        const uint32_t offer=g.memory.u32(at);
        const int active=g.memory.u8(::bmh::layout::ActiveManager);
        if(offer==0xffffffffu || int(offer&31)!=active)continue;
        if(active<0 || (owner ? active!=owner->manager : size_t(active)>=g.manager_teams.size())) {
            report.fail("DispatchClubPlayerPendingOffers: invalid active owner");return result;
        }
        const int player=int((offer>>10)&0xfff);
        const auto range=squad_range(owner ? owner->team : g.manager_teams[size_t(active)]);
        size_t offered_slot=range.first;
        while(offered_slot<range.first+range.count && (g.squad_slots.at(offered_slot)&0xfff)!=player)++offered_slot;
        if(offered_slot<range.first+range.count) {
            bool negotiate=true;
            const uint32_t list=kTransferList+uint32_t(raw+40)*kListEntry;
            if(!g.memory.has(list+kListEntry-1)) {
                report.fail("DispatchClubPlayerPendingOffers: truncated special transfer entry");return result;
            }
            if(owner) {
                const int seller=g.memory.u16(list+8)&0x7fff;
                const int expected_seller=owner->team==kNoTeam
                    ? int(g.manager_records.at(4*kRecord+0x290)) |
                        (int(g.manager_records.at(4*kRecord+0x291))<<8) : owner->team;
                if(seller<0 || seller>=kLeagueClubs || seller!=expected_seller ||
                    (g.squad_slots.at(kPoolSlots+size_t(raw+40))&0x0fff)!=player) {
                    report.fail("DispatchClubPlayerPendingOffers: invalid special offer seller/pool provenance (PreMatchOffer requires a club seller)");
                    return result;
                }
            }
            if(offer&0x80000000u) {
                PendingOfferQuestion question;
                question.manager=active;question.raw_offer_slot=raw;question.offered_player=player;
                question.clicked_player=clicked_player;question.clicked_source_slot=clicked_slot;
                question.current_clicked_name=g.player_name(g.squad_slots.at(size_t(clicked_slot))&0xfff);
                question.amount=g.memory.u32(list+0x0c);
                question.original_message=question.current_clicked_name+message_text(g,::bmh::layout::Weekly_ProcessPendingBatch_TextPointer)+
                    format_amount(g,int32_t(question.amount))+message_text(g,::bmh::layout::Weekly_ProcessPendingBatch_TextPointer2);
                if(!hooks.answer) {
                    report.fail("DispatchClubPlayerPendingOffers: missing manual salary answer");return result;
                }
                const auto answer=hooks.answer(g,question);
                if(answer==PendingOfferAnswer::unsupported) {
                    report.fail("DispatchClubPlayerPendingOffers: unsupported manual salary answer");return result;
                }
                if(answer==PendingOfferAnswer::yes) {
                    if(!(offer&0x20)) {
                        uint32_t fee=0,salary=0;
                        market_value(g,clicked_slot,0,98,fee,salary,(board_byte(0x0c)+6)/12,0,rand);
                        if(salary==0) {
                            report.fail("DispatchClubPlayerPendingOffers: salary reference zero (Source division by zero)");return result;
                        }
                        for(int byte=0;byte<4;++byte)write_board(0x50+size_t(byte),uint8_t(question.amount>>(8*byte)));
                        write_board(0x0d,uint8_t(question.amount*100u/salary));
                    }
                    ++result.salary_accepted;negotiate=false;
                } else sticky=true;
            }
            if(negotiate) {
                const auto outcome=pending_negotiate(g,raw,player,sticky,clicked_player,clicked_slot,
                                                     game,rand,hooks,report,owner);
                switch(outcome) {
                    case PendingSettleOutcome::moved: ++result.transferred; break;
                    case PendingSettleOutcome::buyer_funds_refused: ++result.buyer_funds_refused; break;
                    case PendingSettleOutcome::buyer_allocator_blocked: ++result.buyer_allocator_blocked; break;
                    case PendingSettleOutcome::buyer_no_room: ++result.buyer_no_room; break;
                    case PendingSettleOutcome::unsupported:
                        return result;
                    case PendingSettleOutcome::not_moved:
                        break;
                }
            }
            if(report.unsupported)return result;
        }
        // Cleanup applies to owned offers even when the offered player disappeared.
        g.squad_slots.at(kPoolSlots+size_t(raw+40))=0x0fff;
        g.memory.set32(at,0xffffffffu);
        ++result.processed;result.forced_acceptance=sticky;
    }
    result.completed=true;
    return result;
}
} // namespace

PendingOfferResult process_player_card_pending_offers(GameState& g,
    const ClubPlayerDetailSelection& selection, GameRandom& game, WatcomRandom& rand,
    const PendingOfferHooks& hooks, MinuteReport& report) {
    PendingOfferResult result;
    if(report.unsupported)return result;
    const auto clicked=club_player_detail_context(g,selection);
    if(clicked.national_mode!=0 || !clicked.board || clicked.active_offer<0) {
        report.fail("DispatchClubPlayerPendingOffers: clicked card has no ordinary pending action/board");return result;
    }
    return process_pending_batch(g,selection.player_id,clicked.source_slot,clicked.source_manager,
        selection.physical_row,clicked.human_loan,selection.human_loan_index,game,rand,hooks,report);
}

PendingOfferResult process_natural_pending_offers_after_desk(GameState& g,
    const PendingOfferDeskRequest& request, GameRandom& game, WatcomRandom& rand,
    const PendingOfferHooks& hooks, MinuteReport& report) {
    PendingOfferResult result;
    if (report.unsupported) return result;
    const auto fail = [&](const char* why) -> PendingOfferResult {
        report.fail(why); return result;
    };
    if (request.runtime_mode != PendingOfferDeskRuntimeMode::normal_interactive)
        return fail("AdvanceTurn: unsupported or unspecified transient desk runtime mode");
    constexpr uint32_t kManagerCount = ::bmh::layout::ManagerCount, kOrder = ::bmh::layout::NextDeskManager, kDeskPosition = ::bmh::layout::DeskPosition;
    constexpr uint32_t kActiveManager = ::bmh::layout::ActiveManager, kOffers = ::bmh::layout::OfferSlots;
    if (!g.memory.has(kManagerCount) || !g.memory.has(kOrder + 7) ||
        !g.memory.has(kDeskPosition) || !g.memory.has(kActiveManager) ||
        !g.memory.has(kOffers + 15) || kPoolSlots + 44 > g.squad_slots.size())
        return fail("AdvanceTurn: missing native manager/order/offer state");
    const int count = g.memory.u8(kManagerCount);
    if (count < 1 || count > 4 || request.manager_count != count ||
        size_t(count) != g.manager_teams.size())
        return fail("AdvanceTurn: manager count does not match native manager loop");
    if (request.manager_position < 0 || request.manager_position >= count ||
        int(g.memory.u8(kDeskPosition)) != request.manager_position)
        return fail("AdvanceTurn: desk position does not match native bdb77");
    // While a Bundestrainer has taken the national team over (CoachActive), SwapManagerSlots2 has
    // cut ManagerCount to one but left the order NextDeskManager of the full game (OriginalManagerCount
    // managers): PrepareEvent shuffles only a one-entry prefix, the desk is slot 0
    // (ActiveManager = DeskPosition while the count is one) and the offer owner afterwards is NextDeskManager[0],
    // which may be a manager who sits this day out.
    const bool takeover = g.memory.has(::bmh::layout::CoachActive) && g.memory.u8(::bmh::layout::CoachActive) != 0 && g.memory.has(::bmh::layout::OriginalManagerCount);
    if (takeover && (count!=1 || g.memory.u8(::bmh::layout::OriginalManagerCount)<1 || g.memory.u8(::bmh::layout::OriginalManagerCount)>4 ||
        g.manager_teams[0]!=kNoTeam || 5*kRecord>g.manager_records.size() || 5*kBoard>g.tactics.size()))
        return fail("AdvanceTurn: invalid reduced-count national takeover record context");
    const int limit = takeover ? int(g.memory.u8(::bmh::layout::OriginalManagerCount)) : count;
    std::array<bool,4> seen{};
    for (int i=0;i<count;++i) {
        const int manager=g.memory.u16(kOrder+uint32_t(i)*2);
        if (manager<0 || manager>=limit || seen[size_t(manager)])
            return fail("AdvanceTurn: invalid native manager order permutation");
        seen[size_t(manager)]=true;
    }
    const int manager=g.memory.u16(kOrder+uint32_t(request.manager_position)*2);
    if (manager<0 || manager>=limit || (manager<count && size_t(manager)>=g.manager_teams.size()))
        return fail("AdvanceTurn: invalid current ordered manager");
    if (takeover && size_t(manager)*kRecord+0x292>g.manager_records.size())
        return fail("AdvanceTurn: missing current ordered manager record");
    const size_t owner_record=size_t(manager)*kRecord+0x290;
    const int native_team=takeover ? int(g.manager_records[owner_record]) |
        (int(g.manager_records[owner_record+1])<<8) : g.manager_teams[size_t(manager)];
    const PendingOwnerContext owner{manager,native_team,takeover};
    // Records1..3 survive the count-one national takeover. AdvanceTurn reads
    // these records directly even while their clubs have no current desk.
    if ((takeover && manager<count && g.manager_teams[size_t(manager)]!=native_team) ||
        (native_team!=kNoTeam && (native_team<0 || native_team>=kLeagueClubs)) ||
        (native_team==kNoTeam && (!takeover || manager!=0)))
        return fail("AdvanceTurn: invalid native ordered owner team");
    const auto inactive_club_offer = [&](int raw, int player) {
        // Deliberate OpenBMH improvement: AdvanceTurn reports error58 when a
        // retained club offer is looked up in the temporary national squad.
        // Qualify the old club in spare record4 and the offer's pool identity;
        // preserve Source's cleanup, without presenting this context mismatch.
        if (!takeover || count!=1 || manager!=0 ||
            g.memory.u8(::bmh::layout::OriginalManagerCount)<1 || g.memory.u8(::bmh::layout::OriginalManagerCount)>4 ||
            4*kRecord+0x292>g.manager_records.size() ||
            player<0 || size_t(player)>=g.players.size() ||
            (g.squad_slots.at(kPoolSlots+size_t(raw+40))&0x0fff)!=player)
            return false;
        const auto record_team = [&](size_t base) {
            return int(g.manager_records[base+0x290]) |
                (int(g.manager_records[base+0x291])<<8);
        };
        if (record_team(0)!=kNoTeam) return false;
        const int club=record_team(4*kRecord);
        if (club<0 || club>=runtime_dimensions.league_teams) return false;
        const auto squad=squad_range(club);
        if (squad.first+squad.count>g.squad_slots.size()) return false;
        for (size_t i=squad.first;i<squad.first+squad.count;++i)
            if ((g.squad_slots[i]&0x0fff)==player) return true;
        return false;
    };
    g.memory.set8(kActiveManager,uint8_t(manager));
    int anchor_player=-1,anchor_slot=-1;
    for(int raw=0;raw<4;++raw) {
        const uint32_t offer=g.memory.u32(kOffers+uint32_t(raw)*4);
        if(offer==0xffffffffu || int(offer&31)!=manager)continue;
        const int team=native_team;
        const bool national_owner = takeover && team == kNoTeam;
        if (team<0 || (team>=runtime_dimensions.league_teams && !national_owner))
            return fail("AdvanceTurn: national or unknown owner is unsupported for normal-club offers");
        // AdvanceTurn scans raw offer words until the first owned player resolves in
        // an ordinary squad. A missing earlier player requires PresentLoanReturn's error
        // presentation; later words belong to DispatchClubPlayerPendingOffers's own batch scan.
        const auto range=squad_range(team);
        if (range.first+range.count>g.squad_slots.size())
            return fail("AdvanceTurn: truncated ordinary owner squad");
        const int player=int((offer>>10)&0xfff);
        size_t slot=range.first;
        while(slot<range.first+range.count && int(g.squad_slots[slot]&0x0fff)!=player)++slot;
        if(slot==range.first+range.count) {
            if(national_owner && inactive_club_offer(raw,player)) continue;
            if(!hooks.program_error)
                return fail("AdvanceTurn: missing earliest owned offer requires PresentLoanReturn error presentation");
            // Source shows error58 and keeps scanning. Club offers in a genuine
            // coach takeover must not resolve against spare club record4.
            hooks.program_error(g, 0x3a);
            continue;
        }
        anchor_player=player;anchor_slot=int(slot);break;
    }

    if(anchor_slot>=0) {
        const auto range=squad_range(native_team);
        const int physical_row=anchor_slot-int(range.first);
        result=process_pending_batch(g,anchor_player,anchor_slot,manager,physical_row,false,-1,
                                     game,rand,hooks,report,takeover?&owner:nullptr);
        if(report.unsupported || !result.completed)return result;
    } else {
        result.completed=true; // Source still reaches the end-of-loop cleanup.
    }
    if(request.manager_position==count-1) {
        for(int raw=0;raw<4;++raw) {
            g.squad_slots.at(kPoolSlots+size_t(raw+40))=0x0fff;
            g.memory.set32(kOffers+uint32_t(raw)*4,0xffffffffu);
        }
        g.memory.set8(kDeskPosition,uint8_t(count));
    }
    return result;
}

PendingOfferResult process_national_nomination_pending_after_desk(GameState& g,
    const PendingOfferDeskRequest& request, GameRandom& game, WatcomRandom& rand,
    const PendingOfferHooks& hooks, MinuteReport& report) {
    PendingOfferResult result;
    if (report.unsupported) return result;
    const auto fail = [&](const char* why) -> PendingOfferResult {
        report.fail(why);
        return result;
    };
    if (request.runtime_mode != PendingOfferDeskRuntimeMode::normal_interactive)
        return fail("AdvanceTurn: unsupported or unspecified national desk runtime mode");

    constexpr uint32_t kCalendar = ::bmh::layout::CompetitionCalendar, kCompetition = ::bmh::layout::SavedCompetition;
    constexpr uint32_t kCoachActive = ::bmh::layout::CoachActive, kOriginalCount = ::bmh::layout::OriginalManagerCount;
    constexpr uint32_t kManagerCount = ::bmh::layout::ManagerCount, kOrder = ::bmh::layout::NextDeskManager;
    constexpr uint32_t kPosition = ::bmh::layout::DeskPosition, kActive = ::bmh::layout::ActiveManager, kOffers = ::bmh::layout::OfferSlots;
    if (!g.memory.has(kCalendar + uint32_t(g.season_day()) * 2 + 1) ||
        !g.memory.has(kCompetition + 3) || !g.memory.has(kCoachActive) ||
        !g.memory.has(kOriginalCount) || !g.memory.has(kManagerCount) ||
        !g.memory.has(kOrder + 1) || !g.memory.has(kPosition) || !g.memory.has(kActive) ||
        !g.memory.has(kOffers + 15) || kPoolSlots + 44 > g.squad_slots.size())
        return fail("AdvanceTurn: missing native national desk state");

    const uint16_t calendar_entry = g.memory.u16(kCalendar + uint32_t(g.season_day()) * 2);
    const int original_count = g.memory.u8(kOriginalCount);
    const int restored_owner = g.memory.u16(kOrder);
    if (request.manager_count != 1 || request.manager_position != 0 ||
        g.memory.u8(kManagerCount) != 1 || g.manager_teams.size() != 1 ||
        g.manager_teams[0] != kNoTeam || g.memory.u8(kCoachActive) == 0 ||
        g.memory.u32(kCompetition) != 0 || calendar_entry != 0x4006 ||
        day_competitions(g, calendar_entry) != 0x800000 ||
        original_count < 1 || original_count > 4 || restored_owner >= original_count ||
        g.memory.u8(kPosition) != 0 || g.memory.u8(kActive) != 0)
        return fail("AdvanceTurn: national nomination desk does not match the saved Source context");

    // Nomination uses the same post-return AdvanceTurn/DispatchClubPlayerPendingOffers sequence. Its
    // calendar/competition identity is checked above before the shared scan can
    // claim ActiveManager, invoke a presenter or consume either generator.
    return process_natural_pending_offers_after_desk(g,request,game,rand,hooks,report);
}

bool accept_transfer_bid_immediately(GameState& g, int entry, int bid, WatcomRandom& rand,
                                     MinuteReport& report, TransferAllocatorContext allocator) {
    if (entry < 0 || entry >= 40 || bid < 0 || bid >= 4) return false;
    const uint32_t list = kTransferList + uint32_t(entry) * kListEntry;
    if ((g.squad_slots.at(kPoolSlots + size_t(entry)) & 0xfff) == 0xfff ||
        g.memory.u16(list + uint32_t(bid) * 2) == 0xffff) return false;
    execute_transfer(g, entry, list, bid, rand, report, allocator);
    if (report.unsupported) return false;
    for (int b = 0; b < 4; ++b) g.memory.set16(list + uint32_t(b) * 2, 0xffff);
    g.squad_slots.at(kPoolSlots + size_t(entry)) = 0x0fff;
    return true;
}

void transfer_list_countdown(GameState& g, GameRandom& game, WatcomRandom& rand, MinuteReport& report, bool all,
                             TransferAllocatorContext allocator) {
    StaticMemory& mem = g.memory;
    for (int k = 0; k < 40; k++) {
        const int player = g.squad_slots.at(kPoolSlots + size_t(k)) & 0xFFF;
        if (player == 0xFFF) continue;
        const uint32_t e = kTransferList + uint32_t(k) * kListEntry;
        const uint8_t left = mem.u8(e + 0x56);
        if (left == 0 || left > 9) {
            for (int b = 0; b < 4; b++) mem.set16(e + uint32_t(b) * 2, 0xffff);
            g.squad_slots.at(kPoolSlots + size_t(k)) = 0x0FFF;
            continue;
        }
        mem.set8(e + 0x56, uint8_t(left - 1));
        const int seller = mem.u16(e + 8) & 0x7FFF;  // club of the player (0xFFF: none)
        if (all) {
            mem.set8(e + 0x56, 0);  // TransferListCountdown(1) at the season end: every list is settled now
        } else {
            if (left - 1 == 1) {
                // TransferListCountdown's one-week notice is a system modal (SystemIdentity(4))
                // only when a human seller or any human bidder participates.
                // It observes the countdown and raw offer rows; it consumes no RNG
                // and cannot change the later expiry decision.
                const auto human_participant = [&](int club) {
                    // FindManagerForClub also recognizes the coach's temporarily saved
                    // club as owner 4 while manager 0 runs the national team.
                    return g.manager_of(club) >= 0 || (!g.manager_teams.empty() &&
                        g.manager_teams[0] == kNoTeam && mem.has(::bmh::layout::DeskActions_NationalOpponentManager_RuntimeField) &&
                        int(mem.u16(::bmh::layout::CupDraw_FormatError_RuntimeField3)) == club);
                };
                bool human_involved = human_participant(seller);
                for (int bid = 0; bid < 4 && !human_involved; ++bid) {
                    const int buyer = int16_t(mem.u16(e + uint32_t(bid) * 2));
                    if (buyer != -1) human_involved = human_participant(buyer);
                }
                if (human_involved && g.transfer_decision_observer) {
                    TransferDecisionEvent event;
                    event.kind = TransferDecisionEventKind::last_week;
                    event.offers = project_transfer_offer_list(g, k);
                    event.viewer_manager = -1; // Source SystemIdentity(4), system identity
                    event.result_text_address = ::bmh::layout::PendingOffer_TransferLastWeekDialog_ExecutableField;
                    g.transfer_decision_observer(event);
                }
                continue;
            }
            if (left - 1 != 0) continue;
        }
        if (int16_t(mem.u16(e)) == -1) {
            // no bid at all: the player leaves the list, a human seller is told
            if (const int m = g.manager_of(seller); m >= 0)
                post_composed(g, m, message_text(g, ::bmh::layout::Weekly_TransferListCountdown_TextPointer) + g.player_name(player) + message_text(g, ::bmh::layout::Weekly_TransferListCountdown_TextPointer2));
            for (int b = 0; b < 4; b++) mem.set16(e + uint32_t(b) * 2, 0xffff);
            g.squad_slots.at(kPoolSlots + size_t(k)) = 0x0FFF;
            continue;
        }
        if (!decide_bids(g, k, game, rand, report, allocator)) return;
    }
}

void weekly_statistics(GameState& g, int manager) {
    Record rec = record(g, manager);
    uint8_t week = rec.b(0x285);
    auto shift = [&](size_t to, size_t from, size_t n) {
        // Int16T: memmove from -> to
        std::vector<uint8_t> tmp(g.manager_records.begin() + rec.base + from, g.manager_records.begin() + rec.base + from + n);
        std::copy(tmp.begin(), tmp.end(), g.manager_records.begin() + rec.base + to);
    };
    if (week < 0x34) {
        rec.b(0x285) = uint8_t(week + 1);
    } else {
        shift(0x3d8, 0x3dc, 0xcc);
        shift(0x17a, 0x17b, 0x33);
        shift(0x648, 0x64c, 0xcc);
        shift(0x4a8, 0x4ac, 0xcc);
        shift(0x578, 0x57c, 0xcc);
        week = 0x33;
    }
    // HighscoreScore(0x1e, 0, 0xff): account, deposits, shares and bonds
    int32_t assets = rec.i32(0x318);
    for (int k = 0; k < 6; k++)
        assets += int32_t(g.memory.u16(::bmh::layout::Advertising_OriginalShareAdjustment_RuntimeField + uint32_t(manager) * 2 + uint32_t(k) * 0x1c) *
                          g.memory.u32(::bmh::layout::FundBlock + uint32_t(k) * 0x1c));
    for (int k = 0; k < 12; k++)
        assets += int32_t(uint32_t(g.stocks.at(size_t(k) * 0x5c + 0x50 + size_t(manager) * 2) | g.stocks.at(size_t(k) * 0x5c + 0x51 + size_t(manager) * 2) << 8) *
                          uint32_t(g.stocks.at(size_t(k) * 0x5c + 0x4e) | g.stocks.at(size_t(k) * 0x5c + 0x4f) << 8));  // shares (block StockExchange)
    for (int b = 0; b < 5; b++) assets -= int32_t(g.memory.u32(::bmh::layout::Analytics_SourceAssetMaskE_RuntimeField + uint32_t(manager) * 0x78 + uint32_t(b) * 0x18));  // credits
    rec.set32(0x3d8 + size_t(week) * 4, uint32_t(assets));
    int second = 0;
    rec.b(0x17a + week) = uint8_t(facility_rating(g, manager, &second) * 2);
    int value = 0;  // BMHExeXa
    for (int i = 0; i < 0xe; i++) value += kFacilityValue[i];
    for (int i = 0xe; i < 0x16; i++) value += (rec.b(0x2e + size_t(i)) & 7) * kFacilityValue[i];
    rec.set32(0x648 + size_t(week) * 4, uint32_t(value));
    int32_t sum = 0;
    for (int i = 0; i < 0x11; i++) sum += rec.i32(0xa34 + size_t(i) * 0xc);
    rec.set32(0x4a8 + size_t(week) * 4, uint32_t(sum));
    rec.set32(0x578 + size_t(week) * 4, uint32_t(rec.i32(0x314)));
}

void weekly_processing(GameState& g, GameRandom& game, WatcomRandom& rand, MinuteReport& report,
                       TransferAllocatorContext allocator) {
    const int managers = int(g.manager_teams.size());
    // (BMH_WEEKLY: the draw counts after each step, for comparing with a recording)
    auto tr = [&](const char* what) {
        if (std::getenv("BMH_WEEKLY")) std::printf("  [weekly %s: game %llu, Watcom %llu]\n", what, (unsigned long long)game.calls(), (unsigned long long)rand.calls());
    };
    tr("start");
    for (int m = 0; m < managers && !report.unsupported; m++) {
        manager_offers(g, m, rand, report);
        tr("offers");
        if (report.unsupported) return;
        facility_ageing(g, m, game, rand, report);
        tr("ageing");
        facility_damage(g, m, game, rand);
        tr("damage");
        if (g.memory.u8(::bmh::layout::Savestate_Layout_ExecutableField) % 3 == 0) advertising_offers(g, m, rand, report);
        tr("advertising");
    }
    transfer_list_countdown(g, game, rand, report, false, allocator);
    tr("list countdown");
    if (report.unsupported) return;
    // new bids for listed players
    for (int k = 0; k < 40; k++) {
        if (rand.range(0, 1) != 0) continue;
        const uint32_t e = kTransferList + uint32_t(k) * kListEntry;
        const size_t slot = kPoolSlots + size_t(k);
        if (g.squad_slots.at(slot) == 0x0FFF || int16_t(g.memory.u16(e + 6)) != -1) continue;
        if (rand.range(0, 1) == 0) continue;
        do {
            transfer_bid(g, int(slot), e, false, -1, game, rand, report);
            if (report.unsupported) return;
        } while (rand.range(0, 2) == 0);
    }
    tr("bids");
    // every player: injuries in training, players out coming back
    const bool quiet = g.memory.u8(::bmh::layout::GameMode) == 1;
    for (size_t slot = 0; slot < kPoolSlots; slot++) {
        const int player = g.squad_slots.at(slot) & 0x0FFF;
        if (player == 0x0FFF) continue;
        Player& p = g.players.at(size_t(player));
        const uint32_t out = p.w35c >> 27;
        const uint32_t w364 = p.w364;
        int injury = int((w364 >> 16) & 0x1F);
        if (!quiet && game.range(0, 0xb4 - g.level * 2) == 0 && injury == 0 && out == 0) {
            if (slot >= 0x870 && rand.range(0, 1) == 0) continue;
            int type = rand.range(0, 0x1e);
            const int team = slot_team(int(slot));
            const int manager = g.manager_of(team);
            type = injure_player(g, int(slot), type, false, manager, rand);  // (unless MatchEventsDisabled, off)
            if (manager >= 0) {
                // "Ihr Spieler X faellt ... aus. Der Grund: <injury>."
                post_composed(g, manager, message_text(g, ::bmh::layout::Weekly_WeeklyProcessing_TextPointer2) + g.player_name(player) +
                                              message_text(g, type < 6 ? ::bmh::layout::Weekly_WeeklyProcessing_TextPointer3 : ::bmh::layout::Weekly_WeeklyProcessing_TextPointer4) + message_text(g, ::bmh::layout::Weekly_WeeklyProcessing_TextPointer5) +
                                              message_text(g, ::bmh::layout::Weekly_WeeklyProcessing_TextPointer + uint32_t(type) * 4) + ".");
            }
            continue;
        }
        if (out == 0 || out == 0x1F) continue;
        if (injury > 0x18 && rand.range(0, 0x25) == 0) {
            // the career is over
            p.w35c = (p.w35c & 0x07FFFFFFu) | 0xF8000000u;
            p.w364 |= 0x1Fu << 16;
            if (const int m = g.manager_of(slot_team(int(slot))); m >= 0)
                post_composed(g, m, message_text(g, ::bmh::layout::Weekly_WeeklyProcessing_TextPointer2) + g.player_name(player) + message_text(g, ::bmh::layout::Weekly_WeeklyProcessing_TextPointer6) + ".");
            continue;
        }
        const bool last = injury == 1;
        const int team = slot_team(int(slot));
        const int manager = g.manager_of(team);
        int odds = 4;
        if (manager >= 0) odds = 6 - record(g, manager).i16(0x8ae) / 0x113;
        if (injury != 0 && injury != 0x1F) {
            if (injury < 4 && rand.range(0, int16_t(odds)) == 0) injury = 1;
            p.w364 = (w364 & 0xFFE0FFFFu) | uint32_t(injury - 1) << 16;
        }
        if (last || (rand.range(0, int16_t(odds)) == 0 && injury == 0)) {
            p.w35c &= 0x07FFFFFFu;
            // Int2(team, 100); human teams: the assistant's line-up (option byte 0)
            try {
                auto_lineup(g, team, 100);
            } catch (const std::exception& ex) {
                report.fail(std::string("line-up after an injury: ") + ex.what());
                return;
            }
        }
    }
    tr("injuries and line-ups");
    // per human manager
    for (int m = 0; m < managers; m++) {
        Record rec = record(g, m);
        const int debt = rec.b(0x174);
        if (debt != 0) book_money(g, m, -debt * 1000, true);
        const SquadRange range = squad_range(int16_t(rec.u16(0x290)));
        if (rec.b(0x287) != 0 && --rec.b(0x287) == 0) {
            // the bill of the training camp
            int16_t mood = 0;
            for (int i = 0; i < 10; i++) mood = int16_t(mood + (g.manager_ratings.at(size_t(m) * 0x3c + size_t(i)) - 100));
            mood = int16_t(mood / 10 + 0x46);
            const int a = rand.range(0, 8);
            const int b = rand.range(0, 8);
            mood = int16_t(std::clamp(int(int16_t(mood + a - b)), 0x14, 0xb4));
            int32_t tmp = mood;
            for (int i = 10; i < 0xe; i++)
                if (rec.b(size_t(i)) != 0) tmp += rand.range(5, 0xc);
            const uint32_t bill = uint32_t(int16_t(tmp) * rec.i32(0x850)) / 100;
            book_money(g, m, -int32_t(bill), true);
            rec.set32(0x850, 0);
        }
        for (int s = 0; s < 24; s++) {
            const size_t at = size_t(m) * kBoard + size_t(s) * kBoardSlot;
            if (g.squad_slots.at(range.first + size_t(s)) == 0x0FFF) continue;
            if ((g.tactics.at(at + 0x4f) & 0x80) && rand.range(0, 6) == 0) g.tactics.at(at + 0x4f) &= 0x7F;
            if (g.tactics.at(at + 0xc) == 2) g.tactics.at(at + 0xb) &= 0x3F;
            // SalaryInjuredPlayerOnBoardPaidOn: the salary of an injured player on the board is paid on
            const int player = g.squad_slots.at(range.first + size_t(s)) & 0x0FFF;
            if ((g.players.at(size_t(player)).w35c >> 27) != 0 && g.tactics.at(at + 0x43) != 0) {
                const int32_t base = int32_t(g.tactics.at(at + 0x50) | g.tactics.at(at + 0x51) << 8 |
                                             g.tactics.at(at + 0x52) << 16 | uint32_t(g.tactics.at(at + 0x53)) << 24);
                const uint32_t pay = uint32_t(base * int32_t(((int8_t(g.tactics.at(at + 0x43)) >> 6) & 3) * 0x32 + 0x32)) / 100;
                book_money(g, m, -int32_t(pay), true);
            }
            const uint8_t mood = g.tactics.at(at + 0xc);
            if (mood % 0xc <= 7 && !(g.tactics.at(at + 0xb) & 0x20) && mood % 0xc != 0 && mood <= 0xf &&
                !(g.tactics.at(at + 0x4f) & 0x80) && !quiet) {
                const int16_t rating = int16_t(rec.i16(0x8aa) + (rec.i16(0x8ac) >> 2));
                if (rand.range(0, 500 - (rating - 0x2a8) / 2) == 0) {
                    // InstallDefaultHooks(slot | 0x40): the player asks to talk about his contract.
                    // Before the contracts screen opens he names the term he wants,
                    // WatcomRandom(years left, 4), one more if that is no change (below 4),
                    // and the request is shown (SystemIdentity). The negotiation itself is UI:
                    // ported is a manager who leaves the screen without an offer, which
                    // changes nothing else.
                    const int years = mood / 0xc;
                    int wanted = rand.range(years, 4);
                    if (wanted == years && wanted < 4) wanted++;
                    (void)wanted;
                }
            }
        }
        weekly_statistics(g, m);
    }
}

}  // namespace bmh

namespace bmh {

bool pre_match_offer(GameState& g, int manager, GameRandom& game, WatcomRandom& rand, MinuteReport& report) {
    StaticMemory& mem = g.memory;
    if (mem.u32(::bmh::layout::CompetitionSelection) == 2 || mem.u8(::bmh::layout::GameMode) == 1) return false;
    Record rec = record(g, manager);
    const int chance = rand.range(0, 0x46 - rec.i16(0x8aa) / 0x32);
    if (std::getenv("BMH_TRACE_OFFER"))
        std::printf("  [pre-match offer, manager %d: W(0, %d) = %d, slots %08x %08x %08x %08x]\n", manager,
                    0x46 - rec.i16(0x8aa) / 0x32, chance, mem.u32(::bmh::layout::OfferSlots), mem.u32(::bmh::layout::Weekly_PreMatchOffer_RuntimeField), mem.u32(::bmh::layout::Weekly_PreMatchOffer_RuntimeField2), mem.u32(::bmh::layout::Weekly_PreMatchOffer_RuntimeField3));
    if (chance != 0) return false;  // (unless WeeklyRuntimeField8)
    const int team = int16_t(rec.u16(0x290));
    if (team == kNoTeam) return false;
    // RowWhose: a free offer slot k (OfferSlots, 4 entries of -1 when free); the
    // offer uses the special transfer list entry 40 + k
    int k = 0;
    while (k < 4 && mem.u32(::bmh::layout::OfferSlots + uint32_t(k) * 4) != 0xFFFFFFFFu) k++;
    if (k == 4) return false;
    const uint32_t e = kTransferList + uint32_t(0x28 + k) * kListEntry;
    const SquadRange range = squad_range(team);
    // a random fit player of the squad who is not bought for later (up to 100 draws hit an
    // empty slot; after that the empty slot is taken, as in the original)
    int slot = 0, player = 0, empty = 0;
    for (;;) {
        slot = rand.range(int(range.first), int(range.first + range.count) - 1);
        const uint16_t v = g.squad_slots.at(size_t(slot));
        if (v == 0x0FFF && empty++ < 100) continue;
        // Deliberate deviation: after 100 empty draws the original goes on with the empty
        // slot and reads "player 0xFFF" beyond its player table. That happens only with a
        // squad that is nearly empty (a club whose contracts all ran out); there is no
        // player to make an offer for, so no offer comes (the draws so far stay as they were).
        if (v == 0x0FFF) return false;
        player = v & 0x0FFF;
        if ((g.players.at(size_t(player)).w35c >> 27) != 0) continue;
        bool bought = false;
        for (int b = 0; b < 200 && !bought; b++) {
            const uint32_t x = mem.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + uint32_t(b) * 4);
            bought = x != 0 && int(x & 0xFFF) == player;
        }
        if (!bought) break;
    }
    // Range2(slot, 0, W(90, 130), 100, 1, k): the list entry of the offer - the club,
    // no bids, asking/demand percentages, market value and salary, the player in pool slot
    // 0x28 + k
    const int percent = rand.range(0x5a, 0x82);
    mem.set16(e + 8, uint16_t(slot_team(slot)));
    for (int b = 0; b < 4; b++) mem.set16(e + uint32_t(b) * 2, 0xFFFF);
    mem.set8(e + 0x54, uint8_t(percent));
    mem.set8(e + 0x55, 100);
    uint32_t fee = 0, salary = 0;
    market_value(g, slot, 0, percent, fee, salary, 1, 0, rand);
    mem.set32(e + 0x2c, fee);
    mem.set32(e + 0x30, salary);
    g.squad_slots.at(kPoolSlots + 0x28 + size_t(k)) = uint16_t(player);
    for (int b = 0; b < 4; b++) mem.set16(e + uint32_t(b) * 2, 0xFFFF);
    // half of the offers are about the salary only (a club tries to lure him away)
    const bool lure = rand.range(0, 1) != 0;
    if (lure) mem.set16(e + 8, 0xffe);
    const int board_slot = slot - int(range.first);
    const size_t board = size_t(manager) * kBoard + size_t(board_slot) * kBoardSlot;
    const uint32_t saved_mailbox_recipient_mask = mem.has(::bmh::layout::MailboxRecipientMask) ? mem.u32(::bmh::layout::MailboxRecipientMask) : 0;  // (restored below)
    transfer_bid(g, slot, e, true, g.tactics.at(board + 0xc) / 0xc + 1, game, rand, report);
    if (report.unsupported || mem.u16(e) == 0xFFFF) return false;
    const int bidder = mem.u16(e);
    std::string text;
    bool warned = false;
    if (lure) {
        // what he earns now: the salary on the board plus the bonus he can expect
        // (ContractOffer with the fitness correction)
        const uint32_t earns = expected_bonus(g, manager, 0xff, board_slot, player, true) +
                               uint32_t(g.tactics.at(board + 0x50) | g.tactics.at(board + 0x51) << 8 |
                                        g.tactics.at(board + 0x52) << 16 | uint32_t(g.tactics.at(board + 0x53)) << 24);
        const uint32_t r = uint32_t(rand.range(0x62, 0x78));
        if (r * earns / 100 < mem.u32(e + 0xc)) {
            warned = true;
            // "VORSICHT! <club> bietet <player> ein Gehalt von X EU. (momentan Y EU). Abloesesumme: Z EU."
            text = message_text(g, ::bmh::layout::Weekly_PreMatchOffer_TextPointer) + g.club_name(bidder) + message_text(g, ::bmh::layout::Weekly_PreMatchOffer_TextPointer2) +
                   g.player_name(g.squad_slots.at(size_t(slot)) & 0xFFF) + message_text(g, ::bmh::layout::Weekly_PreMatchOffer_TextPointer3) +
                   format_amount(g, int32_t(mem.u32(e + 0xc))) + message_text(g, ::bmh::layout::Weekly_PreMatchOffer_TextPointer4) +
                   format_amount(g, int32_t(uint32_t(g.tactics.at(board + 0x50) | g.tactics.at(board + 0x51) << 8 |
                                                     g.tactics.at(board + 0x52) << 16 |
                                                     uint32_t(g.tactics.at(board + 0x53)) << 24))) +
                   message_text(g, ::bmh::layout::Weekly_PreMatchOffer_TextPointer5) + format_amount(g, int32_t(mem.u32(e + 0x1c))) + ".";  // (WeeklyRuntimeField9)
        }
    }
    if (!warned) {
        // (ClubName2 with 0x4000 empties the buffer: the text WeeklyRuntimeField10 is lost)
        const std::string verb = message_text(g, mem.u8(e + 0x34) > 0x7f ? ::bmh::layout::Weekly_PreMatchOffer_TextPointer8 : ::bmh::layout::Weekly_PreMatchOffer_TextPointer9);
        const std::string amount = format_amount(g, int32_t(mem.u32(e + 0x1c)));
        text = g.club_name(bidder) + message_text(g, ::bmh::layout::Weekly_PreMatchOffer_TextPointer6) + g.player_name(g.squad_slots.at(size_t(slot)) & 0xFFF) +
               message_text(g, ::bmh::layout::Weekly_PreMatchOffer_TextPointer7) +
               // German: "<amount> KAUFEN" (with WeeklyRuntimeField11 " "); the English MessageText puts
               // the verb first: "BUY him for <amount>"
               (kEnglishCompetitions ? verb + amount : amount + " " + verb) + message_text(g, ::bmh::layout::Weekly_PreMatchOffer_TextPointer10);
    }
    // SystemIdentity shows it, PostMessage posts it
    post_composed(g, manager, text);
    if (mem.has(::bmh::layout::MailboxRecipientMask)) mem.set32(::bmh::layout::MailboxRecipientMask, saved_mailbox_recipient_mask);
    mem.set16(e + 8, uint16_t(team | 0x8000));
    mem.set32(::bmh::layout::OfferSlots + uint32_t(k) * 4, uint32_t(player) * 0x400 + uint32_t(manager) | (warned ? 0x80000000u : 0));
    return true;
}

}  // namespace bmh
