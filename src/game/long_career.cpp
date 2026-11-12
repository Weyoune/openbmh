// SPDX-License-Identifier: GPL-3.0-only
// Long-career extension: per-club player statistics (appearances, goals, assists) and title
// history archives kept in an OpenBMH extension block outside the original save fields, with
// encoding to and decoding from bytes for saving.
// Reads and writes GameState (memory extension, manager records). Draws no random numbers.
// Interface in long_career.hpp.
#include "game/long_career.hpp"
#include "game/state.hpp"
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace bmh {
namespace {
uint32_t key(int team, int player) {
    if (team < 0 || team > 0xfff || player < 0 || player >= 0xfff)
        throw FormatError("long career: invalid player/club identity");
    return uint32_t(team) << 12 | uint32_t(player);
}
int manager_team(const GameState& g, int manager) {
    if (manager >= 0 && size_t(manager) < g.manager_teams.size()) return g.manager_teams[size_t(manager)];
    const size_t at = size_t(manager) * kManagerRecordSize + 0x290;
    if (manager < 0 || manager > 4 || at + 1 >= g.manager_records.size())
        throw FormatError("long career: invalid manager");
    return int(g.manager_records[at] | uint16_t(g.manager_records[at + 1]) << 8);
}
PlayerClubStatistics legacy(const std::array<uint8_t, 0x58>& b) {
    auto word = [&](size_t at) { return uint64_t(b[at] | uint16_t(b[at + 1]) << 8); };
    return {{b[7], b[8], b[9]}, word(0x48), word(0x4a), word(0x4c)};
}
void increment(uint64_t& n) {
    if (n == std::numeric_limits<uint64_t>::max()) throw FormatError("long career: statistics counter exhausted");
    ++n;
}
void put(Bytes& b, uint64_t n, int width) { for (int i = 0; i < width; ++i) b.push_back(uint8_t(n >> (i * 8))); }
struct Reader {
    const Bytes& bytes; size_t pos = 0;
    uint64_t get(int width) {
        if (size_t(width) > bytes.size() - pos) throw FormatError("long career: truncated extension");
        uint64_t n = 0; for (int i = 0; i < width; ++i) n |= uint64_t(bytes[pos++]) << (i * 8); return n;
    }
    std::string string() {
        const size_t size = size_t(get(2));
        if (size > bytes.size() - pos) throw FormatError("long career: truncated title name");
        std::string s(bytes.begin() + pos, bytes.begin() + pos + size); pos += size; return s;
    }
};
}
PlayerClubStatistics player_club_statistics(const GameState& g, int team, int player,
    const std::array<uint8_t, 0x58>& board) {
    const auto found = g.player_statistics.find(key(team, player));
    return found == g.player_statistics.end() ? legacy(board) : found->second;
}
void record_player_statistics(GameState& g, int manager, int row, int player, int cls, int kind) {
    const size_t at = size_t(manager) * 0x840 + size_t(row) * 0x58;
    if (manager < 0 || row < 0 || row >= 24 || at + 0x58 > g.tactics.size() || cls < 0 || cls > 3 || kind < 0 || kind > 2)
        throw FormatError("long career: invalid statistics row/event");
    std::array<uint8_t, 0x58> board; std::copy_n(g.tactics.begin() + at, board.size(), board.begin());
    const auto identity = key(manager_team(g, manager), player);
    auto [found, inserted] = g.player_statistics.try_emplace(identity, legacy(board));
    auto next = found->second;
    if (kind == 0) { if (cls < 3) increment(next.appearances[size_t(cls)]); increment(next.total); }
    else if (kind == 1) increment(next.goals);
    else increment(next.assists);
    found->second = next;
}
void reset_player_statistics(GameState& g, int manager, int player) {
    g.player_statistics.erase(key(manager_team(g, manager), player));
}
void swap_statistics_clubs(GameState& g, int first, int second) {
    PlayerStatisticsArchive next;
    for (const auto& [identity, value] : g.player_statistics) {
        const int club = int(identity >> 12), player = int(identity & 0xfff);
        next.emplace(key(club == first ? second : club == second ? first : club, player), value);
    }
    g.player_statistics = std::move(next);
}
TitleHistoryArchive title_history_rows(const GameState& g) {
    if (g.season < 0) throw FormatError("long career: negative season index");
    auto rows = g.title_history;
    if (!g.memory.has(layout::Titles + 899)) return rows;
    const int first = std::max(0, g.season - 49);
    for (int slot = 0; slot < 50 && first + slot <= g.season; ++slot) {
        const auto season = uint32_t(first + slot);
        if (rows.contains(season)) continue;
        std::array<std::string, 9> winners;
        for (size_t category = 0; category < 9; ++category) {
            const auto team = g.memory.u16(layout::Titles + uint32_t(category) * 100 + uint32_t(slot) * 2);
            if (team != 0xffff) winners[category] = g.club_name(team);
        }
        rows.emplace(season, std::move(winners));
    }
    return rows;
}
void capture_title_history(GameState& g) { g.title_history = title_history_rows(g); }
void record_title_holder(GameState& g, int category, uint16_t winner) {
    if (category < 0 || category >= 9 || g.season < 0 || winner >= g.club_ids.size())
        throw FormatError("long career: invalid title award");
    capture_title_history(g);
    const auto name = g.club_name(winner);
    g.memory.set16(layout::Titles + uint32_t(category) * 100 + uint32_t(std::min(g.season, 49)) * 2, winner);
    g.title_history[uint32_t(g.season)][size_t(category)] = name;
}
void shift_legacy_title_history(GameState& g) {
    if (g.season <= 49) return;
    for (uint32_t category = 0; category < 9; ++category) {
        for (uint32_t i = 0; i < 98; ++i)
            g.memory.set8(layout::Titles + category * 100 + i,
                g.memory.u8(layout::Season_SeasonEndSteps_RuntimeField + category * 100 + i));
        g.memory.set16(layout::Season_SeasonEndSteps_RuntimeField2 + category * 100, 0xffff);
    }
}
Bytes encode_long_career(const GameState& g) {
    if (g.player_statistics.empty() && g.title_history.empty()) return {};
    Bytes b; put(b, 1, 4); put(b, g.player_statistics.size(), 4); put(b, g.title_history.size(), 4);
    for (const auto& [identity, s] : g.player_statistics) {
        put(b, identity, 4); for (const auto n : s.appearances) put(b, n, 8);
        for (const auto n : {s.total, s.goals, s.assists}) put(b, n, 8);
    }
    for (const auto& [season, names] : g.title_history) {
        if (season > 32767) throw FormatError("long career: archived season exceeds native calendar range");
        put(b, season, 4);
        for (const auto& name : names) {
            if (name.size() > UINT16_MAX) throw FormatError("long career: title name too long");
            put(b, name.size(), 2); b.insert(b.end(), name.begin(), name.end());
        }
    }
    return b;
}
void decode_long_career(GameState& g, const Bytes& bytes) {
    Reader r{bytes};
    if (r.get(4) != 1) throw FormatError("long career: unsupported extension version");
    const auto players = r.get(4), seasons = r.get(4);
    if (players > (bytes.size() - r.pos) / 52 || seasons > 32768)
        throw FormatError("long career: invalid extension counts");
    PlayerStatisticsArchive stats; TitleHistoryArchive history;
    for (uint64_t i = 0; i < players; ++i) {
        const auto identity = uint32_t(r.get(4));
        if (identity > 0xffffff || (identity & 0xfff) == 0xfff) throw FormatError("long career: invalid archived identity");
        PlayerClubStatistics s; for (auto& n : s.appearances) n = r.get(8);
        s.total = r.get(8); s.goals = r.get(8); s.assists = r.get(8);
        if (!stats.emplace(identity, s).second) throw FormatError("long career: duplicate archived identity");
    }
    for (uint64_t i = 0; i < seasons; ++i) {
        const auto season = uint32_t(r.get(4));
        if (season > 32767) throw FormatError("long career: invalid archived season");
        std::array<std::string, 9> names; for (auto& name : names) name = r.string();
        if (!history.emplace(season, std::move(names)).second) throw FormatError("long career: duplicate archived season");
    }
    if (r.pos != bytes.size()) throw FormatError("long career: trailing extension bytes");
    g.player_statistics = std::move(stats); g.title_history = std::move(history);
}
}
