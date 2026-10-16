// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "formats/font.hpp"
#include "formats/le.hpp"
#include "formats/textd.hpp"
#include "formats/vga.hpp"
#include "game/desk_actions.hpp"
#include "game/stadium_blocks.hpp"
#include "ui/desk_state.hpp"
#include "ui/game_end.hpp"
#include "ui/sprite_font.hpp"

namespace bmh::ui {

struct StadiumInvestmentRow {
    StadiumInvestment investment = StadiumInvestment::Floodlights;
    std::string original_label; // Original ISO646-DE bytes
    uint8_t current_status = 0, current_level = 0;
    uint8_t construction_weeks = 0;
    bool available = false;
    std::vector<StadiumInvestmentOption> options;
    std::vector<std::string> option_price_text; // Original formatted display, parallel to options
};

struct StadiumBlockOverview {
    uint8_t construction_weeks = 0; // manager record +0x34+s
    uint8_t flags = 0;              // manager record +0x44+s
    uint8_t capacity_level = 0;     // manager record +0x4c+s
    uint8_t capacity = 0;           // manager record +0x54+s, in thousands
};

// App-owned quote plus renderer-visible draft for Source choices 8..15. The
// quote is copied unchanged into every block request; edits remain presentation
// state until CareerSession returns an accepted projection.
struct StadiumBlockExpansionEditor {
    StadiumBlockExpansionQuote quote;
    uint32_t total_target_places = 0;
    uint8_t desired_features = 0;
    int header_text_variant = -1; // App supplies Source G; rendering consumes no RNG.
    std::optional<StandBlockOffer> offer;
    std::string current_places_text, new_places_text, target_places_text;
    std::string per_thousand_cost_text, new_places_cost_text, seats_cost_text;
    std::string roof_cost_text, total_cost_text, discount_text;
};

struct StadiumDataProjection {
    uint8_t flags = 0x40, ticket_price = 0;
    std::array<std::vector<int32_t>, 6> series;
    std::array<int32_t, 6> minimum{}, maximum{};
    std::array<std::array<std::string, 3>, 7> table;
    std::string required_attendance_text;
    bool operator==(const StadiumDataProjection&) const = default;
};
StadiumDataProjection stadium_data_from_game(const GameState&, const LeImage&, int manager, uint8_t flags);
std::vector<std::pair<int, int>> stadium_data_points(const StadiumDataProjection&, const LeImage&, int curve);

struct StadiumState {
    int manager_slot = -1, runtime_club = -1;
    uint64_t revision = 0;
    std::string source, club_name;
    int32_t balance = 0;
    int construction_percent = 100;
    bool opened = false;

    // Existing fourteen-object periphery projection remains independent.
    std::array<uint8_t, 14> level{}, days{};
    // Kept in-place for old projection/aggregate callers while the App migrates;
    // the Issue 16 UI never displays or routes these synthetic six-slot fields.
    std::array<uint8_t, 6> block_days{}, block_status{};
    std::array<std::array<uint32_t, 3>, 6> block_prices{};
    std::array<uint8_t, 6> block_build_days{};
    std::optional<int> quoted_object;
    int32_t quoted_price = 0;
    std::optional<int> selected_block;
    int block_text_variant = -1;
    std::optional<int> block_offer_slot;
    int block_offer_option = -1, block_offer_percent = 0, block_offer_days = 0;
    uint32_t block_offer_base_price = 0, block_offer_price = 0;
    std::string action_feedback;

    // New issue-16 state is appended to retain legacy aggregate initialization order.
    std::optional<int> stable_club_identity;
    uint64_t scope = 0, sequence = 0;
    std::string balance_text;
    bool investments_enabled = true;
    std::array<StadiumInvestmentRow, 8> investments{};
    std::optional<StadiumInvestment> selected_investment;
    std::optional<int> selected_option;
    int investment_text_variant = -1;
    std::optional<TaxDialogProjection> construction_tip_dialog;
    uint32_t construction_tip_price = 0;

