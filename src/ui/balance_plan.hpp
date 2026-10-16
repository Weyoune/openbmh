// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <optional>
#include <filesystem>
#include <array>
#include <string>
#include <string_view>
#include "formats/font.hpp"
#include "formats/le.hpp"
#include "formats/textd.hpp"
#include "formats/vga.hpp"
#include "game/balance_plan.hpp"
#include "ui/game_end.hpp"
#include "ui/outgoing_transfer.hpp"
#include "ui/sprite_font.hpp"

namespace bmh::ui {
struct BalanceIdentity {
    int manager_slot=-1, runtime_club=-1, club_identity=-1;
    uint32_t day_counter=0;
    uint64_t revision=0, scope=0, sequence=0;
    bool operator==(const BalanceIdentity&) const=default;
};
enum class BalanceAction : uint8_t { open, edit_row, cycle_view, take_actuals, close, confirm_close };
struct BalanceRequest {
    BalanceIdentity identity;
    BalanceAction kind=BalanceAction::open;
    int row=-1;
    int32_t value=0;
    bool operator==(const BalanceRequest&) const=default;
};
struct BalanceState {
    BalanceIdentity identity;
    BalancePlanDraft draft;
    Image underlay; // Captured accepted desktop; BalanceFromGame2 draws over this frame.
    std::string club_name; // Original ISO646-DE bytes.
    std::array<OutgoingTransferCalculatorProfile,kBalanceRows> calculators{};
    uint32_t currency_numerator=1, currency_denominator=1;
    TaxDialogProjection read_only_notice, close_notice;
    bool read_only_notice_enabled=true; // PresentManagerDialog BalancePlan_BalanceFromGame_RuntimeField2/CompactDialogFrame/BalancePlan_BalanceFromGame_RuntimeField visibility gate, resolved by owner.
    bool pending_close=false; // Core close staged once; warning precedes live publish.
    bool decorated=true, original_panel=true, gradient=true, cached_backdrop=false;
    int calculator_x=100, calculator_y=80; // BalanceFromGame persisted caller origin.
};
BalanceState balance_from_game(const GameState&,const BalancePlanDraft&,const LeImage&,
                              BalanceIdentity,const Image& underlay);

class BalanceScreen {
public:
    BalanceScreen(const std::filesystem::path&,BalanceState);
    Image render() const;
    bool pointer_down(int x,int y,bool right=false);
    bool release(int x,int y);
    bool move(int x,int y);
    bool key(std::string_view);
    bool process_timer_expired();
    bool text(std::string_view);
    bool tick_source_milliseconds(uint64_t now);
    void mark_presented_source_milliseconds(uint64_t now);
    void reject_action(std::string reason);
    const BalanceState& state() const { return state_; }
    const std::optional<BalanceRequest>& pending() const { return pending_; }
    bool calculator_open() const;
    bool notice_open() const;
    std::string state_json() const;
private:
    BalanceState state_;
    GameState format_state_;
    std::optional<BalanceRequest> pending_;
    std::filesystem::path data_;
    LeImage exe_;
    TextDb texts_;
    Font body_;
    Font heading_;
    Image calculator_art_, button_art_, panel_art_, frame_art_;
    SpriteFont sprite_;
    OutgoingTransferCalculatorProfile active_profile_;
    int active_row_=-1, pointer_x_=-1, pointer_y_=-1;
    int drag_offset_x_=0, drag_offset_y_=0;
    int pressed_calculator_control_=-1, flash_calculator_control_=-1, pressed_main_button_=-1;
    bool dragging_calculator_=false, main_button_armed_=false;
    bool calculator_value_edited_=false, calculator_cancel_waiting_=false;
    bool calculator_cancel_started_=false, calculator_deferred_accept_=false;
    uint32_t calculator_value_=0;
    std::string numeric_text_;
    bool calculator_open_=false, notice_visible_=false, close_notice_visible_=false;
    uint64_t notice_started_=0, current_milliseconds_=0, calculator_flash_until_=0, calculator_cancel_until_=0;
    bool notice_clock_started_=false;
    bool close_notice_finishes_=false;
    bool notice_consumed_=false;
    void open_calculator(int row);
    bool handle_calculator_key(std::string_view key);
    bool commit_calculator(bool accepted);
    void draw_panels(Image&) const;
    void draw_rows(Image&) const;
    void draw_calculator(Image&) const;
    void draw_buttons(Image&) const;
    void draw_notice(Image&) const;
    std::string source_money(int32_t value) const;
    BalanceRequest request(BalanceAction, int row=-1, int32_t value=0) const;
};
} // namespace bmh::ui
