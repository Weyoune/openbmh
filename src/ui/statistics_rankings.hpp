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
#include "ui/sprite_font.hpp"

namespace bmh::ui {

// StatisticsRankingsFromGame's nine historical title lists. Each Original list is one title id
// in Titles, with 50 little-endian club ids (or 0xffff for an empty season).
struct StatisticsRankingCategory {
    uint8_t title_id = 0;
    bool alternate_label_table = false;
    bool operator==(const StatisticsRankingCategory&) const = default;
};

struct StatisticsRankingState {
    std::array<StatisticsRankingCategory, 9> categories{};
    std::array<std::array<uint16_t, 50>, 9> title_holders{};
    std::array<std::array<std::string, 50>, 9> club_names{};
    std::array<std::array<uint8_t, 50>, 9> club_name_colors{};
    int current_season = 0; // SeasonIndex: latest history index; current empty slot is retained.
    int manager_slot = -1;
    int season_number_base = 0; // AchievementsRuntimeField[manager] + Achievements_FormatError_RuntimeField2; StatisticsRankingsFromGame adds one.
    int category = 0;       // StatisticsRankingsFromGame starts its child page at category 0.
    int first_season = 0;   // visible page starts in 4-season increments.
    Image underlay;         // accepted parent frame under StatisticsRankingsFromGame2's list area
};

enum class StatisticsRankingsRequestKind : uint8_t { none, close, select_category };
struct StatisticsRankingsRequest {
    StatisticsRankingsRequestKind kind = StatisticsRankingsRequestKind::none;
    int category = 7;
    bool operator==(const StatisticsRankingsRequest&) const = default;
};

StatisticsRankingState statistics_rankings_from_game(const GameState&, const LeImage& exe,
                                                       int manager_slot, int category = 0, int first_season = 0,
                                                       const Image& underlay = {});

class StatisticsRankingsScreen {
public:
    StatisticsRankingsScreen(const std::filesystem::path& data, StatisticsRankingState);
    Image render() const;
    bool pointer_down(int x, int y, bool right = false);
    bool move(int x, int y);
    bool release(int x, int y);
    void reject_action();
    void acknowledge_category(StatisticsRankingState accepted);
    void scroll(int delta); // Source changes the page in groups of four.
    const StatisticsRankingState& state() const { return state_; }
    const std::string& category_label(size_t index) const { return category_labels_.at(index); }
    const std::optional<StatisticsRankingsRequest>& pending() const { return pending_; }
private:
    StatisticsRankingState state_;
    LeImage exe_;
    Font font_;
    SpriteFont sprite_;
    Image button_art_;
    TextDb texts_;
    std::array<std::string, 9> category_labels_{};
    std::string empty_label_;
    int pressed_category_ = -1;
    std::optional<StatisticsRankingsRequest> pending_;
    void draw_category_button(Image&, int index, bool pressed) const;
};

} // namespace bmh::ui
