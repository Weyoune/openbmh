// SPDX-License-Identifier: GPL-3.0-only
// Moving own home league matches (menu 7, docs/CLI_CONCEPT/06-verein.md 13, H2 p. 52): list
// the matches that can be moved, the days each one can move to, move it and take a move back.
//
// The rules are the engine's (src/game/reschedule.hpp, a port of the calendar screen
// CalendarCell in mode 2): own home league matches in the future, not on the first or the
// last two matchdays; target days free of other matches and friendlies, outside the winter
// break; the Friday before the matchday only with floodlights. Dates are written like the
// other date commands (YYYY-MM-DD).

#include <algorithm>

#include "app/desktop_actions.hpp"
#include "cli/console/console.hpp"
#include "cli/console/detail.hpp"
#include "game/reschedule.hpp"
#include "ui/desk_state.hpp"

namespace bmh::cli::console {

using namespace detail;

void Console::register_reschedule_commands() {
    auto manager = [this] { return active_slot(); };
    auto day_text = [this](int day) {
        const ui::DeskDate date = ui::desk_date(session_->state().season_start + uint32_t(day), exe_);
        return date_text(date.day, date.month, date.year);
    };
    // the movable match on the date `text`
    auto find_game = [this, manager, day_text](const std::string& text) {
        for (const LeagueMatchRef& m : reschedulable_matches(session_->state(), manager()))
            if (day_text(m.day) == text) return m;
        fail("E_RULE", "no movable home league match on " + text + " (ListRescheduleGames)");
    };
    openers_[7] = [this] {
        open_window("Reschedule");
        emit('+', "reschedule: ListRescheduleGames, ListRescheduleDates(game:date), MoveGame(game:date, to:date), ListMovedGames, UndoMove(game:date)");
    };
    add({"Reschedule", "ListRescheduleGames", {}, "own home league matches that can be moved", false},
        [this, manager, day_text](const Values&) {
            const GameState& g = session_->state();
            std::vector<std::vector<std::string>> rows;
            for (const LeagueMatchRef& m : reschedulable_matches(g, manager()))
                rows.push_back({day_text(m.day), std::to_string(m.round + 1), utf8(g.club_name(m.away))});
            if (rows.empty()) emit('i', "no home league match can be moved");
            emit_table({"Date", "Matchday", "Opponent"}, rows);
        });
    Param game = detail::text("game");
    game.display = "game:date";
    Param to = detail::text("to");
    to.display = "to:date";
    add({"Reschedule", "ListRescheduleDates", {game}, "the days the match can be moved to", false},
        [this, manager, day_text, find_game](const Values& v) {
            const LeagueMatchRef m = find_game(v.text("game"));
            std::vector<std::vector<std::string>> rows;
            for (int day : reschedule_target_days(session_->state(), manager(), m)) rows.push_back({day_text(day), std::to_string(day)});
            if (rows.empty()) emit('i', "no free day for this match");
            emit_table({"Date", "SeasonDay"}, rows);
        });
    add({"Reschedule", "ListMovedGames", {}, "moved matches that can still be taken back (new date)", false},
        [this, manager, day_text](const Values&) {
            const GameState& g = session_->state();
            std::vector<std::vector<std::string>> rows;
            for (const LeagueMatchRef& m : moved_matches(g, manager()))
                rows.push_back({day_text(m.day), std::to_string(m.round + 1), utf8(g.club_name(m.home)), utf8(g.club_name(m.away))});
            if (rows.empty()) emit('i', "no moved match");
            emit_table({"Date", "Matchday", "Home", "Away"}, rows);
        });
    add({"Reschedule", "UndoMove", {game}, "take a move back (game = the new date); the match is played on its matchday again", true},
        [this, manager, day_text](const Values& v) {
            const GameState& g = session_->state();
            for (const LeagueMatchRef& m : moved_matches(g, manager())) {
                if (day_text(m.day) != v.text("game")) continue;
                app::RescheduleActions(*session_).undo_console(manager(), m.day, revision_);
                emit('+', utf8(g.club_name(m.home)) + " - " + utf8(g.club_name(m.away)) + " back on matchday " + std::to_string(m.round + 1));
                return;
            }
            fail("E_RULE", "no moved match on " + v.text("game") + " (ListMovedGames)");
        });
    add({"Reschedule", "MoveGame", {game, to}, "move the match to a free day", true},
        [this, manager, day_text, find_game](const Values& v) {
            const LeagueMatchRef m = find_game(v.text("game"));
            int target = -1;
            for (int day : reschedule_target_days(session_->state(), manager(), m))
                if (day_text(day) == v.text("to")) target = day;
            if (target < 0) fail("E_RULE", v.text("to") + " is not free for this match (ListRescheduleDates)");
            switch (app::RescheduleActions(*session_).move(manager(), m, target, revision_)) {
            case RescheduleStatus::moved: break;
            case RescheduleStatus::no_free_slot: fail("E_RULE", "too many postponed matches this season");
            case RescheduleStatus::day_full:
            case RescheduleStatus::team_clash: fail("E_RULE", "that day is already full of replays");
            default: fail("E_RULE", "the match cannot be moved");
            }
            const GameState& g = session_->state();
            emit('+', utf8(g.club_name(m.home)) + " - " + utf8(g.club_name(m.away)) + " moved from " + day_text(m.day) + " to " +
                          day_text(target));
        });
}

}  // namespace bmh::cli::console
