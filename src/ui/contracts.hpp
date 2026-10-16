// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "formats/font.hpp"
#include "formats/le.hpp"
#include "ui/sprite_font.hpp"
#include "ui/lineup.hpp"

namespace bmh { struct GameState; }

namespace bmh::ui {

struct ContractPlayer {
    int row = -1;
    int player_id = -1;
    std::string name;
    int age = 0;
    int position = 0;
    uint8_t remaining_months = 0;
    uint32_t current_monthly_salary = 0;
    uint8_t insurance = 0;
    bool insurance_available = false;
    bool offer_blocked = false;
    uint16_t insurance_fee = 0;
    std::array<uint8_t, 7> team_premiums{};
    std::array<uint8_t, 4> attributes{};
    LineupPlayer source;
    uint8_t board_flags = 0;
};

struct ContractsState {
    bool gradient_text = true, decorated_background = true, mode_three = true;
    uint32_t currency_numerator = 1, currency_denominator = 1;
    int manager_slot = -1;
    int runtime_club = -1;
    uint64_t revision = 0;
    uint64_t request_id_base = 0;
    std::string club_name;
    std::string manager_name;
    int manager_portrait = 0;
    int badge_sprite = 0;
    std::vector<ContractPlayer> players;
    std::array<uint8_t, 7> team_premiums{};
    int selected_row = -1;
    std::optional<std::array<uint32_t, 4>> demands;
    uint64_t quote_id = 0;
    int quote_row = -1;
    std::string feedback;
};

enum class ContractActionKind : uint8_t { request_demands, submit_offer, submit_bonuses, insure_player };
struct ContractRequest {
    ContractActionKind kind = ContractActionKind::request_demands;
    uint64_t request_id = 0;
    uint64_t revision = 0;
    int manager_slot = -1;
    int runtime_club = -1;
    int row = -1;
    int player_id = -1;
    uint64_t quote_id = 0;
    uint8_t term_column = 0; // 0..3 represents one to four years
    uint32_t demand = 0;
    uint32_t monthly_salary = 0;
    std::array<uint8_t, 7> proposed_bonuses{};
    uint8_t insurance_months = 0;
    friend bool operator==(const ContractRequest&, const ContractRequest&) = default;
};

struct ContractActionResult {
    bool success = false;
    bool accepted = false;
    uint64_t quote_id = 0;
    std::array<uint32_t, 4> demands{};
    std::string feedback;
};

ContractsState contracts_from_game(const GameState&, int manager_slot, uint64_t revision);
std::string contract_request_json(const ContractRequest&);
std::string contracts_state_json(const ContractsState&);

class ContractsScreen {
public:
    ContractsScreen(const std::filesystem::path&, ContractsState);
    Image render() const;
    bool click(int x, int y, bool right = false);
    bool process_timer_expired(int pointer_x, int pointer_y);
    bool key(std::string_view);
    bool move(int x, int y);
    int take_player_info_request() { const int row=player_info_row_; player_info_row_=-1; return row; }
    bool text(std::string_view);
    void acknowledge_quote(const ContractsState&);
    void acknowledge_offer(const ContractsState&);
    void acknowledge_bonus_offer(const ContractsState&);
    void acknowledge_insurance(const ContractsState&);
    void reject(std::string feedback);
    bool close_requested() const { return close_requested_; }
    bool result_page_open() const { return page_ == Page::result; }
    const ContractsState& state() const { return state_; }
    const ContractRequest* pending() const { return pending_ ? &*pending_ : nullptr; }
    std::string state_json() const;
private:
    enum class Page : uint8_t { roster, player_offer, team_premiums, calculator, insurance, result };
    ContractsState state_;
    LeImage exe_;
    Font font_;
    Font large_font_;
    SpriteFont sprite_font_;
    TextDb texts_;
    Image atlas_, flags_;
    int hovered_row_ = -1;
    int player_info_row_ = -1;
    Image render_roster() const;
    Image background_;
    Image widgets_;
    Image calculator_;
    Image faces_;
    Image badge_atlas_;
    std::optional<ContractRequest> pending_;
    std::optional<Image> result_backdrop_;
    std::optional<Image> calculator_backdrop_;
    uint64_t next_request_id_ = 1;
    uint32_t salary_input_ = 0;
    uint32_t calculator_initial_ = 0;
    uint32_t offer_salary_ = 0;
    std::array<uint32_t, 8> calculator_presets_{};
    bool salary_set_ = false;
    bool team_premium_mode_ = false;
    uint8_t selected_term_ = 0;
    uint8_t offer_term_ = 0;
    uint8_t insurance_term_ = 0;
    bool salary_offer_set_ = false;
    bool editing_salary_ = false;
    Page page_ = Page::roster;
    Page result_return_ = Page::roster;
    uint8_t tab_ = 2;
    Page calculator_return_ = Page::player_offer;
    int calculator_bonus_index_ = -1;
    std::array<uint8_t, 7> proposed_bonuses_{};
    uint8_t insurance_months_ = 0;
    bool close_requested_ = false;
    uint32_t calculator_max_ = 200000;
    void open_calculator(int target, Page return_page, uint32_t initial);
    void text(Image&, int x, int baseline, std::string_view utf8, uint8_t color, int right = 314) const;
    void emit(ContractRequest);
};

} // namespace bmh::ui
