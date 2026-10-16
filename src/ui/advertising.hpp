// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "formats/font.hpp"
#include "formats/le.hpp"
#include "formats/textd.hpp"
#include "formats/vga.hpp"
#include "game/desk_actions.hpp"
#include "ui/sprite_font.hpp"

namespace bmh { struct GameState; }

namespace bmh::ui {

struct AdvertisingContract {
    int record_slot = -1;          // manager record +0xa34 + slot*0x0c
    int sponsor_id = -1;           // contract byte +7; indexes the original TEXTD/logo set
    std::string sponsor_name;      // owned UTF-8, from the matching TEXTD pointer table
    std::string amount_text;       // original FormatNumber(0x101) currency formatting, owned UTF-8
    int32_t monthly_amount = 0;    // contract dword +0
    uint8_t months_remaining = 0;  // byte +5
    uint8_t extension_months = 0;  // byte +6, low seven bits
    bool option_marker = false;    // byte +6, high bit (shown by OriginalFinancePreview3)
    bool active = false;
    std::array<uint8_t, 12> bytes{};
};

struct AdvertisingFinancePreview {
  int32_t income = 0;   // FinanceTotalsFromGame3(0), exact Original preview formula
  int32_t expenses = 0; // OriginalFinancePreview(0), exact Original preview formula
  int32_t balance = 0;  // income - expenses, OriginalFinancePreview2
  std::string income_text;   // FormatNumber(..., 0x188, AdvertisingRuntimeField2), owned UTF-8
  std::string expenses_text;
  std::string balance_text;
};

// Read-only Source monthly preview shared by statistics page 5.
AdvertisingFinancePreview original_finance_preview(const bmh::GameState&, int manager);

struct AdvertisingState {
    int manager_slot = -1;
    int runtime_team_id = -1;
    uint64_t revision = 0;
    std::string club_name;         // owned UTF-8
    std::string title;             // original TEXTD title (Werbung)
    std::string manager_name;
    int manager_portrait = -1;
    int badge_sprite = -1;
    std::string notification_text;
    std::string notification_ack_label;
    std::string spending_blocked_text;
    std::string spending_funds_text;
    bool notification_warning = false;
    bool original_panel = true;    // SolidPanelBackground clear selects KB/56 panel art
    bool decorated_background = true; // PlainBackground clear selects KB/129
    bool font_gradient = true;    // TextGradientDisabled clear selects six-row FNT3 foreground
    std::vector<AdvertisingContract> boards; // visible order after AdBoardOrder (14 boards)
    AdvertisingContract shirt;     // contract slot 14, manual: Trikotwerbung
    AdvertisingContract television; // contract slot 15, manual: TV-Rechte
    AdvertisingFinancePreview finance_preview;
    std::array<std::string, 3> finance_labels; // TEXTD Advertising_FormatError_TextPointer16/ae288/ae28c
    std::array<std::string, 3> auxiliary_text; // AdvertisingFromGame, hitboxes 16..18
    std::array<bmh::AdvertisingOfferList, 16> offers;
    std::array<bmh::AdvertisingSpendingView, 3> spending;
    std::array<std::string, 3> spending_labels;
    std::string merchandise_calculator_title, budget_calculator_title;
    std::array<std::string, 10> board_sponsor_names;
    std::array<std::string, 10> television_sponsor_names;
    std::array<std::array<std::string, 10>, 16> offer_amount_text;
    std::string duration_label, month_suffix, option_label, terminable_label, boards_label;
    std::string offer_label, bonus_label, per_month_suffix, no_offer_text, space_available_text;
    std::string cancel_label, accept_label;
};

AdvertisingState advertising_from_game(const bmh::GameState&, int manager_slot, uint64_t revision = 0);
std::string advertising_state_json(const AdvertisingState&, int selected_contract = -1,
                                   bool close_requested = false, int selected_offer = -1,
                                   bool offer_open = false);

enum class AdvertisingActionKind : uint8_t { accept_offer, set_spending, check_renewal };

struct AdvertisingRequest {
    uint64_t request_id = 0;
    int manager_slot = -1;
    int runtime_club = -1;
    uint64_t revision = 0;
    AdvertisingActionKind kind = AdvertisingActionKind::accept_offer;
    bmh::AdvertisingOfferRequest offer;
    bmh::AdvertisingSpendingRequest spending;
    int renewal_display_slot = -1;
    int renewal_contract_slot = -1;
    std::array<uint8_t, 12> renewal_expected_contract{};
    bool operator==(const AdvertisingRequest&) const = default;
};

class AdvertisingScreen {
public:
    AdvertisingScreen(const std::filesystem::path& data, AdvertisingState state,
                      uint64_t request_id_seed = 1);
    Image render() const;
    bool click(int x, int y);
    bool key(std::string_view key);
    bool open_offers(int display_slot);
    bool text(std::string_view input);
    const std::optional<AdvertisingRequest>& pending() const { return pending_; }
    bool reject_action(std::string reason);
    int selected_contract() const { return selected_contract_; }
    int selected_offer() const { return selected_offer_; }
    bool offer_open() const { return offer_open_; }
    bool spending_calculator_open() const { return spending_calculator_open_; }
    bool timed_notice_open() const { return notification_open() && state_.notification_warning; }
    bool expire_timed_notice() { return timed_notice_open() && dismiss_notification(); }
    bool notification_open() const { return !state_.notification_text.empty() && !notification_dismissed_; }
    int selected_spending_slot() const { return selected_spending_slot_; }
    uint32_t spending_input() const { return spending_input_; }
    bool close_requested() const { return close_requested_; }
    std::string state_json() const;
private:
    AdvertisingState state_;
    LeImage exe_;
    TextDb texts_;
    Font font_;
    Font large_font_;
    SpriteFont sprite_font_;
    Image desktop_, art_, logos_, panel_;
    Image widgets_;
    Image calculator_;
    Image faces_;
    std::optional<Image> badge_atlas_;
    std::optional<Image> spending_backdrop_;
    int selected_contract_ = -1;
    int selected_spending_slot_ = -1;
    int selected_offer_ = -1;
    uint64_t next_request_id_ = 1;
    bool offer_open_ = false;
    bool spending_calculator_open_ = false;
    bool notification_dismissed_ = false;
    bool close_requested_ = false;
    uint32_t spending_input_ = 0;
    uint32_t spending_initial_ = 0;
    uint32_t spending_maximum_ = 0;
    std::array<uint32_t, 8> spending_presets_{};
    std::string feedback_;
    std::optional<AdvertisingRequest> pending_;
    void text(Image&, int x, int baseline, std::string_view, uint8_t color,
              int max_width = 310, bool use_gradient = true) const;
    void begin_offer_browser(int display_slot);
    void submit_selected_offer();
    void open_spending_calculator(int display_slot);
    bool accept_spending_calculator();
    void close_spending_calculator();
    bool dismiss_notification();
    void draw_spending_calculator(Image&) const;
    void draw_notification(Image&) const;
    void pattern_rect(Image&, int x, int y, int width, int height) const;
    void source_frame(Image&, int x, int y, int width, int height, bool shadow = true) const;
    void button(Image&, int x, int y, int width, std::string_view label) const;
};

} // namespace bmh::ui
