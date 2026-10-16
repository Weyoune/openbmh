// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "formats/font.hpp"
#include "formats/le.hpp"
#include "formats/textd.hpp"
#include "formats/vga.hpp"
#include "game/state.hpp"
#include "ui/desktop.hpp"
#include "ui/sprite_font.hpp"

namespace bmh::ui {

// AchievementsFromGame's 0xda record: one triple for each of the manager's 50 seasons.
struct AchievementSeason {
    uint8_t league_result = 0xff, cup_result = 0, europe_result = 0;
    uint8_t league_color = 0x69;
    int graph_y = 0;
    std::string season_label;
    std::string league_text, cup_text, europe_text;
};

struct AchievementsState {
    DeskRequest owner{};
    int manager_slot = -1;
    int season_count = 0;
    int selected_season = 0;
    std::array<AchievementSeason, 50> seasons{};
    std::string title;
    std::string manager_team_name;
    Image underlay;
    bool decorated_background = true;
    bool source_panel = true;
    bool gradient_text = true;
};

AchievementsState achievements_from_game(const GameState&, const LeImage&,
                                           const DeskRequest&, const Image& underlay);
bool achievements_available(const GameState&);

enum class AchievementsRequestKind : uint8_t { none, close };
struct AchievementsRequest {
    AchievementsRequestKind kind = AchievementsRequestKind::none;
    DeskRequest owner{};
    int selected_season = 0;
    bool operator==(const AchievementsRequest&) const = default;
};

class AchievementsScreen {
public:
    AchievementsScreen(const std::filesystem::path&, AchievementsState);
    Image render() const;
    bool move(int x, int y);
    AchievementsRequest pointer_down(int x, int y, bool right = false);
    const AchievementsState& state() const { return state_; }
    const std::optional<AchievementsRequest>& pending() const { return pending_; }
private:
    LeImage exe_;
    TextDb texts_;
    Font body_, header_;
    SpriteFont sprite_font_;
    Image background_, panel_;
    AchievementsState state_;
    std::optional<AchievementsRequest> pending_;
    void draw_text(Image&, int, int, std::string_view, uint8_t) const;
};

} // namespace bmh::ui
