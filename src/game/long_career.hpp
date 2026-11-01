// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include "formats/binary.hpp"

namespace bmh {
struct GameState;
struct PlayerClubStatistics {
    std::array<uint64_t, 3> appearances{};
    uint64_t total = 0, goals = 0, assists = 0;
    bool operator==(const PlayerClubStatistics&) const = default;
};
// Optional OBMHEXT1 block, outside the Original guest address space. Payload v1.
inline constexpr uint32_t kLongCareerExtension = 0x4f424c43;
using PlayerStatisticsArchive = std::map<uint32_t, PlayerClubStatistics>;
using TitleHistoryArchive = std::map<uint32_t, std::array<std::string, 9>>;
PlayerClubStatistics player_club_statistics(const GameState&, int team, int player,
    const std::array<uint8_t, 0x58>& board);
void record_player_statistics(GameState&, int manager, int row, int player, int competition_class, int kind);
void reset_player_statistics(GameState&, int manager, int player);
void swap_statistics_clubs(GameState&, int first, int second);
TitleHistoryArchive title_history_rows(const GameState&);
void capture_title_history(GameState&);
void record_title_holder(GameState&, int category, uint16_t winner);
void shift_legacy_title_history(GameState&);
Bytes encode_long_career(const GameState&);
void decode_long_career(GameState&, const Bytes&);
}
