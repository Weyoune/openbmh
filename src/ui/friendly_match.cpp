// SPDX-License-Identifier: GPL-3.0-only
#include "ui/friendly_match.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <sstream>

#include "game/state.hpp"
#include "ui/desk_state.hpp"
#include "ui/manager.hpp"
#include "ui/setup.hpp"

namespace bmh::ui {
namespace {
bool left_modal_button(int x, int y) { return x >= 76 && x < 154 && y >= 139 && y < 158; }

std::string stage_name(FriendlyStage stage) {
    switch (stage) {
    case FriendlyStage::opponents: return "opponents";
    case FriendlyStage::venue: return "venue";
    case FriendlyStage::quote_pending: return "quote_pending";
    case FriendlyStage::cost_confirmation: return "cost_confirmation";
    case FriendlyStage::calendar: return "calendar";
    case FriendlyStage::owner_approval: return "owner_approval";
    case FriendlyStage::booking_pending: return "booking_pending";
    case FriendlyStage::complete: return "complete";
    case FriendlyStage::unavailable: return "unavailable";
    }
    return "unavailable";
}

int initial_league(const FriendlyMatchState& state) {
    if (state.selected_league>=0 && state.selected_league<9) return state.selected_league;
    for (size_t league = 0; league < state.leagues.size(); ++league)
        if (std::any_of(state.leagues[league].rows.begin(), state.leagues[league].rows.end(),
                        [&](const LeagueTableRow& row) { return row.runtime_team_id == state.runtime_team; }))
            return int(league);
    return 0;
}
bool needs_other_manager(const FriendlyQuoteProjection& quote,int manager) {
    return (quote.payer_manager>=0&&quote.payer_manager!=manager)||
           (quote.opponent_manager>=0&&quote.opponent_manager!=manager);
}
}

std::string friendly_match_request_json(const FriendlyMatchRequest& request) {
    std::ostringstream out;
    out << "{\"kind\":" << json_quote(request.kind == FriendlyRequestKind::quote ? "quote" : "book")
        << ",\"request_id\":" << request.request_id << ",\"quote_id\":" << request.quote_id
        << ",\"revision\":" << request.revision << ",\"manager_slot\":" << request.manager_slot
        << ",\"runtime_team\":" << request.runtime_team << ",\"opponent\":" << request.opponent
        << ",\"opponent_manager\":" << request.opponent_manager << ",\"season_day\":" << request.season_day
        << ",\"payer_manager\":" << request.payer_manager << ",\"home_team\":" << request.home_team
        << ",\"away_team\":" << request.away_team << ",\"cost\":" << request.cost
        << ",\"planner_count\":" << request.planner_count
        << ",\"approving_manager\":" << request.approving_manager
        << ",\"manager_home\":" << (request.manager_home ? "true" : "false")
        << ",\"accept_cost\":" << (request.accept_cost ? "true" : "false")
        << ",\"accept_opponent\":" << (request.accept_opponent ? "true" : "false")
        << ",\"consenting_manager\":" << request.consenting_manager << '}';
    return out.str();
}

FriendlyCalendarCell friendly_calendar_cell(const LeImage& executable, uint32_t season_start_counter, int season_day) {
    if (season_day < 0 || season_day > 365) throw FormatError("friendly calendar: invalid season day");
    const auto date = desk_date(season_start_counter + uint32_t(season_day), executable);
    CalendarDayProjection day;
    day.date = date;
    const auto cell = calendar_cell(day);
    return {cell.x, cell.y, cell.width, cell.height};
}

FriendlyMatchScreen::FriendlyMatchScreen(const std::filesystem::path& data, FriendlyMatchState state,
                                         int desk_menu_kind)
    : state_(std::move(state)), desk_menu_kind_(desk_menu_kind),
      table_(data, state_.leagues, initial_league(state_), LeagueTableScreen::OpponentMode{}),
      font_(load_font(asset_path(data / "F", "BMH2.FNT"))),
      exe_(load_le(asset_path(data, "BMH.exe"))),
      data_(data), next_request_id_(state_.first_request_id) {
    const bool valid_national_slot = state_.national_team && state_.runtime_team == bmh::kNoTeam;
    if (state_.manager_slot < 0 || state_.manager_slot > 3 || state_.runtime_team < 0 ||
        (state_.runtime_team >= 168 && !valid_national_slot) ||
        state_.revision == UINT64_MAX || !state_.first_request_id || state_.planner_count < 0 ||
        state_.planner_count > std::numeric_limits<int>::max() - 2)
        throw FormatError("friendly screen: invalid manager/request identity");
    if (!state_.mode_three) {
        stage_ = FriendlyStage::unavailable;
        feedback_ = "Freundschaftsspiele sind nur in Modus III verfuegbar.";
    } else if (state_.national_team || state_.runtime_team == bmh::kNoTeam) {
        stage_ = FriendlyStage::unavailable;
        feedback_ = "Freundschaftsspiele fuer die Nationalmannschaft sind hier nicht verfuegbar.";
    } else if (desk_menu_kind_ != 6) {
        stage_ = FriendlyStage::unavailable;
        feedback_ = "Diese Freundschaftsspiel-Ansicht ist nicht verfuegbar.";
    }
    if (state_.selected_opponent>=0) {
        if (stage_!=FriendlyStage::opponents || state_.selected_opponent==state_.runtime_team ||
            std::none_of(table_.state().rows.begin(),table_.state().rows.end(),
                [&](const auto& row){return row.runtime_team_id==state_.selected_opponent;}))
            throw FormatError("friendly screen: invalid authoritative opponent return");
        selected_opponent_=state_.selected_opponent;
        stage_=FriendlyStage::venue;
    }
}

FriendlyMatchScreen::FriendlyMatchScreen(const std::filesystem::path& data, FriendlyMatchState state,
        FriendlyQuoteProjection national_quote, NationalFriendlyBookingRequest national_booking,
        int desk_menu_kind)
    : FriendlyMatchScreen(data, std::move(state), desk_menu_kind) {
    const bool source_owner = desk_menu_kind == 6 && national_quote.runtime_team == bmh::kNoTeam &&
        national_booking.owner.menu_kind == 6 && national_booking.owner.manager == national_quote.manager_slot &&
        national_booking.owner.runtime_team == bmh::kNoTeam &&
        national_booking.owner.revision == national_quote.revision &&
        national_booking.quote.terms.opponent == national_quote.opponent &&
        national_booking.quote.terms.manager_home == national_quote.manager_home &&
        national_booking.quote.terms.planner_count == national_quote.planner_count;
    if (!source_owner || !state_.national_team || state_.runtime_team != bmh::kNoTeam ||
        state_.manager_slot != national_quote.manager_slot || state_.revision != national_quote.revision ||
        national_quote.dates.size() != 366 || national_quote.cost < 0 ||
        national_booking.quote.terms.status != bmh::FriendlyQuoteStatus::quoted ||
        national_booking.quote.owner.request_id == 0) {
        stage_ = FriendlyStage::unavailable;
        feedback_ = "Diese Freundschaftsspielanfrage ist nicht mehr aktuell.";
        return;
    }
    const int expected_home = national_quote.manager_home ? bmh::kNoTeam : national_quote.opponent;
    const int expected_away = national_quote.manager_home ? national_quote.opponent : bmh::kNoTeam;
    if (national_quote.home_team != expected_home || national_quote.away_team != expected_away ||
        national_quote.payer_manager != national_booking.quote.terms.payer_manager ||
        national_quote.opponent_manager != national_booking.quote.terms.opponent_manager ||
        national_quote.cost != national_booking.quote.terms.cost) {
        stage_ = FriendlyStage::unavailable;
        feedback_ = "Das Freundschaftsspielangebot ist veraltet.";
        return;
    }
    quote_ = std::move(national_quote);
    national_booking_ = std::move(national_booking);
    selected_opponent_ = quote_->opponent;
    manager_home_ = quote_->manager_home;
    cost_confirmed_ = true;
    owner_approved_ = true;
    stage_ = FriendlyStage::calendar;
    feedback_.clear();
    prepare_calendar();
}

void FriendlyMatchScreen::prepare_calendar() {
    if (!quote_) throw FormatError("friendly calendar: missing owned quote");
    std::array<bool, 366> seen{};
    for (const auto& option : quote_->dates) {
        if (option.season_day < 0 || option.season_day > 365 || seen[size_t(option.season_day)])
            throw FormatError("friendly calendar: inconsistent owned date options");
        seen[size_t(option.season_day)] = true;
    }
    if (std::find(seen.begin(), seen.end(), false) != seen.end())
        throw FormatError("friendly calendar: incomplete owned date options");
    auto calendar = state_.calendar;
    if (calendar.days.empty() ||
        calendar.season_start_counter + uint32_t(calendar.leading_days) != state_.season_start_counter ||
        calendar.current_day - calendar.leading_days != state_.current_day)
        throw FormatError("friendly calendar: calendar and quote have different native anchors");
    calendar.friendly_selection = true;
    for (auto& day : calendar.days) {
        const auto option = std::find_if(quote_->dates.begin(), quote_->dates.end(),
            [&](const FriendlyDateOption& value) { return value.season_day == day.season_day; });
        day.friendly_target = option != quote_->dates.end() &&
            (option->mark == FriendlyDateMark::available || option->mark == FriendlyDateMark::available_flagged);
    }
    calendar_.emplace(data_, std::move(calendar), state_.manager_name, true, state_.runtime_team);
}

bool FriendlyMatchScreen::move(int x, int y) {
    if (!close_requested_ && stage_==FriendlyStage::opponents) return table_.move(x,y);
    if(pressed_question_&&*pressed_question_!=GameEndTaxDialog::Button::none&&
       question_button(x,y)!=*pressed_question_) {
        pressed_question_=GameEndTaxDialog::Button::none;return true;
    }
    return !close_requested_ && stage_ == FriendlyStage::calendar && calendar_ && calendar_->move(x, y);
}

const TaxDialogProjection* FriendlyMatchScreen::question_projection() const {
    if(stage_==FriendlyStage::venue)return state_.venue_question?&*state_.venue_question:nullptr;
    if(stage_==FriendlyStage::cost_confirmation&&quote_)
        return quote_->cost_question?&*quote_->cost_question:nullptr;
    if(stage_==FriendlyStage::owner_approval&&quote_)
        return quote_->owner_question?&*quote_->owner_question:nullptr;
    return nullptr;
}

GameEndTaxDialog::Button FriendlyMatchScreen::question_button(int x,int y) const {
    const auto* p=question_projection();
    if(!p)return GameEndTaxDialog::Button::none;
    return GameEndTaxDialog(data_,*p,{},table_.render()).hit_test(x,y);
}

bool FriendlyMatchScreen::pointer_down(int x,int y,bool right) {
    if (!close_requested_ && stage_==FriendlyStage::opponents) return table_.pointer_down(x,y,right);
    if(close_requested_||right||!question_projection())return false;
    pressed_question_=question_button(x,y);return true;
}

void FriendlyMatchScreen::text(Image& image, int x, int y, std::string_view value,
                               uint8_t color, int max_width) const {
    std::string bytes;
    try { bytes = utf8_to_original(value); }
    catch (const FormatError&) {
        for (unsigned char c : value) bytes += c < 128 ? char(c) : '?';
    }
    while (!bytes.empty() && font_.text_width(bytes) > max_width) bytes.pop_back();
    if (x >= 0 && y >= 0 && y + font_.height <= image.height) font_.draw(image, x, y, bytes, color);
}

Image FriendlyMatchScreen::render_modal(Image image, std::string_view heading, std::string_view detail,
                                        std::string_view left, std::string_view right) const {
    constexpr int x0 = 48, y0 = 76, x1 = 272, y1 = 165;
    for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) image.at(x, y) = 0x42;
    for (int x = x0; x < x1; ++x) { image.at(x, y0) = 0x5f; image.at(x, y1 - 1) = 0x5f; }
    for (int y = y0; y < y1; ++y) { image.at(x0, y) = 0x5f; image.at(x1 - 1, y) = 0x5f; }
    text(image, x0 + 12, y0 + 10, heading, 0x4f, x1 - x0 - 24);
    text(image, x0 + 12, y0 + 31, detail, 0x6a, x1 - x0 - 24);
    const int button_y = y0 + 67;
    text(image, x0 + 34, button_y, left, 0x4f, 72);
    text(image, x0 + 126, button_y, right, 0x4f, 72);
    return image;
}

