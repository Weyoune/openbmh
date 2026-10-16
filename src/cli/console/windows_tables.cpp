// SPDX-License-Identifier: GPL-3.0-only
// The league table and the fixtures window (docs/CLI_CONCEPT/05-information.md).
//
// LeagueTable reads the pure projection ui::league_table_from_game. Fixtures goes through the
// engine like the SDL app (CareerSession::open_league_pairings / dispatch_league_pairings):
// the original draws random numbers when the window opens and when it closes
// (pairings_runtime_draw, stir_random), so a console that only printed the schedule would
// leave the game on a different random sequence than the original.

#include "app/window_actions.hpp"
#include <algorithm>

#include "cli/console/console.hpp"
#include "cli/console/detail.hpp"
#include "game/schedule.hpp"
#include "ui/desk_state.hpp"
#include "ui/desktop.hpp"
#include "ui/league_table.hpp"

namespace bmh::cli::console {

using namespace detail;

void Console::register_table_commands() {
    closers_["Fixtures"] = [this] { fixtures_action(LeaguePairingsAction::close); };
    // ListLeagues is global (commands.csv "*"): the nine leagues with their number, as
    // LeagueTable/Fixtures take them; during the manager selection the five start leagues.
    add({"*", "ListLeagues", {}, "leagues with number and size", false}, [this](const Values&) {
        std::vector<std::vector<std::string>> rows;
        if (career_) {
            for (int league = 0; league < kLeagueCount; ++league) {
                const ui::LeagueTableState t = ui::league_table_from_game(session_->state(), active_slot(), league);
                rows.push_back({std::to_string(league), t.league_name, std::to_string(t.rows.size())});
            }
        } else if (picker_) {
            for (int league = 0; league < 5; ++league)
                rows.push_back({std::to_string(league), utf8(picker_->group_label(league)),
                                std::to_string(picker_->group_last(league) - picker_->group_first(league) + 1)});
        } else {
            fail("E_RULE", "the leagues are known after NewGame(...)");
        }
        emit_table({"League", "Name", "Clubs"}, rows);
    });
    // --- LeagueTable ----------------------------------------------------------------------
    add({"LeagueTable", "ShowTable", {integer("league", 0, kLeagueCount - 1, true)}, "the table (without league: the one shown)", false},
        [this](const Values& v) {
            if (v.has("league")) table_league_ = int(v.integer("league"));
            show_league_table();
        });
    add({"LeagueTable", "SetLeague", {integer("league", 0, kLeagueCount - 1)}, "show another league", false}, [this](const Values& v) {
        table_league_ = int(v.integer("league"));
        show_league_table();
    });
    add({"LeagueTable", "OpenFixtures", {}, "the fixtures of this league", false}, [this](const Values&) { open_fixtures(); });
    for (const char* name : {"ToggleEurope", "SetView", "SetCriteria", "SetRange", "SetPointsMode", "ComputerTip", "OpenStrengths"}) {
        add({"LeagueTable", name, {}, "not in the console yet", false}, [name](const Values&) {
            fail("E_ENGINE_GAP", std::string(name) + " of the league table is not in the console yet");
        });
    }

    // --- Fixtures -------------------------------------------------------------------------
    add({"Fixtures", "ShowFixtures", {integer("league", 0, kLeagueCount - 1, true), integer("matchday", 1, 38, true)},
         "the page shown (optionally another league / matchday first)", false},
        [this](const Values& v) {
            if (v.has("league")) fixtures_action(LeaguePairingsAction::select_league, int(v.integer("league")));
            if (v.has("matchday")) {
                const int wanted = int(v.integer("matchday")) - 1;
                for (int step = 0; step < 40 && pairings_ && pairings_->identity.round != wanted; ++step)
                    fixtures_action(LeaguePairingsAction::next);
                if (pairings_ && pairings_->identity.round != wanted) fail("E_BAD_ARGUMENT", "the league has no such matchday");
            }
            if (!v.has("league") && !v.has("matchday")) show_fixtures();
        });
    add({"Fixtures", "SetLeague", {integer("league", 0, kLeagueCount - 1)}, "another league", false},
        [this](const Values& v) { fixtures_action(LeaguePairingsAction::select_league, int(v.integer("league"))); });
    add({"Fixtures", "SetMatchday", {integer("matchday", 1, 38)}, "go to a matchday", false}, [this](const Values& v) {
        const int wanted = int(v.integer("matchday")) - 1;
        // page through without printing every page on the way: only the target is shown
        quiet_fixtures_ = true;
        try {
            for (int step = 0; step < 40 && pairings_ && pairings_->identity.round != wanted; ++step)
                fixtures_action(LeaguePairingsAction::next);
        } catch (...) {
            quiet_fixtures_ = false;
            throw;
        }
        quiet_fixtures_ = false;
        if (pairings_ && pairings_->identity.round != wanted) fail("E_BAD_ARGUMENT", "the league has no such matchday");
        if (pairings_) show_fixtures();
    });
    add({"Fixtures", "NextMatchday", {}, "the next matchday (arrow)", false},
        [this](const Values&) { fixtures_action(LeaguePairingsAction::next); });
    add({"Fixtures", "PreviousMatchday", {}, "the previous matchday (arrow)", false},
        [this](const Values&) { fixtures_action(LeaguePairingsAction::previous); });
    add({"Fixtures", "ShowPostponed", {}, "the postponed games", false},
        [this](const Values&) { fixtures_action(LeaguePairingsAction::postponed_list); });
    add({"Fixtures", "ComputerTip", {}, "the computer's forecast for this page", false},
        [this](const Values&) { fixtures_action(LeaguePairingsAction::computer_tip); });
    add({"Fixtures", "OpenTable", {}, "the league table", false}, [this](const Values&) {
        // the table window is a pure projection; leaving the fixtures closes them first
        const int league = pairings_ ? pairings_->identity.league : -1;
        fixtures_action(LeaguePairingsAction::close);
        open_window("LeagueTable");
        table_league_ = league;
        show_league_table();
    });
    for (const char* name : {"FindGame", "GamesOf", "Page", "OpenStrengths"}) {
        add({"Fixtures", name, {}, "not in the console yet", false}, [name](const Values&) {
            fail("E_ENGINE_GAP", std::string(name) + " of the fixtures is not in the console yet");
        });
    }
}

void Console::show_league_table() {
    const ui::DeskState d = desk();
    const int league = table_league_ >= 0 ? table_league_ : (d.league >= 0 ? d.league : 0);
    table_league_ = league;
    const ui::LeagueTableState table = ui::league_table_from_game(session_->state(), d.manager_slot, league);
    emit('i', table.league_name + ", " + std::to_string(table.rounds_played) + " matchdays played");
    std::vector<std::vector<std::string>> rows;
    for (const auto& r : table.rows) {
        std::string name = r.name;
        for (size_t m = 0; m < d.manager_teams.size(); ++m)
            if (d.manager_teams[m] == r.runtime_team_id) name += " [human:" + std::to_string(m + 1) + "]";
        rows.push_back({std::to_string(r.rank), name, std::to_string(r.games), std::to_string(r.wins), std::to_string(r.draws),
                        std::to_string(r.losses), std::to_string(r.goals_for) + ":" + std::to_string(r.goals_against),
                        std::to_string(r.goals_for - r.goals_against), std::to_string(r.points)});
    }
    emit_table({"Rank", "Club", "G", "W", "D", "L", "Goals", "Diff", "Pts"}, rows);
}

void Console::open_fixtures() {
    const ui::DeskState d = desk();
    std::string error;
    std::optional<ui::LeaguePairingsState> page;
    try {
        page = app::LeagueFamilyActions(*session_).open_pairings(d, revision_, error);
    } catch (const std::exception& e) {
        fail("E_INTERNAL", std::string("fixtures: ") + e.what());
    }
    if (!page) fail("E_RULE", std::string("fixtures cannot open") + (d.national_team ? " while you coach the national team" : "") + ": " + error);
    pairings_ = std::move(page);
    open_window("Fixtures");
    show_fixtures();
}

void Console::show_fixtures() {
    if (!pairings_) fail("E_INTERNAL", "no fixtures page");
    const auto& p = *pairings_;
    // the original pads its title parts for the screen layout
    auto trim = [](std::string s) {
        while (!s.empty() && s.front() == ' ') s.erase(0, 1);
        while (!s.empty() && s.back() == ' ') s.pop_back();
        return s;
    };
    std::string title = trim(utf8(p.title));
    for (const std::string& part : {trim(utf8(p.subtitle)), trim(utf8(p.date))})
        if (!part.empty()) title += ", " + part;
    emit('i', title);
    std::vector<std::vector<std::string>> rows;
    for (const auto& r : p.rows) {
        std::string result = utf8(r.result);
        // the original note already names the leg ("2:2 Hinspiel", message LeaguePairings_FormatError_TextPointer13)
        if (!r.first_leg.empty()) result += " (" + utf8(r.first_leg) + ")";
        if (!r.scheduled_date.empty())
            result += " " + utf8(r.scheduled_ordinal) + utf8(r.scheduled_date);
        rows.push_back({utf8(r.names[0]), utf8(r.names[1]), result.empty() ? "-" : result});
    }
    emit_table({"Home", "Away", "Result"}, rows);
}

// One action in the fixtures window, applied by the engine on a staged copy; a rejected
// action leaves state and random generators as they were (as src/app/controller.cpp does).
void Console::fixtures_action(LeaguePairingsAction kind, int value) {
    if (!pairings_) fail("E_INTERNAL", "the fixtures window is not open");
    std::string error;
    std::optional<app::CareerSession::LeaguePairingsDispatch> applied;
    try {
        applied = app::LeagueFamilyActions(*session_).pairings(pairings_->identity, kind, value, revision_, error);
    } catch (const std::exception& e) {
        fail("E_RULE", std::string("fixtures: ") + e.what());
    }
    if (!applied) fail("E_RULE", "fixtures: " + error);
    if (applied->closed) {
        pairings_.reset();
        close_window();
        emit('+', "fixtures closed");
        return;
    }
    pairings_ = std::move(applied->page);
    if (!quiet_fixtures_) show_fixtures();
}

}  // namespace bmh::cli::console
