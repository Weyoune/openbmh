// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "app/controller.hpp"
#include "game/top_scorers.hpp"
#include "game/schedule.hpp"
#include "game/save.hpp"
#include "game/national.hpp"
#include "ui/flow.hpp"
#include "ui/manager.hpp"

using namespace bmh;

namespace {

void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

GameState empty_game(std::vector<int> managers = {0}) {
    GameState g;
    g.memory.add_block(::bmh::layout::ManagerCount, 1);
    g.memory.set8(::bmh::layout::ManagerCount, uint8_t(managers.size()));
    g.memory.add_block(::bmh::layout::CupDraw_FormatError_RuntimeField3, 2);
    g.memory.set16(::bmh::layout::CupDraw_FormatError_RuntimeField3, 0);
    g.manager_teams = std::move(managers);
    g.club_ids.assign(138, 0);
    g.club_names = {0};
    g.player_name_offsets = {};
    g.player_names = {0};
    g.squad_slots.assign(3100, kEmptySlot);
    return g;
}

int add_player(GameState& g, int team, int squad_slot, uint8_t goals, uint8_t assists) {
    const int player = int(g.players.size());
    Player p;
    p.w364 = uint32_t(goals) << 2;
    p.w368 = uint32_t(assists) << 18;
    g.players.push_back(p);
    g.player_name_offsets.push_back(0);
    const auto range = squad_range(team);
    check(size_t(squad_slot) < range.count, "test player fits Source squad boundary");
    g.squad_slots.at(range.first + size_t(squad_slot)) = uint16_t(player);
    return player;
}

void test_source_ranges_and_metrics() {
    for (int league = 0; league < kLeagueCount; ++league) {
        const int first = league_first_team(league);
        auto g = empty_game({first});
        const int player = add_player(g, first, 0, 7, 5);
        const auto goals = game::top_scorers_from_game(g, 0, game::TopScorersMetric::goals,
                                                        game::TopScorersScope::league);
        const auto assists = game::top_scorers_from_game(g, 0, game::TopScorersMetric::assists,
                                                          game::TopScorersScope::league);
        const auto scorer = game::top_scorers_from_game(g, 0, game::TopScorersMetric::scorer,
                                                         game::TopScorersScope::league);
        check(goals.league == league && goals.rows.size() == 1 && goals.rows[0].player == player &&
              goals.rows[0].team == first && goals.rows[0].score == 7 && goals.rows[0].goals == 7 &&
              goals.rows[0].assists == 0, "Source goals projection traverses each league and gates scratch counters");
        check(assists.rows.size() == 1 && assists.rows[0].score == 5 && assists.rows[0].goals == 0 &&
              assists.rows[0].assists == 5, "Source assists projection gates goals scratch counter");
        check(scorer.rows.size() == 1 && scorer.rows[0].score == 12 && scorer.rows[0].goals == 7 &&
              scorer.rows[0].assists == 5, "Source scorer projection sums and retains both counters");
    }
    check(squad_range(89).count == 24 && squad_range(90).count == 16,
          "the 90-club boundary selects Source 24/16-slot roster layouts");
    auto lower = empty_game({90});
    const int p = add_player(lower, 90, 15, 1, 0);
    const auto page = game::top_scorers_from_game(lower, 0, game::TopScorersMetric::goals,
                                                   game::TopScorersScope::league);
    check(page.league == 5 && page.rows.size() == 1 && page.rows[0].player == p &&
          page.rows[0].squad_slot == int(squad_range(90).first + 15),
          "the first 12-club league accepts its final of 16 Source squad slots");
    lower.squad_slots.at(squad_range(90).first + 1) = kEmptySlot;
    const int after_gap = add_player(lower, 90, 2, 2, 0);
    const auto gap_page = game::top_scorers_from_game(lower, 0, game::TopScorersMetric::goals,
                                                       game::TopScorersScope::league);
    check(gap_page.rows.size() == 2 && gap_page.rows[0].player == after_gap,
          "Source skips empty squad slots and continues scanning later fixed roster positions");
}

void test_zero_strict_ties_and_tenth_cutoff() {
    auto empty = empty_game({0});
    check(game::top_scorers_from_game(empty, 0, game::TopScorersMetric::goals,
          game::TopScorersScope::league).no_results, "all-zero Source scores produce the empty state");

    auto g = empty_game({0});
    const int stronger = add_player(g, 1, 0, 21, 0);
    std::array<int, 11> tied{};
    for (int i = 0; i < 11; ++i) {
        const int team = i < 23 ? 0 : 1;
        const int slot = i < 23 ? i : i - 23;
        tied[size_t(i)] = add_player(g, team, slot, 20, 0);
    }
    const auto page = game::top_scorers_from_game(g, 0, game::TopScorersMetric::goals,
                                                   game::TopScorersScope::league);
    check(page.rows.size() == 10 && page.rows[0].player == stronger && page.rows[0].rank == 1,
          "Source inserts a later strictly higher score before the existing list");
    for (int i = 0; i < 9; ++i)
        check(page.rows[size_t(i + 1)].player == tied[size_t(i)] && page.rows[size_t(i + 1)].rank == i + 2,
              "equal scores keep earlier team/squad traversal order");
    check(std::find_if(page.rows.begin(), page.rows.end(), [&](const auto& row) {
        return row.player == tied[9] || row.player == tied[10];
    }) == page.rows.end(), "equal scores beyond Source's tenth-place boundary are excluded");
}

void test_manager_and_national_original_club_scope() {
    auto multiple = empty_game({0, 1});
    const int first = add_player(multiple, 0, 0, 8, 1);
    const int second = add_player(multiple, 1, 0, 7, 1);
    add_player(multiple, 2, 0, 30, 0);
    const auto managers = game::top_scorers_from_game(multiple, 0, game::TopScorersMetric::goals,
                                                       game::TopScorersScope::managers);
    check(managers.rows.size() == 2 && managers.rows[0].player == first &&
          managers.rows[1].player == second, "manager scope includes every active manager's club only");

    auto national = empty_game({kNoTeam, 1});
    // Source's live ManagerCount/CupDraw_FormatError_RuntimeField3 counterparts can diverge from these loaded
    // StaticMemory bytes after the typed national-coach handoff.
    national.memory.set8(::bmh::layout::ManagerCount, 1);
    national.memory.set16(::bmh::layout::CupDraw_FormatError_RuntimeField3, 2);
    national.manager_records.assign(5 * 0xb0c, 0);
    national.manager_records[4 * 0xb0c + 0x290] = 0;
    national.manager_records[4 * 0xb0c + 0x291] = 0;
    const int original = add_player(national, 0, 0, 4, 0);
    const int other = add_player(national, 1, 0, 3, 0);
    add_player(national, 2, 0, 40, 0);
    const auto national_page = game::top_scorers_from_game(national, 0, game::TopScorersMetric::goals,
                                                            game::TopScorersScope::managers);
    check(national_page.league == 0 && national_page.rows.size() == 2 &&
          national_page.rows[0].player == original && national_page.rows[1].player == other,
          "national manager maps to Source league zero and retains the original-club slot-4 filter");

    auto cup_only = empty_game({150});
    bool rejected = false;
    try { (void)game::top_scorers_from_game(cup_only, 0, game::TopScorersMetric::goals,
                                            game::TopScorersScope::league); }
    catch (const FormatError&) { rejected = true; }
    check(rejected, "ordinary cup-only manager teams do not invent a domestic league");
}

void start_career(const std::filesystem::path& data, ui::StartupFlow& flow,
                  std::unique_ptr<app::CareerSession>& session, uint64_t& revision,
                  openbmh::audio::Playback& playback, std::optional<uint64_t>& audio_session) {
    flow.click(150, 215);
    flow.click(5, 225, true);
    flow.click(5, 225, true);
    const auto started = flow.click(90, 169);
    const auto error = app::serve_flow_request(flow, started, session, data, data, 0x1789abcd,
        revision, playback, audio_session, false);
    check(error.empty() && session && flow.screen_name() == "desktop",
          "TopScorers test starts a real production career through Flow/Controller");
}

void test_app_owner_and_readonly_transactions(const std::filesystem::path& data) {
    ui::StartupFlow flow(data);
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision = 0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
    start_career(data, flow, session, revision, playback, audio_session);

    const auto item = std::find_if(flow.desktop()->state().items.begin(), flow.desktop()->state().items.end(),
        [](const ui::DeskItem& candidate) { return candidate.kind == 20 && !candidate.disabled; });
    check(item != flow.desktop()->state().items.end(), "real desktop exposes enabled menu 20");
    const int x = item->x + item->width / 2, y = item->y + item->height / 2;
    const auto open = flow.click(x, y);
    check(open.request == ui::FlowRequest::desk_action && open.desk_action.menu_kind == 20,
          "menu-20 open is a typed desktop request");
    const auto native = save_stream(session->state());
    const auto w = session->rand();
    const auto g = session->game();
    const auto revision_before = revision;
    const auto opened_error = app::serve_flow_request(flow, open, session, data, data, 0x1789abcd,
        revision, playback, audio_session, false);
    check(opened_error.empty() && flow.in_top_scorers() && revision == revision_before + 1,
          "menu 20 receives its acknowledged TopScorers owner exactly once");
    const auto unchanged = [&] {
        check(save_stream(session->state()) == native && session->rand().seed() == w.seed() &&
              session->rand().calls() == w.calls() && session->game().seed() == g.seed() &&
              session->game().calls() == g.calls(),
              "TopScorers open/view/close preserves complete Native and both RNG streams");
    };
    unchanged();
    check(flow.top_scorers()->state().underlay.width == 320 &&
          flow.top_scorers()->state().underlay.height == 240,
          "open captures the actual rendered desktop underlay");

    const auto initial = flow.top_scorers()->state();
    check(initial.metric == ui::TopScorersMetric::goals && initial.scope == ui::TopScorersScope::league &&
          initial.identity.sequence == 0 && initial.identity.revision == revision,
          "menu 20 starts in Source goal/all-league state with a fresh modal sequence");
    const auto expected = game::top_scorers_from_game(session->state(), initial.identity.manager_slot,
        game::TopScorersMetric::goals, game::TopScorersScope::league);
    check(initial.projection == expected, "typed UI adapter carries the exact const Source projection");

    ui::TopScorersRequest forged{initial.identity, ui::TopScorersAction::set_metric,
        ui::TopScorersMetric::scorer, ui::TopScorersScope::league};
    const auto stale_revision = revision;
    std::string error;
    check(!session->dispatch_top_scorers(forged, revision, error) && !error.empty() &&
          revision == stale_revision && session->top_scorers_owner()->page.identity == initial.identity &&
          session->top_scorers_owner()->page.projection == initial.projection,
          "forged metric skip rejects without consuming scope or changing owner");
    ui::TopScorersRequest wrong_scope{initial.identity, ui::TopScorersAction::toggle_scope,
        ui::TopScorersMetric::goals, ui::TopScorersScope::managers};
    ++wrong_scope.identity.scope;
    check(!session->dispatch_top_scorers(wrong_scope, revision, error) && revision == stale_revision,
          "forged scope/replay identity rejects without Native or revision changes");
    flow.pointer_down(80, 220);
    const auto ack_action = flow.click(80, 220);
    check(ack_action.request == ui::FlowRequest::top_scorers_action && flow.pending_top_scorers(),
          "negative ACK fixture owns a valid pending metric request");
    const auto ack_native = save_stream(session->state());
    const auto ack_w = session->rand(); const auto ack_g = session->game();
    auto staged_checkpoint = session->top_scorers_checkpoint();
    const auto staged_revision = revision;
    std::string staged_error;
    auto staged_ack = session->dispatch_top_scorers(*flow.pending_top_scorers(), revision, staged_error);
    check(staged_ack.has_value() && staged_error.empty(),
          "negative ACK fixture can stage the valid requested page and preparation");
    session->restore_top_scorers_checkpoint(std::move(staged_checkpoint));
    revision = staged_revision;
    const auto reject_bad_ack = [&](bool reserved) {
        auto invalid = staged_ack->page;
        if (reserved) ++invalid.identity.expected_family_word;
        else ++invalid.identity.league;
        bool rejected = false;
        try { flow.acknowledge_top_scorers(invalid, staged_ack->preparation); }
        catch (const FormatError&) { rejected = true; }
        check(rejected && flow.in_top_scorers() && flow.pending_top_scorers() &&
              flow.top_scorers()->state().identity == initial.identity &&
              save_stream(session->state()) == ack_native && session->rand().seed() == ack_w.seed() &&
              session->rand().calls() == ack_w.calls() && session->game().seed() == ack_g.seed() &&
              session->game().calls() == ack_g.calls(),
              reserved ? "reserved-field ACK mismatch preserves owner, frame, Native and RNG" :
                         "league ACK mismatch preserves owner, frame, Native and RNG");
    };
    reject_bad_ack(false);
    reject_bad_ack(true);
    check(app::serve_flow_request(flow, ack_action, session, data, data, 0x1789abcd,
              revision, playback, audio_session, false).empty() && revision == revision_before + 2 &&
          !flow.pending_top_scorers(),
          "valid pending request remains retryable after malformed ACKs are rejected");
    unchanged();
    const auto post_ack_revision = revision;
    ui::FlowResult nested; nested.request = ui::FlowRequest::kickoff;
    check(!app::serve_flow_request(flow, nested, session, data, data, 0x1789abcd,
              revision, playback, audio_session, false).empty() && revision == post_ack_revision &&
          flow.in_top_scorers(), "a cross-family kickoff is blocked while the read-only owner is open");
    ui::DeskRequest kickoff;
    const auto blocked_turn = session->dispatch_kickoff(kickoff, revision, error);
    check(blocked_turn.kind == TurnDeskKind::Failed && !error.empty() && revision == post_ack_revision,
          "direct engine kickoff is also blocked by the active TopScorers session owner");

    const auto click_button = [&](int bx, int by) {
        const auto down = flow.pointer_down(bx, by);
        check(down.request == ui::FlowRequest::none && !flow.top_scorers()->pending(),
              "Source footer press alone does not dispatch an action");
        const auto action = flow.click(bx, by);
        check(action.request == ui::FlowRequest::top_scorers_action && flow.pending_top_scorers(),
              "Source footer release creates one owned typed request");
        const auto before = revision;
        check(app::serve_flow_request(flow, action, session, data, data, 0x1789abcd,
              revision, playback, audio_session, false).empty() && revision == before + 1 &&
              !flow.pending_top_scorers(), "Source footer action receives one accepted ACK");
        check(app::serve_flow_request(flow, action, session, data, data, 0x1789abcd,
              revision, playback, audio_session, false).empty() && revision == before + 1,
              "an accepted TopScorers request replay cannot advance twice");
        unchanged();
    };

    for (const auto metric : {ui::TopScorersMetric::scorer, ui::TopScorersMetric::goals,
                              ui::TopScorersMetric::assists}) {
        click_button(80, 220);
        check(flow.top_scorers()->state().metric == metric &&
              flow.top_scorers()->state().projection == game::top_scorers_from_game(session->state(),
                  initial.identity.manager_slot, ui::top_scorers_metric_to_game(metric),
                  game::TopScorersScope::league),
              "metric footer cycles through Source goals/assists/scorer projections");
    }
    click_button(230, 220);
    check(flow.top_scorers()->state().scope == ui::TopScorersScope::managers,
          "scope footer switches to Source Nur Manager");
    for (const auto metric : {ui::TopScorersMetric::scorer, ui::TopScorersMetric::goals,
                              ui::TopScorersMetric::assists}) {
        click_button(80, 220);
        check(flow.top_scorers()->state().metric == metric &&
              flow.top_scorers()->state().projection == game::top_scorers_from_game(session->state(),
                  initial.identity.manager_slot, ui::top_scorers_metric_to_game(metric),
                  game::TopScorersScope::managers),
              "manager-scope metric state follows the same Source cycle");
    }

    const auto close = flow.pointer_down(10, 10, true);
    check(close.request == ui::FlowRequest::top_scorers_action &&
          close.top_scorers_action.action == ui::TopScorersAction::close,
          "Source event 8/right press owns the TopScorers close action");
    const auto before_close = revision;
    check(app::serve_flow_request(flow, close, session, data, data, 0x1789abcd,
          revision, playback, audio_session, false).empty() && !flow.in_top_scorers() &&
          revision == before_close + 1, "close ACK returns to the desktop and advances one UI revision");
    check(flow.click(10, 10, true).request == ui::FlowRequest::none,
          "right-release after close does not leak into parent desktop input");
    unchanged();
}

void test_live_national_takeover_projection(const std::filesystem::path& data) {
    ui::StartupFlow flow(data);
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision = 0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
    start_career(data, flow, session, revision, playback, audio_session);
    auto& live = session->state();
    const int home = live.manager_teams.at(0);
    national_coach_take_over(live, 1, uint16_t(live.date - live.season_start));
    check(live.manager_teams.size() == 1 && live.manager_teams[0] == kNoTeam,
          "real career enters the Source national-coach temporary owner");
    const size_t backup = 4 * 0xb0c + 0x290;
    const int typed_home = int(uint16_t(live.manager_records.at(backup) |
        uint16_t(live.manager_records.at(backup + 1)) << 8));
    check(typed_home == home, "Source takeover preserves the original club in typed spare manager slot 4");

    const auto roster = squad_range(home);
    int player = -1;
    for (size_t i = 0; i < roster.count; ++i) {
        const uint16_t slot = live.squad_slots.at(roster.first + i);
        if ((slot & 0x0fff) != kEmptySlot) { player = slot & 0x0fff; break; }
    }
    check(player >= 0, "genuine manager home club has a player for the national-scope control");
    live.players.at(size_t(player)).w364 = (live.players.at(size_t(player)).w364 & ~(0x3fu << 2)) | (9u << 2);

    // Deliberately make the loaded StaticMemory copy disagree with the live
    // Source global; the native manager-record swap is authoritative.
    if (!live.memory.has(::bmh::layout::CupDraw_FormatError_RuntimeField3)) live.memory.add_block(::bmh::layout::CupDraw_FormatError_RuntimeField3, 2);
    live.memory.set16(::bmh::layout::CupDraw_FormatError_RuntimeField3, uint16_t((home + 1) % 138));
    const auto native = save_stream(live);
    const auto w = session->rand(); const auto g = session->game();
    const auto page = game::top_scorers_from_game(live, 0, game::TopScorersMetric::goals,
                                                   game::TopScorersScope::managers);
    check(std::any_of(page.rows.begin(), page.rows.end(), [&](const auto& row) {
        return row.team == home && row.player == player && row.score == 9;
    }), "national manager scope follows the live Original club rather than stale loaded b9ad8");
    check(save_stream(live) == native && session->rand().seed() == w.seed() &&
          session->rand().calls() == w.calls() && session->game().seed() == g.seed() &&
          session->game().calls() == g.calls(),
          "national TopScorers projection preserves full Native, W and G state");

    national_coach_hand_back(live);
    check(live.manager_teams.at(0) == home, "real Source hand-back restores the original manager club");
    const auto returned = game::top_scorers_from_game(live, 0, game::TopScorersMetric::goals,
                                                        game::TopScorersScope::managers);
    check(std::any_of(returned.rows.begin(), returned.rows.end(), [&](const auto& row) {
        return row.team == home && row.player == player;
    }), "manager scope continues to show the restored original club after hand-back");
}

void test_national_controller_modal(const std::filesystem::path& data) {
    ui::StartupFlow starting_flow(data);
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision = 0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
    start_career(data, starting_flow, session, revision, playback, audio_session);
    auto& live = session->state();
    const int home = live.manager_teams.at(0);
    national_coach_take_over(live, 1, uint16_t(live.date - live.season_start));
    check(live.manager_teams.at(0) == kNoTeam, "controller national fixture enters Source national ownership");

    // Build a fresh UI desktop from this genuine typed national state so the
    // controller receives the actual Source runtime team (0x165), while the
    // visible stable club remains the coach's original club.
    ui::StartupFlow flow(data);
    flow.click(150, 215); flow.click(5, 225, true); flow.click(5, 225, true);
    const auto pending_career = flow.click(90, 169);
    check(pending_career.request == ui::FlowRequest::new_career && flow.pending_career(),
          "national controller fixture has a real pending career owner");
    auto preparation = session->preparation_projection(revision);
    std::vector<ui::InitializedManager> managers;
    const auto* request = flow.pending_career();
    check(request->managers.size() == preparation.desktop.manager_teams.size(),
          "national desktop projection retains the confirmed manager envelope");
    for (size_t i = 0; i < request->managers.size(); ++i)
        managers.push_back({int(i), request->managers[i].master_club,
                            preparation.desktop.manager_teams[i]});
    flow.attach_initialized_career({preparation.desktop, managers, preparation});
    flow.attach_preparation_context(preparation);
    check(flow.desktop()->state().runtime_club == kNoTeam &&
          flow.desktop()->state().managers[0].club_identity_key == live.club_ids[size_t(home)],
          "national desk carries Source team 0x165 and original stable club identity");

    const auto item = std::find_if(flow.desktop()->state().items.begin(), flow.desktop()->state().items.end(),
        [](const ui::DeskItem& candidate) { return candidate.kind == 20 && !candidate.disabled; });
    check(item != flow.desktop()->state().items.end(), "national desk exposes menu 20");
    const auto native = save_stream(live); const auto w = session->rand(); const auto g = session->game();
    const auto open = flow.click(item->x + item->width / 2, item->y + item->height / 2);
    check(open.request == ui::FlowRequest::desk_action &&
          app::serve_flow_request(flow, open, session, data, data, 0x1789abcd, revision,
              playback, audio_session, false).empty() && flow.in_top_scorers() &&
          flow.top_scorers()->state().identity.runtime_team == kNoTeam &&
          flow.top_scorers()->state().identity.league == 0,
          "real controller opens National TopScorers with Source league-zero identity");
    const auto unchanged = [&] {
        check(save_stream(session->state()) == native && session->rand().seed() == w.seed() &&
              session->rand().calls() == w.calls() && session->game().seed() == g.seed() &&
              session->game().calls() == g.calls(),
              "National TopScorers controller actions preserve Native and both RNG streams");
    };
    unchanged();
    for (const auto [x, y] : {std::pair{80, 220}, std::pair{230, 220}}) {
        flow.pointer_down(x, y);
        const auto action = flow.click(x, y);
        check(action.request == ui::FlowRequest::top_scorers_action &&
              app::serve_flow_request(flow, action, session, data, data, 0x1789abcd,
                  revision, playback, audio_session, false).empty(),
              "National metric/scope action receives a controller ACK");
        unchanged();
    }
    const auto close = flow.pointer_down(10, 10, true);
    check(close.request == ui::FlowRequest::top_scorers_action &&
          app::serve_flow_request(flow, close, session, data, data, 0x1789abcd,
              revision, playback, audio_session, false).empty() && !flow.in_top_scorers(),
          "National menu-20 owner closes through the controller");
    unchanged();
}

void test_process_timer_owner_rejected(const std::filesystem::path& data) {
    ui::StartupFlow flow(data);
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision = 0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
    start_career(data, flow, session, revision, playback, audio_session);
    auto& memory = session->state().memory;
    if (!memory.has(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField2)) memory.add_block(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField2, 4);
    if (!memory.has(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField)) memory.add_block(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField, 2);
    memory.set8(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField2, 1); memory.set8(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField3, 0);
    memory.set8(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField4, 0); memory.set8(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField5, 0);
    memory.set16(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField, 199);
    const auto native = save_stream(session->state());
    const auto w = session->rand(); const auto g = session->game();
    const auto item = std::find_if(flow.desktop()->state().items.begin(), flow.desktop()->state().items.end(),
        [](const ui::DeskItem& candidate) { return candidate.kind == 20 && !candidate.disabled; });
    check(item != flow.desktop()->state().items.end(), "timer guard fixture exposes menu 20");
    const auto open = flow.click(item->x + item->width / 2, item->y + item->height / 2);
    const auto before = revision;
    const auto error = app::serve_flow_request(flow, open, session, data, data, 0x1789abcd,
        revision, playback, audio_session, false);
    check(!error.empty() && !flow.in_top_scorers() && !session->top_scorers_active() &&
          revision == before && save_stream(session->state()) == native &&
          session->rand().seed() == w.seed() && session->rand().calls() == w.calls() &&
          session->game().seed() == g.seed() && session->game().calls() == g.calls(),
          "active short process timer rejects menu 20 before lease, revision, Native or RNG changes");
}

void test_failed_top_scorers_ack_restores_owner(const std::filesystem::path& data) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto temp = std::filesystem::temp_directory_path() /
        ("openbmh-e28-top-scorers-ack-" + std::to_string(nonce));
    std::filesystem::create_directories(temp);
    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(data)) {
            const auto relative = std::filesystem::relative(entry.path(), data);
            const auto target = temp / relative;
            if (entry.is_directory()) std::filesystem::create_directories(target);
            else if (entry.is_regular_file()) {
                std::filesystem::create_directories(target.parent_path());
                std::filesystem::copy_file(entry.path(), target);
            }
        }
        ui::StartupFlow flow(temp);
        std::unique_ptr<app::CareerSession> session;
        uint64_t revision = 0;
        openbmh::audio::Playback playback;
        std::optional<uint64_t> audio_session;
        start_career(temp, flow, session, revision, playback, audio_session);
        const auto item = std::find_if(flow.desktop()->state().items.begin(), flow.desktop()->state().items.end(),
            [](const ui::DeskItem& candidate) { return candidate.kind == 20 && !candidate.disabled; });
        check(item != flow.desktop()->state().items.end(), "asset-fault fixture has menu 20");
        const auto open = flow.click(item->x + item->width / 2, item->y + item->height / 2);
        const auto native = save_stream(session->state());
        const auto w = session->rand(); const auto g = session->game(); const auto old_revision = revision;
        check(app::serve_flow_request(flow, open, session, temp, temp, 0x1789abcd,
              revision, playback, audio_session, false).empty() && flow.in_top_scorers(),
              "asset-fault fixture opens before introducing the mid-transaction fault");
        const auto prior = flow.top_scorers()->state();
        const auto pending_asset = temp / "KB" / "18.VGA";
        std::filesystem::remove(pending_asset);
        flow.pointer_down(80, 220);
        const auto action = flow.click(80, 220);
        check(action.request == ui::FlowRequest::top_scorers_action, "asset-fault fixture has a pending footer action");
        const auto ack_error = app::serve_flow_request(flow, action, session, temp, temp, 0x1789abcd,
            revision, playback, audio_session, false);
        check(!ack_error.empty() && flow.in_top_scorers() && !flow.pending_top_scorers() &&
              flow.top_scorers()->state().identity == prior.identity &&
              session->top_scorers_owner()->page.identity == prior.identity && revision == old_revision + 1 &&
              save_stream(session->state()) == native && session->rand().seed() == w.seed() &&
              session->rand().calls() == w.calls() && session->game().seed() == g.seed() &&
              session->game().calls() == g.calls(),
              "failed renderer ACK restores the full owner, revision, Native and both RNG streams for retry");
    } catch (...) {
        std::filesystem::remove_all(temp);
        throw;
    }
    std::filesystem::remove_all(temp);
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("top_scorers_app_tests requires game data directory");
        test_source_ranges_and_metrics();
        test_zero_strict_ties_and_tenth_cutoff();
        test_manager_and_national_original_club_scope();
        test_app_owner_and_readonly_transactions(argv[1]);
        test_live_national_takeover_projection(argv[1]);
        test_national_controller_modal(argv[1]);
        test_process_timer_owner_rejected(argv[1]);
        test_failed_top_scorers_ack_restores_owner(argv[1]);
        std::puts("top_scorers_app_tests: Source projection and App transactions passed");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "top_scorers_app_tests: %s\n", e.what());
        return 1;
    }
}
