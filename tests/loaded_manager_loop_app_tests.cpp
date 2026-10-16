// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "app/controller.hpp"
#include "game/desk.hpp"
#include "game/matchday_setup.hpp"
#include "game/save.hpp"
#include "game/weekly.hpp"

using namespace bmh;
namespace fs = std::filesystem;
namespace {
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
struct TemporarySaves {
    fs::path path;
    explicit TemporarySaves(const fs::path& input) {
        static unsigned serial = 0;
        path = fs::temp_directory_path() / ("openbmh-loaded-manager-app-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(++serial));
        require(fs::create_directory(path), "create isolated loaded-loop test save directory");
        fs::copy_file(input, path / "AUTOSAVE.FBM");
    }
    ~TemporarySaves() { std::error_code ignored; fs::remove_all(path, ignored); }
};
struct Context {
    fs::path data;
    TemporarySaves saves;
    ui::StartupFlow flow;
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision = 0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio;
    Context(const fs::path& d, const fs::path& input) : data(d), saves(input), flow(d, saves.path) {
        require(load_savegame(input).checksum_ok, "input Native fixture checksum");
        playback.set_muted(true);
        flow.click(214, 215);
        const auto request = flow.send_key("ENTER");
        require(request.request == ui::FlowRequest::file_action && request.file_action.action == ui::SaveLoadAction::load,
                "actual StartupFlow requests Native load");
        require(serve(request).empty() && session && revision == 1 && flow.screen_name() == "desktop",
                "actual controller loads two-manager Native desk");
        const auto driver = session->manager_join_checkpoint().driver;
        require(session->state().manager_teams.size() == 2 && driver.loaded_matchday_resume &&
                !driver.suppress_remaining_pre_match_offers && driver.stage == TurnDriver::Stage::MatchdayDesks,
                "successful actual FBM restore owns provenance without automatically selecting suppression");
    }
    std::string serve(const ui::FlowResult& request, PendingOfferHooks hooks = {},
                      app::PendingOfferEntryPolicy policy = app::PendingOfferEntryPolicy::normal_interactive) {
        return app::serve_flow_request(flow, request, session, data, saves.path, 1, revision, playback, audio, false,
                                      {}, {}, {}, {}, {}, {}, {}, {}, std::move(hooks), policy);
    }
    ui::FlowResult advance() {
        require(flow.desktop() && !flow.desktop()->pending(), "real desktop owns no previous request");
        const auto& items = flow.desktop()->state().items;
        const auto item = std::find_if(items.begin(), items.end(), [](const auto& i) {
            return !i.disabled && (i.kind == 12 || (i.kind == 58 && i.open));
        });
        require(item != items.end(), "actual enabled ADVANCE/Weiter item exists");
        auto request = flow.click(item->x + item->width / 2, item->y + item->height / 2);
        require(request.request == ui::FlowRequest::kickoff && flow.desktop()->pending() &&
                request.desk_action.revision == revision, "actual flow issues current owned kickoff request");
        return request;
    }
};
struct Snapshot { Bytes native; WatcomRandom w; GameRandom g; };
Snapshot snapshot(const GameState& s, const WatcomRandom& w, const GameRandom& g) { return {save_stream(s), w, g}; }
Snapshot snapshot(app::CareerSession& s) { return snapshot(s.state(), s.rand(), s.game()); }
bool equal(const Snapshot& a, const Snapshot& b) {
    return a.native == b.native && a.w.seed() == b.w.seed() && a.w.calls() == b.w.calls() &&
        a.g.seed() == b.g.seed() && a.g.calls() == b.g.calls();
}
void same(const Snapshot& a, const Snapshot& b) { require(equal(a, b), "entire Native stream and W/G seeds/counts match Source oracle"); }
bool same_group(const MatchGroup& a, const MatchGroup& b) {
    return a.desc == b.desc && a.fixtures == b.fixtures && a.results == b.results &&
        a.cup_results == b.cup_results && a.cup_other_leg == b.cup_other_leg &&
        a.cup_result_address[0] == b.cup_result_address[0] && a.cup_result_address[1] == b.cup_result_address[1] &&
        a.phase == b.phase && a.records == b.records && a.end_minute == b.end_minute &&
        a.spectators == b.spectators && a.log == b.log && a.ended == b.ended && a.kickoff_void_mask == b.kickoff_void_mask;
}
bool same_groups(const std::vector<MatchGroup>& a, const std::vector<MatchGroup>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), same_group);
}
void same_driver(const TurnDriver& a, const TurnDriver& b) {
    require(a.stage == b.stage && a.manager_position == b.manager_position && a.revision == b.revision &&
        a.calendar_entry == b.calendar_entry && a.session_id == b.session_id && a.next_event_id == b.next_event_id &&
        a.loaded_matchday_resume == b.loaded_matchday_resume &&
        a.suppress_remaining_pre_match_offers == b.suppress_remaining_pre_match_offers &&
        a.referee_source == b.referee_source && a.referees == b.referees && same_groups(a.groups, b.groups) &&
        a.last_results.valid == b.last_results.valid && a.last_results.date == b.last_results.date &&
        a.last_results.calendar_entry == b.last_results.calendar_entry && a.last_results.matchday == b.last_results.matchday &&
        same_groups(a.last_results.groups, b.last_results.groups), "rollback retains complete bound driver including both loop flags");
}
void restored(Context& c) {
    require(c.session->pending_offer_entry_policy() == app::PendingOfferEntryPolicy::legacy_unpresented &&
        !c.session->pending_offer_hooks().answer && !c.session->pending_offer_hooks().select_transfer &&
        !c.session->pending_offer_hooks().notice, "controller restores previous policy and empty pending hooks");
}
PendingOfferHooks supported_answers() {
    PendingOfferHooks hooks;
    hooks.answer = [](const GameState&, const PendingOfferQuestion&) { return PendingOfferAnswer::yes; };
    // Explicit test answer to an actual presented quote; never a default/fabricated NO.
    hooks.select_transfer = [](const GameState&, const TransferOfferListView&) {
        return PendingOfferTransferChoice{PendingOfferTransferChoiceKind::decline, -1};
    };
    hooks.notice = [](const GameState&, const PendingOfferNotice&) {};
    return hooks;
}
struct QuestionPoint { Bytes native; PendingOfferQuestion question; };
struct Oracle {
    Snapshot pending, final;
    TurnDriver driver;
    std::vector<QuestionPoint> questions;
    unsigned processed = 0;
};
Oracle next_desk_oracle(app::CareerSession::ManagerJoinCheckpoint checkpoint, bool process_pending,
                       bool include_pre_match_offer, uint64_t revision) {
    Oracle out; MinuteReport report;
    if (process_pending) {
        const auto result = process_natural_pending_offers_after_desk(checkpoint.state,
            {PendingOfferDeskRuntimeMode::normal_interactive, 2, checkpoint.driver.manager_position},
            checkpoint.game, checkpoint.rand, supported_answers(), report);
        require(result.completed && !report.unsupported, "Source Core pending phase completes before independent next-desk composition");
    }
    out.pending = snapshot(checkpoint.state, checkpoint.rand, checkpoint.game);
    // Independent format field-22561 composition, deliberately not enter_event_desk(false).
    const int position = checkpoint.driver.manager_position + 1;
    const int manager = checkpoint.state.memory.u16(::bmh::layout::NextDeskManager + uint32_t(position) * 2);
    checkpoint.state.memory.set8(::bmh::layout::DeskPosition, uint8_t(position));
    checkpoint.state.memory.set8(::bmh::layout::ActiveManager, uint8_t(manager));
    if (include_pre_match_offer) pre_match_offer(checkpoint.state, manager, checkpoint.game, checkpoint.rand, report);
    build_pools_coupon(checkpoint.state, checkpoint.driver.groups, &checkpoint.rand);
    enter_desk(checkpoint.state, manager, checkpoint.game, checkpoint.rand);
    require(!report.unsupported, "Source coupon/desk composition supported");
    checkpoint.driver.manager_position = position;
    checkpoint.driver.revision = revision + 1;
    out.final = snapshot(checkpoint.state, checkpoint.rand, checkpoint.game);
    out.driver = std::move(checkpoint.driver);
    return out;
}
Oracle pending_oracle(Context& c, const ui::FlowResult& request) {
    auto checkpoint = c.session->manager_join_checkpoint(); Oracle out;
    auto hooks = supported_answers();
    hooks.answer = [&](const GameState& state, const PendingOfferQuestion& q) {
        out.questions.push_back({save_stream(state), q}); return PendingOfferAnswer::yes;
    };
    MinuteReport report;
    const auto pending = process_natural_pending_offers_after_desk(checkpoint.state,
        {PendingOfferDeskRuntimeMode::normal_interactive, 2, checkpoint.driver.manager_position},
        checkpoint.game, checkpoint.rand, hooks, report);
    require(pending.completed && !report.unsupported && pending.salary_accepted == 1 && pending.processed == 1,
            "actual generated salary demand has one complete direct Core oracle");
    out.processed = pending.processed;
    out.pending = snapshot(checkpoint.state, checkpoint.rand, checkpoint.game);
    require(out.questions.size() == 1, "one actual Source question per offered manager");
    if (checkpoint.driver.manager_position == 0) {
        auto desk = next_desk_oracle(checkpoint, false, false, c.revision);
        out.final = std::move(desk.final); out.driver = std::move(desk.driver);
        out.driver.suppress_remaining_pre_match_offers = true;
    } else {
        checkpoint.driver.suppress_remaining_pre_match_offers = checkpoint.driver.loaded_matchday_resume;
        checkpoint.driver.revision = c.revision;
        MinuteReport turn_report;
        const auto result = advance_to_next_desk(checkpoint.driver, checkpoint.state,
            {request.desk_action.manager_slot, request.desk_action.revision}, checkpoint.game, checkpoint.rand, turn_report);
        require(result.kind == TurnDeskKind::NextDesk && !turn_report.unsupported,
                "bound direct driver oracle completes last manager and reaches fresh event");
        out.final = snapshot(checkpoint.state, checkpoint.rand, checkpoint.game);
        out.driver = std::move(checkpoint.driver);
    }
    return out;
}
void original_empty_handoff(const fs::path& data, const fs::path& input) {
    Context normal(data, input);
    const auto initial = normal.session->manager_join_checkpoint();
    require(initial.state.date == 0 && initial.driver.manager_position == 0 &&
        initial.state.memory.u16(::bmh::layout::NextDeskManager) == 0 && initial.state.memory.u16(::bmh::layout::FixtureSecondDeskCursor) == 1 &&
        initial.state.manager_teams == std::vector<int>{1, 54} && initial.state.memory.u32(::bmh::layout::CompetitionSelection) != 2 &&
        initial.state.memory.u8(::bmh::layout::GameMode) != 1, "untouched genuine2PNWK001 permits observable pre-match offer draws");
    for (int raw = 0; raw < 4; ++raw) require(initial.state.memory.u32(::bmh::layout::OfferSlots + uint32_t(raw) * 4) == 0xffffffffu,
                                            "genuine fixture has no pending offers");
    const auto expected = next_desk_oracle(initial, true, false, 1);
    const auto legacy_expected = next_desk_oracle(initial, false, true, 1);
    require(expected.final.w.seed() != legacy_expected.final.w.seed() ||
        expected.final.w.calls() != legacy_expected.final.w.calls() || expected.final.g.seed() != legacy_expected.final.g.seed() ||
        expected.final.g.calls() != legacy_expected.final.g.calls(), "counterfactual actual30f2d has observable RNG effect");
    require(normal.serve(normal.advance()).empty() && normal.revision == 2, "actual loaded normal GUI handoff completes once");
    same(snapshot(*normal.session), expected.final); restored(normal);
    auto driver = normal.session->manager_join_checkpoint().driver;
    require(driver.loaded_matchday_resume && driver.suppress_remaining_pre_match_offers && driver.manager_position == 1 &&
        normal.session->state().date == 0 && normal.session->state().memory.u8(::bmh::layout::ActiveManager) == 1,
        "loaded provenance/suppression survive real handoff to ordered second manager");

    // This is a product-generated second-manager checkpoint, not an Original recording.
    const auto save_path = normal.saves.path / "SECOND.FBM"; std::string error;
    auto save_w = normal.session->rand();
    const auto expected_save = make_savegame(normal.session->state(), save_w);
    require(normal.session->save_game(save_path, 1, false, error), "actual second-manager desk save succeeds");
    require(load_savegame(save_path).stream == expected_save.stream && normal.session->rand().seed() == save_w.seed() &&
        normal.session->rand().calls() == save_w.calls(), "production save has explicit expected save draws and full native bytes");
    Context reloaded(data, save_path);
    const auto reload_driver = reloaded.session->manager_join_checkpoint().driver;
    require(reload_driver.loaded_matchday_resume && !reload_driver.suppress_remaining_pre_match_offers &&
        reload_driver.manager_position == 1 && reloaded.session->state().memory.u8(::bmh::layout::ActiveManager) == 1,
        "new actual load regenerates provenance without serializing borrowed suppression");
    const auto reloaded_message = reloaded.serve(reloaded.advance());
    if (!reloaded_message.empty() || reloaded.revision != 2) {
        const auto after = reloaded.session->manager_join_checkpoint();
        throw std::runtime_error("reloaded last manager advances to a fresh loop: message=" + reloaded_message +
            " revision=" + std::to_string(reloaded.revision) + " stage=" + std::to_string(int(after.driver.stage)) +
            " position=" + std::to_string(after.driver.manager_position) + " calendar=" + std::to_string(after.driver.calendar_entry) +
            " date=" + std::to_string(after.state.date) + " flow=" + reloaded.flow.state_json());
    }
    const auto fresh = reloaded.session->manager_join_checkpoint();
    require(!fresh.driver.loaded_matchday_resume && !fresh.driver.suppress_remaining_pre_match_offers &&
        fresh.driver.manager_position == 0, "last departure consumes both runtime flags before fresh event");
    const auto fresh_expected = next_desk_oracle(fresh, true, true, reloaded.revision);
    const auto falsely_suppressed = next_desk_oracle(fresh, true, false, reloaded.revision);
    require(!equal(fresh_expected.final, falsely_suppressed.final), "fresh loop fixture detects accidental offer suppression");
    require(reloaded.serve(reloaded.advance(), supported_answers()).empty(), "actual fresh loop still enters later manager desk");
    same(snapshot(*reloaded.session), fresh_expected.final); restored(reloaded);
    require(!reloaded.session->manager_join_checkpoint().driver.loaded_matchday_resume &&
        !reloaded.session->manager_join_checkpoint().driver.suppress_remaining_pre_match_offers,
        "fresh GUI handoff retains fresh provenance");

    Context legacy(data, input);
    require(legacy.serve(legacy.advance(), {}, app::PendingOfferEntryPolicy::legacy_unpresented).empty(),
            "default legacy-compatible loaded path completes handoff");
    same(snapshot(*legacy.session), legacy_expected.final); restored(legacy);
    require(legacy.session->manager_join_checkpoint().driver.loaded_matchday_resume &&
        !legacy.session->manager_join_checkpoint().driver.suppress_remaining_pre_match_offers,
        "legacy retains provenance without selecting suppression");
    require(legacy.serve(legacy.advance(), {}, app::PendingOfferEntryPolicy::legacy_unpresented).empty(),
            "legacy last manager completes its original driver path");
    driver = legacy.session->manager_join_checkpoint().driver;
    require(!driver.loaded_matchday_resume && !driver.suppress_remaining_pre_match_offers,
            "legacy also consumes provenance at completed loop");
}

