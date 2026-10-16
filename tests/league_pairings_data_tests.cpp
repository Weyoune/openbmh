// SPDX-License-Identifier: GPL-3.0-only
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>

#include "app/career_session.hpp"
#include "game/save.hpp"
#include "game/schedule.hpp"
#include "ui/league_pairings.hpp"

using namespace bmh;
namespace {
void check(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
}

// The same executable is run on DE and on the prepared FLTD cache. Start a real
// career: a German FBM would silently retain the wrong clubs/league structure.
int main(int argc, char** argv) {
    try {
        check(argc == 2, "usage: league_pairings_data_tests <prepared game data>");
        const std::filesystem::path data = argv[1];
        app::CareerSession session(data, 0x1789abcd);
        ui::SetupOptions options;
        options.world = false;
        ui::ManagerChoice manager;
        manager.name = "Codex";
        std::string error;
        check(session.confirm_manager(options, manager, error), error);
        ui::CareerInitializationResult initialized;
        check(session.finish(initialized, error), error);
        uint64_t revision = initialized.desktop.revision;
        const auto native = save_stream(session.state());
        const auto game = session.game();
        const auto rand = session.rand();
        const auto runtime = session.club_modal_runtime_checkpoint();
        const auto stable = [&] {
            return save_stream(session.state()) == native &&
                session.game().seed() == game.seed() && session.game().calls() == game.calls() &&
                session.rand().seed() == rand.seed() && session.rand().calls() == rand.calls() &&
                session.club_modal_runtime_checkpoint() == runtime;
        };
        const ui::DeskRequest origin{ui::DeskRequestKind::open_menu, 0, 0, 1, 0, revision};
        auto page = session.open_league_pairings(origin, revision, error);
        check(page.has_value(), error);
        const bool english = kLeagueSizes[0] == 20;
        const int domestic_rows = english ? 10 : 9;
        check(page->rows.size() == size_t(domestic_rows) && stable(),
              "actual career opens all nine/ten pairings without changing live state or RNG");
        auto dispatch = [&](LeaguePairingsEvent event) {
            const auto next = session.dispatch_league_pairings({page->identity, event}, revision, error);
            check(next.has_value() && !next->closed, error);
            page = next->page;
            check(stable(), "navigation and forecasts stay in their staged owner");
        };
        // Four English divisions / five German divisions, then all four foreign
        // leagues. English league 4 has no calendar group and no selector button.
        for (int league = 0; league < 9; ++league) {
            if (kLeagueBit[league] == 0xff) continue;
            dispatch({LeaguePairingsAction::select_league, league});
            check(page->source.total_rounds == (league < 5 ? (english ? 38 : 34) : 22),
                  "round count follows the loaded domestic/foreign league");
            const int expected = league < 5 ? domestic_rows : 6;
            check(page->rows.size() == size_t(expected), "all fixture rows survive the app projection");
            const auto fixtures = league_fixtures(league, 1);
            check(page->source.rows.back().fixture_index == expected - 1 &&
                  page->source.rows.back().home.runtime_team == fixtures.back().home &&
                  page->source.rows.back().away.runtime_team == fixtures.back().away,
                  "last displayed pairing retains the actual fixture identities");
            ui::LeaguePairingsScreen screen(data, *page);
            check(screen.render().height == 240, "full page renders within the Original screen");
            check(screen.pointer_down(5, 29 + (expected - 1) * 15) && screen.pending() &&
                  screen.pending()->event.kind == LeaguePairingsAction::team_info &&
                  screen.pending()->event.value == fixtures.back().home,
                  "bottom pairing opens its own home club, including English row ten");
            auto short_page = *page;
            short_page.rows.pop_back();
            short_page.source.rows.pop_back();
            bool rejected = false;
            try { ui::LeaguePairingsScreen incomplete(data, std::move(short_page)); }
            catch (const FormatError&) { rejected = true; }
            check(rejected, "an incomplete ordinary round is still rejected");
            dispatch({LeaguePairingsAction::computer_tip, 4});
            check(page->source.owner.computer_tip_results.size() == size_t(expected),
                  "Computer Tip predicts every fixture, including row ten");
            for (size_t row = 0; row < page->rows.size(); ++row)
                check(page->source.rows[row].raw_result == 0xcc &&
                      page->source.rows[row].computer_tip_result.has_value() && !page->rows[row].result.empty(),
                      "Computer Tip overlays scores without replacing the Native unplayed results");
            ui::LeaguePairingsScreen forecast(data, *page);
            (void)forecast.render();
            dispatch({LeaguePairingsAction::next});
            check(!page->source.owner.computer_tip_active, "navigation expires the one-display forecast");
        }
        dispatch({LeaguePairingsAction::select_league, 0});
        const int rounds = english ? 38 : 34;
        while (page->identity.round < rounds - 1) dispatch({LeaguePairingsAction::next});
        check(page->source.rows.size() == size_t(domestic_rows), "last round keeps every pairing");
        dispatch({LeaguePairingsAction::computer_tip, 4});
        check(page->source.owner.computer_tip_results.size() == size_t(domestic_rows),
              "Computer Tip covers the complete final round");
        std::cout << (english ? "EN" : "DE") << ": ordinary pairings, last-row hit testing, complete forecasts, "
                  << rounds << " rounds and four foreign leagues passed; live Native/W/G unchanged\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