Image FriendlyMatchScreen::render() const {
    if (stage_ == FriendlyStage::opponents) {
        return table_.render();
    }
    if(stage_==FriendlyStage::venue||stage_==FriendlyStage::cost_confirmation||
       stage_==FriendlyStage::owner_approval) {
        const auto* p=question_projection();
        if(!p||p->kind!=TaxDialogKind::question)
            throw FormatError("friendly question: missing Source question projection");
        return GameEndTaxDialog(data_,*p,{},table_.render()).render(pressed_question_);
    }
    if (stage_ == FriendlyStage::quote_pending)
        return render_modal(table_.render(), "Freundschaftsspiel", "Kosten und Termine werden geprueft.", "", "");
    if (stage_ == FriendlyStage::booking_pending)
        return render_modal(table_.render(), "Freundschaftsspiel", "Termin wird verbindlich geprueft.", "", "");
    if (stage_ == FriendlyStage::complete)
        return render_modal(table_.render(), "Freundschaftsspiel vereinbart", feedback_, "WEITER", "");
    if (stage_ == FriendlyStage::unavailable)
        return render_modal(table_.render(), "Nicht verfuegbar", feedback_, "ZURUECK", "");
    if (stage_ != FriendlyStage::calendar || !quote_) return table_.render();

    if (!calendar_) throw FormatError("friendly calendar: missing Source calendar view");
    return calendar_->render();
}

