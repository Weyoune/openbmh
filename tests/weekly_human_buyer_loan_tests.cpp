// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "formats/fbm.hpp"
#include "formats/savestate.hpp"
#include "formats/textd.hpp"
#include "game/desk_actions.hpp"
#include "game/save.hpp"
#include "game/transfer_allocator.hpp"
#include "game/weekly.hpp"

using namespace bmh;
namespace {
void check(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
void write32(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
    for (size_t n = 0; n < 4; ++n) bytes.at(at + n) = uint8_t(value >> (n * 8));
}
uint32_t read32(const std::vector<uint8_t>& bytes, size_t at) {
    uint32_t value = 0;
    for (size_t n = 0; n < 4; ++n) value |= uint32_t(bytes.at(at + n)) << (n * 8);
    return value;
}
uint32_t expected_market_fee(const GameState& g, int buyer_manager, int player, uint32_t fee) {
    const size_t rec = size_t(buyer_manager) * 0xb0c;
    const uint8_t subsidy = g.manager_records.at(rec + 0x170);
    const int position = int(g.players.at(size_t(player)).w364 & 3);
    if (fee > read32(g.manager_records, rec + 0x314) || subsidy == 0 || (subsidy & 3) != position)
        return fee;
    int league = g.manager_records.at(rec + 0x210);
    if (league > 2) --league;
    const uint32_t threshold = (((uint32_t(subsidy) >> 2) & 7u) * 50000u + 150000u) * uint32_t(4 - league);
    if (fee >= threshold) return fee;
    const uint32_t discount = (((uint32_t(subsidy) >> 5) + 3u) * fee * 10u) / 100u;
    return fee - discount;
}
bool contains(const GameState& g, int team, int player) {
    const auto range = squad_range(team);
    for (size_t n = 0; n < range.count; ++n)
        if ((g.squad_slots.at(range.first + n) & 0x0fff) == player) return true;
    return false;
}
struct WeekCase { GameState game; int entry = -1; int player = -1; };

WeekCase make_week_case(const TextDb& texts, const GameState& original, bool full_buyer = false, bool loan = true) {
    auto g = original;
    WatcomRandom listing_w(37);
    MinuteReport listing_report;
    const auto listed = list_manager_player(g, 0, 5, listing_w, listing_report);
    check(listed.status == TransferActionStatus::accepted && !listing_report.unsupported,
          "build ordinary human-seller week-list entry through source listing primitive");
    const int buyer = g.manager_teams.at(1);
    if (full_buyer) {
        const auto range = squad_range(buyer);
        for (size_t n = 0; n < range.count; ++n)
            if (g.squad_slots.at(range.first + n) == 0x0fff)
                g.squad_slots.at(range.first + n) = 0xffff;
    }
    const uint32_t list = ::bmh::layout::EditorList + uint32_t(listed.list_entry) * 0x58;
    g.memory.set16(list, uint16_t(buyer));
    g.memory.set32(list + 0x1c, 250000);
    g.memory.set32(list + 0x0c, 70000);
    g.memory.set8(list + 0x34, loan ? 0x8c : 12); // 12-month loan or ordinary 12-month purchase
    for (uint32_t n = 0; n < 7; ++n) g.memory.set8(list + 0x38 + n, uint8_t(n + 3));
    g.memory.set8(list + 0x56, 1); // accepted and settled on this week's countdown
    g.memory.set8(::bmh::layout::ActiveManager, 1); // current owner is the human buyer
    g.level = 1;
    write32(g.manager_records, 0xb0c + 0x314, 10000000);
    write32(g.manager_records, 0xb0c + 0x31c, 0);
    g.transfer_offer_selector = [](const GameState&, const TransferOfferListView& view) {
        return view.offers[0].present ? 0 : -1;
    };
    g.texts = &texts;
    return {std::move(g), listed.list_entry, listed.player};
}

void run_case(const TextDb& texts, const GameState& original, TransferAllocatorContext gate,
              bool full_buyer = false) {
    auto prepared = make_week_case(texts, original, full_buyer);
    auto& g = prepared.game;
    const int entry = prepared.entry;
    const int player = prepared.player;
    const uint32_t starting_balance = read32(g.manager_records, 0xb0c + 0x314);
    const uint32_t offer = ::bmh::layout::EditorList + uint32_t(entry) * 0x58;
    GameRandom game(73);
    WatcomRandom rand(0x13579);
    MinuteReport report;
    transfer_list_countdown(g, game, rand, report, false, gate);
    check(!report.unsupported, "general legacy routine path reaches shared loan settlement");
    bool in_buyer = false;
    const int buyer = g.manager_teams.at(1);
    const auto buyer_range = squad_range(buyer);
    for (size_t n = 0; n < buyer_range.count; ++n)
        if ((g.squad_slots.at(buyer_range.first + n) & 0x0fff) == player) in_buyer = true;
    if (gate == TransferAllocatorContext::zero) {
        int loan = -1;
        for (int n = 0; n < 200; ++n)
            if (g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + uint32_t(n) * 4) != 0 &&
                int(g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + uint32_t(n) * 4) & 0x0fff) == player) { loan = n; break; }
        check(in_buyer && loan >= 0 && g.memory.u8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField + uint32_t(loan)) == 12,
              "zero allocator books buyer board and loan return record");
    } else {
        bool ledger_written = false;
        for (int n = 0; n < 200; ++n)
            if (g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + uint32_t(n) * 4) != 0 &&
                int(g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + uint32_t(n) * 4) & 0x0fff) == player) ledger_written = true;
        check(!in_buyer && !ledger_written && contains(g, g.manager_teams.at(0), player),
              "blocked allocator charges before allocation but leaves seller squad and loan ledger untouched");
        if (full_buyer)
            check(read32(g.manager_records, 0xb0c + 0x314) < starting_balance,
                  "full human buyer is charged before the Source allocator-block result");
    }
    check(g.squad_slots.at(0xb70 + size_t(entry)) == 0x0fff && g.memory.u16(offer) == 0xffff,
          "weekly countdown completes and clears accepted list entry");
}

