// SPDX-License-Identifier: GPL-3.0-only
#include "ui/top_scorers.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace bmh;
using namespace bmh::ui;

namespace {
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

TopScorersState state_for(TopScorersMetric metric, TopScorersScope scope) {
    TopScorersState state;
    state.identity.manager_slot = 0;
    state.identity.runtime_team = 0;
    state.identity.club_identity = 8;
    state.identity.league = 0;
    state.identity.day_counter = 4;
    state.identity.revision = 7;
    state.identity.scope = 0x1234;
    state.identity.sequence = 0;
    state.metric = metric;
    state.scope = scope;
    state.projection.manager_slot = 0;
    state.projection.league = 0;
    state.projection.metric = top_scorers_metric_to_game(metric);
    state.projection.scope = top_scorers_scope_to_game(scope);
    state.projection.no_results = true;
    return state;
}

void press_and_release(TopScorersScreen& screen, int x, int y) {
    check(screen.pointer_down(x, y), "Source footer down should arm the button");
    check(screen.release(x, y), "Source footer up should resolve the button");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: ui_top_scorers_tests <BMH data>");
        const std::filesystem::path data(argv[1]);

        for (auto metric : {TopScorersMetric::goals, TopScorersMetric::assists, TopScorersMetric::scorer}) {
            for (auto scope : {TopScorersScope::league, TopScorersScope::managers}) {
                TopScorersScreen view(data, state_for(metric, scope));
                const auto image = view.render();
                check(image.width == 320 && image.height == 240, "Original logical frame is 320 by 240");
                check(!view.pointer_down(80, 108), "Source rows have no registered actions");
                check(!view.release(80, 108), "Row release has no invented navigation");
            }
        }

        TopScorersScreen metric(data, state_for(TopScorersMetric::goals, TopScorersScope::league));
        press_and_release(metric, 140, 225); // inclusive Source boundary
        check(metric.pending() && metric.pending()->action == TopScorersAction::set_metric &&
              metric.pending()->metric == TopScorersMetric::assists &&
              metric.pending()->scope == TopScorersScope::league,
              "left footer cycles goals to assists without changing scope");
        const auto original = metric.state();
        metric.reject_action("fixture rejection");
        check(metric.state().identity == original.identity && metric.state().metric == original.metric &&
              metric.state().scope == original.scope && metric.state().projection == original.projection &&
              !metric.pending(), "rejection preserves the displayed projection");

        press_and_release(metric, 280, 225); // inclusive rightmost Source boundary
        check(metric.pending() && metric.pending()->action == TopScorersAction::toggle_scope &&
              metric.pending()->metric == TopScorersMetric::goals &&
              metric.pending()->scope == TopScorersScope::managers,
              "right footer toggles manager scope without changing metric");
        metric.reject_action("fixture rejection");

        check(metric.pointer_down(40, 215), "footer left edge is inclusive");
        check(metric.move(141, 225), "drag outside cancels the pressed footer state");
        check(!metric.release(141, 225) && !metric.pending(), "outside mouse up does not dispatch");

        check(metric.pointer_down(160, 160, true), "Source right-click requests close");
        check(metric.pending() && metric.pending()->action == TopScorersAction::close,
              "right-click close is an owned pending action");
        metric.reject_action("fixture rejection");
        std::cout << "TopScorers Original view states and input ownership passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
