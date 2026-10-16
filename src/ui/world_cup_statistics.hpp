// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "formats/font.hpp"
#include "formats/le.hpp"
#include "formats/vga.hpp"
#include "game/state.hpp"

namespace bmh::ui {

struct WorldCupNation { uint8_t id = 0; std::string name; uint32_t medals = 0; };
struct WorldCupFlagCell { int x = 0, y = 0; };
struct WorldCupStatisticsProjection {
    std::array<uint8_t, 72> editions{}; // exactly 18 editions × four placements
    std::array<WorldCupNation, 62> ranking{};
    std::array<WorldCupFlagCell, 62> flag_cells{};
    std::array<std::string, 62> country_names{};
    std::vector<uint8_t> badge_heap;
    bool can_select_opponent = false;
};

// Owner is explicit: only the verified Friendly Match caller can enable a commit.
enum class WorldCupStatisticsOwner : uint8_t { desk_read_only, friendly_match_nation_picker };
enum class WorldCupStatisticsRequestKind : uint8_t { none, close, select_opponent };
struct WorldCupStatisticsRequest {
    WorldCupStatisticsRequestKind kind = WorldCupStatisticsRequestKind::none;
    int runtime_team = -1;
};

WorldCupStatisticsProjection world_cup_statistics_from_game(const GameState&, const LeImage&,
                                                               WorldCupStatisticsOwner);
uint32_t world_cup_medal_key(const std::array<uint8_t, 72>&, uint8_t nation);
void order_world_cup_nations(std::array<WorldCupNation, 62>&);
std::optional<uint8_t> world_cup_nation_at(const WorldCupStatisticsProjection&, int x, int y);
WorldCupStatisticsRequest world_cup_selection_request(const WorldCupStatisticsProjection&,
                                                       std::optional<uint8_t> nation);

class WorldCupStatisticsScreen {
public:
    WorldCupStatisticsScreen(const std::filesystem::path& data, WorldCupStatisticsProjection,
                             Image accepted_parent_underlay, bool flat_source_window = false);
    Image render() const;
    bool move(int x, int y);
    WorldCupStatisticsRequest pointer_down(int x, int y, bool right = false);
    const std::optional<uint8_t>& hovered_nation() const { return hovered_; }
private:
    WorldCupStatisticsProjection projection_;
    std::filesystem::path data_directory_;
    Image underlay_, flags_, window_atlas_;
    LeImage exe_;
    Font body_;
    std::optional<uint8_t> hovered_;
    bool hover_frame_shown_ = false;
    bool flat_source_window_ = false;
    void draw_flag(Image&, uint8_t nation, int x, int y) const;
    void draw_badge(Image&, uint8_t nation, int x, int y) const;
};

} // namespace bmh::ui