std::string FriendlyMatchScreen::state_json() const {
    std::ostringstream out;
    out << "{\"screen\":\"friendly_match\",\"stage\":" << json_quote(stage_name(stage_))
        << ",\"menu_kind\":" << desk_menu_kind_ << ",\"revision\":" << state_.revision
        << ",\"manager_slot\":" << state_.manager_slot << ",\"runtime_team\":" << state_.runtime_team
        << ",\"league\":" << table_.selected_league() << ",\"opponent\":" << selected_opponent_
        << ",\"close_requested\":" << (close_requested_ ? "true" : "false")
        << ",\"feedback\":" << json_quote(feedback_)
        << ",\"planner_count\":" << state_.planner_count;
    if (stage_==FriendlyStage::opponents) out << ",\"picker\":" << table_.state_json();
    if (quote_) out << ",\"quote_id\":" << quote_->request_id << ",\"payer_manager\":" << quote_->payer_manager
        << ",\"opponent_manager\":" << quote_->opponent_manager << ",\"quoted_planner_count\":" << quote_->planner_count
        << ",\"cost\":" << quote_->cost << ",\"home_team\":" << quote_->home_team
        << ",\"away_team\":" << quote_->away_team;
    if (selected_day_) out << ",\"selected_day\":" << *selected_day_;
    out << '}';
    return out.str();
}

