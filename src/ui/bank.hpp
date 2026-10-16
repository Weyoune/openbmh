// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <array>
#include <optional>
#include <string>
#include <vector>

#include "formats/font.hpp"
#include "formats/le.hpp"
#include "formats/textd.hpp"
#include "formats/vga.hpp"
#include "game/bank_credit.hpp"
#include "ui/desk_state.hpp"
#include "ui/manager.hpp"
#include "ui/sprite_font.hpp"

namespace bmh::ui {

struct BankState {
    int manager_slot = -1, runtime_club = -1;
    std::string manager_name, club_name, source;
    uint64_t revision = 0;
    bool gradient_text = false;
    int32_t balance = 0, deposit = 0, credit_line = 0, investment_balance = 0;
    std::string balance_text, deposit_text, account_text;
    std::string investment_limit_text;
    int manager_portrait = 0, badge_sprite = 0;
    // MaxSharesToBuy case 1: the seven read-only values shown in the finance view.
    std::array<int32_t, 7> finance_values{};
    std::array<int32_t, 2> finance_gains{};
    std::array<std::string, 7> finance_value_text{};
    std::array<std::string, 2> finance_gain_text{};
    // Case 4 (bank credit) readonly values as seen before any local edits.
    // The source UI only displays the first seven when a credit is outstanding.
    std::array<int32_t, 8> credit_values{};
    std::array<std::string, 8> credit_value_text{};
    std::array<std::string, 8> credit_labels{};
    bmh::BankCreditContext credit;
    // Source MaxSharesToBuy lender candidates. The bank itself is counterparty 0;
    // other manager slots use slot+1 and are kept in ascending Source order.
    struct CreditLender {
        int manager_slot = -1;
        int counterparty = 0;
        std::string manager_name;
        bmh::BankCreditContext credit;
    };
    std::vector<CreditLender> credit_lenders;
    int credit_counterparty = 0;
    struct CreditNotice {
        int32_t refund = 0;
        std::string text;
        friend bool operator==(const CreditNotice&, const CreditNotice&) = default;
    };
    std::optional<CreditNotice> credit_notice;
    std::array<int32_t, 2> credit_currency{1, 1};
    std::array<std::string, 6> finance_fund_labels{}, finance_fund_prices{};
    std::array<std::string, 12> finance_stock_labels{}, finance_stock_prices{};
    struct Stock {
        int32_t long_term_value = 0, paid = 0;
        int max_to_buy = 0;
        uint16_t price = 0, previous_price = 0, owned = 0, trend = 0, trend_age = 0;
        std::array<uint16_t, 30> history{};
        std::array<std::string, 4> market_text{};
    };
    std::array<Stock, 12> stocks{};
    struct Fund {
        uint32_t price = 0, paid = 0;
        uint16_t owned = 0;
        int max_to_buy = 0;
        std::string name, unit_price_text, average_paid_text, quantity_text, one_quantity_text;
        std::string singular_suffix, plural_suffix;
    };
    std::array<Fund, 6> funds{};
    struct PropertyNotice {
        int asset_index = -1;
        int32_t additional_cost = 0;
        std::string text;
        friend bool operator==(const PropertyNotice&, const PropertyNotice&) = default;
    };
    std::optional<PropertyNotice> property_notice;
    std::optional<int> last_tip;
};

enum class BankRequestKind { none, move_to_deposit, buy_shares, sell_shares, stock_tip, buy_fund, sell_fund, bank_credit };
struct BankRequest {
    BankRequestKind kind = BankRequestKind::none;
    int manager_slot = -1, runtime_club = -1;
    uint64_t revision = 0;
    int32_t amount = 0; // positive: account -> fixed deposit; negative: fixed deposit -> account
    int asset_index = -1, count = 0; // asset_index addresses a stock or fund as selected by kind
    std::array<uint32_t, 6> expected_credit{};
    std::vector<bmh::BankCreditEdit> credit_edits;
    bool accept_credit_fees = false;
    // 0 the bank (case 4), else the lending manager + 1 (case 5, credit between managers)
    int credit_counterparty = 0;
    friend bool operator==(const BankRequest&, const BankRequest&) = default;
};

BankState bank_from_game(const GameState&, const LeImage&, uint64_t revision);
std::string bank_state_json(const BankState&);
std::string bank_request_json(const BankRequest&);

class BankScreen {
public:
    BankScreen(const std::filesystem::path&, BankState);
    Image render() const;
    BankRequest click(int x, int y, bool right = false);
    BankRequest key(std::string_view);
    bool text(std::string_view);
    bool close_requested() const { return close_requested_; }
    bool timed_notice_open() const { return investment_warning_open_; }
    bool expire_timed_notice() {
        if(!investment_warning_open_)return false;
        investment_warning_open_=false;return true;
    }
    bool calculator_open() const { return calculator_open_; }
    bool credit_open() const { return credit_open_; }
    const BankState& state() const { return state_; }
    const std::optional<BankRequest>& pending() const { return pending_; }
    void acknowledge(BankState);
    void reject_action(std::string);
    std::string state_json() const;
private:
    BankState state_;
    Image background_, deposit_frame_, finance_frame_, credit_lender_frame_, property_art_, calculator_art_, widgets_, faces_;
    std::optional<Image> badge_atlas_;
    bool investment_warning_open_ = false;
    LeImage exe_;
    TextDb texts_;
    Font font_;
    Font large_font_;
    SpriteFont sprite_font_;
    std::string amount_text_;
    std::string feedback_;
    std::optional<BankRequest> pending_;
    std::optional<int> visible_tip_;
    bool withdrawing_ = false, deposit_open_ = false, finance_open_ = false, calculator_open_ = false;
    bool exchange_open_ = false, property_open_ = false, close_requested_ = false;
    bool credit_open_ = false;
    bmh::BankCreditContext bank_credit_{};
    void restore_bank_credit();
    int credit_selected_row_ = -1;
    bmh::BankCreditDraft credit_draft_{};
    int calculator_credit_field_ = -1;
    uint32_t calculator_minimum_ = 0;
    bool credit_fee_confirmation_open_ = false, credit_notice_open_ = false;
    bool credit_fees_accepted_ = false;
    bool selling_shares_ = false;
    bool calculator_stock_count_ = false;
    bool calculator_property_count_ = false;
    int selected_stock_ = 11;
    int selected_fund_ = 0;
    int selected_owned_fund_ = -1;
    bool property_notice_open_ = false;
    uint32_t calculator_value_ = 0, calculator_max_ = 0, calculator_initial_ = 0;
    std::array<uint32_t, 8> calculator_presets_{};
    std::optional<Image> calculator_backdrop_;
    void text_at(Image&, int, int, std::string_view, uint8_t) const;
    void original_text(Image&, int, int, std::string_view, uint8_t) const;
    void body_baseline(Image&, int, int, std::string_view, uint8_t) const;
    void body_baseline_raw(Image&, int, int, std::string_view, uint8_t, bool gradient) const;
    void open_calculator(bool withdrawing);
    void open_stock_calculator(bool selling);
    void open_property_calculator(bool selling);
    void open_credit_calculator(int field);
    void refresh_credit_rows();
    BankRequest finish_credit();
    void draw_calculator(Image&) const;
    void draw_investment_warning(Image&) const;
    void draw_property_notice(Image&) const;
    void draw_credit_notice(Image&) const;
    void draw_credit_fee_confirmation(Image&) const;
    void draw_credit_lender_selector(Image&) const;
    void draw_property_card(Image&, int property, int y, bool owned) const;
    void draw_manager_dialog(Image&, std::string_view, bool with_ack_button) const;
    uint32_t accept_calculator();
    BankRequest submit();
};

} // namespace bmh::ui
