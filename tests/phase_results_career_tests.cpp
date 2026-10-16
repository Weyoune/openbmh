// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <cstdio>
#include <stdexcept>

#include "app/career_session.hpp"
#include "game/driver.hpp"
#include "game/matchday.hpp"
#include "game/save.hpp"
#include "ui/league_pairings.hpp"
#include "ui/other_games.hpp"

using namespace bmh;
namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
uint64_t start(app::CareerSession& session) {
    ui::SetupOptions options;
    ui::ManagerChoice choice;
    choice.name = "PHASE"; choice.master_club = 0; choice.strength = 2;
    std::string error;
    if (!session.confirm_manager(options, choice, error)) throw std::runtime_error(error);
    ui::CareerInitializationResult initialized;
    if (!session.finish(initialized, error)) throw std::runtime_error(error);
    return initialized.desktop.revision;
}
void advance(app::CareerSession& session, uint64_t& revision) {
    const auto desk = session.desktop_projection(revision);
    ui::DeskRequest request;
    request.kind = ui::DeskRequestKind::advance_turn;
    request.manager_slot = desk.manager_slot; request.revision = revision;
    std::string error;
    const auto result = session.dispatch_kickoff(request, revision, error);
    check(result.kind == TurnDeskKind::NextDesk, error.empty() ? result.reason.c_str() : error.c_str());
}

void interactive_viewers(const char* data) {
    app::CareerSession shown(data, 8128), quiet(data, 8128);
    uint64_t revision = start(shown), quiet_revision = start(quiet);
    for (auto* session : {&shown, &quiet})
        for (int row = 0; row < 20; ++row)
            for (int column : {1, 2}) session->state().memory.set8(::bmh::layout::LeaguePresentationOptions+uint32_t(row*9+column),0);
    std::string error;
    const auto native = save_stream(shown.state());
    check(!shown.open_phase_results(revision, Image(320,240), error) &&
          save_stream(shown.state()) == native, "phase viewer rejects an invented out-of-boundary opening");
    int leagues = 0, cups = 0, nonfirst = 0, tables = 0;
    shown.set_phase_results([&](const PhaseResultRequest& request,
                               const std::vector<MatchGroup>& groups, const GameState&) {
        const auto game = shown.game(); const auto rand = shown.rand();
        auto page = shown.open_phase_results(revision, Image(320,240), error);
        check(bool(page), error.c_str());
        check(!shown.open_phase_results(revision, Image(320,240), error), "phase viewer rejects nested opening");
        nonfirst += request.group_index != 0;
        if (request.kind == PhaseResultKind::League) {
            ++leagues;
            check(page->pairings && page->family_identity.source_param2 == request.phase_end_minute &&
                  page->pairings->identity.league == request.competition_id,
                  "phase family binds the actual league and Original nonzero minute");
            const auto& rows = page->pairings->source.rows;
            const auto& group = groups.at(request.group_index);
            check(rows.size() == group.results.size(), "phase pairing projection covers the actual group");
            for (size_t n = 0; n < rows.size(); ++n)
                check(rows[n].raw_result == group.results[n] &&
                      rows[n].home.runtime_team == group.fixtures[n*2] &&
                      rows[n].away.runtime_team == group.fixtures[n*2+1],
                      "phase pairing scores are current process scores, not stale Native results");
            ui::LeaguePairingsScreen screen(data, *page->pairings,
                ui::LeaguePairingsScreen::FamilyMode{page->family_identity});
            screen.mark_presented_source_seconds(0);
            check(!screen.tick_source_seconds(3), "Original nonzero phase minute suppresses periodic weather alternation");
            ui::LeagueFamilyRequest action;
            action.identity = page->family_identity; action.from_child = 2;
            if (!tables) {
                action.kind = ui::LeagueFamilyRequestKind::navigate; action.target_child = 1;
                auto result = shown.dispatch_phase_results(action, revision, error);
                check(result && !result->closed && result->table, error.c_str());
                ++tables;
                action.identity = result->family_identity; action.from_child = 1; action.target_child = 0;
            }
            action.kind = ui::LeagueFamilyRequestKind::close;
            auto closed = shown.dispatch_phase_results(action, revision, error);
            check(closed && closed->closed && !shown.live_overview_active(), error.c_str());
        } else {
            ++cups;
            check(page->cup.has_value(), "Source cup phase opens the existing OtherGames viewer");
            const auto& actual=groups.at(request.group_index);
            check(page->cup->projection.rows.size()==actual.cup_results.size(),
                  "cup viewer covers the real current fixture vector");
            for(size_t n=0;n<actual.cup_results.size();++n)
                check(page->cup->projection.rows[n].source_result==actual.cup_results[n] &&
                      page->cup->projection.rows[n].home_team==actual.fixtures[n*2] &&
                      page->cup->projection.rows[n].away_team==actual.fixtures[n*2+1],
                      "cup phase shows actual scores and team identities before settlement");
            std::printf("CUP phase=%d group=%u entry=%d competition=%d round=%d/%d view=%u rows=%zu heading=%zu title=%zu\n",
                request.phase_end_minute,request.group_index,page->cup->projection.entry_index,
                page->cup->projection.competition,page->cup->projection.round,page->cup->projection.rounds,
                page->cup->identity.expected_view_byte,page->cup->rows.size(),
                page->cup->projection.round_heading.size(),page->cup->projection.title.size());
            ui::OtherGamesScreen screen(data, *page->cup);
            (void)screen.render();
            ui::OtherGamesRequest action{page->cup->identity, ui::OtherGamesAction::close, -1};
            auto closed = shown.dispatch_phase_results(action, revision, error);
            check(closed && closed->closed && !shown.live_overview_active(), error.c_str());
        }
        check(shown.game().seed() == game.seed() && shown.game().calls() == game.calls() &&
              shown.rand().seed() == rand.seed() && shown.rand().calls() == rand.calls(),
              "viewer opening/navigation/close leaves the single outer ACK draw to the engine");
    });
    for (int turn = 0; turn < 24 && (!leagues || !cups); ++turn) {
        advance(shown, revision); advance(quiet, quiet_revision);
        check(shown.game().seed() == quiet.game().seed() && shown.game().calls() == quiet.game().calls() &&
              shown.rand().seed() == quiet.rand().seed() && shown.rand().calls() == quiet.rand().calls(),
              "interactive phase close does not add or remove Source RNG draws");
        const auto& a=shown.last_turn_results(); const auto& b=quiet.last_turn_results();
        check(a.valid == b.valid && a.groups.size() == b.groups.size(), "phase UI preserves completed groups");
        for (size_t n=0;n<a.groups.size();++n)
            check(a.groups[n].results == b.groups[n].results && a.groups[n].cup_results == b.groups[n].cup_results &&
                  a.groups[n].end_minute == b.groups[n].end_minute && a.groups[n].log == b.groups[n].log,
                  "phase UI preserves actual league/cup outcomes, settlement and event logs");
    }
    check(leagues && cups && nonfirst && tables, "actual phase viewers cover leagues, cups, nonfirst group and Table navigation");
    check(!shown.open_phase_results(revision, Image(320,240), error), "phase scope expires before returning to the desk");
    std::printf("INTERACTIVE leagues=%d cups=%d nonfirst=%d tables=%d; current scores, Source actions and single ACK RNG\n",
                leagues, cups, nonfirst, tables);
}
}

