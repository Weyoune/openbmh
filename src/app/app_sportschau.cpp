// SPDX-License-Identifier: GPL-3.0-only
// App: the Sportschau (intro, replay scenes, fixtures, winner, awards, team info) and its waits.
// (moved out of main.cpp unchanged; the class is declared in app/app.hpp)
#include "app/app.hpp"

// Same acknowledgement wait as above, used by the Sportschau screens.
auto App::wait_sportschau_ack() -> void {
    // Source SDLFlushEvents2 flushes pending mouse input, then accepts fresh
    // left/right input. Keyboard Escape is not an Original ACK here.
    SDL_FlushEvents(SDL_MOUSEMOTION, SDL_MOUSEBUTTONUP);
    while (running) {
        SDL_Event event;
        while (quit_confirmation.poll(&event)) {
            if (event.type == SDL_QUIT) { running = false; return; }
            if (event.type == SDL_MOUSEBUTTONDOWN &&
                (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT)) return;
        }
        SDL_Delay(10);
    }
}

// Waits `ticks` units of 20 ms; only a window close request interrupts it.
auto App::source_tick_wait(unsigned ticks) -> void {
    for (unsigned i = 0; running && i < ticks; ++i) {
        SDL_Delay(20); // Source timer unit used by the traced intro path.
        SDL_PumpEvents();
        SDL_Event close;
        while (quit_confirmation.peep(&close, 1, SDL_GETEVENT, SDL_QUIT, SDL_QUIT) > 0)
            running = false; // platform close is the only intro-loop escape
    }
}

// Waits `count` original-display refresh periods using the high-resolution counter so
// the pacing is the same on 60/120/144 Hz monitors; only a close request interrupts it.
auto App::wait_source_retraces(unsigned count) -> void {
    // Keep the VGA cadence on 60/120/144-Hz host displays alike.
    constexpr auto refresh_hz = ui::kOriginalRefreshHz;
    const Uint64 frequency = SDL_GetPerformanceFrequency();
    const Uint64 ticks_per_wait = (frequency + Uint64(refresh_hz) - 1) / Uint64(refresh_hz);
    for (unsigned i = 0; running && i < count; ++i) {
        const Uint64 started = SDL_GetPerformanceCounter();
        while (running && SDL_GetPerformanceCounter() - started < ticks_per_wait) {
            SDL_PumpEvents();
            SDL_Event close;
            while (quit_confirmation.peep(&close, 1, SDL_GETEVENT, SDL_QUIT, SDL_QUIT) > 0)
                running = false; // platform close is the only animation-loop escape
            if (running) SDL_Delay(1);
        }
    }
}

// Builds the render options (panel background, gradient text) for Sportschau screens.
auto App::sportschau_context(const GameState& state) -> bmh::ui::SportschauReportContext {
    ui::SportschauReportContext context;
    context.solid_panel_background = state.memory.has(::bmh::layout::SolidPanelBackground) && state.memory.u8(::bmh::layout::SolidPanelBackground) != 0;
    context.gradient_text = state.memory.has(::bmh::layout::TextGradientDisabled) && state.memory.u8(::bmh::layout::TextGradientDisabled) == 0 &&
                            season_exe.u8(::bmh::layout::TextGradientMode) == 2;
    return context;
}

// --- presenters the engine calls through hooks (wired in run()): Sportschau, tax, promotion, the club
//     dialogs, the poach selection and the cup draw ---

// Plays the Sportschau intro animation frames with fanfare, then waits for a click.
auto App::present_sportschau_intro(const GameState&, const SportschauIntroView&) -> void {
    if (!running) return;
    // PaletteOverride installs KB132's embedded palette; SportschauIntro restores the
    // normal palette on return. Keep the override local to intro frames.
    const auto& intro_palette = sportschau_intro.palette_override();
    const Palette* intro_colors = intro_palette ? &*intro_palette : nullptr;
    for (uint32_t frame = 0; running && frame < ui::SportschauIntro::frame_count; ++frame) {
        if (frame != 0) wait_source_retraces(5);
        if (!running) return;
        present_season(sportschau_intro.render_frame(frame), "OpenBMH - sportschau_intro", intro_colors);
        if (frame == 0) {
            source_tick_wait(70);
            app::original_audio_command(playback, 2); // SportschauIntro, Original S/2002 fanfare.
            source_tick_wait(45);
        }
    }
    if (!running) return;
    source_tick_wait(100);
    app::original_audio_command(playback, 1);
    if (!running) return;
    present_season(sportschau_intro.render_frame(ui::SportschauIntro::frame_count - 1),
                   "OpenBMH - sportschau_intro_ack", intro_colors);
    wait_sportschau_ack();
}

