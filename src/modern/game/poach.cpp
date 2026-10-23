// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "modern/game/modern_state.hpp"
#include "modern/game/poach.hpp"

#include <algorithm>

#include "game/messages.hpp"
#include "modern/game/month.hpp"
#include "modern/game/weekly.hpp"

namespace bmh::modern {

namespace {

constexpr size_t kRecordSize = 0xb0c;
constexpr size_t kAttempts = 0x8b6;           // five player ids, 0xffff free
constexpr uint32_t kTransferList = ::bmh::layout::EditorList;   // 44 rows of 0x58 bytes
constexpr uint32_t kListRow = 0x58;
constexpr uint32_t kOfferSlots = ::bmh::layout::OfferSlots;     // OfferSlots: 4 words, -1 free
// Attempt: how many players of each position a club wants (MonthRuntimeField4 / MonthRuntimeField5)
constexpr int8_t kWantedBig[4] = {2, 6, 7, 5}, kWantedSmall[4] = {2, 4, 5, 4};

uint16_t attempt(const ModernState& g, int m, int i) {
    const size_t at = size_t(m) * kRecordSize + kAttempts + size_t(i) * 2;
    return uint16_t(g.manager_records.at(at) | g.manager_records.at(at + 1) << 8);
}

// PoachAttemptsLeft(player, club): the asking percentage of the club for the player, or -1 with
// the reason in `messages`. A club of a human manager asks 100 %.
int club_asking_percent(const ModernState& g, int player, int club, std::vector<std::string>& messages, WatcomRandom& rand) {
    if (g.manager_of(club) >= 0) return 100;
    const Player& p = g.players.at(size_t(player));
    const int position = int(p.w364 & 3);
    const int strength = p.strength(true);  // Strength(w35c, 1)
    const int8_t* wanted = club < german_clubs() ? kWantedBig : kWantedSmall;
    // SquadCount(club, 0): the fit players
    if (modern::squad_count(g, club, 0) < (club < german_clubs() ? 3 : 0) + 0xe) {
        messages.push_back(message_text(g, ::bmh::layout::Poach_ClubAskingPercent_TextPointer) + g.club_name(club) + message_text(g, ::bmh::layout::Poach_ClubAskingPercent_TextPointer2));
        return -1;
    }
    int sum[4], count[4];
    for (int k = 0; k < 4; k++) {
        // QuoteNationalFriendlyMatch with 0x607 + the position bit: sum of the fit players and their number
        const TeamAverage a = team_average(g, club, uint16_t((1 << (k + 4)) + 0x607));
        sum[k] = int16_t(a.value);
        count[k] = int8_t(a.players);
    }
    const int short_of = int16_t(wanted[position] - count[position]);  // local_24
    if (short_of >= 3 || (short_of == 0 && position == 0)) {
        // the club needs him (a club always keeps its second goalkeeper)
        messages.push_back(g.club_name(club) + message_text(g, ::bmh::layout::Poach_ClubAskingPercent_TextPointer3));
        return -1;
    }
    int percent = short_of < 0 ? short_of * 0x18 + 0x8c : short_of * 16 + 0x82;  // local_34
    if (percent < 0x5a) percent = 0x5a;
    int averages = 0;
    for (int k = 0; k < 4; k++) averages = int16_t(averages + sum[k] / count[k]);
    const int team = averages / 4;
    int relative = team != 0 ? int16_t(strength * 100 / team) : 100;  // local_30
    if (relative < 0x65) {
        percent -= std::clamp(100 - relative, 0, 0x14);  // ClampInteger
    } else {
        const int over = int16_t(relative - 100) >> 1;
        percent = int16_t(percent + int16_t(over * over));
    }
    if (short_of != 0 && percent >= 0x100) {
        messages.push_back(message_text(g, ::bmh::layout::Poach_ClubAskingPercent_TextPointer4));  // FINANCIAL FAIRPLAY
        return -1;
    }
    if (percent > 0xbe) percent = 0xbe;
    return uint8_t(rand.range(0xc, 0x14) + percent);
}

}  // namespace

int poach_attempts_left(const ModernState& g, int manager) {
    int left = 0;
    for (int i = 0; i < 5; i++)
        if (attempt(g, manager, i) == 0xffff) left++;
    return left;
}

PoachStart start_poach(ModernState& g, int manager, int squad_slot, WatcomRandom& rand) {
    PoachStart out;
    out.manager = manager;
    out.squad_slot = squad_slot;
    const size_t rec = size_t(manager) * kRecordSize;
    if (attempt(g, manager, 4) != 0xffff) {
        out.status = PoachStatus::attempts_used;
        out.messages.push_back(message_text(g, ::bmh::layout::CareerSession_DispatchPoachUi_TextPointer2));
        return out;
    }
    if (g.manager_records.at(rec + 0x220) >> 5 == 1) {  // PushBack
        out.status = PoachStatus::transfer_ban;
        out.messages.push_back(message_text(g, ::bmh::layout::CareerSession_DispatchPoachUi_TextPointer));
        return out;
    }
    if (squad_slot < 0 || size_t(squad_slot) >= transfer_list_slot() || g.squad_slots.at(size_t(squad_slot)) == 0xfff) return out;
    out.player = g.squad_slots.at(size_t(squad_slot)) & 0xfff;
    out.club = modern::slot_team(squad_slot);  // SlotTeam
    if (out.club < 0 || out.club == g.manager_teams.at(size_t(manager))) return out;
    // BoughtEntry: a player on loan (table Career_RemoveJoinLoans_RuntimeField)
    for (int k = 0; k < 200; k++) {
        const uint32_t e = g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + uint32_t(k) * 4);
        if (e != 0 && int(e & 0xfff) == out.player) {
            out.status = PoachStatus::on_loan;
            out.messages.push_back(message_text(g, ::bmh::layout::Poach_StartPoach_TextPointer));
            return out;
        }
    }
    for (int i = 0; i < 5; i++)
        if (attempt(g, manager, i) == out.player) {
            out.status = PoachStatus::tried_before;
            out.messages.push_back(message_text(g, ::bmh::layout::Poach_StartPoach_TextPointer2));
            return out;
        }
    // PlayerOnTransferListFirstPoolSlots: the player is on the transfer list (first 44 pool slots)
    for (size_t s = transfer_list_slot(); s < transfer_list_slot() + 0x2c; s++)
        if ((g.squad_slots.at(s) & 0xfff) == out.player) {
            out.status = PoachStatus::on_transfer_list;
            out.messages.push_back(message_text(g, ::bmh::layout::Poach_StartPoach_TextPointer3));
            return out;
        }
    const int percent = club_asking_percent(g, out.player, out.club, out.messages, rand);
    if (percent < 0) {
        out.status = PoachStatus::club_refuses;
        out.messages.push_back(message_text(g, ::bmh::layout::Poach_StartPoach_TextPointer5));
        return out;
    }
    out.asking_percent = percent;
    // RowWhose: a special row whose OfferSlots word is free
    int k = 0;
    while (k < 4 && g.memory.u32(kOfferSlots + uint32_t(k) * 4) != 0xffffffffu) k++;
    if (k == 4) {
        out.status = PoachStatus::market_busy;
        out.messages.push_back(message_text(g, ::bmh::layout::Poach_StartPoach_TextPointer4));
        modern::finish_poach(g, out);
        return out;
    }
    out.offer_row = k;
    const int demand = rand.range(0x50, 100);
    // Range2(slot, 0, percent, demand, 0, k): the row like a market row of the club.
    // (Its salary floor for a club of a human manager searches the buyer's own squad for
    // the player and so never applies; the 0x8000 flag of the club word is kept.)
    const uint32_t row = kTransferList + uint32_t(0x28 + k) * kListRow;
    uint16_t club_word = uint16_t(out.club);
    if (g.manager_of(out.club) >= 0) club_word |= 0x8000;
    g.memory.set16(row + 8, club_word);
    for (int b = 0; b < 4; b++) g.memory.set16(row + uint32_t(b) * 2, 0xffff);
    g.memory.set8(row + 0x54, uint8_t(percent));
    g.memory.set8(row + 0x55, uint8_t(demand));
    uint32_t fee = 0, salary = 0;
    modern::market_value(g, squad_slot, 0, uint8_t(percent), fee, salary, 1, 0, rand);
    g.memory.set32(row + 0x2c, fee);
    g.memory.set32(row + 0x30, salary);
    g.squad_slots.at(transfer_list_slot() + 0x28 + size_t(k)) = uint16_t(out.player);
    out.status = PoachStatus::ready;
    return out;
}

void finish_poach(ModernState& g, const PoachStart& poach) {
    if (poach.offer_row >= 0) {
        g.squad_slots.at(transfer_list_slot() + 0x28 + size_t(poach.offer_row)) = 0xfff;
        g.memory.set32(kOfferSlots + uint32_t(poach.offer_row) * 4, 0xffffffffu);
    }
    for (int i = 0; i < 5; i++)
        if (attempt(g, poach.manager, i) == 0xffff) {
            const size_t at = size_t(poach.manager) * kRecordSize + kAttempts + size_t(i) * 2;
            g.manager_records.at(at) = uint8_t(poach.player);
            g.manager_records.at(at + 1) = uint8_t(poach.player >> 8);
            break;
        }
}

}  // namespace bmh::modern
