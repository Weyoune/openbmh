// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/sportschau_presentation.hpp"

namespace bmh {

std::vector<SportschauReportStep> sportschau_report_steps(const SportschauFixtureView& fixture) {
    std::vector<SportschauReportStep> steps{{0, 0, false, false}};
    uint16_t cursor = 1;
    size_t visible = 0;
    int baseline = 66;
    while (visible < fixture.log_events.size()) {
        const auto& event = fixture.log_events[visible];
        while (cursor < event.source_value)
            steps.push_back({visible, uint8_t(cursor++), true, false});
        ++visible;
        if (event.consumes_following_pair && visible < fixture.log_events.size() &&
            fixture.log_events[visible].source_word_index == event.source_word_index + 2)
            ++visible;
        if (baseline < 179)
            steps.push_back({visible, uint8_t(cursor - 1), false, event.source_code < 2});
        if (event.source_code != 1) baseline += 7;
    }
    while (cursor <= fixture.source_played_minute)
        steps.push_back({visible, uint8_t(cursor++), true, false, true});
    return steps;
}

void present_sportschau_replay(GameState& state, const MatchGroup& group,
    const SportschauFixtureView& fixture, GameRandom& game, WatcomRandom& rand,
    const SportschauReplayHooks& hooks) {
    if (!fixture.source_selection_available || !fixture.source_selected ||
        !fixture.has_replay_progression_mode || fixture.replay_progression_mode != 0 ||
        !state.memory.has(::bmh::layout::Controller_FormatError_RuntimeField3) || state.memory.u8(::bmh::layout::Controller_FormatError_RuntimeField3) != 0 ||
        fixture.source_setting_index < 0 || fixture.source_setting_index > 19 ||
        !state.memory.has(::bmh::layout::Controller_FormatError_ExecutableField2 + uint32_t(fixture.source_setting_index) * 9) ||
        state.memory.u8(::bmh::layout::Controller_FormatError_ExecutableField2 + uint32_t(fixture.source_setting_index) * 9) != 0) return;
    bool progressive = true;
    for (const auto& step : sportschau_report_steps(fixture)) {
        if (!progressive) continue;
        const bool skip = hooks.step && hooks.step(state, fixture, step);
        if (step.minute_wait && !step.terminal_wait && skip) progressive = false;
        if (!step.chance_wait || !progressive || !fixture.has_replay_chance_mode || fixture.replay_chance_mode != 0)
            continue;
        const SportschauLogEvent* chance = nullptr;
        int home_before = 0, away_before = 0;
        for (size_t i = 0; i < step.visible_log_entries; ++i) {
            const auto& event = fixture.log_events[i];
            const bool paired = event.consumes_following_pair && i + 1 < step.visible_log_entries &&
                fixture.log_events[i + 1].source_word_index == event.source_word_index + 2;
            if (i + (paired ? 2 : 1) == step.visible_log_entries) chance = &event;
            else if (event.source_code == 0) {
                if (event.team_id == fixture.home_team) ++home_before; else ++away_before;
            }
            if (paired) ++i;
        }
        if (!chance || chance->source_code > 1 || chance->group != fixture.group ||
            chance->fixture != fixture.fixture || (chance->team_id != fixture.home_team &&
            chance->team_id != fixture.away_team)) throw FormatError("sportschau: invalid Source chance owner");
        const int side = chance->team_id == fixture.home_team ? 0 : 1;
        const int kind = chance->has_following_word && (chance->following_code_player_word >> 12) > 6
            ? (chance->following_code_player_word >> 12) - 6 : 0;
        if (kind > 3) throw FormatError("sportschau: unsupported Source set-piece code");
        SportschauSceneView scene;
        scene.report_step = step;
        scene.home_goals_before = home_before; scene.away_goals_before = away_before;
        scene.groups = {group};
        std::vector<uint8_t> referees;
        MatchEngine engine{state, scene.groups, game, rand, referees};
        engine.live_set_piece = [&](const LiveSetPieceEvent& event) { scene.set_piece = event; };
        engine.live_shot = [&](const LiveShotEvent& event) { scene.shot = event; };
        if (kind) engine.set_piece_animation(kind, 0, 1 - side, 0, fixture.fixture, chance->source_value);
        engine.shot_animation(chance->source_code == 1, kind == 2, side, 0, fixture.fixture, chance->source_value);
        // The presentation adapter owns the unsaved music/ambience transition;
        // the replay itself introduces no sound-related RNG or Native writes.
        if (hooks.scene) hooks.scene(state, fixture, scene);
    }
}

} // namespace bmh