int main(int argc, char** argv) { try {
    check(argc == 2, "usage: phase_results_career_tests <Original data>");
    app::CareerSession observed(argv[1], 8128), plain(argv[1], 8128);
    uint64_t a = start(observed), b = start(plain);
    // Enable only the documented result-display cells on both fresh careers.
    // Default setup may hide them; this is a visibility test, not a new seed.
    for (auto* session : {&observed, &plain}) {
        for (int row = 0; row < 20; ++row)
            for (int column : {1, 2})
                session->state().memory.set8(::bmh::layout::LeaguePresentationOptions + uint32_t(row*9+column), 0);
    }
    int league_half = 0, league_full = 0, cups = 0;
    std::vector<PhaseResultRequest> owned;
    observed.set_phase_results([&](const PhaseResultRequest& request,
                                  const std::vector<MatchGroup>& groups, const GameState& state) {
        check(&state == &observed.state() && request.group_index < groups.size(),
              "phase observation belongs to the actual career and group");
        const auto& group = groups.at(request.group_index);
        check(request.phase_end_minute == 45 || request.phase_end_minute == 90 ||
              request.phase_end_minute == 105 || request.phase_end_minute == 120 ||
              request.phase_end_minute == 20 || request.phase_end_minute == 40,
              "request uses an Original phase boundary, not the next desk minute");
        check(request.matches.size() == size_t(group.matches()), "owned request covers its actual group");
        for (size_t m = 0; m < request.matches.size(); ++m) {
            const auto& row = request.matches[m];
            int home = 0, away = 0;
            const bool known = match_score(group, int(m), home, away);
            check(row.home_team == group.fixtures[m*2] && row.away_team == group.fixtures[m*2+1] &&
                  row.score_known == known && row.home_score == home && row.away_score == away,
                  "request scores come from the real unfinished group");
        }
        if (request.kind == PhaseResultKind::League) {
            check(request.competition_id >= 0 && request.competition_id < 9,
                  "league result has its Source league ID");
            league_half += request.phase_end_minute == 45;
            league_full += request.phase_end_minute == 90;
        } else ++cups;
        owned.push_back(request);
    });
    for (int turn = 0; turn < 24 && (!league_half || !league_full || !cups); ++turn) {
        advance(observed, a); advance(plain, b);
        check(a == b && save_stream(observed.state()) == save_stream(plain.state()) &&
              observed.game().seed() == plain.game().seed() && observed.game().calls() == plain.game().calls() &&
              observed.rand().seed() == plain.rand().seed() && observed.rand().calls() == plain.rand().calls(),
              "observing phase requests preserves full Native and both Source generators");
        const auto& x = observed.last_turn_results();
        const auto& y = plain.last_turn_results();
        check(x.valid == y.valid && x.groups.size() == y.groups.size(), "completed result snapshots agree");
        for (size_t i = 0; i < x.groups.size(); ++i)
            check(x.groups[i].desc == y.groups[i].desc && x.groups[i].results == y.groups[i].results &&
                  x.groups[i].cup_results == y.groups[i].cup_results && x.groups[i].log == y.groups[i].log,
                  "phase observation preserves league/cup results, counters and event logs");
    }
    std::printf("COVERAGE league-half=%d league-full=%d cup=%d\n", league_half, league_full, cups);
    check(league_half && league_full && cups && !owned.empty(),
          "actual fresh career reaches league half/full time and cup result requests");
    check(!owned.front().matches.empty(), "copied result request survives later phases and calendar advance");
    observed.set_phase_results({});
    interactive_viewers(argv[1]);
    std::printf("PASS: actual career phase requests league-half=%d league-full=%d cup=%d; full Native/RNG neutral, owned scores; no Main viewer claim\n",
                league_half, league_full, cups);
    return 0;
} catch (const std::exception& error) { std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1; } }
