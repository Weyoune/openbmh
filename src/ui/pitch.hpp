// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "ui/lineup.hpp"
#include "ui/sprite_font.hpp"
#include <memory>

namespace bmh::ui {

struct PitchOpponent {
    int row = -1, player_id = -1;
    uint16_t slot = 0x0fff;
    std::string name;
    std::string detail_text;
    int position = 0;
    int current_marker_player_id = -1; // display-only own player assigned to this opponent
    bool operator==(const PitchOpponent&) const = default;
};

// Owned, authoritative context outside the existing squad projection. The indoor
// flag is transient and absent from FBM: a save projection leaves it unknown.
struct PitchExtras {
    int manager_slot = -1, runtime_club = -1;
    uint64_t revision = 0;
    std::string source;
    std::optional<bool> indoor;
    int mode = 3;
    uint16_t badge = 0;
    uint8_t formation_mask = 0, automatic_weights = 0;
    std::array<uint8_t, 8 * 22> formations{};
    std::array<uint16_t, 3> set_piece_takers{}; // player_id+1; offsets +afc,+afe,+b08
    uint16_t set_piece_signature = 0;
    uint16_t native_display_word = 0; // per-manager word at CareerSession_AnyOf_ExecutableField+2*manager
    int opponent_team = -1;
    std::string opponent_name;
    std::vector<PitchOpponent> opponent_players; // authenticated current live pairing only
};
PitchExtras pitch_extras_from_save(const SaveState&, const LeImage&, uint64_t revision = 0);
PitchExtras pitch_extras_from_game(const GameState&, const LeImage&, uint64_t revision,
                                   int opponent_team = -1);

enum class PitchRequestKind { none, move_player, swap_players, open_player_info,
                              set_automatic_weight, apply_formation, save_formation,
                              open_set_piece_editor, close_set_piece_editor,
                              set_piece_taker, auto_set_piece_takers, set_player_order,
                              select_pitch_page, set_player_marking };
struct PitchRequest {
    PitchRequestKind kind = PitchRequestKind::none;
    int manager_slot = -1, runtime_club = -1;
    uint64_t revision = 0;
    bool indoor = false; // captured live pitch mode; never inferred from an FBM
    int row = -1, player_id = -1;
    uint16_t slot = 0;
    int old_depth = -1, old_lane = -1, new_depth = -1, new_lane = -1;
    int second_row = -1, second_player = -1;
    uint16_t second_slot = 0;
    int formation = -1; // zero-based original template; AA option stores formation + 1
    int weight_index = -1; // one of three 2-bit fields in manager record +0x288
    uint8_t old_weights = 0, new_weights = 0;
    std::array<uint8_t,22> formation_positions{}; // depth[11], lane[11], save only
    int set_piece_selector = 0; // original selection 2/3/4 (Freistoß/Elfmeter/Eckball)
    uint16_t expected_set_piece_value = 0;
    std::array<uint16_t, 3> expected_set_piece_values{};
    uint16_t expected_display_word = 0, new_display_word = 0;
    uint16_t expected_set_piece_signature = 0;
    uint8_t expected_mode = 0;
    int order_aspect = -1, expected_order_value = -1, new_order_value = -1;
    int page_control = -1;
    int opponent_team = -1, opponent_row = -1, opponent_player = -1;
    uint16_t opponent_slot = 0x0fff, expected_marking_player_id = 0x0fff;
};
std::string pitch_request_json(const PitchRequest&);

// Native PitchScreen pitch branch. Selection/hover/ghost are local; a confirmed
// placement emits a command, and only an acknowledged projection updates rows.
class PitchScreen {
public:
    PitchScreen(const std::filesystem::path& data, LineupState, PitchExtras);
    PitchScreen(const PitchScreen&);
    PitchScreen& operator=(const PitchScreen&);
    PitchScreen(PitchScreen&&) noexcept = default;
    PitchScreen& operator=(PitchScreen&&) noexcept = default;
    Image render() const;
    // PitchScreen: outline only, before the pending save is dispatched/ACKed.
    static constexpr int formation_save_flash_ms = 5 * 20; // DecodeTaxAnimationScript2(5)
    std::optional<Image> formation_save_feedback(bool restored) const;
    PitchRequest click(int x, int y, bool right = false);
    // Used by the authenticated Lineup -> Pitch transition. The caller still
    // dispatches and acknowledges the returned native page-word request.
    PitchRequest request_page_control(int control);
    PitchRequest pointer_down(int x,int y,bool right=false);
    bool take_source_down_release_barrier(){const bool value=source_down_release_barrier_;source_down_release_barrier_=false;return value;}
    PitchRequest save_formation(int x, int y); // completed long press, own slots 4..7
    bool move(int x, int y);
    PitchRequest key(std::string_view);
    bool close_requested() const { return close_; }
    bool return_team_requested() const { return return_team_; }
    const LineupState& state() const { return state_; }
    const PitchExtras& extras() const { return extras_; }
    const std::optional<PitchRequest>& pending() const { return pending_; }
    std::optional<int> selected_row() const { return selected_; }
    void acknowledge(LineupState, PitchExtras);
    void reject_action(std::string reason);
    void resolve_local_navigation(); // only open_player_info, never a mutation
    std::string state_json() const;
private:
    std::filesystem::path data_;
    LineupState state_;
    PitchExtras extras_;
    LeImage exe_;
    Font font_;
    SpriteFont sprite_;
    TextDb texts_;
    Image background_, atlas_, widgets_, letters_, badge_;
    std::unique_ptr<LineupScreen> sheet_;
    std::optional<PitchRequest> pending_;
    std::optional<int> selected_, hovered_;
    std::optional<std::pair<int,int>> ghost_;
    int scroll_ = 0;
    bool close_ = false, return_team_ = false;
    bool weights_dirty_ = false; // original local_30: reapply current AA after a weight change
    bool shooter_editor_open_ = false;
    int shooter_selection_ = 1; // original 20740: neutral=1, Freistoß=2, Elfmeter=3, Eckball=4
    bool zone_cover_open_ = false; // Scratch bit 0x80, toggled by control 5 while editor is open
    bool order_panel_open_ = false;
    std::optional<int> marking_target_row_;
    int marking_scroll_ = 0;
    int pressed_page_control_ = -1;
    bool source_down_release_barrier_ = false;
    std::string rejection_;
    bool automatic_page() const;
    void validate(const LineupState&, const PitchExtras&) const;
    static std::optional<std::pair<int,int>> grid(int x, int y);
    std::optional<int> hit_row(int x, int y) const;
    std::vector<std::pair<int,int>> rows() const;
    void text(Image&, int x, int baseline, std::string_view, uint8_t color) const;
    void button(Image&, int x, int y, std::string_view, bool selected = false) const;
    void jersey(Image&, const LineupPlayer&, int depth, int lane, int color, bool number = true) const;
    PitchRequest request(PitchRequestKind, int row, int second = -1, int depth = -1, int lane = -1);
    PitchRequest automatic_request(PitchRequestKind, int index);
    PitchRequest set_piece_request(PitchRequestKind);
    PitchRequest order_request(int aspect, int value);
    PitchRequest marking_request(int own_row, int opponent_row);
    PitchRequest page_request(int control);
    std::optional<int> hit_opponent_row(int x, int y) const;
    void diamond_button(Image&, int x, int y, std::string_view, bool selected) const;
};
} // namespace bmh::ui
