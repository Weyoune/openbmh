#include "runtime_fixture_tables.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
// The supported edition youth academy's player (legacy routine, src/game/season.hpp) on the game-end
// savegame: nothing (and no draw) while the academy is unpaid; once paid, a young player of
// a computer club joins the squad with the league's level, a fresh board entry and an age of
// 16..20.

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include "formats/fbm.hpp"
#include "formats/le.hpp"
#include "formats/savestate.hpp"
#include "game/career.hpp"
#include "game/matchday.hpp"
#include "game/schedule.hpp"
#include "game/season.hpp"
#include "game/weekly.hpp"

using namespace bmh;

namespace {

void check(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

void set_rec32(GameState& g, size_t o, uint32_t v) {
    for (int i = 0; i < 4; i++) g.manager_records.at(o + size_t(i)) = uint8_t(v >> (8 * i));
}

void youth_test(GameState g) {
    const int team = g.manager_teams.at(0);
    const int league = team_league(team);
    const uint32_t price = league == 0 ? 1500000 : league == 1 ? 1000000 : league <= 3 ? 580000 : 280000;
    GameRandom game(4711);
    WatcomRandom rand(815);
    MinuteReport report;
    // unpaid: nothing happens, nothing is drawn
    set_rec32(g, 0x880, price - 1);
    check(!youth_academy_player(g, 0, game, rand, report) && game.calls() == 0 && rand.calls() == 0, "unpaid academy");
    // paid
    set_rec32(g, 0x880, price);
    const int before = squad_count(g, team, 3);
    check(before <= 0x16, "the savegame's squad is full");
    check(youth_academy_player(g, 0, game, rand, report) && !report.unsupported, "no youth player");
    check(squad_count(g, team, 3) == before + 1, "the squad did not grow");
    // the new player: the squad slot whose player was not there before is the youth player
    const SquadRange own = squad_range(team);
    int player = -1, row = -1;
    for (size_t s = 0; s < own.count; s++) {
        const int p = g.squad_slots.at(own.first + s) & 0xfff;
        if (p == 0xfff) continue;
        if (g.tactics.at(0 * 0x840 + s * 0x58 + 0xd) == 0x69 && g.players.at(size_t(p)).age() <= 20) player = p, row = int(s);
    }
    check(player >= 0, "no new player with mood 0x69 and age up to 20");
    const Player& p = g.players.at(size_t(player));
    static const int kLevel[4][2] = {{0x4f, 0x5f}, {0x45, 0x55}, {0x31, 0x44}, {0x1d, 0x30}};
    const int band = league == 0 ? 0 : league == 1 ? 1 : league < 4 ? 2 : 3;
    check(p.condition() >= kLevel[band][0] && p.condition() <= kLevel[band][1] && p.technique() == p.condition(),
          "level of the league (exactly the level for condition and technique)");
    // he left his former club
    int clubs = 0;
    for (size_t s = 0; s < 0xb70; s++)
        if ((g.squad_slots.at(s) & 0xfff) == player) clubs++;
    check(clubs == 1, "the player is in more than one squad");
    std::cout << "youth player " << g.player_name(player) << " (row " << row << ", age " << p.age() << ", KO/TE " << p.condition()
              << "), draws G " << game.calls() << " W " << rand.calls() << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 3, "usage: youth_academy_tests ORIGINAL_DATA SAVEGAME");
        if (!*argv[1]) {
            std::cout << "youth academy tests: skipped (Original data directory not configured)\n";
            return 0;
        }
        const auto exe = bmh::test::load_executable(std::filesystem::path(argv[1]) / "BMH.exe");
        const auto file = load_savegame(argv[2]);
        const DbCounts counts{exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4)};
        GameState g = GameState::from_save(SaveState(file.stream, counts), counts);
        competition_pointers(g.memory, exe);
        youth_test(g);
        std::cout << "youth academy tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "youth academy tests: " << error.what() << "\n";
        return 1;
    }
}
