// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/national_friendly_ui.hpp"

#include <cstdlib>
#include <iostream>

#include "game/messages.hpp"

namespace bmh {
std::string message_text(const GameState&, uint32_t pointer) {
    if (pointer == ::bmh::layout::Controller_ClubFriendlyState_TextPointer) return "Choose venue";
    if (pointer == ::bmh::layout::Controller_FormatError_TextPointer2) return "Cost: ";
    if (pointer == ::bmh::layout::Controller_FormatError_TextPointer3) return "Confirm: ";
    return {};
}
std::string format_number(const GameState&, int32_t value, int flags) {
    if (value != 35000 || flags != 0x101) std::exit(2);
    return "35.000 TEST";
}
}

namespace {
void check(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
}

int main() {
    using namespace bmh;
    using namespace bmh::ui;
    kFriendlyPayerPrefixText = "Payer: ";
    const NationalFriendlyIdentity manager{"Nationaltrainer", 14, false, 0, 0};
    NationalFriendlyUi ordinary({23, 0, 0x165, 4, 9}, manager);
    check(ordinary.stage() == NationalFriendlyUiStage::rejected,
          "menu 23 must not authorize the national picker");

    NationalFriendlyUi ui({6, 0, 0x165, 4, 9}, manager);
    check(ui.begin().kind == NationalFriendlyUiActionKind::open_picker,
          "menu 6 national caller opens the picker");
    check(ui.select_opponent(GameState{}, 0x165).kind == NationalFriendlyUiActionKind::return_to_caller,
          "nation zero / own national team must be rejected");

    NationalFriendlyUi session({6, 0, 0x165, 4, 9}, manager);
    const GameState game{};
    const auto venue = session.select_opponent(game, 0x166);
    check(venue.kind == NationalFriendlyUiActionKind::show_question && venue.question &&
          venue.question->body == "Choose venue" && venue.question->source_flags == 0x606 &&
          venue.question->source_manager_flags == 0,
          "venue prompt uses the source text, mode, and manager");
    auto quote_request = session.answer_venue(true);
    check(quote_request.kind == NationalFriendlyUiActionKind::quote &&
          quote_request.opponent == 0x166 && quote_request.manager_home &&
          quote_request.engine_owner && quote_request.engine_owner->runtime_team == 0x165 &&
          quote_request.engine_owner->menu_kind == 6,
          "venue answer requests a quote without changing the own team");

    NationalFriendlyQuote quote;
    quote.owner = session.engine_owner();
    quote.terms.status = FriendlyQuoteStatus::quoted;
    quote.terms.manager = 0;
    quote.terms.manager_team = 0x165;
    quote.terms.opponent = 0x166;
    quote.terms.manager_home = true;
    quote.terms.home_team = 0x165;
    quote.terms.away_team = 0x166;
    quote.terms.cost = 35000;
    quote.terms.payer_manager = 0;
    const auto cost = session.accept_quote(game, quote);
    check(cost.kind == NationalFriendlyUiActionKind::show_question && cost.question &&
          cost.question->body == "Cost: 35.000 TEST" && cost.question->source_flags == 0x404,
          "home question displays its bound quote using Source currency formatting");
    check(session.answer_cost(game, false).kind == NationalFriendlyUiActionKind::return_to_caller,
          "declining cost exits without creating a booking request");

    NationalFriendlyUi confirmed({6, 0, 0x165, 4, 10}, manager);
    confirmed.select_opponent(game, 0x166);
    confirmed.answer_venue(true);
    quote.owner = confirmed.engine_owner();
    check(confirmed.accept_quote(game, quote).question.has_value(), "quote binds to its caller revision");
    check(confirmed.answer_cost(game, true).kind == NationalFriendlyUiActionKind::open_calendar,
          "accepted cost opens the calendar");
    const auto booking = confirmed.request_booking(196);
    check(booking.kind == NationalFriendlyUiActionKind::book && booking.booking &&
          booking.booking->owner == confirmed.engine_owner() && booking.booking->accept_cost &&
          booking.booking->approving_manager == 0 && booking.booking->season_day == 196,
          "calendar booking carries the bound quote, owner, day, and approval");
    NationalFriendlyBookingResult ack;
    ack.status = FriendlyBookingStatus::booked;
    ack.manager = 0;
    ack.manager_team = 0x165;
    ack.opponent = 0x166;
    ack.manager_home = true;
    ack.payer_manager = 0;
    ack.cost = 35000;
    check(confirmed.accept_booking(ack).kind == NationalFriendlyUiActionKind::return_to_caller,
          "successful booking returns to the menu-6 caller");

    NationalFriendlyUi with_opponent_manager({6, 0, 0x165, 4, 11}, manager);
    with_opponent_manager.set_opponent_manager_identity({"Gegnertrainer", 2, false, 0, 2});
    with_opponent_manager.select_opponent(game, 0x166);
    with_opponent_manager.answer_venue(true);
    quote.owner = with_opponent_manager.engine_owner();
    quote.terms.opponent_manager = 2;
    quote.terms.payer_manager = 0;
    with_opponent_manager.accept_quote(game, quote);
    const auto opponent_prompt = with_opponent_manager.answer_cost(game, true);
    check(opponent_prompt.question && opponent_prompt.question->source_flags == 0x303 &&
          opponent_prompt.question->source_manager_flags == 2 &&
          opponent_prompt.question->body == "Confirm: ",
          "home-side cost approval is followed by the original opponent consent question");
    const auto consent = with_opponent_manager.answer_payer(true);
    check(consent.kind == NationalFriendlyUiActionKind::open_calendar,
          "opponent consent advances to the calendar");
    const auto consent_booking = with_opponent_manager.request_booking(197);
    check(consent_booking.booking && consent_booking.booking->accept_opponent &&
          consent_booking.booking->consenting_manager == 2 && consent_booking.booking->accept_cost &&
          consent_booking.booking->approving_manager == 0,
          "separate opponent consent and home-side cost approval are both carried");

    NationalFriendlyUi away_venue({6, 0, 0x165, 4, 12}, manager);
    away_venue.set_opponent_manager_identity({"Gegnertrainer", 2, false, 0, 2});
    away_venue.select_opponent(game, 0x166);
    away_venue.answer_venue(false);
    quote.owner = away_venue.engine_owner();
    quote.terms.manager_home = false;
    quote.terms.payer_manager = 2;
    quote.terms.opponent_manager = 2;
    const auto other_manager_prompt = away_venue.accept_quote(game, quote);
    check(other_manager_prompt.question && other_manager_prompt.question->source_flags == 0x303 &&
          other_manager_prompt.question->source_manager_flags == 2 &&
          other_manager_prompt.question->body == "Confirm: Payer: 35.000 TEST",
          "opponent-home question appends the source payment wording and amount");
    away_venue.answer_payer(true);
    const auto away_booking = away_venue.request_booking(198);
    check(away_booking.booking && away_booking.booking->accept_opponent &&
          away_booking.booking->consenting_manager == 2 && away_booking.booking->accept_cost &&
          away_booking.booking->approving_manager == 2,
          "opponent-home consent carries that manager as both consenter and payer");
    return 0;
}
