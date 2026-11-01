// SPDX-License-Identifier: GPL-3.0-only
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include "formats/executable_layout.hpp"
#include "game/long_career.hpp"
#include "game/matchday.hpp"
#include "game/national.hpp"
#include "game/save.hpp"
using namespace bmh;
namespace {
void check(bool yes, const char* why) { if (!yes) throw std::runtime_error(why); }
GameState fixture() {
    GameState g; g.season = 0; g.manager_teams = {0};
    g.manager_records.resize(5 * 0xb0c); g.tactics.resize(5 * 0x840);
    g.players = {{"Synthetic", 70u << 7 | 70u << 14 | 70u, 0, 6u << 21 | 1u}};
    g.player_bonus.resize(1); g.squad_slots.resize(3072, 0xfff); g.squad_slots[0] = 0;
    g.club_ids = {0, 5}; g.club_names = {'C','l','u','b',0,'R','i','v','a','l',0};
    g.memory.add_block(layout::Titles, 900);
    for (uint32_t n = 0; n < 900; ++n) g.memory.set8(layout::Titles + n, 0xff);
    g.memory.add_block(layout::ManagerCount, 1); g.memory.set8(layout::ManagerCount, 1);
    return g;
}
std::array<uint8_t, 0x58> board(const GameState& g) {
    std::array<uint8_t, 0x58> b; std::copy_n(g.tactics.begin(), b.size(), b.begin()); return b;
}
void counters() {
    auto g = fixture();
    g.tactics[7] = g.tactics[8] = g.tactics[9] = 255;
    for (size_t n = 0x48; n <= 0x4d; ++n) g.tactics[n] = 255;
    player_statistic(g, 0, 0, 0, 0, 0, 0);
    player_statistic(g, 1, 0, 0, 0, 0, 0);
    player_statistic(g, 2, 0, 0, 0, 0, 0);
    player_statistic(g, 0, 0, 0, 0, 1, 0);
    player_statistic(g, 0, 0, 0, 0, 2, 0);
    const auto stats = player_club_statistics(g, 0, 0, board(g));
    check(stats.appearances == std::array<uint64_t,3>{256,256,256}, "all cumulative byte counters survive 255 -> 256");
    check(stats.total == 65538 && stats.goals == 65536 && stats.assists == 65536, "word totals survive 65535 -> 65536");
    check(g.tactics[7] == 0 && g.tactics[8] == 0 && g.tactics[9] == 0 && g.tactics[0x48] == 2,
          "native byte/word fields retain compatible modulo representation");
    const auto native = save_stream(g); const SaveState saved(native);
    auto loaded = GameState::from_save(saved);
    check(loaded.player_statistics == g.player_statistics && save_stream(loaded) == native,
          "optional wide counters survive exact native-stream save/load/save roundtrip");
    const auto payload = encode_long_career(g);
    auto plain = g; plain.player_statistics.clear();
    const auto original = save_stream(plain);
    check(original.size() + payload.size() + 18 == native.size() &&
          std::equal(original.begin(), original.end(), native.begin()), "extension leaves every Original block and name pool byte unchanged");
    swap_statistics_clubs(g, 0, 1);
    check(player_club_statistics(g, 1, 0, board(g)) == stats, "club promotion remapping retains the same player's counts");
    swap_statistics_clubs(g, 0, 1);
    WatcomRandom random(713);
    new_board_entry(g, 0, 0, 0, random);
    check(g.player_statistics.empty() && player_club_statistics(g, 0, 0, board(g)).total == 0,
          "replacement player never inherits the previous occupant's wide/native totals");
    auto exhausted = fixture();
    exhausted.player_statistics.emplace(0, PlayerClubStatistics{{255,0,0}, UINT64_MAX,0,0});
    const auto exhausted_before = save_stream(exhausted); bool refused = false;
    try { player_statistic(exhausted, 0, 0, 0, 0, 0, 0); } catch (const FormatError&) { refused = true; }
    check(refused && save_stream(exhausted) == exhausted_before, "uint64 exhaustion is explicit and inert, never a silent clamp");
    auto wrapped = fixture(); // Already lost high bits cannot be reconstructed from a legacy import.
    player_statistic(wrapped, 0, 0, 0, 0, 0, 0);
    check(player_club_statistics(wrapped, 0, 0, board(wrapped)).total == 1, "legacy zero is a known lower bound, never fabricated prior appearances");
}
void history() {
    auto g = fixture();
    for (int season = 0; season <= 300; ++season) {
        if (season) {
            capture_title_history(g); ++g.season; shift_legacy_title_history(g);
        }
        record_title_holder(g, 0, uint16_t(season % 2));
        record_title_holder(g, 1, uint16_t((season + 1) % 2));
    }
    check(g.title_history.size() == 301 && g.title_history.at(0)[0] == "Club" &&
          g.title_history.at(49)[0] == "Rival" && g.title_history.at(50)[0] == "Club" &&
          g.title_history.at(300)[1] == "Rival", "title and cup winners remain available across seasons 49/50/300");
    check(g.memory.u16(layout::Titles) == 1 && g.memory.u16(layout::Titles + 98) == 0,
          "Original block still contains exactly the latest 50 seasons");
    const auto bytes = save_stream(g); const SaveState saved(bytes); auto loaded = GameState::from_save(saved);
    check(loaded.title_history == g.title_history && save_stream(loaded) == bytes, "301 seasons persist in the optional save extension");
    g.club_names[0] = 'X';
    check(g.title_history.at(0)[0] == "Club", "winner names are owned snapshots, independent of later renaming/promotion");
    auto imported = fixture(); imported.season = 300; imported.memory.set16(layout::Titles, 0);
    capture_title_history(imported);
    check(imported.title_history.size() == 50 && imported.title_history.begin()->first == 251 &&
          !imported.title_history.contains(0), "old late-career imports retain only known rows and do not fabricate lost history");
}
void malformed() {
    auto g = fixture(); record_title_holder(g, 0, 0); const auto original = encode_long_career(g);
    for (size_t size = 0; size < original.size(); ++size) {
        auto old = g; bool refused = false;
        try { decode_long_career(old, Bytes(original.begin(), original.begin() + size)); }
        catch (const FormatError&) { refused = true; }
        check(refused && old.title_history == g.title_history, "truncated archive rejects atomically");
    }
    auto version = original; version[0] = 2; bool refused = false;
    try { decode_long_career(g, version); } catch (const FormatError&) { refused = true; }
    check(refused, "future archive versions are explicit unsupported outcomes");
    auto future = g; future.title_history.emplace(1, std::array<std::string,9>{});
    const auto future_bytes = save_stream(future); refused = false;
    try { (void)GameState::from_save(SaveState(future_bytes)); } catch (const FormatError&) { refused = true; }
    check(refused, "future season history cannot silently bind to a different saved calendar");
    auto base = g; base.title_history.clear(); auto duplicate = save_stream(base);
    append_save_extension(duplicate, {{kLongCareerExtension,original},{kLongCareerExtension,original}});
    refused = false;
    try { (void)GameState::from_save(SaveState(duplicate)); } catch (const FormatError&) { refused = true; }
    check(refused, "duplicate archive containers reject rather than overwrite history");
}
}
int main() try {
    counters(); history(); malformed();
    std::cout << "PASS cumulative 256/65536 counters, 301 title seasons, legacy compatibility and archive roundtrip/validation\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
