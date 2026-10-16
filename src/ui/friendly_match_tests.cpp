// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/friendly_match.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>

#include "ui/setup.hpp"
#include "ui/desk_state.hpp"
#include "game/schedule.hpp"
#include "game/state.hpp"
#include "game/messages.hpp"

using namespace bmh::ui;

namespace {
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

FriendlyMatchState fixture(const std::filesystem::path& data) {
    FriendlyMatchState state;
    state.revision = 7;
    state.manager_slot = 0;
    state.runtime_team = 0;
    state.current_day = 40;
    state.planner_count = 4;
    const auto exe = bmh::load_le(asset_path(data, "BMH.exe"));
    // legacy routine: word zero is the first July Wednesday, not necessarily July 1.
    uint32_t display_start = 1300;
    while (desk_date(display_start, exe).month != 7 || desk_date(display_start, exe).day != 1)
        ++display_start;
    state.season_start_counter = display_start;
    while (desk_date(state.season_start_counter, exe).weekday != 2) ++state.season_start_counter;
    state.calendar.season_start_counter = display_start;
    state.calendar.leading_days = int(state.season_start_counter - display_start);
    state.calendar.current_day = state.current_day + state.calendar.leading_days;
    for (int offset = 0; offset <= 366; ++offset) {
        const auto date = desk_date(display_start + uint32_t(offset), exe);
        if (offset && date.month == 7 && date.day == 1) break;
        CalendarDayProjection day;
        day.season_day = offset - state.calendar.leading_days;
        day.day_counter = display_start + uint32_t(offset);
        day.date = date;
        day.current_day = offset == state.calendar.current_day;
        state.calendar.days.push_back(day);
    }
    state.mode_three = true;
    state.manager_name = "Tester";
    TaxDialogProjection venue;
    venue.kind=TaxDialogKind::question;venue.source_flags=0x606;
    venue.manager_name="Tester";
    venue.body=bmh::load_textd(asset_path(data,"TEXTD.BLK"))[bmh::text_id(::bmh::layout::Controller_ClubFriendlyState_TextPointer)];
    state.venue_question=venue;
    int runtime = 0;
    for (int league = 0; league < 9; ++league) {
        auto& table = state.leagues[size_t(league)];
        table.manager_slot = 0;
        table.league_id = league;
        table.league_name = "Liga";
        table.header_text = "Tabelle";
        for (int team = 0; team < bmh::kLeagueSizes[league]; ++team) {
            LeagueTableRow row;
            row.rank = team + 1;
            row.runtime_team_id = runtime++;
            row.club_identity_key = row.runtime_team_id;
            row.name = "Verein " + std::to_string(team);
            row.name_color = 0x5f;
            table.rows.push_back(std::move(row));
        }
    }
    return state;
}

FriendlyQuoteProjection quote_for(const FriendlyMatchRequest& request, int planner_count,
                                  const std::filesystem::path& data) {
    FriendlyQuoteProjection quote;
    quote.request_id = request.request_id;
    quote.revision = request.revision;
    quote.manager_slot = request.manager_slot;
    quote.runtime_team = request.runtime_team;
    quote.opponent = request.opponent;
    quote.payer_manager = 0;
    quote.home_team = request.runtime_team;
    quote.away_team = request.opponent;
    quote.cost = 32000;
    quote.planner_count = planner_count;
    quote.manager_home = request.manager_home;
    const auto texts=bmh::load_textd(asset_path(data,"TEXTD.BLK"));
    TaxDialogProjection cost;
    cost.kind=TaxDialogKind::question;cost.source_flags=0x404;cost.manager_name="Tester";
    cost.body=texts[bmh::text_id(::bmh::layout::Controller_FormatError_TextPointer2)]+bmh::format_number(bmh::GameState{},quote.cost,0x101);
    quote.cost_question=cost;
    cost.source_flags=0x303;cost.source_manager_flags=1;cost.manager_name="Testgegner";
    cost.body=texts[bmh::text_id(::bmh::layout::Controller_FormatError_TextPointer3)];quote.owner_question=cost;
    for (int day = 0; day <= 365; ++day)
        quote.dates.push_back(FriendlyDateOption{day,
            day <= 40 ? FriendlyDateMark::unavailable : FriendlyDateMark::available});
    return quote;
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: ui_friendly_match_tests <BMH data>");
        const std::filesystem::path data(argv[1]);
        auto original = fixture(data);
        FriendlyMatchScreen screen(data, original);
        check(screen.stage() == FriendlyStage::opponents, "club Mode III can enter the opponent view");
        const auto exe = bmh::load_le(asset_path(data, "BMH.exe"));
        const auto date_font = bmh::load_font(asset_path(data / "F", "BMH3.FNT"));
        for (int day = 1; day <= 31; ++day)
            check(date_font.text_width(std::to_string(day)) <= 10,
                  "the Original small font renders every full calendar day in one cell");
        bool checked_week_rollover = false;
        bool checked_saturday_month_start = false;
        for (int day = 0; day <= 365; ++day) {
            const auto date = desk_date(original.season_start_counter + uint32_t(day), exe);
            const auto cell = friendly_calendar_cell(exe, original.season_start_counter, day);
            const int month_slot = (date.month + 5) % 12;
            const int month_x = 28 + (month_slot % 4) * 70;
            const int month_y = 29 + (month_slot / 4) * 70;
            if (date.day == 5 && date.weekday == 0) {
                check(cell.x == month_x + 12 && cell.y == month_y,
                      "Source Monday on day five occupies the second week column");
                checked_week_rollover = true;
            }
            if (date.day == 1 && date.weekday == 5) {
                check(cell.x == month_x && cell.y == month_y + 40,
                      "Source Saturday month start remains in the first week column");
                checked_saturday_month_start = true;
            }
        }
        check(checked_week_rollover && checked_saturday_month_start,
              "calendar geometry covers weekday offsets at two month starts");
        const auto initial_json = screen.state_json();
        const auto texts=bmh::load_textd(asset_path(data,"TEXTD.BLK"));
        check(texts[bmh::text_id(::bmh::layout::LeagueTable_Glyph_TextPointer2)]==" " && texts[bmh::text_id(::bmh::layout::FixtureFriendlySecondBlankLabel)]==" ",
              "German Original mode2 preserves the Source space-only closing footer labels");
        FriendlyMatchScreen full_row(data, original);
        check(!full_row.click(300,28) && full_row.stage()==FriendlyStage::opponents &&
              !full_row.click(300,36) && full_row.stage()==FriendlyStage::venue &&
              full_row.selected_opponent()==1,
              "Source row hit accepts the points/right side and exact row boundaries");
        FriendlyMatchScreen cancelled_footer(data, original);
        check(cancelled_footer.pointer_down(162,207) &&
              cancelled_footer.render().pixels!=screen.render().pixels &&
              cancelled_footer.move(0,0) && !cancelled_footer.click(162,207) &&
              !cancelled_footer.close_requested(),
              "leaving a held blank closing button cancels its later release");
        for (const int y : {207,220}) {
            FriendlyMatchScreen footer(data, original);
            check(footer.pointer_down(162,y) && !footer.close_requested() &&
                  !footer.click(162,y) && footer.close_requested(),
                  "both Source mode2 footer buttons close on their matching left release");
        }
        check(!screen.key("RIGHT") && screen.selected_league() == 1 &&
              !screen.key("LEFT") && screen.selected_league() == 0,
              "local league-key navigation emits no engine action");
        check(!screen.click(40, 38), "opponent selection opens the venue prompt without an engine request");
        check(screen.selected_opponent() == 1 && screen.stage() == FriendlyStage::venue,
              "the selected opponent is an owned runtime-team identity");
        check(!screen.click(80, 180) && screen.stage() == FriendlyStage::venue,
              "clicks outside the visible venue buttons do not choose a venue");
        const auto unpressed_question=screen.render();
        check(!screen.click(99,171,true) && !screen.key("ESC") &&
              !screen.close_requested() && screen.stage()==FriendlyStage::venue &&
              screen.render().pixels==unpressed_question.pixels,
              "ordinary right event and Escape cannot answer or close the Source venue question");
        check(screen.pointer_down(99,171) && screen.render().pixels!=unpressed_question.pixels,
              "Source question button has its actual KB0 pressed frame before release");
        const auto pressed_question=screen.render();
        check(!screen.click(99,171,true) && !screen.key("ESC") &&
              screen.render().pixels==pressed_question.pixels && !screen.close_requested(),
              "unrelated right/key events leave the held Source button owned by its left release");
        check(screen.move(0,0) && screen.render().pixels==unpressed_question.pixels &&
              !screen.click(99,171) && screen.stage()==FriendlyStage::venue,
              "leaving a held Source button restores it and the later UP cannot choose a venue");
        check(!screen.click(80,150) && screen.stage()==FriendlyStage::venue,
              "former synthetic question coordinates no longer answer the Original buttons");
        const auto quote_request = screen.click(99, 171);
        check(quote_request && quote_request->kind == FriendlyRequestKind::quote &&
              quote_request->request_id == 1 && quote_request->revision == 7 &&
              quote_request->planner_count == 4 && quote_request->manager_home,
              "venue selection sends an identity- and revision-bound read-only quote request");
        check(!screen.click(99, 171), "a repeated quote click while pending cannot emit another request");
        check(screen.acknowledge_quote(quote_for(*quote_request, 5, data)) == false,
              "quote is rejected if the original planner counter changed");
        auto stale_revision_quote = quote_for(*quote_request, 4, data);
        stale_revision_quote.revision++;
        check(!screen.acknowledge_quote(stale_revision_quote), "quote from another desk revision is rejected");
        check(screen.acknowledge_quote(quote_for(*quote_request, 4, data)), "matching quote opens fee confirmation");
        check(screen.stage() == FriendlyStage::cost_confirmation, "home fee is shown before date choice");
        check(!screen.click(213,171,true) && !screen.key("ESC") &&
              screen.stage()==FriendlyStage::cost_confirmation && !screen.close_requested(),
              "right event over No and Escape do not decline the Source fee question");
        check(!screen.click(10, 145) && screen.stage() == FriendlyStage::cost_confirmation,
              "clicks outside the visible fee buttons do not accept or decline");
        check(!screen.click(99, 171), "accepting a quoted fee does not book or mutate state");
        check(screen.stage() == FriendlyStage::calendar, "accepted home fee opens the calendar");
        auto calendar_pixels = screen.render();
        check(calendar_pixels.width == 320 && calendar_pixels.height == 240,
              "full calendar renders with the Original small font and one-/two-digit dates");
        auto original_calendar = bmh::load_vga(asset_path(data / "KB", "55.VGA"));
        check(calendar_pixels.at(0, 18) == original_calendar.at(0, 0) &&
              calendar_pixels.at(319, 226) == original_calendar.at(319, 208),
              "friendly calendar uses the Original opaque KB55 crop at y18, not a black custom view");
        for (const auto& date : original.calendar.days) if (date.season_day < 0) {
            const auto cell = calendar_cell(date);
            check(!screen.click(cell.x + 1, cell.y + 1) && screen.stage() == FriendlyStage::calendar,
                  "July dates before Source word zero cannot book native day zero");
        }
        const auto future_cell = friendly_calendar_cell(exe, original.season_start_counter, 41);
        check(screen.move(future_cell.x + 1, future_cell.y + 1), "Source day cell accepts pointer hover");
        auto hovered = screen.render();
        check(hovered.at(future_cell.x, future_cell.y + 1) == 0x75,
              "available friendly day has Source inclusive light hover outline");
        check(screen.move(0, 0) && screen.render().pixels == calendar_pixels.pixels,
              "leaving day cells restores the initial calendar and empty footer");
        const auto blocked_cell = friendly_calendar_cell(bmh::load_le(asset_path(data, "BMH.exe")), original.season_start_counter, 40);
        check(!screen.click(blocked_cell.x + 1, blocked_cell.y + 1) &&
              screen.stage() == FriendlyStage::calendar,
              "a date absent from the approved calendar projection cannot be requested");
        const auto last_date = desk_date(original.season_start_counter + 365, exe);
        if ((last_date.month + 5) % 12 == 0 && last_date.day == 1) {
            const auto year_boundary_cell = friendly_calendar_cell(exe, original.season_start_counter, 365);
            check(!screen.click(year_boundary_cell.x + 1, year_boundary_cell.y + 1) &&
                  screen.stage() == FriendlyStage::calendar,
                  "the next July 1 boundary is never offered as the last date of this season");
        }
        auto day_cell = friendly_calendar_cell(bmh::load_le(asset_path(data, "BMH.exe")), original.season_start_counter, 41);
        auto book_request = screen.click(day_cell.x + 1, day_cell.y + 1);
        check(book_request && book_request->kind == FriendlyRequestKind::book &&
              book_request->season_day == 41 && book_request->planner_count == 4 &&
              book_request->quote_id == quote_request->request_id &&
              book_request->request_id != quote_request->request_id && book_request->cost == 32000,
              "selecting a permitted date emits one booking request with the frozen quote");
        FriendlyBookingAck rejected;
        rejected.request_id = book_request->request_id;
        rejected.quote_id = book_request->quote_id;
        rejected.revision = original.revision;
        rejected.manager_slot = 0;
        rejected.runtime_team = 0;
        rejected.opponent = 1;
        rejected.opponent_manager = -1;
        rejected.season_day = 41;
        rejected.payer_manager = 0;
        rejected.home_team = 0;
        rejected.away_team = 1;
        rejected.cost = 32000;
        rejected.planner_count = 4;
        auto forged_reject = rejected;
        forged_reject.request_id++;
        check(!screen.acknowledge_booking(forged_reject), "a booking ACK with a different request ID is inert");
        check(screen.acknowledge_booking(rejected), "a rejected booking returns to the calendar without revision");
        check(screen.stage() == FriendlyStage::calendar && screen.state_json().find("\"revision\":7") != std::string::npos,
              "rejection keeps the authoritative revision unchanged");
        day_cell = friendly_calendar_cell(bmh::load_le(asset_path(data, "BMH.exe")), original.season_start_counter, 41);
        book_request = screen.click(day_cell.x + 1, day_cell.y + 1);
        check(book_request.has_value(), "the user can retry after an inert rejection");
        FriendlyBookingAck accepted = rejected;
        accepted.accepted = true;
        accepted.revision = 8;
        accepted.planner_count = 6;
        accepted.request_id = book_request->request_id;
        accepted.quote_id = book_request->quote_id;
        accepted.accepted = true;
        check(screen.acknowledge_booking(accepted), "successful booking ACK advances revision and planner counter");
        check(screen.stage() == FriendlyStage::complete && screen.close_requested() == false &&
              screen.state_json().find("\"revision\":8") != std::string::npos,
              "only the authoritative success ACK completes the interaction");
        check(!screen.acknowledge_booking(accepted), "duplicate booking ACK is inert after completion");
        check(original.revision == 7 && original.planner_count == 4 &&
              original.leagues[0].rows.size() == size_t(bmh::kLeagueSizes[0]),
              "screen interaction leaves its source projection unchanged");
        check(initial_json.find("friendly_match") != std::string::npos,
              "screen exposes machine-readable state for app routing");

        auto decline_state = fixture(data);
        FriendlyMatchScreen decline_screen(data, decline_state);
        decline_screen.click(40, 38);
        const auto decline_quote_request = decline_screen.click(99, 171);
        check(decline_screen.acknowledge_quote(quote_for(*decline_quote_request, 4, data)),
              "decline fixture receives the same read-only quote");
        check(!decline_screen.click(213, 171) && decline_screen.close_requested() &&
              decline_state.revision == 7 && decline_state.planner_count == 4,
              "Source declined cost ends the caller without booking or projection mutation");

        auto shared_home = fixture(data);
        FriendlyMatchScreen shared_screen(data, shared_home);
        shared_screen.click(40, 38);
        const auto shared_quote_request = shared_screen.click(99, 171);
        auto shared_quote = quote_for(*shared_quote_request, 4, data);
        shared_quote.opponent_manager = 1;
        check(shared_screen.acknowledge_quote(shared_quote), "human opponent identity is bound to the quote");
        shared_screen.click(99, 171);
        const auto consent_before=shared_screen.state_json();
        check(!shared_screen.click(213,171,true) && !shared_screen.key("ESC") &&
              !shared_screen.close_requested() && shared_screen.state_json()==consent_before,
              "other-manager consent cannot be bypassed or declined by right event or Escape");
        check(shared_screen.stage()==FriendlyStage::owner_approval &&
              shared_screen.state_json().find("selected_day")==std::string::npos,
              "Source asks other-manager consent before selecting any calendar date");
        const auto shared_day = friendly_calendar_cell(bmh::load_le(asset_path(data, "BMH.exe")), original.season_start_counter, 41);
        check(!shared_screen.click(shared_day.x + 1, shared_day.y + 1) &&
              shared_screen.stage() == FriendlyStage::owner_approval,
              "calendar coordinates cannot bypass pending Source consent");
        check(!shared_screen.click(99,171) && shared_screen.stage()==FriendlyStage::calendar,
              "consenting manager opens calendar without booking a previously selected date");
        const auto consented = shared_screen.click(shared_day.x+1,shared_day.y+1);
        check(consented && consented->accept_cost && consented->accept_opponent &&
              consented->approving_manager == 0 && consented->consenting_manager == 1,
              "separate home payer and opponent consent are both carried explicitly");

        FriendlyMatchScreen away_screen(data,fixture(data));
        away_screen.click(40,38);
        const auto away_request=away_screen.click(213,171);
        check(away_request&&!away_request->manager_home,"Source venue No means away");
        auto away_quote=quote_for(*away_request,4,data);
        away_quote.opponent_manager=1;away_quote.payer_manager=1;
        away_quote.home_team=1;away_quote.away_team=0;
        check(away_screen.acknowledge_quote(away_quote)&&away_screen.stage()==FriendlyStage::owner_approval,
              "away quote asks the paying manager before opening its calendar");
        check(!away_screen.click(99,171)&&away_screen.stage()==FriendlyStage::calendar,
              "payer consent opens calendar without emitting a book command");
        const auto away_book=away_screen.click(shared_day.x+1,shared_day.y+1);
        check(away_book&&away_book->accept_cost&&away_book->accept_opponent&&
              away_book->approving_manager==1&&away_book->consenting_manager==1,
              "later away-date request carries its prior bound payer and opponent consent");

        FriendlyMatchScreen denied_screen(data,fixture(data));
        denied_screen.click(40,38);const auto denied_request=denied_screen.click(99,171);
        auto denied_quote=quote_for(*denied_request,4,data);denied_quote.opponent_manager=1;
        check(denied_screen.acknowledge_quote(denied_quote),"denial receives an owned human-opponent quote");
        denied_screen.click(99,171);
        check(!denied_screen.click(213,171)&&denied_screen.close_requested(),
              "other-manager No ends Source caller without date choice or booking");

        FriendlyMatchScreen wrong_menu(data, fixture(data), 23);
        check(wrong_menu.stage() == FriendlyStage::unavailable,
              "separate Desk kind 23 view is not routed to the club booking workflow");
        auto national = fixture(data);
        national.national_team = true;
        national.runtime_team = bmh::kNoTeam;
        FriendlyMatchScreen unavailable(data, national);
        check(unavailable.stage() == FriendlyStage::unavailable && !unavailable.close_requested() &&
              !unavailable.click(80, 150) && unavailable.close_requested(),
              "national-team booking is unavailable and its visible return button works");

        auto cancelled = fixture(data);
        FriendlyMatchScreen cancel_screen(data, cancelled);
        check(!cancel_screen.key("ESC") && cancel_screen.close_requested(),
              "ESC cancels the local view without issuing an engine request");

        FriendlyMatchScreen timed_venue(data,original);
        (void)timed_venue.click(40,38);(void)timed_venue.pointer_down(99,171);
        check(timed_venue.question_held() && timed_venue.question_release_pending() &&
              !timed_venue.move(100,171) && timed_venue.question_held(),
              "moving within a valid question widget retains Source release ownership");
        auto timed_away_request=timed_venue.expire_question();
        check(timed_away_request && !timed_away_request->manager_home && timed_away_request->kind==FriendlyRequestKind::quote &&
              timed_venue.stage()==FriendlyStage::quote_pending && !timed_venue.active_question() &&
              !timed_venue.expire_question(),"semantic expiry answers venue No once, preserving quote ownership");
        FriendlyMatchScreen timed_fee(data,original);
        (void)timed_fee.click(40,38);const auto fee_request=timed_fee.click(99,171);
        check(fee_request && timed_fee.acknowledge_quote(quote_for(*fee_request,original.planner_count,data)),
              "fee expiry starts with an accepted real-shaped quote");
        (void)timed_fee.pointer_down(99,171);(void)timed_fee.move(0,0);
        check(!timed_fee.question_held() && timed_fee.question_release_pending() &&
              !timed_fee.expire_question() && timed_fee.close_requested(),
              "leaving the widget releases its answer but retains the old physical UP for the caller");
        std::cout << "friendly match UI state and ACK checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "friendly match UI test failure: " << e.what() << '\n';
        return 1;
    }
}
