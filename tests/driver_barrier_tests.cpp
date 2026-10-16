// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <cassert>
#include <cstdint>
#include <vector>
#include <cstdio>
#include <stdexcept>

#include "game/driver.hpp"

using namespace bmh;

static MatchGroup league_group(uint32_t id, bool played) {
    MatchGroup g;
    g.desc.resize(0xD0);
    g.desc[0xcc] = uint8_t(id);
    g.end_minute = {0};
    g.fixtures = {1, 2};
    g.results = {0x21};
    g.ended = played ? 1u : 0u;
    return g;
}

// legacy routine uses one local_48 for the whole phase (Original instructions
// 36ae7/3705c/38ae8), including league groups already settled at minute 90.
// Verify the caller policy and the exact 53435 -> 21213 W/G consequence;
// do not use end_phase/stir_random as the expected-value oracle.
static void mixed_extra_time_policy() {
    auto require = [](bool value, const char* message) {
        if (!value) throw std::runtime_error(message);
    };
    for (int last : {105, 120}) {
        for (bool cup_first : {false, true}) {
            for (bool league_visible : {false, true}) {
                GameState state;
                state.memory.add_block(0, ::bmh::layout::FixtureBankMemoryImageSize);
                state.memory.set16(0x15 + 0x18, 0);
                state.memory.set8(::bmh::layout::FriendlyPlanCount, 1);
                auto league = league_group(0, false);
                league.desc[0xb8] = 1; // finished/void, NOT active in extra time
                league.end_minute[0] = 90;
                auto cup = league_group(0x15, true);
                cup.desc[1] = 8;
                cup.results.clear();
                cup.cup_results = {0x400}; // tied and going into extra time
                std::vector<MatchGroup> groups = cup_first
                    ? std::vector<MatchGroup>{cup, league} : std::vector<MatchGroup>{league, cup};
                const uint32_t league_index = cup_first ? 1 : 0;
                const uint32_t cup_index = 1 - league_index;
                const auto old_league = groups[league_index];
                std::vector<uint8_t> switches(0xb4, 1);
                switches[last == 120 ? 2 : 1] = league_visible ? 0 : 1;
                switches[5 * 9 + 2] = 0; // cup's saved result switch
                GameRandom game(17);
                WatcomRandom rand(29);
                MinuteReport report;
                PhaseEndCursor cursor;
                auto out = begin_phase_end(cursor, 900 + last, state, groups, last,
                                           switches, game, rand, report);
                uint32_t expected_w = 29;
                int32_t expected_g = 17;
                uint64_t expected_g_calls = 0, expected_w_calls = 0;
                std::vector<uint32_t> seen;
                while (out.status == PhaseEndStatus::Waiting && out.request) {
                    const auto request = *out.request;
                    seen.push_back(request.group_index);
                    require(request.phase_end_minute == last && request.matches.size() == 1,
                            "mixed phase retains Source minute and actual fixture");
                    if (request.kind == PhaseResultKind::League) {
                        require(request.group_index == league_index && request.matches[0].home_score == 2 &&
                                request.matches[0].away_score == 1,
                                "global phase gate includes settled league's actual score");
                        expected_w = expected_w * 0x41c64e6du + 0x3039u;
                        const uint32_t n = 100 + (((expected_w >> 16) & 0x7fffu) * 201u >> 15);
                        ++expected_w_calls;
                        expected_g_calls += n;
                        for (uint32_t i = 0; i < n; ++i)
                            expected_g = int32_t(uint32_t(expected_g) * 16u + 0xa7u) % 4000000;
                    } else {
                        require(request.group_index == cup_index && request.competition_id == 0,
                                "mixed phase retains actual cup identity");
                    }
                    const PhaseResultReply ack{request.phase_id, request.group_index, request.revision};
                    out = continue_phase_end(cursor, ack);
                    require(game.seed() == expected_g && game.calls() == expected_g_calls &&
                            rand.seed() == expected_w && rand.calls() == expected_w_calls,
                            "each ACK follows independent Original W/G arithmetic");
                    const auto duplicate = continue_phase_end(cursor, ack);
                    require(duplicate.status == out.status && game.seed() == expected_g &&
                            game.calls() == expected_g_calls && rand.seed() == expected_w &&
                            rand.calls() == expected_w_calls,
                            "duplicate mixed-group ACK cannot repeat Source draws");
                    require(bool(duplicate.request) == bool(out.request) &&
                            (!out.request || duplicate.request->group_index == out.request->group_index),
                            "duplicate ACK preserves the next result request");
                }
                const std::vector<uint32_t> expected = league_visible
                    ? std::vector<uint32_t>{0, 1} : std::vector<uint32_t>{cup_index};
                require(out.status == PhaseEndStatus::Complete && seen == expected,
                        "global gate follows Original group order and saved phase switches");
                require(groups[league_index].desc == old_league.desc &&
                        groups[league_index].end_minute == old_league.end_minute &&
                        groups[league_index].results == old_league.results,
                        "showing a settled league never settles its match again");
                require(state.memory.u8(::bmh::layout::CareerSession_FormatError_RuntimeField3) == 7 && groups[cup_index].cup_results[0] ==
                        (last == 120 ? 0x800 : 0x400) &&
                        groups[cup_index].end_minute[0] == (last == 120 ? 120 : 0),
                        "cup effects and extra-time settlement remain at their Source boundaries");
                // The reused cursor must discard the previous phase's global gate.
                for (auto& group : groups) group.ended = 0;
                out = begin_phase_end(cursor, 1200 + last, state, groups, 105,
                                      switches, game, rand, report);
                require(out.status == PhaseEndStatus::Complete &&
                        game.calls() == expected_g_calls && rand.calls() == expected_w_calls,
                        "a wholly unplayed later phase opens no result and draws nothing");
            }
        }
    }
}

