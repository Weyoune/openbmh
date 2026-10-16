// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "game/desk_actions.hpp"
#include "formats/le.hpp"

using namespace bmh;

namespace {
void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
uint32_t u32(const std::vector<uint8_t>& v, size_t at) {
    return uint32_t(v.at(at)) | uint32_t(v.at(at + 1)) << 8 | uint32_t(v.at(at + 2)) << 16 |
           uint32_t(v.at(at + 3)) << 24;
}
void set32(std::vector<uint8_t>& v, size_t at, uint32_t value) {
    for (int i = 0; i < 4; ++i) v.at(at + size_t(i)) = uint8_t(value >> (8 * i));
}
GameState fixture(uint32_t cash) {
    GameState g;
    for (size_t r = 0; r < 8; ++r) {
        g.training_camp_base_prices[r] = int32_t(10000 + r * 5000);
        for (size_t q = 0; q < 4; ++q) g.training_camp_quality[r][q] = uint8_t((r + q) % 5 + 1);
    }
    g.level = 1;
    g.manager_teams = {0};
    g.manager_records.resize(0xb0c);
    g.squad_slots.assign(24, 0x0fff);
    g.team_morale.resize(1);
    set32(g.manager_records, 0x314, cash);
    return g;
}
}

int main() {
    {
        // Authored tables exercise loading independently of either game's content.
        std::array<uint8_t, 32> quality;
        std::array<int32_t, 8> prices;
        for (size_t i = 0; i < quality.size(); ++i) quality[i] = uint8_t((i * 3 + 2) % 5 + 1);
        for (size_t i = 0; i < prices.size(); ++i) prices[i] = int32_t(7000 + i * 30000);
        Bytes table(64);
        std::copy(quality.begin(), quality.end(), table.begin());
        for (size_t i = 0; i < prices.size(); ++i) set32(table, 32 + i * 4, uint32_t(prices[i]));
        auto exe = LeImage::from_bytes(::bmh::layout::DeskActions_ConfigureTrainingCampData_RuntimeField, table);
        auto g = fixture(200000);
        configure_training_camp_data(g, exe);
        check(training_camp_price(g, 0, 0) == 12000 && training_camp_price(g, 0, 3) == 102000,
              "loaded resort prices replace defaults without a language-specific price list");
        WatcomRandom rand(77);
        // Source team_average reports at least one player even for an empty squad.
        check(book_training_camp(g, 0, 3, rand) && u32(g.manager_records, 0x314) == 98000 &&
              u32(g.manager_records, 0x890) == 102000 && g.team_morale[0] == 12,
              "payment and morale effect use the same loaded resort content as the UI stars");
        check(rand.calls() == 0, "empty squad content test consumes no training draws");
        const auto old_prices = g.training_camp_base_prices;
        const auto old_quality = g.training_camp_quality;
        const uint8_t invalid = 0;
        exe.patch(::bmh::layout::DeskActions_ConfigureTrainingCampData_RuntimeField + 31, &invalid, 1);
        bool rejected = false;
        try { configure_training_camp_data(g, exe); } catch (const FormatError&) { rejected = true; }
        check(rejected && g.training_camp_base_prices == old_prices && g.training_camp_quality == old_quality,
              "invalid executable content cannot partially replace runtime resort tables");
    }
    {
        auto g = fixture(0);
        const auto before = g.manager_records;
        WatcomRandom rand(1234);
        check(set_training_camp_focus(g, 0, {0, 1, 14, 10}), "valid focus is accepted");
        check(g.manager_records[0xc1] == 0 && g.manager_records[0xc2] == 1 &&
              g.manager_records[0xc3] == 14 && g.manager_records[0xc4] == 10,
              "focus request writes the four original record bytes");
        check(rand.calls() == 0 && rand.seed() == 1234, "setting focus has no RNG access");
        const auto focus_state = g.manager_records;
        check(!set_training_camp_focus(g, 0, {0, 1, 16, 15}) &&
              !set_training_camp_focus(g, 0, {0, 1, 14, 15}) &&
              !set_training_camp_focus(g, 1, {0, 1, 2, 3}) &&
              !set_training_camp_focus(g, -1, {0, 1, 2, 3}), "invalid focus and manager are rejected");
        check(g.manager_records == focus_state && before != focus_state && rand.calls() == 0,
              "invalid focus has no record or RNG effects");
    }
    {
        auto g = fixture(0);
        WatcomRandom rand(77);
        check(training_camp_booking_block(g, 0) == TrainingCampBookingBlock::none, "booking initially allowed");
        const auto before = g.manager_records;
        check(!book_training_camp(g, 0, 0, rand), "insufficient payment declines camp booking");
        check(g.manager_records == before && rand.calls() == 0 && rand.seed() == 77,
              "failed payment preserves records and RNG state");
    }
    {
        auto g = fixture(200000);
        WatcomRandom rand(77);
        const int32_t cost = training_camp_price(g, 0, 0);
        check(book_training_camp(g, 0, 0, rand), "affordable camp books successfully");
        check(u32(g.manager_records, 0x314) == uint32_t(200000 - cost) && u32(g.manager_records, 0x890) == uint32_t(cost),
              "booking deducts price and adds camp expense");
        check(g.manager_records[0x21f] == 0x81 &&
              training_camp_booking_block(g, 0) == TrainingCampBookingBlock::booked_this_week,
              "booking increments visits and applies weekly gate");
        const auto records = g.manager_records;
        const uint32_t seed = rand.seed();
        check(!book_training_camp(g, 0, 0, rand) && g.manager_records == records &&
              rand.seed() == seed && rand.calls() == 0, "weekly gate prevents second booking before effects");
    }
    {
        auto g = fixture(1000000);
        g.manager_records[0x21f] = 3;
        WatcomRandom rand(77);
        check(training_camp_booking_block(g, 0) == TrainingCampBookingBlock::visit_limit,
              "three stays reach the visit limit");
        const auto before = g.manager_records;
        check(!book_training_camp(g, 0, 0, rand) && before == g.manager_records && rand.calls() == 0,
              "visit cap rejects before payment and effect");
        check(!book_training_camp(g, 0, 8, rand) && rand.calls() == 0, "invalid resort rejects cleanly");
        check(training_camp_booking_block(g, 1) == TrainingCampBookingBlock::invalid_request,
              "invalid manager has explicit booking block");
    }
    std::puts("training camp actions: focus, gates, payment and effects OK");
}
