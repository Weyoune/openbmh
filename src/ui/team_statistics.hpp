// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "formats/le.hpp"
#include "formats/font.hpp"
#include "formats/textd.hpp"
#include "formats/vga.hpp"
#include "game/state.hpp"
#include "game/table.hpp"
#include "ui/sprite_font.hpp"

namespace bmh::ui {

enum class TeamStatisticsMode : uint8_t { home = 0, away = 1, combined = 2 };

// Owned read-only inputs to Source LeagueCalculations(team, mode). League calculations
// come from Core's ComputeLeagueTable model; head-to-head data is kept separately
// because Source consults the currently active manager for that subsection.
struct TeamStatisticsProjection {
    int runtime_team = -1;
    int league = -1;
    int league_row = -1;
    uint8_t flag_index = 0;
    TeamStatisticsMode mode = TeamStatisticsMode::combined;
    uint8_t source_flags = 0;
    uint8_t points_mode = 1;
    std::string original_team_name;
    std::string original_league_name;
    bool gradient_text = true;
    bool decorated_background = true;
    bmh::LeagueTable table;
    std::array<uint16_t, 5> average_strength{}; // GK, DEF, MID, FWD, squad
    std::array<uint16_t, 5> source_averages{}; // Void6 flags 1f1/1f2/1f4/1f8/1ff
    int league_rounds = 0;
    std::array<uint8_t, 0x4a> team_record{};    // Source *ClubSeasonRecords record row, when present
    bool has_team_record = false;
    std::vector<uint8_t> club_name_pool;
    int active_manager_slot = -1;
    int active_manager_team = -1;
    std::string active_manager_team_name;
    std::vector<uint8_t> active_manager_results = std::vector<uint8_t>(size_t(runtime_dimensions.record_teams) * 10);
    bool has_active_head_to_head = false;
    bool source_has_league_panel = true;
    bool source_has_head_to_head = false;
    std::array<uint8_t, 9> source_extrema{};
    std::vector<uint8_t> badge_heap;
    bool source_badge_fallback = false;
    std::array<uint8_t, 0x1a> position_history{};
    bool source_has_position_history = false;
};

TeamStatisticsProjection team_statistics_from_game(const GameState& game, int runtime_team,
                                                     TeamStatisticsMode mode,
                                                     const LeImage& exe, const TextDb& texts);

enum class TeamStatisticsRequestKind : uint8_t { none, close, change_mode };
enum class TeamStatisticsButton : uint8_t { home, away, combined, close };
struct TeamStatisticsRequest {
    TeamStatisticsRequestKind kind = TeamStatisticsRequestKind::none;
    int runtime_team = -1;
    TeamStatisticsMode mode = TeamStatisticsMode::combined;
};

class TeamStatisticsScreen {
public:
    TeamStatisticsScreen(const std::filesystem::path& data_directory,
                         TeamStatisticsProjection projection, Image club_info_backdrop);
    Image render() const;
    std::optional<TeamStatisticsButton> button_at(int x, int y) const;
    void press(int x, int y);
    bool move(int x, int y);
    TeamStatisticsRequest release(int x, int y);
    std::optional<TeamStatisticsButton> pressed_button() const { return pressed_button_; }
    TeamStatisticsRequest right_button_down() const;
    const TeamStatisticsProjection& projection() const { return projection_; }
private:
    TeamStatisticsProjection projection_;
    std::filesystem::path data_directory_;
    LeImage exe_;
    Font font_;
    Font header_font_;
    SpriteFont sprite_font_;
    TextDb texts_;
    Image backdrop_, widgets_, flags_, faces_, ratings_;
    Image crest_;
    std::optional<TeamStatisticsButton> pressed_button_;
    void text(Image&, int x, int baseline, std::string_view, uint8_t color) const;
    void sprite_text(Image&, int x, int baseline, std::string_view, int mode) const;
    void button(Image&, int x, std::string_view label, bool selected) const;
};

} // namespace bmh::ui