// Shows the matchday intro screen for a fixed time; a left click skips it.
auto App::present_matchday_intro(const GameState&, const MatchdayIntroView& view) -> void {
    if (!running) return;
    present_season(matchday_intro.render(view), "OpenBMH - matchday_intro",
        matchday_intro.palette_override() ? &*matchday_intro.palette_override() : nullptr);
    // SDLGetTicks reads BIOS ticks at BDA46c (PIT1193182 /65536 Hz),
    // unlike the separate 20ms units of DecodeTaxAnimationScript2. Source waits36 ticks.
    // 36 ticks of the DOS BIOS timer (1193182 Hz / 65536) converted to milliseconds.
    constexpr Uint32 duration = (36ull * 65536 * 1000 + 1193181) / 1193182;
    const Uint32 start = SDL_GetTicks();
    SDL_FlushEvents(SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP);
    bool skipped = false;
    while (running && !skipped && SDL_GetTicks() - start < duration) {
        SDL_Event input;
        while (quit_confirmation.poll(&input)) {
            if (input.type == SDL_QUIT) running = false;
            if (input.type == SDL_MOUSEBUTTONDOWN && input.button.button == SDL_BUTTON_LEFT)
                skipped = true;
        }
        if (running && !skipped) SDL_Delay(1);
    }
    SDL_FlushEvents(SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP);
}

// Shows one step of the report replay (log entries growing minute by minute).
// Returns true when the replay should stop (quit) or be skipped by a left click.
auto App::present_sportschau_replay_step(const GameState& state,
        const SportschauFixtureView& fixture, const SportschauReportStep& step) -> bool {
    if (!running) return true;
    if (step.visible_log_entries == 0 && step.progress_minute == 0) {
        app::original_match_audio_command(playback, state, 3);
        sportschau_replay_screen.emplace(data, fixture, sportschau_context(state));
        sportschau_step_index = 0;
    }
    if (!sportschau_replay_screen) throw FormatError("sportschau: missing report owner");
    bool skip = false;
    SDL_Event input;
    // Only minute pauses accept input; other steps just look for a close request.
    if (step.minute_wait) {
        while (quit_confirmation.poll(&input)) {
            if (input.type == SDL_QUIT) running = false;
            if (input.type == SDL_MOUSEBUTTONDOWN && input.button.button == SDL_BUTTON_LEFT)
                skip = true;
        }
    } else {
        SDL_PumpEvents();
        while (quit_confirmation.peep(&input, 1, SDL_GETEVENT, SDL_QUIT, SDL_QUIT) > 0) running = false;
    }
    if (!running) return true;
    const std::string title = "OpenBMH - sportschau_replay_" + std::to_string(fixture.group) +
        "_" + std::to_string(fixture.fixture) + "_" + std::to_string(sportschau_step_index++);
    present_season(sportschau_replay_screen->render_report(step), title.c_str());
    const int raw = state.memory.has(::bmh::layout::Main_FormatError_RuntimeField4) ? state.memory.u8(::bmh::layout::Main_FormatError_RuntimeField4) : 0;
    if (step.minute_wait) source_tick_wait(unsigned(raw < 6 ? raw / 2 : raw));
    if (step.chance_wait) source_tick_wait(50);
    return skip;
}