void run_ordinary_case(const TextDb& texts, const GameState& original, TransferAllocatorContext gate,
                       bool full_buyer = false) {
    auto prepared = make_week_case(texts, original, full_buyer, false);
    auto& g = prepared.game;
    const int entry = prepared.entry;
    const int player = prepared.player;
    const size_t buyer_record = 0xb0c;
    const size_t seller_record = 0;
    const uint32_t buyer_start = read32(g.manager_records, buyer_record + 0x314);
    const uint32_t seller_start = read32(g.manager_records, seller_record + 0x314);
    const uint32_t buyer_transfer_sum = read32(g.manager_records, buyer_record + 0x330);
    const uint32_t seller_transfer_sum = read32(g.manager_records, seller_record + 0x330);
    const uint32_t offer = ::bmh::layout::EditorList + uint32_t(entry) * 0x58;
    const uint32_t expected_fee = expected_market_fee(g, 1, player, g.memory.u32(offer + 0x1c));
    GameRandom game(73);
    WatcomRandom rand(0x13579);
    MinuteReport report;
    transfer_list_countdown(g, game, rand, report, false, gate);
    check(!report.unsupported, "ordinary human-buyer purchase reaches shared legacy routine/legacy routine settlement");
    const int buyer = g.manager_teams.at(1);
    const auto buyer_range = squad_range(buyer);
    size_t buyer_row = buyer_range.count;
    for (size_t n = 0; n < buyer_range.count; ++n)
        if ((g.squad_slots.at(buyer_range.first + n) & 0x0fff) == player) buyer_row = n;
    const bool in_buyer = buyer_row < buyer_range.count;
    const size_t board = size_t(1) * 0x840 + buyer_row * 0x58;
    bool loan_written = false;
    for (int n = 0; n < 200; ++n)
        if (g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + uint32_t(n) * 4) != 0 &&
            int(g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + uint32_t(n) * 4) & 0x0fff) == player) loan_written = true;
    if (gate == TransferAllocatorContext::zero) {
        check(in_buyer, "zero allocator moves ordinary purchase into buyer squad");
        check(!loan_written, "ordinary purchase does not write loan return ledger");
        check(g.tactics.at(board + 0x0c) == 12, "ordinary purchase stores its contract term on buyer board");
        check(uint32_t(buyer_start - read32(g.manager_records, buyer_record + 0x314)) == expected_fee &&
              uint32_t(read32(g.manager_records, seller_record + 0x314) - seller_start) == expected_fee,
              "successful purchase books exactly one source-adjusted buyer debit and seller credit");
        check(uint32_t(read32(g.manager_records, buyer_record + 0x330) - buyer_transfer_sum) == expected_fee &&
              uint32_t(seller_transfer_sum - read32(g.manager_records, seller_record + 0x330)) == expected_fee,
              "successful purchase updates each transfer total exactly once");
    } else {
        check(!in_buyer && !loan_written && contains(g, g.manager_teams.at(0), player),
              "blocked allocator leaves ordinary purchase ownership and loan ledger unchanged");
        check(uint32_t(buyer_start - read32(g.manager_records, buyer_record + 0x314)) == expected_fee &&
              uint32_t(read32(g.manager_records, seller_record + 0x314) - seller_start) == expected_fee,
              "blocked allocation retains exactly one source-adjusted buyer debit and seller credit");
        check(uint32_t(read32(g.manager_records, buyer_record + 0x330) - buyer_transfer_sum) == expected_fee &&
              uint32_t(seller_transfer_sum - read32(g.manager_records, seller_record + 0x330)) == expected_fee,
              "blocked allocation updates each transfer total exactly once");
    }
    check(g.squad_slots.at(0xb70 + size_t(entry)) == 0x0fff &&
          g.memory.u16(::bmh::layout::EditorList + uint32_t(entry) * 0x58) == 0xffff,
          "weekly countdown completes and clears settled ordinary purchase");
}
}

int main(int argc, char** argv) try {
    check(argc == 3, "usage: weekly_human_buyer_loan_tests DATA_DIR 2PNWK001.FBM");
    if (!*argv[1]) { std::puts("weekly human buyer loan: skipped (Original data not configured)"); return 77; }
    auto texts = load_textd(std::filesystem::path(argv[1]) / "TEXTD.BLK");
    const auto fbm = load_savegame(argv[2]);
    auto original = GameState::from_save(SaveState(fbm.stream)); original.texts = &texts;
    check(original.manager_teams.size() == 2 && original.manager_teams[0] == 1 &&
          original.manager_teams[1] == 54, "genuine 2PNWK001 human manager identities");
    run_case(texts, original, TransferAllocatorContext::zero);
    run_case(texts, original, TransferAllocatorContext::blocked);
    run_case(texts, original, TransferAllocatorContext::blocked, true);
    run_ordinary_case(texts, original, TransferAllocatorContext::zero);
    run_ordinary_case(texts, original, TransferAllocatorContext::blocked);
    run_ordinary_case(texts, original, TransferAllocatorContext::blocked, true);
    std::puts("PASS ordinary weekly human-buyer purchases and loans with zero/blocked allocator context");
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "weekly_human_buyer_loan_tests: %s\n", e.what()); return 1;
}
