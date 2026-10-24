// SPDX-License-Identifier: GPL-3.0-only
// Friendly matches (menu 6, docs/CLI_CONCEPT/06-verein.md, H2 p. 50-51): choose an opponent and
// the venue, see the costs, pick a free date and book it.
//
// The engine functions are the ones the SDL app's controller uses (src/app/controller.cpp
// serve_friendly_action): quote_friendly_match (pure), friendly_match_date_status (pure) and
// book_friendly_match, which writes the plan. A human opponent must agree and the paying
// manager must accept the costs; the console asks those managers (hot seat) before booking.

#include <algorithm>

#include "app/desktop_actions.hpp"
#include "cli/console/console.hpp"
#include "cli/console/detail.hpp"
#include "game/desk_actions.hpp"
#include "game/schedule.hpp"
#include "ui/desk_state.hpp"
#include "ui/league_table.hpp"

namespace bmh::cli::console {

using namespace detail;

void Console::register_friendly_commands() {
    auto manager = [this] { return active_slot(); };
    openers_[6] = [this] {
        friendly_quote_.reset();
        open_window("Friendlies");
        emit('+', "friendlies: ListFriendlyOpponents, ProposeFriendly(club, Home|Away)");
    };
    add({"Friendlies", "ListFriendlyOpponents", {integer("league", 0, kLeagueCount - 1, true)}, "the clubs of a league (default: own)", false},
        [this](const Values& v) {
            const ui::DeskState d = desk();
            const int league = v.has("league") ? int(v.integer("league")) : std::max(0, d.league);
            const ui::LeagueTableState t = ui::league_table_from_game(session_->state(), d.manager_slot, league);
            std::vector<std::vector<std::string>> rows;
            for (const auto& r : t.rows)
                if (r.runtime_team_id != d.runtime_club) rows.push_back({"club:" + std::to_string(r.runtime_team_id), r.name});
            emit('i', t.league_name);
            emit_table({"Club", "Name"}, rows);
        });
    Param club = detail::text("club");
    club.display = "club:Club";
    add({"Friendlies", "ProposeFriendly", {club, choice("venue", {"Home", "Away"})}, "the offer: costs and who pays", false},
        [this, manager](const Values& v) {
            // a club by club:<runtime id> or a unique part of a name (all leagues)
            const std::string reference = v.text("club");
            int opponent = -1;
            std::string number = lower(reference).rfind("club:", 0) == 0 ? reference.substr(5) : reference;
            Arg a;
            a.text = number;
            if (const auto id = to_int(a)) {
                opponent = int(*id);
            } else {
                std::vector<int> found;
                for (int league = 0; league < kLeagueCount; ++league)
                    for (const auto& r : ui::league_table_from_game(session_->state(), manager(), league).rows)
                        if (fold(r.name).find(fold(reference)) != std::string::npos) found.push_back(r.runtime_team_id);
                if (found.size() != 1) fail(found.empty() ? "E_BAD_ARGUMENT" : "E_AMBIGUOUS", "\"" + reference + "\" fits " +
                                                                                              std::to_string(found.size()) + " clubs; use club:<n>");
                opponent = found.front();
            }
            const FriendlyBookingQuote q = app::FriendlyActions(*session_).quote(manager(), opponent, v.choice("venue") == 0);
            if (q.status != FriendlyQuoteStatus::quoted) fail("E_RULE", "no friendly possible against this club");
            friendly_quote_ = q;
            const GameState& g = session_->state();
            emit('+', club_text(g, q.home_team) + " - " + club_text(g, q.away_team) + ", costs " + money(q.cost) +
                          (q.payer_manager >= 0 ? " (paid by player " + std::to_string(q.payer_manager + 1) + ")" : "") +
                          (q.opponent_manager >= 0 ? ", player " + std::to_string(q.opponent_manager + 1) + " must agree" : "") +
                          "; ListFreeDates, SetFriendlyDate(YYYY-MM-DD)");
        });
    auto free_dates = [this]() {
        if (!friendly_quote_) fail("E_RULE", "no offer (ProposeFriendly first)");
        std::vector<std::pair<int, std::string>> dates;
        const GameState& g = session_->state();
        for (int day = 0; day <= 365; ++day) {
            const FriendlyDateStatus s = app::FriendlyActions(*session_).date_status(*friendly_quote_, uint16_t(day));
            if (s != FriendlyDateStatus::available && s != FriendlyDateStatus::available_with_calendar_event) continue;
            const ui::DeskDate date = ui::desk_date(g.season_start + uint32_t(day), exe_);
            dates.push_back({day, date_text(date.day, date.month, date.year)});
        }
        return dates;
    };
    add({"Friendlies", "ListFreeDates", {}, "the days the friendly can be played", false}, [this, free_dates](const Values&) {
        std::vector<std::vector<std::string>> rows;
        for (const auto& [day, date] : free_dates()) rows.push_back({date, std::to_string(day)});
        emit_table({"Date", "SeasonDay"}, rows);
    });
    add({"Friendlies", "SetFriendlyDate", {detail::text("date")}, "book the friendly on a free date (YYYY-MM-DD)", true},
        [this, free_dates](const Values& v) {
            int day = -1;
            for (const auto& [d, date] : free_dates())
                if (date == v.text("date")) day = d;
            if (day < 0) fail("E_RULE", v.text("date") + " is not free (ListFreeDates)");
            const FriendlyBookingQuote q = *friendly_quote_;
            // the costs are accepted by the paying manager, a human opponent agrees; then book
            auto book = [this, q, day](bool accept_cost, bool accept_opponent) {
                const FriendlyBookingResult r = app::FriendlyActions(*session_).book_console(
                    q, uint16_t(day), accept_opponent, q.opponent_manager, accept_cost, q.payer_manager, revision_);
                friendly_quote_.reset();
                switch (r.status) {
                case FriendlyBookingStatus::booked: break;
                case FriendlyBookingStatus::insufficient_funds: fail("E_INSUFFICIENT_FUNDS", "the money is not enough for this match");
                case FriendlyBookingStatus::declined: fail("E_RULE", "the costs were declined");
                case FriendlyBookingStatus::opponent_declined: fail("E_RULE", "the opponent did not agree");
                default: fail("E_RULE", "the date could not be booked");
                }
                const ui::DeskDate date = ui::desk_date(session_->state().season_start + uint32_t(day), exe_);
                emit('+', "friendly booked for " + date_text(date.day, date.month, date.year));
            };
            auto ask_opponent = [this, q, book](bool accept_cost) {
                if (q.opponent_manager < 0) {
                    book(accept_cost, false);
                    return;
                }
                const int previous = player_;
                player_ = q.opponent_manager + 1;
                ask("Player " + std::to_string(q.manager + 1) + " proposes a friendly. Agree?", {"Yes", "No"}, "No",
                    [this, book, accept_cost, previous](const std::string& answer) {
                        player_ = previous;
                        book(accept_cost, answer == "Yes");
                    });
            };
            if (q.cost > 0 && q.payer_manager >= 0) {
                const int previous = player_;
                player_ = q.payer_manager + 1;
                ask("The friendly costs " + money(q.cost) + ". Pay?", {"Yes", "No"}, "No", [this, ask_opponent, previous](const std::string& answer) {
                    player_ = previous;
                    ask_opponent(answer == "Yes");
                });
                return;
            }
            ask_opponent(false);
        });
}

}  // namespace bmh::cli::console