    // Source A–H overview values and an independently owned block editor quote.
    std::array<StadiumBlockOverview, 8> blocks{};
    std::optional<StadiumBlockExpansionEditor> block_expansion;

    // Original PlainWindowFrameUsedBy header content and background mode. Strings stay in
    // Original bytes until the font renderer converts them for display.
    std::string original_stadium_title, original_stadium_footer;
    bool decorated_stadium_header = true;
    // These are independent Original runtime bytes, not aliases. The stadium
    // header decoration is controlled by !PlainBackground; CompactDialogFrame remains a
    // separate immutable source context value.
    bool plain_background = false;
    bool compact_dialog_frame = false;
    // LoadGameEndTaxAnimationAssets selects textured KB/56 panels when SolidPanelBackground is zero and the
    // Source flat 0x42 fill when it is nonzero. This is independent of the
    // decorated stadium header and the notice gate.
    bool solid_panel_background = false;
    // App resolves TextGradientDisabled==0 && BMH.exe[TextGradientMode]==2 from authorized live
    // memory/runtime/EXE inputs. The renderer never reads those inputs itself.
    bool gradient_text = false;
    // Immutable Source context needed to verify the immediate post-build
    // ticket-compensation projection. App populates these from accepted game
    // records/EXE data; the UI derives expected effects without writing Native.
    uint8_t source_level = 0;
    uint16_t season_tickets = 0;
    uint8_t live_league_round = 0;
    int32_t ticket_refund_rate = 0;
    // ConstructionPricePercent periphery hover and PresentManagerDialog quote, formatted by the game owner.
    std::array<std::string, 14> periphery_price_text{}, periphery_duration_text{};
    std::string periphery_balance_text;
    uint32_t periphery_animation_tick = 0;
    std::optional<TaxDialogProjection> periphery_quote_dialog;
    std::optional<StadiumDataProjection> data;
};

enum class StadiumRequestKind {
    none, open, quote_periphery, decline_periphery, build_periphery,
    // Legacy vocabulary remains source-compatible while CareerSession migrates;
    // the stadium UI does not emit these fake-block requests.
    select_block, prepare_block_offer, decline_block_offer, build_stand_block,
    select_investment, select_investment_option, cancel_investment,
    confirm_investment, answer_construction_tip, close,
    expansion_select, expansion_set_total, expansion_toggle_feature,
    expansion_cancel, expansion_confirm, expansion_answer_tip,
    open_data, toggle_data_curve, toggle_data_scale, set_data_ticket_price, close_data
};
struct StadiumRequest {
    StadiumRequestKind kind = StadiumRequestKind::none;
    int manager_slot = -1, runtime_club = -1, object = -1;
    uint64_t revision = 0;
    int32_t price = 0;
    int option = -1;
    // New Issue 16 fields are appended for compatibility with periphery aggregates.
    bool accept_tip = false;
    std::optional<int> stable_club_identity;
    uint64_t scope = 0, sequence = 0;
    int block_slot = -1;
    uint32_t block_total_places = 0;
    uint8_t block_features = 0;
    std::optional<StadiumBlockExpansionQuote> expected_block_quote;
};

StadiumState stadium_from_game(const GameState&, const LeImage&, uint64_t revision,
                               int construction_percent = 100);
void stadium_apply_price_table(StadiumState&, const LeImage&, int construction_percent);
std::string stadium_state_json(const StadiumState&);
std::string stadium_request_json(const StadiumRequest&);

class StadiumScreen {
public:
    StadiumScreen(const std::filesystem::path&, StadiumState);
    StadiumRequest initialize();
    Image render() const;
    // Pure underlay for the nested Source construction notices. This includes
    // the selected investment view but deliberately excludes the tip overlay.
    Image construction_notice_backdrop() const;
    // Source button helpers paint on DOWN and produce actions only on a matching UP.
    bool pointer_down(int x, int y, bool right = false);
    bool pointer_move(int x, int y);
    StadiumRequest release(int x, int y, bool right = false);
    // Compatibility convenience for callers with a complete click event.
    StadiumRequest click(int x, int y, bool right = false);
    StadiumRequest key(std::string_view);
    StadiumRequest process_timer_expired();
    bool text(std::string_view);
    // Source timeout outcome for the optional construction offer. Uses the
    // accepted projection and is suppressed while another action is pending.
    StadiumRequest expire_construction_tip();
    bool close_requested() const { return close_requested_; }
    bool construction_tip_held() const;
    // Source calculator geometry and waits are driven by the presentation loop.
    // Drag coordinates are caller-screen coordinates, clamped to Source's range.
    bool calculator_drag_to(int x, int y);
    void calculator_mark_presented();
    bool calculator_advance_source_ticks(unsigned ticks);
    bool calculator_source_waiting() const;
    bool calculator_waiting_for_key_release() const;
    // During N's Source exit wait the actual editor underlay is restored,
    // while the host must still keep calculator input blocked.
    bool calculator_open() const { return block_calculator_open_ && !block_calculator_cancel_wait_; }
    const StadiumState& state() const { return state_; }
    const std::optional<StadiumRequest>& pending() const { return pending_; }
    void acknowledge(StadiumState);
    void reject_action(std::string);
    std::string state_json() const;
private:
    enum class Pressed { none, scene_button, investment, option, build, cancel, tip_yes, tip_no, periphery_item, periphery_yes, periphery_no, close, block_row, block_amount, block_seats, block_roof, block_build, block_cancel, block_calculator_key, block_calculator_preset, block_calculator_drag, data_control };
    StadiumState state_;
    std::filesystem::path data_;
    LeImage exe_;
    TextDb texts_;
    Image background_, header_background_, interior_, facilities_, panel_, periphery_overlay_, widgets_, calculator_art_;
    Font font_, heading_font_;
    SpriteFont sprite_font_;
    std::optional<StadiumRequest> pending_;
    bool close_requested_ = false;
    bool panorama_ = false;
    bool future_information_ = false;
    bool block_calculator_open_ = false;
    int block_calculator_x_ = 111, block_calculator_y_ = 31;
    uint32_t block_calculator_value_ = 0;
    std::string block_calculator_text_;
    unsigned block_calculator_digits_ = 0; // BalanceFromGame local18 also counts typed zeroes.
    int block_calculator_pressed_key_ = -1;
    unsigned block_calculator_ticks_ = 0;
    bool block_calculator_presented_ = false;
    bool block_calculator_cancel_wait_ = false;
    bool block_calculator_keyboard_j_ = false;
    bool block_calculator_edited_ = false;
    std::string feedback_;
    Pressed pressed_ = Pressed::none;
    int pressed_index_ = -1;
    int pressed_x_ = -1, pressed_y_ = -1;
    std::optional<GameEndTaxDialog> tip_dialog_, periphery_dialog_;
    int periphery_hover_ = -1;
    int periphery_object_at(int x, int y) const;
    bool periphery_object_contains(int object, int x, int y) const;
    void render_panorama(Image&) const;
    void text_at(Image&, int, int, std::string_view, uint8_t, bool gradient = true) const;
    void original_button(Image&, int, int, int, int, std::string_view, bool) const;
    Image render_scene() const;
    Image render_data() const;
    int data_control_at(int x, int y) const;
    Image render_investment_dialog(Image) const;
    Image render_block_expansion(Image) const;
    Image render_block_calculator(Image) const;
    void rebuild_tip_dialog();
    StadiumRequest request(StadiumRequestKind, int object = -1, int option = -1,
                           int32_t price = 0, bool accept_tip = false) const;
    StadiumRequest block_request(StadiumRequestKind, uint32_t total = 0,
                                 uint8_t features = 0) const;
    StadiumRequest activate_block_calculator_key(int);
    StadiumRequest accept_block_calculator_value();
};

} // namespace bmh::ui
