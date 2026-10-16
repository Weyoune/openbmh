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
#include "ui/desktop.hpp"
#include "ui/sprite_font.hpp"

namespace bmh::ui {
struct AllTimeRecord { uint16_t points=0, goals_for=0, goals_against=0, wins=0, draws=0, losses=0; };
struct AllTimeRow { int team=-1, played=0; uint32_t points_key=0, goal_key=0; uint32_t points=0, goals_for=0, goals_against=0; };
struct AllTimeTableState {
    DeskRequest owner{};
    Image underlay;
    std::vector<AllTimeRecord> records;
    std::array<std::string,5> nation_names{};
    std::array<std::string,168> club_names{};
    std::array<uint8_t,168> club_name_colors{};
    std::vector<AllTimeRow> rows;
    int nation=0;
    uint8_t mask=1;
    bool relative=false;
    bool has_data=false;
};
enum class AllTimeRequestKind : uint8_t { none, close, select_nation, toggle_relative };
struct AllTimeRequest { AllTimeRequestKind kind=AllTimeRequestKind::none; DeskRequest owner{}; int nation=0; bool relative=false; };
AllTimeTableState alltime_table_from_game(const GameState&, const LeImage&, DeskRequest owner,
                                           const Image& underlay, int nation=0, bool relative=false);
class AllTimeTableScreen {
public:
 AllTimeTableScreen(const std::filesystem::path&, AllTimeTableState);
 Image render() const;
 bool pointer_down(int x,int y,bool right=false);
 bool release(int x,int y);
 const std::optional<AllTimeRequest>& pending() const { return pending_; }
 const AllTimeTableState& state() const { return state_; }
private:
 AllTimeTableState state_; std::filesystem::path data_; LeImage exe_; Font font_,header_; SpriteFont sprite_; Image window_,buttons_; TextDb texts_;
 int pressed_=-1; std::optional<AllTimeRequest> pending_;
};
}