// Plays one highlight (header, optional set piece, shot animation) of the replay, using
// the memory-driven delays, then restores the report screen.
auto App::present_sportschau_scene(const GameState& state,
        const SportschauFixtureView& fixture, const SportschauSceneView& scene) -> void {
    if (!running) return;
    ui::LiveMatchdayFrame frame;
    frame.gradient_text = sportschau_context(state).gradient_text;
    LiveChanceEvent event;
    event.group = 0; event.match = scene.shot.match; event.side = scene.shot.acting_side;
    event.minute = scene.shot.minute;
    const auto header = ui::live_chance_projection(event, scene.groups, state, season_exe);
    const auto backdrop = live_screen.render_sportschau_header(header,
        scene.home_goals_before, scene.away_goals_before, frame.gradient_text);
    const auto colors = live_screen.shot_palette(palette, scene.shot, scene.groups, state);
    const std::string id = std::to_string(fixture.group) + "_" + std::to_string(fixture.fixture) +
        "_" + std::to_string(scene.report_step.visible_log_entries);
    // Waits a configured number of ticks read from the game memory row (small values halved).
    const auto delay = [&](int row) {
        const uint32_t address = ::bmh::layout::Main_FormatError_ExecutableFieldUnsigned + uint32_t(row * 9);
        const int raw = state.memory.has(address) ? state.memory.u8(address) : 0;
        source_tick_wait(unsigned(raw < 6 ? raw / 2 : raw));
    };
    present_season(backdrop, ("OpenBMH - sportschau_scene_header_" + id).c_str(), &colors);
    delay(12); // AdvanceStep(12), header preview in ShotAnimation
    if (scene.set_piece) {
        const auto frames = live_screen.render_set_piece(frame, *scene.set_piece, &backdrop);
        for (size_t i = 0; running && i < frames.size(); ++i) {
            present_season(frames[i], ("OpenBMH - sportschau_set_piece_" + id + "_" +
                std::to_string(i)).c_str(), &colors);
            const unsigned retraces = i == 0 ? 40u :
                (state.memory.has(::bmh::layout::Main_FormatError_RuntimeField2) ? state.memory.u8(::bmh::layout::Main_FormatError_RuntimeField2) : 0u);
            wait_source_retraces(retraces); // FirstThen: first40, then F8
        }
    }
    const auto frames = live_screen.render_shot(frame, scene.shot, &backdrop);
    for (size_t i = 0; running && i < frames.size(); ++i) {
        present_season(frames[i], ("OpenBMH - sportschau_shot_" + id + "_" +
            std::to_string(i)).c_str(), &colors);
        const int speed = state.memory.has(::bmh::layout::Main_FormatError_RuntimeField) ? int(int8_t(state.memory.u8(::bmh::layout::Main_FormatError_RuntimeField))) : 0;
        wait_source_retraces(unsigned(std::max(0, speed)));
    }
    source_tick_wait(10); // LiveMatchdayFrame's final ten ticks
    if (!running) return;
    if (app::match_sound_enabled(state))
        app::original_audio_command(playback, scene.shot.miss ? 20 : 19); // SetPieceAnimation.
    if (state.memory.has(::bmh::layout::Main_FormatError_RuntimeField5) && state.memory.u8(::bmh::layout::Main_FormatError_RuntimeField5) == 0) {
        present_season(frames.back(), ("OpenBMH - sportschau_shot_ack_" + id).c_str(), &colors);
        wait_sportschau_ack();
    } else delay(15);
    if (!running) return;
    auto restored = scene.report_step; restored.chance_wait = false;
    present_season(sportschau_replay_screen->render_report(restored),
        ("OpenBMH - sportschau_scene_restored_" + id).c_str());
}

// Shows the full match report of a fixture and waits for acknowledgement.
auto App::present_sportschau_fixture(const GameState& state, const SportschauFixtureView& fixture) -> void {
    if (!running) return;
    ui::SportschauMatchReport screen(data, fixture, sportschau_context(state));
    present_season(screen.render_report(fixture.log_events.size()), "OpenBMH - sportschau_report");
    wait_sportschau_ack();
    sportschau_replay_screen.reset();
}

// Shows the competition winner screen.
auto App::present_competition_winner(const GameState& state, const CompetitionWinnerView& view) -> void {
    if (!running) return;
    const auto& colors = competition_winner.palette_override(view.title);
    present_season(competition_winner.render(state, view),
        ("OpenBMH - competition_winner_" + std::to_string(view.title) + "_" +
         std::to_string(view.team)).c_str(), colors ? &*colors : nullptr);
    wait_sportschau_ack(); // SDLFlushEvents2 waits for a mouse press; ESC is ignored.
}

// Shows the team-of-the-matchday (eleven) screen.
auto App::present_sportschau_awards(const GameState& state, const SportschauAwardsView& view) -> void {
    if (!running) return;
    present_season(sportschau_eleven.render(view, sportschau_context(state).gradient_text),
        ("OpenBMH - sportschau_eleven_" + std::to_string(view.group)).c_str());
    wait_sportschau_ack();
}

// Shows the team information screen.
auto App::present_sportschau_team_info(const GameState& state, const SportschauTeamInfoView& view) -> void {
    if (!running) return;
    present_season(sportschau_team_info.render(view, sportschau_context(state).gradient_text),
        ("OpenBMH - sportschau_teaminfo_" + std::to_string(view.group)).c_str());
    wait_sportschau_ack();
}
