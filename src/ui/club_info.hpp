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
#include "game/club_info.hpp"
#include "game/state.hpp"
#include "ui/sprite_font.hpp"

namespace bmh::ui {

enum class ClubInfoView : uint8_t { data_a = 1, data_b = 2 };

struct ClubInfoPlayer {
    int row = -1; // physical ordinary row, or appended loan projection row
    int rendered_row = -1; // index in Source's occupied-player navigation list
    int physical_row = -1;
    int human_loan_index = -1;
    int player_id = -1;
    uint16_t displayed_slot = 0x0fff;
    std::string original_name;
    Player attributes;
    uint16_t cards = 0;
    uint8_t bonus = 0;
    bool borrowed = false;
    bool ordinary_loan = false;
    bool offer_name_color = false;
    // Present only when this runtime club belongs to a human manager.
    std::optional<std::array<uint8_t, 0x58>> manager_board;
};

// Owned inputs for one render of ClubInfoFromGame / LineupScreen. Build this from the actual
// runtime team and the Core-owned pre-injury-cleanup slot snapshot.
struct ClubInfoProjection {
    int runtime_team = -1;
    int squad_start_offset = -1;
    std::string original_club_name;
    ClubInfoView view = ClubInfoView::data_a;
    bool has_squad = false;
    bool gradient_text = true;
    bool decorated_background = true;
    uint8_t category = 0;
    std::vector<ClubInfoPlayer> players;
};

ClubInfoProjection club_info_from_game(const GameState& game, int runtime_team,
                                       const ClubInfoEntry& entry, const LeImage& exe,
                                       const TextDb& texts);

enum class ClubInfoRequestKind : uint8_t {
    none, close, toggle_view, open_statistics, change_category, open_player_detail
};
enum class ClubInfoButton : uint8_t { view, statistics, category };

struct ClubInfoRequest {
    ClubInfoRequestKind kind = ClubInfoRequestKind::none;
    int runtime_team = -1;
    int squad_start_offset = -1;
    int player_id = -1;
    int rendered_row = -1;
    int physical_row = -1;
    int human_loan_index = -1;
    uint8_t requested_category = 0;
};

class ClubInfoScreen {
public:
    ClubInfoScreen(const std::filesystem::path& data_directory, ClubInfoProjection projection);
    Image render() const;
    bool move(int x, int y);
    std::optional<ClubInfoButton> button_at(int x, int y) const;
    void press(int x, int y);
    ClubInfoRequest release(int x, int y);
    std::optional<ClubInfoButton> pressed_button() const { return pressed_button_; }
    // Source right-button DOWN closes. Left-button DOWN on a player-name hit box
    // routes to the real player-detail modal; all other left clicks are inert.
    ClubInfoRequest pointer_down(int x, int y, bool right_button = false) const;
    const ClubInfoProjection& projection() const { return projection_; }
    std::optional<int> hovered_row() const { return hovered_row_; }
private:
    ClubInfoProjection projection_;
    LeImage exe_;
    Font font_;
    Font font2_;
    TextDb texts_;
    Image background_, widgets_, atlas_, flags_, letters_;
    std::optional<int> hovered_row_;
    std::optional<ClubInfoButton> pressed_button_;
    std::vector<std::pair<int, int>> rows() const;
    void text(Image&, int x, int baseline, std::string_view, uint8_t color, int right = -1) const;
    void sprite_text(Image&, int x, int baseline, std::string_view, int mode = 5) const;
};

} // namespace bmh::ui
