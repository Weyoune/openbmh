// SPDX-License-Identifier: GPL-3.0-only
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include "formats/executable_layout.hpp"
#include "game/desk_actions.hpp"
#include "game/month.hpp"
#include "game/national.hpp"
#include "game/weekly.hpp"

using namespace bmh;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
GameState fixture() {
    GameState g;
    g.manager_teams = {0}; g.manager_records.resize(5 * 0xb0c);
    g.tactics.resize(5 * 0x840); g.squad_slots.resize(3072, 0xfff);
    g.squad_slots[0] = 0x8000;
    g.players = {{"Arrival", 70u << 7 | 70u << 14 | 70u, 0, 6u << 21 | 1u}};
    g.memory.add_block(layout::EditorList, 44 * 0x58);
    for (uint32_t entry = 0; entry < 44; ++entry)
        for (uint32_t bid = 0; bid < 4; ++bid)
            g.memory.set16(layout::EditorList + entry * 0x58 + bid * 2, 0xffff);
    return g;
}
void fresh_arrivals() {
    const auto base = fixture();
    for (uint8_t inherited : {0x00, 0x20, 0x40, 0x60, 0x80, 0xff}) {
        for (uint8_t tenure : {0, 127, 255}) {
            auto g = base;
            g.tactics[0xb] = inherited; g.tactics[0x11] = tenure;
            WatcomRandom actual(713), reference(713);
            auto clean = base;
            new_board_entry(clean, 0, 0, 0, reference);
            new_board_entry(g, 0, 0, 0, actual);
            require(g.tactics == clean.tactics, "new arrival inherits no previous contract flags or tenure");
            require(actual.calls() == reference.calls() && actual.seed() == reference.seed(), "flag fix consumes no extra RNG");
            require(g.tactics[0xc] == 24 && g.tactics[0xb] == 0, "arrival gets ordinary two-year initial term and clean negotiation flags");
            g.tactics[0xd] = 105;
            const auto demands = contract_demands(g, 0, 0, true, actual);
            for (const auto amount : demands)
                require(amount != UINT32_MAX && amount > 0, "young arrival can negotiate every supported term immediately");
        }
    }
    auto retired = base;
    retired.tactics[0xb] = 0x20; retired.tactics[0xc] = 24; retired.tactics[0xd] = 100;
    WatcomRandom rand(713);
    for (const auto demand : contract_demands(retired, 0, 0, true, rand))
        require(demand == UINT32_MAX, "existing player's genuine retirement decision remains effective");
}
void ordinary_list_guards() {
    auto g = fixture();
    for (size_t n = 0; n < 40; ++n) g.squad_slots[0xb70 + n] = 1;
    WatcomRandom rand(713); MinuteReport report;
    const auto full = list_manager_player(g, 0, 0, rand, report);
    require(full.status == TransferActionStatus::blocked_by_board && !report.unsupported && rand.calls() == 0,
            "full ordinary market refuses without taking reserved entries or RNG");
    require(g.squad_slots[0xb70 + 40] == 0xfff, "special offer rows are preserved");
    g.squad_slots[0xb70] = 0;
    const auto listed = list_manager_player(g, 0, 0, rand, report);
    require(listed.status == TransferActionStatus::pending && !report.unsupported && rand.calls() == 0,
            "already listed is an explicit inert outcome");
    const auto native = g.squad_slots;
    const auto rejected = accept_manager_transfer_offer(g, 1, 0, 0, 0, rand, report);
    require(rejected.status == TransferActionStatus::invalid_request && g.squad_slots == native && rand.calls() == 0,
            "unowned listing cannot be settled through seller action");
}
}
int main() try {
    fresh_arrivals(); ordinary_list_guards();
    std::cout << "PASS fresh-arrival contract flags, tenure boundaries and ordinary listing guards\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