GameState generated_demands(Context& c) {
    GameState state = c.session->state();
    for (int manager = 0; manager < 2; ++manager) {
        bool found = false;
        for (uint32_t seed = 1; seed <= 16384 && !found; ++seed) {
            auto candidate = state; auto game = c.session->game(); WatcomRandom rand(seed); MinuteReport report;
            if (!pre_match_offer(candidate, manager, game, rand, report) || report.unsupported) continue;
            const uint32_t word = candidate.memory.u32(::bmh::layout::OfferSlots + uint32_t(manager) * 4);
            if (!(word & 0x80000000u) || (word & 31) != uint32_t(manager)) continue;
            // Owner1 is validated after the actual handoff; generation does not
            // fabricate its cursor or run a known-invalid continuation request.
            if (manager == 0) {
                auto checked = candidate; auto checked_g = c.session->game(); auto checked_w = c.session->rand();
                MinuteReport check_report;
                const auto result = process_natural_pending_offers_after_desk(checked,
                    {PendingOfferDeskRuntimeMode::normal_interactive, 2, manager}, checked_g, checked_w,
                    supported_answers(), check_report);
                if (!result.completed || check_report.unsupported || result.salary_accepted != 1) continue;
            }
            std::printf("controlled pre_match_offer-generated fixture: owner%d seed%u Wcalls%llu Gcalls%llu\n",
                manager, seed, static_cast<unsigned long long>(rand.calls()), static_cast<unsigned long long>(game.calls()));
            state = std::move(candidate); found = true;
        }
        require(found, "actual Source pre_match_offer produces bounded supported salary fixture for each manager");
    }
    return state;
}
void waiting(Context& c, const Snapshot& before, const TurnDriver& driver, uint64_t revision) {
    same(snapshot(*c.session), before); same_driver(c.session->manager_join_checkpoint().driver, driver); restored(c);
    require(c.revision == revision && c.flow.desktop()->state().revision == revision && !c.flow.desktop()->pending() &&
        c.flow.screen_name() == "desktop" && c.playback.queued_bytes() == 0,
        "Waiting rejects owned UI request at same revision without audio or simulation");
}
void generated_offer_handoffs(const fs::path& data, const fs::path& input) {
    Context c(data, input);
    // Controlled transformation of the genuine2P state through actual Source helper
    // calls; no offer word, salary, bid row, player or manager is fabricated.
    c.session->state() = generated_demands(c);
    const auto later_word = c.session->state().memory.u32(::bmh::layout::Weekly_PreMatchOffer_RuntimeField);
    const auto later_pool = c.session->state().squad_slots.at(0xb70 + 41);
    for (int position = 0; position < 2; ++position) {
        const auto before = snapshot(*c.session); const auto checkpoint = c.session->manager_join_checkpoint();
        const auto revision = c.revision;
        require(checkpoint.driver.loaded_matchday_resume && checkpoint.driver.manager_position == position &&
            checkpoint.driver.suppress_remaining_pre_match_offers == (position != 0),
            "correct durable flags enter each actual offered manager desk");
        require(c.serve(c.advance()).empty(), "empty real presenter leaves owned offer at Waiting instead of implicit NO");
        waiting(c, before, checkpoint.driver, revision);
        if (position == 1) {
            PendingOfferHooks stopped;
            stopped.answer = [](const GameState&, const PendingOfferQuestion&) -> PendingOfferAnswer {
                throw std::runtime_error("loaded manager presenter stopped");
            };
            require(c.serve(c.advance(), std::move(stopped)).empty(), "throwing second-manager answer returns controlled Waiting");
            waiting(c, before, checkpoint.driver, revision);
        }
        const auto request = c.advance(); const auto expected = pending_oracle(c, request); unsigned answers = 0;
        auto hooks = supported_answers();
        hooks.answer = [&](const GameState& staged, const PendingOfferQuestion& q) {
            require(answers < expected.questions.size() && save_stream(staged) == expected.questions[answers].native &&
                q.manager == position && q.raw_offer_slot == position && q.source_flags == 1 &&
                q.clicked_source_slot == expected.questions[answers].question.clicked_source_slot &&
                q.amount == expected.questions[answers].question.amount, "actual offered manager borrows exact staged Core question");
            ++answers; same(snapshot(*c.session), before); same_driver(c.session->manager_join_checkpoint().driver, checkpoint.driver);
            require(c.revision == revision && c.flow.desktop()->state().revision == revision,
                    "pending question has no intermediate live revision");
            std::string error;
            require(c.session->dispatch_kickoff(request.desk_action, c.revision, error).kind == TurnDeskKind::Failed &&
                error == "pending_offer_decision_active", "nested kickoff cannot consume loaded-loop provenance");
            require(!c.session->save_game(c.saves.path / "PARTIAL.FBM", position, false, error) &&
                error == "pending_offer_decision_active", "staged loaded-loop state cannot be saved");
            return PendingOfferAnswer::yes;
        };
        require(c.serve(request, std::move(hooks)).empty() && answers == 1 && c.revision == revision + 1,
                "fresh owned retry resolves manager once with single outer revision");
        same(snapshot(*c.session), expected.final); same_driver(c.session->manager_join_checkpoint().driver, expected.driver); restored(c);
        if (position == 0) require(c.session->state().memory.u32(::bmh::layout::OfferSlots) == 0xffffffffu &&
            c.session->state().memory.u32(::bmh::layout::Weekly_PreMatchOffer_RuntimeField) == later_word &&
            c.session->state().squad_slots.at(0xb70 + 41) == later_pool,
            "earlier offered manager clears only its own word and preserves later owner through loaded handoff");
        const auto settled = snapshot(*c.session); const auto settled_driver = c.session->manager_join_checkpoint().driver;
        require(c.serve(request).empty(), "consumed old UI request is ignored before decision dispatch");
        same(snapshot(*c.session), settled); same_driver(c.session->manager_join_checkpoint().driver, settled_driver);
    }
    require(!c.session->manager_join_checkpoint().driver.loaded_matchday_resume &&
        !c.session->manager_join_checkpoint().driver.suppress_remaining_pre_match_offers,
        "actual last offered manager consumes loaded-loop flags before fresh preparation");
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 3, "usage: loaded_manager_loop_app_tests ORIGINAL_DATA 2PNWK001.FBM");
        if (!*argv[1]) { std::puts("loaded manager loop app tests skipped: Original data not configured"); return 77; }
        original_empty_handoff(argv[1], argv[2]); generated_offer_handoffs(argv[1], argv[2]);
        std::puts("loaded manager loop app tests: genuine handoff, Source helper oracle, legacy/fresh lifetime, save/reload and generated pending rollback passed");
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "loaded_manager_loop_app_tests: %s\n", e.what()); return 1; }
}
