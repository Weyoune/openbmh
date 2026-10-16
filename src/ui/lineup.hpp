// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <optional>
#include <vector>

#include "formats/font.hpp"
#include "formats/m3d.hpp"
#include "formats/textd.hpp"
#include "ui/setup.hpp"

namespace bmh { struct GameState; class SaveState; }
namespace bmh::ui {

enum class LineupView { data_a, data_b };
struct LineupPlayer {
    int row = -1, player_id = -1;
    uint16_t slot = 0x0fff;
    std::string name; // original ISO 646-DE, owned storage
    Player attributes;
    uint16_t cards = 0;
    uint8_t bonus = 0;
    std::array<uint8_t, 0x58> board{};
};
struct LineupState {
    int manager_slot = -1, runtime_club = -1;
    uint64_t revision = 0;
    std::string source, club_name;
    uint16_t saved_display_flags = 0;
    LineupView view = LineupView::data_a;
    bool gradient_text = true, decorated_background = true;
    int automatic_formation = 0;
    bool in_match = false;
    bool entry_prepared = false; // controller acknowledgement of original entry effects
    std::vector<LineupPlayer> players; // squad rows, includes empty slots
};
LineupState lineup_from_save(const SaveState&, const LeImage&, uint64_t revision = 0);
LineupState lineup_from_game(const GameState&, const LeImage&, uint64_t revision);

enum class LineupRequestKind { none, prepare_lineup, swap_players, open_player_info, open_pitch };
struct LineupRequest {
    LineupRequestKind kind = LineupRequestKind::none;
    int manager_slot = -1, runtime_club = -1;
    uint64_t revision = 0;
    int first_row = -1, second_row = -1;
    int first_player = -1, second_player = -1;
    uint16_t first_slot = 0, second_slot = 0;
};
std::string lineup_request_json(const LineupRequest&);

// Original team lists (LineupScreen). Selection/view are local; changing the
// squad is an explicit command awaiting a new authoritative projection.
class LineupScreen {
public:
    LineupScreen(const std::filesystem::path& data, LineupState state);
    LineupRequest initialize();
    Image render() const;
    LineupRequest click(int x, int y, bool right = false);
    bool move(int x, int y); // local original name-column hover
    LineupRequest key(std::string_view key);
    bool close_requested() const { return close_; }
    const LineupState& state() const { return state_; }
    const std::optional<LineupRequest>& pending() const { return pending_; }
    std::optional<int> selected_row() const { return selected_; }
    void acknowledge(LineupState updated);
    void reject_action(std::string reason);
    void resolve_local_navigation(LineupRequestKind kind);
    std::string state_json() const;
private:
    LineupState state_;
    LeImage exe_;
    Font font_;
    TextDb texts_;
    Image background_, widgets_, atlas_, flags_, letters_;
    std::optional<LineupRequest> pending_;
    std::optional<int> selected_;
    std::optional<int> hovered_;
    std::string rejection_;
    bool close_ = false;
    std::vector<std::pair<int,int>> rows() const; // (squad row, baseline)
    void text(Image&, int x, int baseline, std::string_view, uint8_t color, int right = -1) const;
    void sprite_text(Image&, int x, int baseline, std::string_view, int font = 5) const;
    void button(Image&, int x, int y, std::string_view value) const;
    LineupRequest request(LineupRequestKind, int first = -1, int second = -1);
};

} // namespace bmh::ui