int main() {
    mixed_extra_time_policy();
    GameState s;
    MinuteReport report;
    std::vector<uint8_t> buttons(0xB4, 0);
    GameRandom game(17), sync_game(17);
    WatcomRandom rand(29), sync_rand(29);
    auto groups = std::vector<MatchGroup>{league_group(0, true), league_group(1, true)};
    auto sync_groups = groups;

    PhaseEndCursor c;
    auto out = begin_phase_end(c, 77, s, groups, 90, buttons, game, rand, report);
    assert(out.status == PhaseEndStatus::Waiting && out.request);
    assert(out.request->group_index == 0 && out.request->phase_id == 77);
    assert(out.request->matches.size() == 1 && out.request->matches[0].home_team == 1);
    assert(out.request->matches[0].score_known && out.request->matches[0].home_score == 2);
    const auto req0 = *out.request;
    auto same_begin = begin_phase_end(c, 77, s, groups, 90, buttons, game, rand, report);
    assert(same_begin.status == PhaseEndStatus::Waiting && same_begin.request->revision == req0.revision);
    auto different_begin = begin_phase_end(c, 999, s, groups, 90, buttons, game, rand, report);
    assert(different_begin.status == PhaseEndStatus::Rejected && game.calls() == 0 && rand.calls() == 0);
    auto polled = continue_phase_end(c);
    assert(polled.status == PhaseEndStatus::Waiting && polled.request->revision == req0.revision);
    assert(game.calls() == 0 && rand.calls() == 0);
    assert(groups[0].end_minute[0] == 0);
    auto wrong = continue_phase_end(c, PhaseResultReply{77, 1, req0.revision});
    assert(wrong.status == PhaseEndStatus::Rejected && game.calls() == 0 && rand.calls() == 0);
    out = continue_phase_end(c, PhaseResultReply{req0.phase_id, req0.group_index, req0.revision});
    assert(out.status == PhaseEndStatus::Waiting && out.request->group_index == 1);
    assert(groups[0].end_minute[0] == 90);
    auto duplicate = continue_phase_end(c, PhaseResultReply{req0.phase_id, req0.group_index, req0.revision});
    assert(duplicate.status == PhaseEndStatus::Waiting && duplicate.request->group_index == 1);
    const uint64_t game_calls = game.calls(), rand_calls = rand.calls();
    auto stale = continue_phase_end(c, PhaseResultReply{77, 0, req0.revision});
    assert(stale.status == PhaseEndStatus::Waiting && game.calls() == game_calls && rand.calls() == rand_calls);
    const auto req1 = *out.request;
    out = continue_phase_end(c, PhaseResultReply{req1.phase_id, req1.group_index, req1.revision});
    assert(out.status == PhaseEndStatus::Complete);
    auto duplicate_final = continue_phase_end(c, PhaseResultReply{req1.phase_id, req1.group_index, req1.revision});
    assert(duplicate_final.status == PhaseEndStatus::Complete);
    auto completed_begin = begin_phase_end(c, 77, s, groups, 90, buttons, game, rand, report);
    assert(completed_begin.status == PhaseEndStatus::Complete);

    end_phase(s, sync_groups, 90, buttons, sync_game, sync_rand, report);
    assert(game.calls() == sync_game.calls() && game.seed() == sync_game.seed());
    assert(rand.calls() == sync_rand.calls() && rand.seed() == sync_rand.seed());
    assert(groups[0].desc == sync_groups[0].desc && groups[1].desc == sync_groups[1].desc);

    auto hidden_groups = std::vector<MatchGroup>{league_group(0, true)};
    buttons[2] = 1;  // second-half result screen for league 0 is switched off
    GameRandom hidden_game(7);
    WatcomRandom hidden_rand(8);
    PhaseEndCursor hidden;
    auto hidden_out = begin_phase_end(hidden, 78, s, hidden_groups, 90, buttons,
                                      hidden_game, hidden_rand, report);
    assert(hidden_out.status == PhaseEndStatus::Complete);
    assert(hidden_game.calls() == 0 && hidden_rand.calls() == 0);

    GameState cup_state;
    cup_state.memory.add_block(0, ::bmh::layout::FixtureBankMemoryImageSize);
    cup_state.memory.set16(0x15 + 0x18, 0);
    cup_state.memory.set8(::bmh::layout::FriendlyPlanCount, 1);  // avoid the unrelated competition-closure path
    auto cup = league_group(0x15, true);
    cup.end_minute = {0};
    std::vector<MatchGroup> cup_groups{cup};
    GameRandom cup_game(5);
    WatcomRandom cup_rand(6);
    PhaseEndCursor cup_cursor;
    auto cup_out = begin_phase_end(cup_cursor, 80, cup_state, cup_groups, 90, {},
                                   cup_game, cup_rand, report);
    assert(cup_out.status == PhaseEndStatus::Waiting && cup_out.request);
    assert(cup_out.request->kind == PhaseResultKind::Cup && cup_out.request->competition_id == 0);
    assert(cup_state.memory.u8(::bmh::layout::CareerSession_FormatError_RuntimeField3) == 7 && cup_groups[0].end_minute[0] == 0);
    if (cup_state.memory.u8(::bmh::layout::CareerSession_FormatError_RuntimeField3) != 7 || cup_groups[0].end_minute[0] != 0)
        throw std::runtime_error("Source cup entry effects precede presentation and settlement");
    cup_state.memory.set8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField, 0x82); // synthetic edited byte: ACK must not reinitialize it
    cup_out = continue_phase_end(cup_cursor, PhaseResultReply{80, 0, cup_out.request->revision});
    assert(cup_out.status == PhaseEndStatus::Complete);
    assert(cup_state.memory.u8(::bmh::layout::CareerSession_FormatError_RuntimeField3) == 7 && cup_groups[0].end_minute[0] == 90);
    assert(cup_state.memory.u8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField) == 0x82);
    if (cup_state.memory.u8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField) != 0x82)
        throw std::runtime_error("cup ACK must preserve the viewer's Source preference edit");

    auto tied_cup = league_group(0x15, true);
    tied_cup.desc[1] = 8;
    tied_cup.end_minute = {0};
    tied_cup.results.clear();
    tied_cup.cup_results = {0};
    std::vector<MatchGroup> extra_groups{tied_cup};
    PhaseEndCursor extra_time;
    auto extra_out = begin_phase_end(extra_time, 81, cup_state, extra_groups, 90, {},
                                     cup_game, cup_rand, report);
    assert(extra_out.status == PhaseEndStatus::Waiting && extra_groups[0].cup_results[0] == 0);
    extra_out = continue_phase_end(extra_time,
        PhaseResultReply{81, 0, extra_out.request->revision});
    assert(extra_out.status == PhaseEndStatus::Complete && extra_groups[0].cup_results[0] == 0x400);
    PhaseEndCursor final_time;
    extra_out = begin_phase_end(final_time, 82, cup_state, extra_groups, 120, {},
                                cup_game, cup_rand, report);
    assert(extra_out.status == PhaseEndStatus::Waiting && extra_groups[0].cup_results[0] == 0x400);
    assert(extra_groups[0].end_minute[0] == 0);
    extra_out = continue_phase_end(final_time,
        PhaseResultReply{82, 0, extra_out.request->revision});
    assert(extra_out.status == PhaseEndStatus::Complete && extra_groups[0].cup_results[0] == 0x800);
    assert(extra_groups[0].end_minute[0] == 120);

    auto unplayed_groups = std::vector<MatchGroup>{league_group(0, false)};
    buttons[2] = 0;
    PhaseEndCursor unplayed;
    auto unplayed_out = begin_phase_end(unplayed, 79, s, unplayed_groups, 90, buttons,
                                        hidden_game, hidden_rand, report);
    assert(unplayed_out.status == PhaseEndStatus::Complete);
    assert(hidden_game.calls() == 0 && hidden_rand.calls() == 0);

    // The synchronous presenter observes each existing barrier before its ACK.
    // This must not bypass the saved switch or move Source effects into the UI.
    auto require = [](bool value, const char* message) {
        if (!value) throw std::runtime_error(message);
    };
    auto presented_groups = std::vector<MatchGroup>{league_group(0, true), league_group(1, true)};
    auto quiet_groups = presented_groups;
    GameRandom pg(17), qg(17); WatcomRandom pw(29), qw(29);
    std::vector<PhaseResultRequest> shown;
    std::vector<uint8_t> enabled(0xb4, 0);
    auto presenter = [&](const PhaseResultRequest& request, const std::vector<MatchGroup>& view,
                         const GameState& state) {
        require(&state == &s && request.group_index == shown.size() && request.phase_end_minute == 90,
                "presenter receives the real state and Source group order");
        require(view[request.group_index].end_minute[0] == 0 && request.matches[0].home_score == 2 &&
                request.matches[0].away_score == 1, "owned scores precede group settlement");
        shown.push_back(request);
    };
    end_phase(s, presented_groups, 90, enabled, pg, pw, report, presenter);
    end_phase(s, quiet_groups, 90, enabled, qg, qw, report);
    require(shown.size() == 2 && pg.seed() == qg.seed() && pg.calls() == qg.calls() &&
            pw.seed() == qw.seed() && pw.calls() == qw.calls() &&
            presented_groups[0].desc == quiet_groups[0].desc && presented_groups[1].desc == quiet_groups[1].desc,
            "presentation preserves single Source ACK effects and both RNG streams");
    shown.clear(); enabled[2] = 1;
    auto disabled = std::vector<MatchGroup>{league_group(0, true)};
    GameRandom dg(17); WatcomRandom dw(29);
    end_phase(s, disabled, 90, enabled, dg, dw, report, presenter);
    require(shown.empty() && dg.calls() == 0 && dw.calls() == 0,
            "disabled Source result switch suppresses presenter and ACK RNG");
    enabled[2] = 0;
    auto aborted = std::vector<MatchGroup>{league_group(0, true)};
    GameRandom ag(17); WatcomRandom aw(29);
    bool interrupted = false;
    try {
        end_phase(s, aborted, 90, enabled, ag, aw, report,
            [](const auto&, const auto&, const auto&) { throw std::runtime_error("presenter stopped"); });
    } catch (const std::runtime_error&) { interrupted = true; }
    require(interrupted && ag.calls() == 0 && aw.calls() == 0 && aborted[0].end_minute[0] == 0 &&
            aborted[0].void_mask() == 0, "interrupted presenter cannot silently ACK or settle its group");
    std::puts("PASS: global Original phase gate, mixed 105/120 groups in both orders, saved switches, independent W/G, duplicate ACKs, settled league preservation and empty next phase; synchronous presenter invariants");
}
