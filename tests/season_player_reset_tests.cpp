// SPDX-License-Identifier: GPL-3.0-only
#include "game/season.hpp"
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
bmh::GameState fixture(int slot, int age, bool foreign = false) {
    bmh::GameState g;
    constexpr uint32_t data_begin = 0xa0000;
    constexpr size_t data_size = 0x21000;
    g.memory.add_block(data_begin, data_size);
    g.squad_slots.assign(3096, 0xfff);
    g.squad_slots.at(size_t(slot)) = 0;
    g.players.resize(1);
    g.players[0].w364 = uint32_t(age - 16) << 21;
    g.players[0].w368 = foreign ? 1u << 27 : 0;
    g.player_bonus.assign(1, 0);
    g.date = 365;
    return g;
}
void run(bmh::GameState& g, bmh::GameRandom& game, bmh::WatcomRandom& rand) {
    bmh::MinuteReport report;
    bmh::season_players(g, game, rand, report);
    check(!report.unsupported, "season reset unexpectedly unsupported");
    check(!g.lineups_frozen, "season reset left lineups frozen");
}
}
int main() try {
    {
        auto g = fixture(2928, 20);
        constexpr uint32_t entry = bmh::layout::CupTable + 14 * 0x28;
        g.memory.set8(entry, 0x7f);
        bmh::initialize_european_qualification_seed(g);
        check(g.memory.u16(entry + 0x22) == 357 && g.memory.u16(entry + 0x24) == 358 &&
              g.memory.u8(entry) == 0x7f, "empty EM seeds did not become national teams without changing descriptor data");
        g.memory.set16(entry + 0x22, 361); g.memory.set16(entry + 0x24, 364);
        bmh::initialize_european_qualification_seed(g);
        check(g.memory.u16(entry + 0x22) == 361 && g.memory.u16(entry + 0x24) == 364,
              "existing original national qualification seeds were overwritten");
    }
    for (const auto [slot, foreign] : {std::pair{2928, false}, {2971, false}, {2972, false}, {3071, true}}) {
        auto g = fixture(slot, 47, foreign);
        bmh::GameRandom game(4711); bmh::WatcomRandom rand(815);
        run(g, game, rand);
        check(g.players[0].age() >= 20 && g.players[0].age() <= 32,
              "old market player wrapped to age 16 instead of retiring/regenerating");
        check(std::count(g.squad_slots.begin(), g.squad_slots.end(), 0) == 1,
              "retired market player duplicated or lost during free-agent relocation");
        check(g.squad_slots[size_t(slot)] == 0xfff,
              "retired market player's source slot was not cleared");
    }
    {
        auto g = fixture(0, 20);
        bmh::GameRandom game(4711); bmh::WatcomRandom rand(815);
        run(g, game, rand);
        check(g.players[0].age() == 21 && g.squad_slots[0] == 0,
              "young club player changed ownership or age progression");
        check(game.calls() == 0 && rand.calls() == 1,
              "unchanged club ageing consumed additional random draws");
    }
    {
        auto g = fixture(0, 20);
        g.squad_slots[2968] = 0; // Cached contract/offer row still references this club player.
        bmh::GameRandom game(4711); bmh::WatcomRandom rand(815);
        run(g, game, rand);
        check(g.players[0].age() == 21 && g.squad_slots[0] == 0 && g.squad_slots[2968] == 0,
              "cached offer aged or recycled a player still owned by his club");
        check(game.calls() == 0 && rand.calls() == 1,
              "cached offer consumed a second retirement draw for its club player");
    }
    {
        auto g = fixture(3072, 47);
        bmh::GameRandom game(4711); bmh::WatcomRandom rand(815);
        run(g, game, rand);
        check(g.players[0].age() == 47 && g.squad_slots[3072] == 0 &&
              game.calls() == 0 && rand.calls() == 0,
              "non-seasonal extra slots entered the retirement loop");
    }
    {
        auto g = fixture(2928, 47); g.date = 0;
        bmh::GameRandom game(4711); bmh::WatcomRandom rand(815);
        run(g, game, rand);
        check(g.players[0].age() == 47 && game.calls() == 0 && rand.calls() == 0,
              "new-career setup unexpectedly aged market players");
    }
    std::cout << "Season player reset: market retirement and unchanged ageing passed\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
