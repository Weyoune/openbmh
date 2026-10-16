// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ui/league_table.hpp"
#include "ui/calendar.hpp"
#include "ui/game_end.hpp"
#include "game/desk_actions.hpp"

namespace bmh::ui {

enum class FriendlyStage { opponents, venue, quote_pending, cost_confirmation, calendar, owner_approval, booking_pending, complete, unavailable };
enum class FriendlyDateMark { unavailable, available, available_flagged, current_day };
enum class FriendlyRequestKind { quote, book };

struct FriendlyDateOption {
    int season_day = -1;
    FriendlyDateMark mark = FriendlyDateMark::unavailable;
};

struct FriendlyCalendarCell { int x = 0, y = 0, width = 11, height = 8; };
FriendlyCalendarCell friendly_calendar_cell(const LeImage& executable, uint32_t season_start_counter, int season_day);

// Controller-facing value request. The app adds/validates the active desk revision,
// manager and request ID before calling the revisionless engine helpers.
struct FriendlyMatchRequest {
    FriendlyRequestKind kind = FriendlyRequestKind::quote;
    uint64_t request_id = 0;
    uint64_t quote_id = 0;
    uint64_t revision = 0;
    int manager_slot = -1;
    int runtime_team = -1;
    int opponent = -1;
    int opponent_manager = -1;
    int season_day = -1;
    int payer_manager = -1;
    int home_team = -1;
    int away_team = -1;
    int cost = 0;
    int planner_count = -1;
    int approving_manager = -1;
    bool manager_home = false;
    bool accept_cost = false;
    bool accept_opponent = false;
    int consenting_manager = -1;
    std::optional<NationalFriendlyBookingRequest> national_booking;
    bool operator==(const FriendlyMatchRequest&) const = default;
};
std::string friendly_match_request_json(const FriendlyMatchRequest& request);

struct FriendlyQuoteProjection {
    uint64_t request_id = 0;
    uint64_t revision = 0;
    int manager_slot = -1;
    int runtime_team = -1;
    int opponent = -1;
    int opponent_manager = -1;
    int payer_manager = -1;
    int home_team = -1;
    int away_team = -1;
    int cost = 0;
    int planner_count = -1;
    bool manager_home = false;
    std::vector<FriendlyDateOption> dates;
    std::optional<TaxDialogProjection> cost_question;
    std::optional<TaxDialogProjection> owner_question;
};

struct FriendlyBookingAck {
    uint64_t request_id = 0;
    uint64_t quote_id = 0;
    uint64_t revision = 0;
    bool accepted = false;
    int manager_slot = -1;
    int runtime_team = -1;
    int opponent = -1;
    int opponent_manager = -1;
    int season_day = -1;
    int payer_manager = -1;
    int home_team = -1;
    int away_team = -1;
    int cost = 0;
    int planner_count = -1;
};

struct FriendlyMatchState {
    uint64_t revision = 0;
    uint64_t first_request_id = 1;
    int manager_slot = -1;
    int runtime_team = -1;
    int current_day = -1;
    int planner_count = -1;
    uint32_t season_start_counter = 0;
    bool mode_three = false;
    bool national_team = false;
    std::string manager_name;
    std::array<LeagueTableState, 9> leagues;
    CalendarState calendar;
    std::optional<TaxDialogProjection> venue_question;
    int selected_opponent = -1; // Accepted FriendlyMatchScreen(2,0) return; never a UI row guess.
    int selected_league = -1;
};

class FriendlyMatchScreen {
public:
    FriendlyMatchScreen(const std::filesystem::path& data, FriendlyMatchState state,
                        int desk_menu_kind = 6);
    FriendlyMatchScreen(const std::filesystem::path& data, FriendlyMatchState state,
                        FriendlyQuoteProjection national_quote,
                        NationalFriendlyBookingRequest national_booking,
                        int desk_menu_kind = 6);
    FriendlyStage stage() const { return stage_; }
    int selected_league() const { return table_.selected_league(); }
    int selected_opponent() const { return selected_opponent_; }
    const LeagueTableScreen* opponent_table() const {
        return stage_==FriendlyStage::opponents ? &table_ : nullptr;
    }
    const std::optional<FriendlyQuoteProjection>& quote() const { return quote_; }
    bool close_requested() const { return close_requested_; }
    const std::string& feedback() const { return feedback_; }
    Image render() const;
    std::string state_json() const;
    bool move(int x, int y);
    bool pointer_down(int x, int y, bool right = false);
    std::optional<FriendlyMatchRequest> click(int x, int y, bool right_button = false);
    std::optional<FriendlyMatchRequest> key(std::string_view key);
    bool acknowledge_quote(FriendlyQuoteProjection quote);
    bool acknowledge_booking(const FriendlyBookingAck& ack);
    void reject(std::string message);
    const TaxDialogProjection* active_question() const { return question_projection(); }
    bool question_held() const {
        return pressed_question_ && *pressed_question_!=GameEndTaxDialog::Button::none;
    }
    bool question_release_pending() const { return pressed_question_.has_value(); }
    // ExpireQuestion forces button1 (No) for an expired active process timer.
    std::optional<FriendlyMatchRequest> expire_question();

private:
    FriendlyMatchState state_;
    int desk_menu_kind_ = 23;
    LeagueTableScreen table_;
    Font font_;
    LeImage exe_;
    std::filesystem::path data_;
    std::optional<CalendarScreen> calendar_;
    FriendlyStage stage_ = FriendlyStage::opponents;
    int selected_opponent_ = -1;
    bool manager_home_ = false;
    bool cost_confirmed_ = false;
    bool owner_approved_ = false;
    bool close_requested_ = false;
    uint64_t next_request_id_ = 1;
    uint64_t pending_booking_request_id_ = 0;
    std::optional<FriendlyQuoteProjection> quote_;
    std::optional<NationalFriendlyBookingRequest> national_booking_;
    std::optional<int> selected_day_;
    std::string feedback_;
    std::optional<GameEndTaxDialog::Button> pressed_question_;
    const TaxDialogProjection* question_projection() const;
    GameEndTaxDialog::Button question_button(int x, int y) const;
    std::optional<FriendlyMatchRequest> answer_question(GameEndTaxDialog::Button);
    void prepare_calendar();
    void text(Image&, int x, int y, std::string_view value, uint8_t color, int max_width) const;
    Image render_modal(Image image, std::string_view heading, std::string_view detail,
                       std::string_view left, std::string_view right) const;
    std::optional<FriendlyMatchRequest> book_request(int season_day, int approving_manager, int consenting_manager);
};

} // namespace bmh::ui
