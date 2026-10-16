// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <cstdio>
#include <stdexcept>

#include "app/career_session.hpp"
#include "game/save.hpp"

using namespace bmh;
namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

struct Coverage {
    int scenes = 0, scene_humans = 0, ordinary_humans = 0, direct_penalties = 0;
    int penalty_presentations = 0;
    bool penalty_goal = false;
};

Coverage advance(app::CareerSession& session, uint32_t seed, bool observe, uint64_t& revision) {
    Coverage coverage;
    bool active = false, settling = false, direct_penalty = false;
    int source_minute = 0;
    RealitySceneResult penalty;
    if (observe) {
        session.set_live_reality([&](const LiveRealityEvent& event, const auto&, const auto&) {
            if (event.stage == LiveRealityStage::begin) {
                check(!active, "natural scenes must not nest");
                active = true; settling = false; source_minute = event.minute;
                ++coverage.scenes;
            } else if (event.stage == LiveRealityStage::settling) {
                check(active, "deferred settlement must follow an active scene");
                settling = true;
            } else if (event.stage == LiveRealityStage::end) {
                check(active && settling && event.tick.finished, "scene ends after its deferred handler");
                if (event.tick.result.restart == 2) {
                    ++coverage.direct_penalties;
                    direct_penalty = true; penalty = event.tick.result;
                    if (seed == 48) check(event.minute == 7, "natural direct penalty remains in minute seven");
                }
                active = settling = false;
            }
            return false;
        });
        session.set_live_chance([&](const LiveChanceEvent& event, const auto&, const auto&) {
            if (!direct_penalty || event.minute != source_minute) return;
            check(event.kind == 2 && event.side == penalty.side,
                  "direct Reality penalty reaches the existing kind-two presentation");
            if (event.stage == LiveChanceStage::preview) ++coverage.penalty_presentations;
            else { coverage.penalty_goal = event.goal; direct_penalty = false; }
        });
    }
    session.set_live_event([&](const LiveMatchEvent& event, MatchEngine& engine, auto&, auto&) {
        const bool deferred = active && settling && event.minute == source_minute;
        if (deferred) {
            ++coverage.scene_humans;
            check(seed == 109 && event.minute == 30 && event.group_index == 0 && event.match_index == 0 &&
                  event.side == 1 && event.team == 9 && event.opponent == 12 && event.player == 15 &&
                  event.kind == 1 && event.manager_slot == 0,
                  "unaltered upload produces the exact natural Bayern red-card request");
            const auto& state = session.state();
            check((state.players.at(15).w368 & 0x07000000u) == 0x01000000u,
                  "Source has recorded the red card before opening the human handler");
            const auto range = squad_range(event.team);
            bool found = false;
            for (size_t slot = range.first; slot < range.first + range.count; ++slot) {
                if ((state.squad_slots.at(slot) & kEmptySlot) != event.player) continue;
                found = true;
                check(!(state.squad_slots.at(slot) & kSlotInLineup), "sent-off player has left the pitch");
            }
            check(found, "Source offender belongs to the human squad");
        } else ++coverage.ordinary_humans;
        std::string error;
        if (!session.begin_live_event_intervention(event, engine, revision, error)) throw std::runtime_error(error);
        if (!session.finish_live_event_intervention(event, engine, revision, error)) throw std::runtime_error(error);
    });
    ui::DeskRequest request;
    request.kind = ui::DeskRequestKind::advance_turn;
    request.manager_slot = session.state().memory.u8(::bmh::layout::ActiveManager);
    request.revision = revision;
    std::string error;
    const auto result = session.dispatch_kickoff(request, revision, error);
    check(result.kind == TurnDeskKind::NextDesk, error.empty() ? result.reason.c_str() : error.c_str());
    check(!active && !direct_penalty && session.last_turn_results().valid,
          "full natural matchday closes every scene and reaches the normal result snapshot");
    // Hooks captured local observation state; remove them before returning.
    session.set_live_event({}); session.set_live_reality({}); session.set_live_chance({});
    return coverage;
}
}

int main(int argc, char** argv) { try {
    check(argc == 3, "usage: reality_career_special_tests <Original data> <ordinary REALITY.FBM>");
    const auto original = read_file(argv[2]);
    for (uint32_t seed : {109u, 48u}) {
        app::CareerSession shown(argv[1], seed, argv[2]), quiet(argv[1], seed, argv[2]);
        uint64_t shown_revision = 1, quiet_revision = 1;
        const auto coverage = advance(shown, seed, true, shown_revision);
        advance(quiet, seed, false, quiet_revision);
        check(shown_revision == quiet_revision && save_stream(shown.state()) == save_stream(quiet.state()) &&
              shown.state().reality == quiet.state().reality &&
              shown.game().seed() == quiet.game().seed() && shown.game().calls() == quiet.game().calls() &&
              shown.rand().seed() == quiet.rand().seed() && shown.rand().calls() == quiet.rand().calls(),
              "owned scene/chance observation is neutral to whole-career Native, residue and both RNG streams");
        const auto& a = shown.last_turn_results();
        const auto& b = quiet.last_turn_results();
        check(a.date == b.date && a.matchday == b.matchday && a.groups.size() == b.groups.size(),
              "observation preserves the actual completed matchday identity");
        for (size_t group = 0; group < a.groups.size(); ++group) {
            check(a.groups[group].results == b.groups[group].results &&
                  a.groups[group].records == b.groups[group].records &&
                  a.groups[group].log == b.groups[group].log && a.groups[group].desc == b.groups[group].desc,
                  "observation preserves completed scores, ratings, event logs and competition counters");
        }
        if (seed == 109) check(coverage.scenes == 10 && coverage.scene_humans == 1 &&
                              !coverage.ordinary_humans && !coverage.direct_penalties,
                              "human handler is reached naturally from Reality, not a normal booking");
        else check(coverage.scenes == 13 && !coverage.scene_humans && !coverage.ordinary_humans &&
                   coverage.direct_penalties == 1 && coverage.penalty_presentations == 1,
                   "natural direct penalty reaches its owned preview and result exactly once");
        std::printf("CASE seed=%u scenes=%d deferred-human=%d ordinary-human=%d direct-penalties=%d previews=%d goal=%d\n",
                    seed, coverage.scenes, coverage.scene_humans, coverage.ordinary_humans,
                    coverage.direct_penalties, coverage.penalty_presentations, int(coverage.penalty_goal));
    }
    check(read_file(argv[2]) == original, "ordinary input must remain byte-identical");
    std::puts("PASS: natural full-career Reality human red-card and direct penalty; Native/RNG observer neutrality. No Original initial-stack or SDL claim.");
    return 0;
} catch (const std::exception& error) { std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1; } }