std::optional<FriendlyMatchRequest> FriendlyMatchScreen::answer_question(GameEndTaxDialog::Button answer) {
    const bool yes=answer==GameEndTaxDialog::Button::yes;
    const bool no=answer==GameEndTaxDialog::Button::no;
    if (stage_ == FriendlyStage::venue && (yes || no)) {
        if (next_request_id_ >= std::numeric_limits<uint64_t>::max() - 1) {
            feedback_ = "Keine weitere Anfrage moeglich.";
            return std::nullopt;
        }
        manager_home_ = yes;
        cost_confirmed_ = false;
        owner_approved_ = false;
        quote_.reset();
        selected_day_.reset();
        FriendlyMatchRequest request;
        request.kind = FriendlyRequestKind::quote;
        request.request_id = next_request_id_++;
        request.revision = state_.revision;
        request.manager_slot = state_.manager_slot;
        request.runtime_team = state_.runtime_team;
        request.opponent = selected_opponent_;
        request.planner_count = state_.planner_count;
        request.manager_home = manager_home_;
        stage_ = FriendlyStage::quote_pending;
        return request;
    }
    if (stage_ == FriendlyStage::cost_confirmation && (yes || no)) {
        if (yes) {
            cost_confirmed_ = true;
            stage_ = needs_other_manager(*quote_,state_.manager_slot)?
                FriendlyStage::owner_approval:FriendlyStage::calendar;
            return std::nullopt;
        }
        close_requested_ = true;
        return std::nullopt;
    }
    if (stage_ == FriendlyStage::owner_approval && (yes || no)) {
        if (yes) {
            owner_approved_ = true;
            stage_ = FriendlyStage::calendar;
            return std::nullopt;
        }
        close_requested_ = true;
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<FriendlyMatchRequest> FriendlyMatchScreen::expire_question() {
    if(close_requested_||!question_projection())return std::nullopt;
    pressed_question_.reset();
    return answer_question(GameEndTaxDialog::Button::no);
}

std::optional<FriendlyMatchRequest> FriendlyMatchScreen::click(int x, int y, bool right_button) {
    if (close_requested_) return std::nullopt;
    if (right_button) {
        // PresentManagerDialog calls4885b for event8 too; without an expired process
        // timer that helper returns -1, leaving the question and hold intact.
        if (!question_projection()) close_requested_ = true;
        return std::nullopt;
    }
    auto answer=question_button(x,y);
    if(pressed_question_&&answer!=*pressed_question_)answer=GameEndTaxDialog::Button::none;
    pressed_question_.reset();
    if (stage_ == FriendlyStage::opponents) {
        if (table_.click(x,y)) {
            close_requested_=table_.close_requested();
            return std::nullopt;
        }
        const auto& rows = table_.state().rows;
        // FriendlyMatchScreen checks only the row interval, not the name-column x.
        if (y<=0x1c || y>=int(rows.size())*7+0x1c) return std::nullopt;
        const int row = (y-0x1d)/7;
        if (row < 0 || size_t(row) >= rows.size()) return std::nullopt;
        selected_opponent_ = rows[size_t(row)].runtime_team_id;
        if (selected_opponent_ == state_.runtime_team || selected_opponent_ < 0 || selected_opponent_ >= 168) {
            selected_opponent_ = -1;
            feedback_ = "Bitte einen anderen Ligaverein waehlen.";
            return std::nullopt;
        }
        stage_ = FriendlyStage::venue;
        feedback_.clear();
        return std::nullopt;
    }
    if (question_projection()) return answer_question(answer);
    if (stage_ == FriendlyStage::calendar) {
        if (!calendar_) return std::nullopt;
        calendar_->move(x, y);
        for (const auto& date : calendar_->state().days) {
            const int day = date.season_day;
            if (day < 0) continue; // July cells preceding Source word zero are display only.
            const auto cell = calendar_cell(date);
            if (x < cell.x || x >= cell.x + cell.width || y < cell.y || y >= cell.y + cell.height) continue;
            const auto it = std::find_if(quote_->dates.begin(), quote_->dates.end(), [day](const FriendlyDateOption& option) {
                return option.season_day == day;
            });
            if (it == quote_->dates.end() || (it->mark != FriendlyDateMark::available &&
                                               it->mark != FriendlyDateMark::available_flagged)) return std::nullopt;
            selected_day_ = day;
            if (national_booking_) {
                return book_request(day, national_booking_->approving_manager,
                                    national_booking_->consenting_manager);
            }
            if(needs_other_manager(*quote_,state_.manager_slot)&&!owner_approved_)return std::nullopt;
            return book_request(day, quote_->payer_manager >= 0 ? quote_->payer_manager : -1,
                                quote_->opponent_manager >= 0 ? quote_->opponent_manager : -1);
        }
    }
    if ((stage_ == FriendlyStage::complete || stage_ == FriendlyStage::unavailable) &&
        left_modal_button(x, y)) close_requested_ = true;
    return std::nullopt;
}

std::optional<FriendlyMatchRequest> FriendlyMatchScreen::key(std::string_view value) {
    if (close_requested_) return std::nullopt;
    // The Original question loop has no keyboard answer/cancel mapping.
    if (question_projection()) return std::nullopt;
    if (value == "ESC") { close_requested_ = true; return std::nullopt; }
    if (stage_ == FriendlyStage::opponents && (value == "LEFT" || value == "RIGHT")) {
        table_.key(value);
        return std::nullopt;
    }
    return std::nullopt;
}

bool FriendlyMatchScreen::acknowledge_quote(FriendlyQuoteProjection quote) {
    if (stage_ != FriendlyStage::quote_pending || quote.request_id != uint64_t(next_request_id_ - 1) ||
        quote.revision != state_.revision || quote.manager_slot != state_.manager_slot ||
        quote.runtime_team != state_.runtime_team || quote.opponent != selected_opponent_ ||
        quote.manager_home != manager_home_ || quote.cost < 0 ||
        quote.planner_count != state_.planner_count || quote.dates.size() != 366)
        return false;
    const int expected_home = manager_home_ ? state_.runtime_team : selected_opponent_;
    const int expected_away = manager_home_ ? selected_opponent_ : state_.runtime_team;
    const int expected_payer = manager_home_ ? state_.manager_slot : quote.opponent_manager;
    if (quote.home_team != expected_home || quote.away_team != expected_away ||
        quote.payer_manager != expected_payer ||
        quote.payer_manager < -1 || quote.payer_manager > 3 ||
        quote.opponent_manager < -1 || quote.opponent_manager > 3)
        return false;
    std::array<bool, 366> seen{};
    for (const auto& day : quote.dates) {
        if (day.season_day < 0 || day.season_day > 365 || seen[size_t(day.season_day)]) return false;
        seen[size_t(day.season_day)] = true;
    }
    if (std::find(seen.begin(), seen.end(), false) != seen.end()) return false;
    quote_ = std::move(quote);
    prepare_calendar();
    stage_ = manager_home_ ? FriendlyStage::cost_confirmation :
        needs_other_manager(*quote_,state_.manager_slot)?FriendlyStage::owner_approval:FriendlyStage::calendar;
    cost_confirmed_ = !manager_home_;
    selected_day_.reset();
    owner_approved_ = false;
    feedback_.clear();
    return true;
}

std::optional<FriendlyMatchRequest> FriendlyMatchScreen::book_request(int season_day, int approving_manager,
                                                                      int consenting_manager) {
    if (!quote_ || !selected_day_ || *selected_day_ != season_day || !cost_confirmed_ ||
        next_request_id_ == std::numeric_limits<uint64_t>::max()) return std::nullopt;
    FriendlyMatchRequest request;
    request.kind = FriendlyRequestKind::book;
    request.request_id = next_request_id_++;
    request.quote_id = quote_->request_id;
    request.revision = state_.revision;
    request.manager_slot = quote_->manager_slot;
    request.runtime_team = quote_->runtime_team;
    request.opponent = quote_->opponent;
    request.opponent_manager = quote_->opponent_manager;
    request.season_day = season_day;
    request.payer_manager = quote_->payer_manager;
    request.home_team = quote_->home_team;
    request.away_team = quote_->away_team;
    request.cost = quote_->cost;
    request.planner_count = quote_->planner_count;
    request.approving_manager = approving_manager;
    request.consenting_manager = consenting_manager;
    request.manager_home = quote_->manager_home;
    request.accept_cost = quote_->payer_manager >= 0 &&
        (quote_->payer_manager == state_.manager_slot ? cost_confirmed_ : owner_approved_);
    request.accept_opponent = quote_->opponent_manager >= 0 &&
        quote_->opponent_manager != state_.manager_slot && owner_approved_;
    if (national_booking_) {
        auto national = *national_booking_;
        national.season_day = uint16_t(season_day);
        request.national_booking = std::move(national);
        request.accept_cost = request.national_booking->accept_cost;
        request.accept_opponent = request.national_booking->accept_opponent;
        request.approving_manager = request.national_booking->approving_manager;
        request.consenting_manager = request.national_booking->consenting_manager;
    }
    pending_booking_request_id_ = request.request_id;
    stage_ = FriendlyStage::booking_pending;
    return request;
}

bool FriendlyMatchScreen::acknowledge_booking(const FriendlyBookingAck& ack) {
    if (stage_ != FriendlyStage::booking_pending || !quote_ ||
        ack.request_id != pending_booking_request_id_ || ack.quote_id != quote_->request_id ||
        ack.manager_slot != quote_->manager_slot ||
        ack.runtime_team != quote_->runtime_team || ack.opponent != quote_->opponent ||
        ack.opponent_manager != quote_->opponent_manager ||
        !selected_day_ || ack.season_day != *selected_day_ || ack.payer_manager != quote_->payer_manager ||
        ack.home_team != quote_->home_team || ack.away_team != quote_->away_team || ack.cost != quote_->cost)
        return false;
    if (!ack.accepted) {
        if (ack.revision != state_.revision || ack.planner_count != state_.planner_count) return false;
        stage_ = FriendlyStage::calendar;
        feedback_ = "Termin konnte nicht gebucht werden.";
        selected_day_.reset();
        pending_booking_request_id_ = 0;
        return true;
    }
    if (ack.revision != state_.revision + 1 || ack.planner_count != quote_->planner_count + 2) return false;
    stage_ = FriendlyStage::complete;
    state_.revision = ack.revision;
    state_.planner_count = ack.planner_count;
    pending_booking_request_id_ = 0;
    feedback_ = "Der Termin wurde eingetragen.";
    return true;
}

void FriendlyMatchScreen::reject(std::string message) {
    if (stage_ == FriendlyStage::quote_pending) {
        stage_ = FriendlyStage::venue;
    } else if (stage_ == FriendlyStage::booking_pending) {
        stage_ = FriendlyStage::calendar;
        selected_day_.reset();
    }
    feedback_ = std::move(message);
}

} // namespace bmh::ui
