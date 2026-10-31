// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "formats/font.hpp"
#include "formats/textd.hpp"
#include "ui/desk_state.hpp"
#include "ui/desk_drawers.hpp"

namespace bmh::ui {

enum class DeskRequestKind { none, open_menu, advance_turn, change_layout, change_option, change_ticket_price, end_session };
enum class DeskLayoutOperation : uint8_t { none, move_item, set_folder, toggle_quickbar_drawer, return_folder_child, send_to_folder, restore_folder_child };
struct DeskRequest {
    DeskRequestKind kind = DeskRequestKind::none;
    int manager_slot = -1, item_id = -1, menu_kind = -1, value = -1;
    uint64_t revision = 0;
    DeskLayoutOperation layout_operation = DeskLayoutOperation::none;
    int layout_target_item = -1;
    std::array<uint8_t, 54 * 16> layout_before{};
    std::array<uint8_t, 54 * 16> layout_after{};
    bool operator==(const DeskRequest&) const = default;
};
std::string desk_request_json(const DeskRequest&);
std::string_view desk_menu_name(int kind);

// Read-only state + local UI presentation. Engine changes are explicit requests.
// A request waits for acknowledgement; repeated input never advances time twice.
class DesktopScreen {
public:
    DesktopScreen(const std::filesystem::path& data, DeskState state);
    Image render() const;
    DeskRequest click(int x, int y, bool right = false);
    DeskRequest pointer_down(int x, int y, bool right = false);
    bool pointer_move(int x, int y);
    DeskRequest pointer_up(int x, int y, bool right = false);
    DeskRequest key(std::string_view key);
    DeskRequest process_timer_expired(); // Original event8, no synthetic icon click
    // Ordered Source sound-ID boundary; the application owns playback/options.
    std::vector<uint8_t> take_sound_ids() const;
    const DeskState& state() const { return state_; }
    const std::optional<DeskRequest>& pending() const { return pending_; }
    bool socket_confirmation_armed() const { return socket_confirmation_armed_; }
    void acknowledge(DeskState updated); // strictly newer authoritative revision
    void acknowledge_mailbox_parent(DeskState updated); // refresh a mailbox's parent without losing local presentation state
    void reject_action(std::string reason); // rejection changes no game state
    void resolve_local_menu(); // consume only a verified open_menu presentation request
    std::string state_json() const;
private:
    std::filesystem::path data_;
    DeskState state_;
    LeImage exe_;
    Image background_, icons_, widgets_, portraits_, badge_, letters_;
    std::optional<Image> next_opponent_badge_;
    Font small_;
    TextDb texts_;
    DeskDrawerRenderer drawers_;
    std::optional<DeskRequest> pending_;
    std::string last_rejection_;
    bool shortcut_bar_visible_ = false; // presentation-local afb86; no saved-option mutation
    bool shortcut_bar_edge_toggle_ = false; // afb86 bit0; always-visible option owns bit1
    int shortcut_last_y_ = 0; // DeskDate local_48, suppresses repeated samples at y239
    int shortcut_hover_record_ = -1;
    bool socket_confirmation_armed_ = false;
    bool socket_release_pending_ = false;
    bool bin_mode_ = false; // DeskDate local_50; presentation-local transfer into/out of folders
    mutable std::vector<uint8_t> sound_ids_;
    struct LayoutDrag { int item=-1, grab_x=0, grab_y=0, start_x=0, start_y=0, folder=-1; bool moved=false, folder_child=false, drawer_handle=false, right_button=false; };
    std::optional<LayoutDrag> layout_drag_;
    bool drawer_close_release_pending_ = false;
    DeskRequest close_drawer(const DeskItem& item);
    std::optional<DeskState> layout_preview_;
    void text(Image&, int x, int baseline, std::string_view, uint8_t color, int right = -1) const;
    void sprite_text(Image&, int x, int baseline, std::string_view, int font) const;
    void frame(Image&, int x, int y, int width, int height) const;
    void digits(Image&, int value, int sx, int sy, int width, int height, int x, int y, int base, int gap) const;
    void draw_item(Image&, const DeskItem&) const;
    bool sample_shortcut_pointer(int x, int y);
    std::optional<DeskItem> folder_child_at(int x, int y) const;
    DeskRequest request(DeskRequestKind, const DeskItem&, int value = -1);
};

} // namespace bmh::ui
