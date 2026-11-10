// SPDX-License-Identifier: GPL-3.0-only
// App: the live match (minute display, the observer with its interventions, the event animations, reality mode, phase results).
// (moved out of main.cpp unchanged; the class is declared in app/app.hpp)
#include "app/app.hpp"

// Live-match intervention: opens the lineup/pitch screen for the clicked team and runs it
// until closed. Returns true if the intervention was completed successfully.
auto App::present_live_lineup(int minute, MatchEngine& engine, std::vector<MatchGroup>& groups,
        GameState& state, uint64_t& live_revision, int selected_team, int selected_opponent,
        const app::LiveLineupDispatcher& dispatch, const app::LiveTacticsDispatcher& dispatch_tactic,
        const app::LivePitchDispatcher& dispatch_pitch,
        const app::LiveInterventionScopeDispatcher& dispatch_scope,
        bool live_skipped, bool event_origin) -> bool {
    // Shows the engine's notice about a refused substitution as a dialog over the pitch.
    const auto show_live_substitution_notice = [&](const app::LiveSubstitutionResult& result,
            const Image& backdrop, const char* parent_title) {
        if (result.status != app::LiveSubstitutionStatus::source_notice) return;
        if (result.manager_slot < 0 || result.source_text_address == 0)
            throw FormatError("live substitution notice has no Source owner or text");
        const PendingOfferNotice notice{result.manager_slot, -1, result.source_text_address,
            message_text(state, result.source_text_address)};
        const auto projection = ui::pending_offer_notice_dialog(state, notice);
        if (present_pending_dialog(state, projection, backdrop,
                "OpenBMH - live_substitution_notice") == PendingOfferAnswer::unsupported)
            throw FormatError("live substitution notice presenter stopped");
        SDL_FlushEvents(SDL_MOUSEMOTION, SDL_MOUSEBUTTONUP);
        SDL_SetWindowTitle(window, parent_title);
    };
    bool completed = false;
    const int selected_manager = selected_team >= 0 ? state.manager_of(selected_team) : -1;
    // Only for a human-managed team, and only when started from a match event or while the
    // match is not being skipped past its first two minutes.
    if ((event_origin || (!live_skipped && minute > 2)) && selected_team >= 0 && selected_manager >= 0) {
        bool scope_started = false, scope_finished = false;
        std::string scope_error;
        try {
            if (!dispatch_scope(app::LiveInterventionBoundary::begin, 2,
                    selected_manager, selected_team, scope_error))
                throw FormatError(scope_error.empty() ? "live intervention could not begin" : scope_error);
            scope_started = true;
            // FindManagerForClub selects the manager owned by
            // the clicked side. The live scope owns that
            // manager until Source menu completion or abort.
            auto lineup_state = state;
            if (lineup_state.memory.has(::bmh::layout::ActiveManager))
                lineup_state.memory.set8(::bmh::layout::ActiveManager, uint8_t(selected_manager));
            auto live_lineup = ui::lineup_from_game(lineup_state, season_exe, live_revision);
            live_lineup.in_match = true;
            live_lineup.entry_prepared = true;
            // The live menu owns the same native display word and both pages
            // as the desk. A separate team-sheet parent can hide a saved
            // pitch page and strand its navigation request after return.
            ui::PitchScreen screen(data, std::move(live_lineup),
                ui::pitch_extras_from_game(state, season_exe, live_revision, selected_opponent));
            // Window title depends on which page (bit 8 of the display word) is showing.
            const auto live_title = [&] {
                return (screen.extras().native_display_word & 8)
                    ? "OpenBMH - live_pitch" : "OpenBMH - live_lineup";
            };
            SDL_SetWindowTitle(window, live_title());
            bool closed = false;
            // Executes a request from the pitch screen: player info card or a substitution/order
            // change through the engine, then refreshes or reports the result on the screen.
            const auto apply_live_pitch = [&](const ui::PitchRequest& action) {
                if (action.kind == ui::PitchRequestKind::none) return;
                pitch_pointer.show({}, renderer);
                if (action.kind == ui::PitchRequestKind::open_player_info) {
                    try {
                        if (action.row < 0 || size_t(action.row) >= screen.state().players.size() ||
                            screen.state().players.at(size_t(action.row)).player_id != action.player_id)
                            throw FormatError("live player information request is stale");
                        auto services = session->live_club_modal_services(live_revision);
                        const ClubPlayerDetailSelection selection{selected_team, action.player_id,
                            action.row, action.row, -1};
                        if (!present_player_detail)
                            throw FormatError("live player information presenter stopped");
                        const auto return_parent = present_player_detail(services, selection, screen.render());
                        if (!return_parent) throw FormatError("live player information presenter stopped");
                        if (*return_parent) closed = true;
                    } catch (const std::exception& e) {
                        std::fprintf(stderr, "live player information: %s\n", e.what());
                    }
                    screen.resolve_local_navigation();
                } else {
                    const auto result = dispatch_pitch(action);
                    if (result.status == app::LiveSubstitutionStatus::applied) {
                        auto updated = ui::lineup_from_game(state, season_exe, live_revision);
                        updated.in_match = true; updated.entry_prepared = true;
                        screen.acknowledge(std::move(updated),
                            ui::pitch_extras_from_game(state, season_exe, live_revision, selected_opponent));
                    } else if (result.status == app::LiveSubstitutionStatus::source_notice) {
                        show_live_substitution_notice(result, screen.render(), live_title());
                        auto updated = ui::lineup_from_game(state, season_exe, live_revision);
                        updated.in_match = true; updated.entry_prepared = true;
                        screen = ui::PitchScreen(data, std::move(updated),
                            ui::pitch_extras_from_game(state, season_exe, live_revision, selected_opponent));
                    } else {
                        screen.reject_action(result.error.empty()
                            ? "Der Spielerwechsel ist nicht freigegeben." : result.error);
                    }
                }
                SDL_SetWindowTitle(window, live_title());
            };
            // left_held/right_held pair a release with its press. left_release_consumed is set when the
            // screen already acted on the press, so the release must not act a second time.
            bool left_held = false, left_release_consumed = false, right_held = false;
            // Modal sub-loop: draws the pitch every pass and polls events itself.
            while (running && !closed) {
                pitch_pointer.show(screen.pointer_cursor(palette), renderer);
                const Image image = screen.render();
                canvas.show(image, palette);
                SDL_Event modal;
                bool redrawn = false;
                while (quit_confirmation.poll(&modal)) {
                    if (modal.type == SDL_QUIT) { running = false; closed = true; break; }
                    if (modal.type == SDL_KEYDOWN && modal.key.keysym.sym == SDLK_ESCAPE) {
                        screen.key("ESC"); closed = true; break;
                    }
                    if (modal.type == SDL_MOUSEBUTTONDOWN && modal.button.button == SDL_BUTTON_LEFT) {
                        left_held = true;
                        const auto action = screen.pointer_down(modal.button.x, modal.button.y);
                        // Release barrier: remember whether this press already triggered its action.
                        left_release_consumed = screen.take_source_down_release_barrier();
                        // Native page/order controls act on DOWN. ACK before
                        // polling another event; their UP cannot toggle twice.
                        apply_live_pitch(action);
                        redrawn = true;
                    } else if (modal.type == SDL_MOUSEBUTTONUP && modal.button.button == SDL_BUTTON_LEFT && left_held) {
                        left_held = false;
                        if (!left_release_consumed)
                            apply_live_pitch(screen.click(modal.button.x, modal.button.y));
                        left_release_consumed = false;
                        redrawn = true;
                    } else if (modal.type == SDL_MOUSEBUTTONDOWN && modal.button.button == SDL_BUTTON_RIGHT) {
                        right_held = true;
                        (void)screen.pointer_down(modal.button.x, modal.button.y, true);
                        (void)screen.take_source_down_release_barrier();
                        redrawn = true;
                    } else if (modal.type == SDL_MOUSEBUTTONUP && modal.button.button == SDL_BUTTON_RIGHT && right_held) {
                        right_held = false;
                        // Mouse movement between DOWN and UP must not leave
                        // a closed modal or an accepted request suspended.
                        if (screen.close_requested()) closed = true;
                        redrawn = true;
                    } else if (modal.type == SDL_MOUSEMOTION) {
                        redrawn |= screen.move(modal.motion.x, modal.motion.y);
                    }
                    if (closed) break;
                }
                if (!redrawn && !closed) SDL_Delay(8);
            }
            // Leaving: quitting aborts the live intervention, otherwise it is finished normally.
            if (!running) {
                dispatch_scope(app::LiveInterventionBoundary::abort, 2, selected_manager,
                    selected_team, scope_error);
                scope_finished = true;
            } else if (!dispatch_scope(app::LiveInterventionBoundary::finish, 2,
                    selected_manager, selected_team, scope_error)) {
                std::fprintf(stderr, "live intervention finish: %s\n", scope_error.c_str());
                dispatch_scope(app::LiveInterventionBoundary::abort, 2,
                    selected_manager, selected_team, scope_error);
                scope_finished = true;
            } else {
                scope_finished = true;
                completed = true;
            }
            pitch_pointer.show({}, renderer);
            SDL_SetWindowTitle(window, "OpenBMH - live_matchday");
        } catch (const std::exception& e) {
            pitch_pointer.show({}, renderer);
            if (scope_started && !scope_finished)
                dispatch_scope(app::LiveInterventionBoundary::abort, 2,
                    selected_manager, selected_team, scope_error);
            std::fprintf(stderr, "live lineup: %s\n", e.what());
            if (event_origin) throw;
        }
    }
    return completed;
}

// Presents the overview shown while the matchday is running (league table, other games,
// statistics) in its own loop, driven by the engine's overview dispatcher.
auto App::present_running_overview(std::optional<app::CareerSession::LiveOverviewProjection> projection,
        const GameState& state, uint64_t& live_revision,
        const RunningOverviewDispatcher& dispatch_overview,
        const PhaseResultRequest* phase_result) -> bool {
    const int manager = projection->owner.manager_slot;
    std::unique_ptr<ui::LeagueTableScreen> table_screen;
    // Only one of table/pairings/strengths/cup screen exists at a time, matching projection->kind.
    std::unique_ptr<ui::LeaguePairingsScreen> pairings_screen;
    std::unique_ptr<ui::StrengthsScreen> strengths_screen;
    std::unique_ptr<ui::OtherGamesScreen> cup_screen;
    const auto overview_clock_start = std::chrono::steady_clock::now();
    // Seconds since the overview opened, converted to source units for the pairings screen.
    const auto overview_source_seconds = [&] {
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - overview_clock_start).count();
        return ui::league_pairings_source_seconds(uint64_t(milliseconds));
    };
    // Creates the screen that matches the current projection kind (throws if its data is missing).
    const auto rebuild_screen = [&] {
        table_screen.reset(); pairings_screen.reset(); strengths_screen.reset(); cup_screen.reset();
        if (projection->kind == app::CareerSession::LiveOverviewKind::league_table &&
            projection->table) {
            auto tables = ui::league_tables_from_game(state, manager);
            const int selected = projection->table->league_id;
            if (selected < 0 || selected >= int(tables.size()))
                throw FormatError("live overview: Source table league is invalid");
            tables[size_t(selected)] = *projection->table;
            table_screen = std::make_unique<ui::LeagueTableScreen>(data,
                std::move(tables), selected,
                ui::LeagueTableScreen::FamilyMode{projection->family_identity, 0});
        } else if (projection->kind == app::CareerSession::LiveOverviewKind::league_pairings &&
                   projection->pairings) {
            pairings_screen = std::make_unique<ui::LeaguePairingsScreen>(data,
                *projection->pairings,
                ui::LeaguePairingsScreen::FamilyMode{projection->family_identity});
        } else if (projection->kind == app::CareerSession::LiveOverviewKind::league_strengths &&
                   projection->strengths) {
            strengths_screen = std::make_unique<ui::StrengthsScreen>(data, *projection->strengths);
        } else if (projection->kind == app::CareerSession::LiveOverviewKind::cup_other_games &&
                   projection->cup) {
            cup_screen = std::make_unique<ui::OtherGamesScreen>(data, *projection->cup);
        } else {
            throw FormatError("live overview: Source presenter projection is missing");
        }
    };
    // Window title of the overview; inside a phase result it also carries the phase end minute
    // and group index.
    const auto overview_title = [&]() -> std::string {
        if (phase_result) {
            const std::string prefix = "OpenBMH - phase_";
            const std::string suffix = "_" + std::to_string(phase_result->phase_end_minute) +
                "_" + std::to_string(phase_result->group_index);
            switch (projection->kind) {
            case app::CareerSession::LiveOverviewKind::league_table: return prefix+"table"+suffix;
            case app::CareerSession::LiveOverviewKind::league_pairings: return prefix+"pairings"+suffix;
            case app::CareerSession::LiveOverviewKind::league_strengths: return prefix+"strengths"+suffix;
            case app::CareerSession::LiveOverviewKind::cup_other_games: return prefix+"cup"+suffix;
            }
        }
        switch (projection->kind) {
        case app::CareerSession::LiveOverviewKind::league_table:
            return "OpenBMH - live_league_table";
        case app::CareerSession::LiveOverviewKind::league_pairings:
            return "OpenBMH - live_pairings";
        case app::CareerSession::LiveOverviewKind::league_strengths:
            return "OpenBMH - live_strengths";
        case app::CareerSession::LiveOverviewKind::cup_other_games:
            return "OpenBMH - live_other_games";
        }
        return "OpenBMH - live_overview";
    };
    // Draws the active overview screen and presents it.
    const auto render_overview = [&] {
        const Image image = table_screen ? table_screen->render()
            : pairings_screen ? pairings_screen->render()
            : strengths_screen ? strengths_screen->render() : cup_screen->render();
        SDL_SetWindowTitle(window, overview_title().c_str());
        canvas.show(image, palette);
        if (pairings_screen)
            pairings_screen->mark_presented_source_seconds(overview_source_seconds());
    };
    rebuild_screen();
    render_overview();
    bool closed = false;
    // The mouse press currently held; once the overview is closed its release is still
    // swallowed here so it does not reach whatever is shown next.
    std::optional<SDL_MouseButtonEvent> down;
    // Modal table range dialog (same key queueing as the poach table version): returns its
    // accept/cancel outcome.
    const auto run_table_range = [&](const Image& backdrop,
            ui::TableRangeDialogProfile profile) -> ui::TableRangeDialogOutcome {
        ui::LeagueTableRangeDialog dialog(data, backdrop, std::move(profile));
        std::deque<std::string> keys;
        Uint32 clock_anchor = SDL_GetTicks();
        std::optional<ui::TableRangeDialogOutcome> outcome;
        // Presents the range dialog and lets it report an outcome and restart its tick clock.
        const auto show_dialog = [&] {
            const Image image = dialog.render();
            SDL_SetWindowTitle(window, "OpenBMH - live_table_range");
            canvas.show(image, palette);
            outcome = dialog.after_present();
            clock_anchor = SDL_GetTicks();
        };
        show_dialog();
        while (running && !outcome) {
            SDL_Event input;
            while (quit_confirmation.poll(&input)) {
                if (input.type == SDL_QUIT) { running = false; break; }
                bool changed = false;
                if (input.type == SDL_TEXTINPUT) {
                    for (char c : std::string_view(input.text.text))
                        if (c >= '0' && c <= '9') keys.emplace_back(1, c);
                } else if (input.type == SDL_KEYDOWN) {
                    if (input.key.keysym.sym == SDLK_RETURN) keys.emplace_back("ENTER");
                    else if (input.key.keysym.sym == SDLK_BACKSPACE) keys.emplace_back("BACKSPACE");
                    else if (input.key.keysym.sym == SDLK_SPACE) keys.emplace_back("SPACE");
                } else if (input.type == SDL_MOUSEMOTION)
                    changed = dialog.move(input.motion.x, input.motion.y);
                else if (input.type == SDL_MOUSEBUTTONDOWN &&
                         (input.button.button == SDL_BUTTON_LEFT ||
                          input.button.button == SDL_BUTTON_RIGHT))
                    changed = dialog.pointer_down(input.button.x, input.button.y,
                        input.button.button == SDL_BUTTON_RIGHT);
                else if (input.type == SDL_MOUSEBUTTONUP &&
                         (input.button.button == SDL_BUTTON_LEFT ||
                          input.button.button == SDL_BUTTON_RIGHT))
                    changed = dialog.pointer_up(input.button.x, input.button.y,
                        input.button.button == SDL_BUTTON_RIGHT);
                if (changed && input.type != SDL_MOUSEBUTTONUP) show_dialog();
                if (outcome) break;
            }
            if (!running || outcome) break;
            while (!keys.empty() && dialog.source_input_ready()) {
                const std::string key = std::move(keys.front());
                keys.pop_front();
                if (dialog.key(key)) show_dialog();
                if (outcome) break;
            }
            const Uint32 now = SDL_GetTicks();
            const Uint32 units = Uint32(now - clock_anchor) / 20;
            if (units) {
                clock_anchor += units * 20;
                if (dialog.tick_source_units(units)) show_dialog();
            }
            SDL_Delay(10);
        }
        if (!outcome) throw FormatError("live table range presenter stopped");
        return *outcome;
    };
    // Sends an action of the overview to the engine and applies the answer: close, switch to
    // another overview kind, or run the range dialog and send its result back (recursive).
    std::function<void(const app::CareerSession::LiveOverviewAction&)> dispatch_action;
    dispatch_action = [&](const app::CareerSession::LiveOverviewAction& action) {
        const Image previous_table = table_screen ? table_screen->render() : Image{};
        std::string dispatch_error;
        auto result = dispatch_overview(action, dispatch_error, [&](app::ClubModalServices& services, int selected_team,
                                const Image&) {
                return present_club_information(services, selected_team) && running;
            });
        if (!result) {
            if (!dispatch_error.empty())
                std::fprintf(stderr, "live overview: %s\n", dispatch_error.c_str());
            rebuild_screen();
            render_overview();
            return;
        }
        if (result->closed) { closed = true; return; }
        projection->kind = result->kind;
        if (result->family_identity.manager >= 0)
            projection->family_identity = result->family_identity;
        projection->table = std::move(result->table);
        projection->pairings = std::move(result->pairings);
        projection->strengths = std::move(result->strengths);
        projection->cup = std::move(result->cup);
        rebuild_screen();
        const bool table_dialog_pending =
            projection->kind == app::CareerSession::LiveOverviewKind::league_table &&
            result->table_dialog && result->table_phase != app::CareerSession::TableDispatchPhase::ready;
        if (!table_dialog_pending) render_overview();
        if (table_dialog_pending) {
            auto profile = *result->table_dialog;
            auto range = run_table_range(previous_table, std::move(profile));
            ui::LeagueFamilyRequest reply;
            reply.identity = projection->family_identity;
            reply.kind = ui::LeagueFamilyRequestKind::table_range_result;
            reply.from_child = 1;
            reply.control_index = 9;
            reply.table_dialog_reply = ui::TableDialogReply{
                result->table_phase == app::CareerSession::TableDispatchPhase::range_start
                    ? ui::TableDialogStage::start : ui::TableDialogStage::end,
                range.result == ui::TableRangeDialogResult::accept
                    ? ui::TableDialogOutcome::accept : ui::TableDialogOutcome::cancel,
                range.result == ui::TableRangeDialogResult::accept ? int(range.value) : 0,
                range.x, range.y};
            dispatch_action(app::CareerSession::LiveOverviewAction{reply});
        } else if (projection->kind == app::CareerSession::LiveOverviewKind::league_table &&
                   result->table_phase == app::CareerSession::TableDispatchPhase::forecast_pending_present) {
            ui::LeagueFamilyRequest ack;
            ack.identity = projection->family_identity;
            ack.kind = ui::LeagueFamilyRequestKind::table_forecast_presented;
            ack.from_child = 1;
            dispatch_action(app::CareerSession::LiveOverviewAction{ack});
        }
    };
    // Drop stale key releases left over from the match.
    SDL_FlushEvent(SDL_KEYUP);
    // Modal sub-loop. After closing it keeps polling until the held button's release has
    // arrived, so that release is not seen by the caller.
    while (running && (!closed || down)) {
        SDL_Event input;
        bool redrawn = false;
        while (quit_confirmation.poll(&input)) {
            if (input.type == SDL_QUIT) { running = false; break; }
            if (closed) {
                if (input.type == SDL_MOUSEBUTTONUP && down &&
                    input.button.button == down->button) { down.reset(); break; }
                continue;
            }
            if (input.type == SDL_MOUSEMOTION) {
                const bool changed = table_screen ? table_screen->move(input.motion.x, input.motion.y)
                    : pairings_screen ? pairings_screen->move(input.motion.x, input.motion.y)
                    : strengths_screen ? strengths_screen->move(input.motion.x, input.motion.y)
                                      : cup_screen->move(input.motion.x, input.motion.y);
                if (changed) { render_overview(); redrawn = true; }
            } else if (input.type == SDL_MOUSEBUTTONDOWN &&
                (input.button.button == SDL_BUTTON_LEFT || input.button.button == SDL_BUTTON_RIGHT)) {
                down = input.button;
                if (table_screen) table_screen->pointer_down(input.button.x, input.button.y,
                    input.button.button == SDL_BUTTON_RIGHT);
                else if (pairings_screen) pairings_screen->pointer_down(input.button.x,
                    input.button.y, input.button.button == SDL_BUTTON_RIGHT);
                else if (strengths_screen) strengths_screen->pointer_down(input.button.x,
                    input.button.y, input.button.button == SDL_BUTTON_RIGHT);
                else cup_screen->pointer_down(input.button.x, input.button.y,
                    input.button.button == SDL_BUTTON_RIGHT);
            } else if (input.type == SDL_MOUSEBUTTONUP && down &&
                input.button.button == down->button) {
                down.reset();
                if (table_screen) table_screen->click(input.button.x, input.button.y,
                    input.button.button == SDL_BUTTON_RIGHT);
                else if (pairings_screen) pairings_screen->release(input.button.x, input.button.y);
                else if (strengths_screen) strengths_screen->release(input.button.x, input.button.y);
                else cup_screen->release(input.button.x, input.button.y);
            }

            // Whichever screen exists reports a pending request through its own accessor.
            std::optional<app::CareerSession::LiveOverviewAction> action;
            if (table_screen && table_screen->pending_family_action())
                action = *table_screen->pending_family_action();
            else if (pairings_screen && pairings_screen->pending_family_action())
                action = *pairings_screen->pending_family_action();
            else if (pairings_screen && pairings_screen->pending())
                action = *pairings_screen->pending();
            else if (strengths_screen && strengths_screen->pending())
                action = *strengths_screen->pending();
            else if (cup_screen && cup_screen->pending())
                action = *cup_screen->pending();
            if (action) { dispatch_action(*action); redrawn = true; }
            if ((closed && !down) || !running) break;
        }
        if (!running || (closed && !down)) break;
        if (closed) { SDL_Delay(8); continue; }
        // The pairings screen has a timer in source seconds that may raise a request by itself.
        if (pairings_screen && pairings_screen->tick_source_seconds(overview_source_seconds())) {
            if (pairings_screen->pending())
                dispatch_action(app::CareerSession::LiveOverviewAction{*pairings_screen->pending()});
            redrawn = true;
        }
        if ((closed && !down) || !running) break;
        if (!redrawn) SDL_Delay(8);
    }
    return running && closed;
}

// Callback per simulated match minute: shows the live matchday frame (unless skipped)
// and waits frame_delay ms, taken from --live-frame-ms or from the game's own speed setting.
auto App::present_live_minute(int minute, const std::vector<MatchGroup>& groups, const GameState& state) -> void {
        if (!running) return;
        bool live_frame_shown = false;
        if (!live_skipped) {
            const auto frame = ui::live_matchday_frame(minute, groups, state);
            if (!frame.matches.empty()) {
                SDL_SetWindowTitle(window, "OpenBMH - live_matchday");
                const Image live_image = live_screen.render(frame);
                canvas.show(live_image, palette);
                live_frame_shown = true;
            }
        }
        const int frame_delay = live_frame_ms_explicit ? live_frame_ms :
            int(ui::live_delay_milliseconds(state.memory.u8(::bmh::layout::Main_FormatError_ExecutableField)));
        if (live_frame_shown && !live_skipped && frame_delay > 0)
            SDL_Delay(Uint32(frame_delay));
}

// =====================================================================================
// Live match observer
// =====================================================================================
// Called by the engine once per simulated minute of a live match. Besides pausing, it is
// where the player interacts: ESC or window close skips the live display, P pauses, the
// keys for overview/table/settings open their modal screens (key letters come from the
// game executable), clicks on the top strip open an overview or the settings, and clicks
// on the scoreboard rows open the lineup (left) or tactics (right) of that team.
auto App::observe_live_minute(int minute, MatchEngine& engine, std::vector<MatchGroup>& groups,
        GameState& state, uint64_t& live_revision,
        const app::LiveLineupDispatcher& dispatch,
        const app::LiveTacticsDispatcher& dispatch_tactic,
        const app::LivePitchDispatcher& dispatch_pitch,
        const app::LiveInterventionScopeDispatcher& dispatch_scope) -> void {
        if (!running) return;
        bool quit = false;
        // Shows the engine's notice about a refused substitution as a dialog.
        const auto show_live_substitution_notice = [&](const app::LiveSubstitutionResult& result,
                const Image& backdrop, const char* parent_title) {
            if (result.status != app::LiveSubstitutionStatus::source_notice) return;
            if (result.manager_slot < 0 || result.source_text_address == 0)
                throw FormatError("live substitution notice has no Source owner or text");
            const PendingOfferNotice notice{result.manager_slot, -1,
                result.source_text_address,
                message_text(state, result.source_text_address)};
            const auto projection = ui::pending_offer_notice_dialog(state, notice);
            if (present_pending_dialog(state, projection, backdrop,
                    "OpenBMH - live_substitution_notice") == PendingOfferAnswer::unsupported)
                throw FormatError("live substitution notice presenter stopped");
            // SDLFlushEvents3 is a timed Source notice. Do not let its release
            // event become another click in the resumed parent screen.
            SDL_FlushEvents(SDL_MOUSEMOTION, SDL_MOUSEBUTTONUP);
            SDL_SetWindowTitle(window, parent_title);
        };
        // Opens the tactics screen for the team on the clicked scoreboard row (side 0 = home,
        // row positions come from the game executable, 32 pixels tall, at most 4 rows). Only for
        // human-managed teams, after minute 2 and when the display is not skipped. The
        // intervention scope is begun first and finished or aborted afterwards.
        const auto open_live_tactics = [&](int side, int y) {
            const auto frame = ui::live_matchday_frame(minute, groups, state);
            int selected_team = -1;
            for (size_t row = 0; row < frame.matches.size() && row < 4; ++row) {
                const int row_y = season_exe.u8(::bmh::layout::LiveMatchday_Render_ExecutableFieldUnsigned + uint32_t(row));
                if (y < row_y || y > row_y + 32) continue;
                selected_team = side == 0 ? frame.matches[row].home_team : frame.matches[row].away_team;
                break;
            }
            const int selected_manager = selected_team >= 0 ? state.manager_of(selected_team) : -1;
            if (live_skipped || minute <= 2 || selected_team < 0 || selected_manager < 0) return;
            std::string scope_error;
            if (!dispatch_scope(app::LiveInterventionBoundary::begin, 8,
                    selected_manager, selected_team, scope_error)) {
                std::fprintf(stderr, "live tactics: %s\n", scope_error.c_str());
                return;
            }
            bool scope_active = true;
            try {
                auto tactics_game = state;
                if (tactics_game.memory.has(::bmh::layout::ActiveManager))
                    tactics_game.memory.set8(::bmh::layout::ActiveManager, uint8_t(selected_manager));
                ui::TacticsScreen screen(data,
                    ui::tactics_from_game(tactics_game, season_exe, live_revision));
                SDL_SetWindowTitle(window, "OpenBMH - live_tactics");
                bool closed = false;
                // A click counts only when the release matches the press (same button and position).
                std::optional<SDL_MouseButtonEvent> tactics_down;
                bool source_completed = false;
                while (running && !closed) {
                    const Image image = screen.render();
                    canvas.show(image, palette);
                    SDL_Event modal;
                    bool redrawn = false;
                    while (quit_confirmation.poll(&modal)) {
                        if (modal.type == SDL_QUIT) { running = false; closed = true; break; }
                        if (modal.type == SDL_MOUSEBUTTONDOWN &&
                            (modal.button.button == SDL_BUTTON_LEFT || modal.button.button == SDL_BUTTON_RIGHT))
                            tactics_down = modal.button;
                        else if (modal.type == SDL_MOUSEBUTTONUP && tactics_down &&
                                 modal.button.button == tactics_down->button &&
                                 modal.button.x == tactics_down->x && modal.button.y == tactics_down->y) {
                            tactics_down.reset();
                            const auto request = screen.click(modal.button.x, modal.button.y,
                                modal.button.button == SDL_BUTTON_RIGHT);
                            if (screen.close_requested()) { source_completed = true; closed = true; break; }
                            if (request.kind == ui::TacticsRequestKind::set_tactic) {
                                std::string error;
                                if (dispatch_tactic(request, error)) {
                                    tactics_game = state;
                                    if (tactics_game.memory.has(::bmh::layout::ActiveManager))
                                        tactics_game.memory.set8(::bmh::layout::ActiveManager, uint8_t(selected_manager));
                                    screen.acknowledge(ui::tactics_from_game(
                                        tactics_game, season_exe, live_revision));
                                } else screen.reject_action(error);
                                redrawn = true;
                            }
                        }
                    }
                    if (!redrawn && !closed) SDL_Delay(8);
                }
                // Closing through the screen's own close control finishes the intervention; leaving any
                // other way (or quitting) aborts it.
                if (scope_active && source_completed) {
                    if (!dispatch_scope(app::LiveInterventionBoundary::finish, 8,
                            selected_manager, selected_team, scope_error)) {
                        std::fprintf(stderr, "live tactics: %s\n", scope_error.c_str());
                        dispatch_scope(app::LiveInterventionBoundary::abort, 8,
                            selected_manager, selected_team, scope_error);
                    }
                    scope_active = false;
                } else if (scope_active) {
                    dispatch_scope(app::LiveInterventionBoundary::abort, 8,
                        selected_manager, selected_team, scope_error);
                    scope_active = false;
                }
                SDL_SetWindowTitle(window, "OpenBMH - live_matchday");
            } catch (const std::exception& e) {
                if (scope_active)
                    dispatch_scope(app::LiveInterventionBoundary::abort, 8,
                        selected_manager, selected_team, scope_error);
                std::fprintf(stderr, "live tactics: %s\n", e.what());
            }
        };
        // The pause key is stored in the executable; pausing is only offered when it is P.
        const uint8_t source_pause_key = season_exe.u8(::bmh::layout::Main_Screen_RuntimeField);
        const bool source_pause_is_p = source_pause_key == 'p' || source_pause_key == 'P';
        // Shows the current live frame as paused and waits until P is pressed again (key repeats
        // ignored) or the program quits.
        const auto pause_live_match = [&] {
            if (!source_pause_is_p) return;
            const auto frame = ui::live_matchday_frame(minute, groups, state);
            const Image image = live_screen.render(frame);
            for (size_t i = 0; i < pixels.size(); ++i)
                pixels[i] = palette.argb[image.pixels[i]];
            SDL_UpdateTexture(texture, nullptr, pixels.data(), 320 * 4);
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, nullptr, nullptr);
            SDL_SetWindowTitle(window, "OpenBMH - live_paused");
            app::present(renderer);

            bool resumed = false;
            while (running && !resumed) {
                SDL_Event paused_event;
                while (quit_confirmation.poll(&paused_event)) {
                    if (paused_event.type == SDL_QUIT) {
                        quit = true;
                        live_skipped = true;
                        running = false;
                        break;
                    }
                    if (paused_event.type == SDL_KEYDOWN && !paused_event.key.repeat &&
                        paused_event.key.keysym.sym == SDLK_p)
                        resumed = true;
                }
                if (!resumed && running) SDL_Delay(8);
            }
            if (running) SDL_SetWindowTitle(window, "OpenBMH - live_matchday");
        };
        // Redraws the live matchday frame after a modal screen has closed.
        const auto restore_live_frame = [&] {
            if (!running) return;
            const Image image = live_screen.render(ui::live_matchday_frame(minute, groups, state));
            SDL_SetWindowTitle(window, "OpenBMH - live_matchday");
            canvas.show(image, palette);
        };
        // Opens the overview selected by key_slot through the session and runs it via
        // present_running_overview. The sequence counter numbers the overviews opened in this
        // match; the overview is taken from the first live group.
        const auto show_live_overview = [&](int key_slot) {
            if (!running || live_skipped || live_overview_sequence == UINT64_MAX) return;
            const int manager = state.memory.has(::bmh::layout::ActiveManager) ? state.memory.u8(::bmh::layout::ActiveManager) : -1;
            // EndPhase reads the keyboard overview descriptor from the
            // first live group (CareerSessionRuntimeField5 = MatchdayGroupDescriptors + 0xcc). DeskPosition is
            // the later per-group processing cursor and is not this owner.
            const int group_index = groups.empty() ? -1 : 0;
            if (manager < 0 || size_t(manager) >= state.manager_teams.size() ||
                group_index < 0 || size_t(group_index) >= groups.size()) return;
            const int team = state.manager_teams[size_t(manager)];
            const auto frame = ui::live_matchday_frame(minute, groups, state);
            const Image underlay = live_screen.render(frame);
            app::CareerSession::LiveOverviewRequest request;
            request.key_slot = key_slot;
            request.minute = minute;
            request.group_index = group_index;
            request.manager_slot = manager;
            request.runtime_team = team;
            request.revision = live_revision;
            request.sequence = live_overview_sequence + 1;
            std::string error;
            auto projection = session->open_live_overview(request, engine, live_revision,
                underlay, error);
            if (!projection) {
                if (!error.empty()) std::fprintf(stderr, "live overview: %s\n", error.c_str());
                return;
            }
            ++live_overview_sequence;

            const bool closed = present_running_overview(std::move(projection), state, live_revision,
                [&](const app::CareerSession::LiveOverviewAction& action, std::string& dispatch_error,
                    const app::TransferOfferTeamInfoPresenter& team_info) {
                    return session->dispatch_live_overview(action, engine, live_revision, dispatch_error, team_info);
                }, nullptr);
            if (running && closed) restore_live_frame();
        };
        // Modal settings screen inside a live match. `trigger` is the mouse press that opened it,
        // so that its release can be matched. Every change is dispatched to the session.
        const auto show_live_settings = [&](std::optional<SDL_MouseButtonEvent> trigger = std::nullopt) {
            if (!running || live_skipped || !state.memory.has(::bmh::layout::ActiveManager)) return;
            const int manager = state.memory.u8(::bmh::layout::ActiveManager);
            if (manager < 0 || size_t(manager) >= state.manager_teams.size()) return;
            app::CareerSession::LiveSettingsRequest request;
            request.minute = minute;
            request.manager_slot = manager;
            request.runtime_team = state.manager_teams[size_t(manager)];
            request.revision = live_revision;
            request.sequence = 0; // Session assigns and returns the scoped owner nonce.
            std::string error;
            auto projection = session->open_live_settings(request, engine, live_revision, error);
            if (!projection) {
                if (!error.empty()) std::fprintf(stderr, "live settings: %s\n", error.c_str());
                return;
            }
            app::original_audio_command(playback, 1); // SavePathInDirectory before the live settings modal.
            ui::SettingsScreen screen(data, projection->state);
            std::unique_ptr<ui::SettingsReadmeScreen> readme;
            // Draws the settings screen, or its readme viewer when that is open.
            const auto render_settings = [&] {
                const Image image = readme ? readme->render() : screen.render();
                SDL_SetWindowTitle(window, readme ? "OpenBMH - settings_readme" : "OpenBMH - live_settings");
                canvas.show(image, palette);
            };
            render_settings();
            // Modal save/load screen for settings files opened from the settings screen; the result is
            // dispatched to the session and the settings screen is shown again afterwards.
            const auto show_settings_file = [&] {
                const auto* pending = screen.pending_file();
                if (!pending) return;
                const auto menu_request = *pending;
                ui::SaveLoadScreen file_screen(data, save_directory,
                    menu_request.action == ui::SaveLoadAction::load ?
                        ui::SaveLoadMode::load : ui::SaveLoadMode::save,
                    menu_request.manager_slot, live_revision, screen.render(),
                    !state.memory.has(::bmh::layout::PlainBackground) || state.memory.u8(::bmh::layout::PlainBackground) == 0,
                    ui::SaveLoadFileKind::settings, menu_request.origin_item_id);
                // Draws the settings file screen.
                const auto render_file = [&] {
                    const Image image = file_screen.render();
                    SDL_SetWindowTitle(window, "OpenBMH - live_settings_file");
                    canvas.show(image, palette);
                };
                render_file();
                // Give the filename field the native text input focus.
                focus_filename_input(renderer);
                bool finished = false;
                while (running && !finished) {
                    SDL_Event file_input;
                    bool file_changed = false;
                    while (quit_confirmation.poll(&file_input)) {
                        if (file_input.type == SDL_QUIT) { running = false; break; }
                        if (file_input.type == SDL_MOUSEBUTTONUP &&
                            (file_input.button.button == SDL_BUTTON_LEFT ||
                             file_input.button.button == SDL_BUTTON_RIGHT)) {
                            // clicks >= 2 reports a double click.
                            file_changed |= file_screen.click(file_input.button.x,
                                file_input.button.y,
                                file_input.button.button == SDL_BUTTON_RIGHT,
                                file_input.button.clicks >= 2);
                        // Only keys the file screen understands are forwarded; Ctrl/Cmd+A means select all.
                        } else if (file_input.type == SDL_KEYDOWN && !file_input.key.repeat) {
                            std::string_view key;
                            switch (file_input.key.keysym.sym) {
                            case SDLK_ESCAPE: key = "ESC"; break;
                            case SDLK_RETURN: key = "ENTER"; break;
                            case SDLK_BACKSPACE: key = "BACKSPACE"; break;
                            case SDLK_UP: key = "UP"; break;
                            case SDLK_DOWN: key = "DOWN"; break;
                            case SDLK_LEFT: key = "LEFT"; break;
                            case SDLK_RIGHT: key = "RIGHT"; break;
                            case SDLK_a:
                                if (file_input.key.keysym.mod & (KMOD_CTRL | KMOD_GUI)) key = "SELECT_ALL";
                                break;
                            default: break;
                            }
                            if (!key.empty()) file_changed |= file_screen.key(key);
                        } else if (file_input.type == SDL_TEXTINPUT) {
                            file_changed |= file_screen.text(file_input.text.text);
                        }
                        if (const auto& file_request = file_screen.pending()) {
                            const app::CareerSession::LiveSettingsAction action{
                                app::CareerSession::LiveSettingsFile{
                                    projection->owner.sequence, *file_request}};
                            auto result = session->dispatch_live_settings(action,
                                engine, live_revision, error);
                            if (!result) {
                                file_screen.reject(error.empty() ?
                                    "Dateiaktion fehlgeschlagen." : error);
                                file_changed = true;
                            } else {
                                projection->owner = result->owner;
                                screen.acknowledge_file(std::move(result->state));
                                file_screen.acknowledge();
                                finished = true;
                            }
                        } else if (file_screen.close_requested()) {
                            screen.cancel_file_request();
                            finished = true;
                        }
                        if (!running || finished) break;
                        if (file_changed) render_file();
                    }
                    if (!file_changed && running && !finished) SDL_Delay(8);
                }
                if (running) render_settings();
            };
            // settings_down tracks the held press so its release is matched. readme_release_barrier is
            // set when the readme was closed by a press: that press's release must not act on the
            // settings screen.
            bool closed = false;
            bool readme_release_barrier = false;
            std::optional<SDL_MouseButtonEvent> settings_down = trigger;
            while (running && !closed) {
                SDL_Event input;
                bool changed = false;
                while (quit_confirmation.poll(&input)) {
                    if (input.type == SDL_QUIT) { running = false; break; }
                    if (readme) {
                        if (input.type == SDL_MOUSEBUTTONDOWN &&
                            (input.button.button == SDL_BUTTON_LEFT ||
                             input.button.button == SDL_BUTTON_RIGHT)) {
                            settings_down = input.button;
                            readme->pointer_down(true);
                            if (readme->close_requested()) {
                                readme.reset();
                                readme_release_barrier = true;
                            }
                            render_settings();
                        }
                        // SDLFlushEvents2 waits for a mouse press; keys and
                        // motion do not close the Source readme viewer.
                        continue;
                    }
                    if (input.type == SDL_KEYDOWN && !input.key.repeat &&
                        input.key.keysym.sym == SDLK_ESCAPE) {
                        screen.key("ESC");
                        changed = true;
                    } else if (input.type == SDL_MOUSEBUTTONDOWN &&
                               (input.button.button == SDL_BUTTON_LEFT ||
                                input.button.button == SDL_BUTTON_RIGHT)) {
                        settings_down = input.button;
                        // The footer buttons (x 1..54, y 188..223) act on the matching release; other controls
                        // act on the press itself.
                        const bool footer_button = input.button.button == SDL_BUTTON_LEFT &&
                            input.button.x >= 1 && input.button.x < 55 &&
                            input.button.y >= 188 && input.button.y < 224;
                        if (!footer_button)
                            changed = screen.click(input.button.x, input.button.y,
                                input.button.button == SDL_BUTTON_RIGHT);
                    } else if (input.type == SDL_MOUSEBUTTONUP && settings_down &&
                               input.button.button == settings_down->button &&
                               input.button.x == settings_down->x && input.button.y == settings_down->y) {
                        if (readme_release_barrier) {
                            readme_release_barrier = false;
                        } else if (input.button.button == SDL_BUTTON_LEFT &&
                            input.button.x >= 1 && input.button.x < 55 &&
                            input.button.y >= 188 && input.button.y < 224)
                            changed = screen.click(input.button.x, input.button.y);
                        settings_down.reset(); // Cells act on DOWN; file buttons on matching UP.
                    }

                    if (!running) break;
                    if (screen.readme_requested()) {
                        screen.consume_readme_request();
                        readme = std::make_unique<ui::SettingsReadmeScreen>(
                            data, screen.state().decorated_background);
                        changed = true;
                    }
                    if (screen.pending_file()) {
                        show_settings_file();
                        changed = false; // The child restores its parent on return.
                    }
                    if (const auto* write = screen.pending()) {
                        const auto request_write = *write;
                        const app::CareerSession::LiveSettingsAction action{
                            app::CareerSession::LiveSettingsWrite{projection->owner.sequence,
                                request_write}};
                        auto result = session->dispatch_live_settings(action, engine, live_revision, error);
                        if (!result) {
                            const auto notice = ui::settings_source_refusal(state, request_write, live_revision);
                            if (notice && error == ui::original_to_utf8(message_text(state, *notice))) {
                                auto view = ui::environment_notice_dialog(state, request_write.manager_slot,
                                    message_text(state, *notice));
                                (void)present_pending_dialog(state, std::move(view), screen.render(),
                                    "OpenBMH - live_settings_notice");
                                screen.reject({});
                                screen.consume_release();
                                settings_down.reset();
                                SDL_FlushEvents(SDL_MOUSEMOTION, SDL_MOUSEBUTTONUP);
                                SDL_SetWindowTitle(window, "OpenBMH - live_settings");
                            } else {
                                screen.reject(error.empty() ? "Diese Einstellung wurde nicht übernommen." : error);
                                std::fprintf(stderr, "live settings: %s\n", error.c_str());
                            }
                        } else {
                            projection->owner = result->owner;
                            screen.acknowledge(std::move(result->state));
                            playback.set_original_options(
                                app::original_audio_options(session->state(), music_enabled));
                        }
                        changed = true;
                    }
                    if (screen.close_requested()) {
                        const app::CareerSession::LiveSettingsAction action{
                            app::CareerSession::LiveSettingsCloseRequest{projection->owner}};
                        auto result = session->dispatch_live_settings(action, engine, live_revision, error);
                        if (!result || !result->closed) {
                            std::fprintf(stderr, "live settings close: %s\n",
                                error.empty() ? "Source close rejected" : error.c_str());
                            running = false;
                        } else {
                            projection->owner = result->owner;
                            closed = true;
                        }
                    } else if (changed) {
                        render_settings();
                    }
                    if (closed || !running) break;
                }
                if (!running || closed) break;
                if (!changed) SDL_Delay(8);
            }
            // A Source close may be requested on the opening/closing DOWN;
            // consume its physical release before the live observer resumes.
            if (closed) SDL_FlushEvent(SDL_MOUSEBUTTONUP);
            if (running && closed) app::original_match_audio_command(playback, state, 3);
            if (running && closed) restore_live_frame();
        };
        // True at the first minute of a phase (1, then 21 indoors or 46/91/106 outdoors); the
        // pause and overview keys and strip clicks are ignored then.
        const bool source_phase_start = minute == 1 ||
            (state.indoor ? minute == 21 : (minute == 46 || minute == 91 || minute == 106));
        // Poll this minute's events directly (the live display is its own input loop).
        SDL_Event event;
        while (quit_confirmation.poll(&event)) {
            if (event.type == SDL_QUIT) { quit = true; live_skipped = true; }
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
                live_skipped = true;
            else if (!source_phase_start && source_pause_is_p && event.type == SDL_KEYDOWN &&
                     !event.key.repeat && event.key.keysym.sym == SDLK_p)
                pause_live_match();
            else if (!source_phase_start && event.type == SDL_KEYDOWN && !event.key.repeat &&
                     season_exe.u8(::bmh::layout::Main_FileScreen_RuntimeField) == 'g' && event.key.keysym.sym == SDLK_g)
                show_live_overview(0);
            else if (!source_phase_start && event.type == SDL_KEYDOWN && !event.key.repeat &&
                     season_exe.u8(::bmh::layout::Main_FileScreen_RuntimeField2) == 't' && event.key.keysym.sym == SDLK_t)
                show_live_overview(1);
            else if (!source_phase_start && event.type == SDL_KEYDOWN && !event.key.repeat &&
                     season_exe.u8(::bmh::layout::Main_FileScreen_RuntimeField3) == 's' && event.key.keysym.sym == SDLK_s)
                show_live_settings();
            // Press on the top strip (x 0..63, y 50..64) opens an overview or the settings.
            else if (event.type == SDL_MOUSEBUTTONDOWN &&
                     (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT) &&
                     event.button.x >= 0 && event.button.x <= 63 &&
                     event.button.y > 49 && event.button.y < 65) {
                // OriginalMouseSound sets CareerSession_BeginFriendlyOpponentTable_ExecutableField2 from this physical event: left=2,
                // right=8. Keep it as input context; do not persist it in GameState.
                const uint8_t source_mouse_mode =
                    event.button.button == SDL_BUTTON_LEFT ? 2 : 8;
                if ((source_mouse_mode != 2 && source_mouse_mode != 8) ||
                    source_phase_start || minute <= 2)
                    continue;
                // EndPhase retains the sampled coordinates while each
                // modal runs. Its inclusive ranges overlap at21/42:
                // after closing the first viewer, enter its neighbour
                // before resuming the same match minute.
                for (int candidate = 0; candidate < 3 && running; ++candidate) {
                    const int left = candidate * 21;
                    if (event.button.x >= left && event.button.x <= left + 21) {
                        if (candidate < 2) show_live_overview(candidate);
                        else show_live_settings(event.button);
                    }
                }
            }
            // Press on a scoreboard side: right = tactics, left = lineup of the clicked row's team.
            else if (const auto side = app::live_matchday_board_side(event, minute, state.indoor)) {
                if (event.button.button == SDL_BUTTON_RIGHT) {
                    open_live_tactics(*side, event.button.y);
                    continue;
                }
                const auto frame = ui::live_matchday_frame(minute, groups, state);
                int selected_team = -1, selected_opponent = -1;
                for (size_t row = 0; row < frame.matches.size() && row < 4; ++row) {
                    const int y = season_exe.u8(::bmh::layout::LiveMatchday_Render_ExecutableFieldUnsigned + uint32_t(row));
                    if (event.button.y < y || event.button.y > y + 32) continue;
                    if (*side == 0) {
                        selected_team = frame.matches[row].home_team;
                        selected_opponent = frame.matches[row].away_team;
                    } else {
                        selected_team = frame.matches[row].away_team;
                        selected_opponent = frame.matches[row].home_team;
                    }
                    break;
                }
                // EndPhase: physical left-DOWN is Source mode 2; the
                // matching right-DOWN is mode 8.
                (void)present_live_lineup(minute, engine, groups,
                    state, live_revision, selected_team, selected_opponent,
                    dispatch, dispatch_tactic, dispatch_pitch, dispatch_scope, live_skipped, false);
            }
        }
        if (quit) running = false;
}

// Next callback: a lineup change forced by a live match event; runs the same lineup
// screen as the click on the scoreboard. It must complete unless the program quits.
auto App::present_live_event_lineup(const LiveMatchEvent& event, MatchEngine& engine,
           std::vector<MatchGroup>& groups, GameState& state, uint64_t& live_revision,
           const app::LiveLineupDispatcher& dispatch,
           const app::LiveTacticsDispatcher& dispatch_tactic,
           const app::LivePitchDispatcher& dispatch_pitch,
           const app::LiveInterventionScopeDispatcher& dispatch_scope) -> void {
        if (!running) return;
        const bool completed = present_live_lineup(event.minute, engine, groups, state, live_revision,
            event.team, event.opponent, dispatch, dispatch_tactic, dispatch_pitch,
            dispatch_scope, false, true);
        if (!running) return; // SDL_QUIT aborts the Source scope without failing the event.
        if (!completed)
            throw FormatError("The Source event line-up did not complete.");
}

// =====================================================================================
// Live match animation callbacks
// =====================================================================================
// The following callbacks each show one kind of match event (ball movement, chance, shot,
// set piece, booking, penalty shootout, reality mode) and then pause. The pause is
// --live-frame-ms when given, otherwise a value saved in the game memory: values below 6
// are halved and each unit is 20 ms. Only a window close request is consumed here; other
// input stays queued for the per-minute observer.
//
// Callback: ball movement frames of one displacement.
auto App::animate_ball(const LiveBallEvent& event, const std::vector<MatchGroup>& groups,
           const GameState& state) -> void {
        if (!running || live_skipped) return;
        const auto frame = live_score_reveal.project(ui::live_matchday_frame(event.minute, groups, state));
        if (frame.matches.empty()) return;
        // Local helper: paints an image with the normal palette and presents it.
        const auto present = [&](const Image& image) {
            canvas.show(image, palette);
        };
        const int count = ui::LiveMatchdayScreen::ball_frame_count(event.displacement);
        // Converts the saved pause number at the given row into milliseconds.
        const auto source_delay = [&](int pause) {
            const uint32_t address = ::bmh::layout::Main_FormatError_ExecutableFieldUnsigned + uint32_t(pause * 9);
            const int saved = state.memory.has(address) ? state.memory.u8(address) : 0;
            return (saved < 6 ? saved / 2 : saved) * 20;
        };
        for (int index = 0; index < count && running; ++index) {
            const std::string title = "OpenBMH - live_ball_" + std::to_string(event.minute) + "_" +
                std::to_string(event.group) + "_" + std::to_string(event.match) + "_" +
                std::to_string(event.displacement) + "_" + std::to_string(index);
            SDL_SetWindowTitle(window, title.c_str());
            const auto* chance = live_chance_context &&
                live_chance_context->minute == event.minute &&
                live_chance_context->group == event.group &&
                live_chance_context->match == event.match ? &*live_chance_context : nullptr;
            present(live_screen.render_ball(frame, event.displacement, index, chance));
            SDL_PumpEvents();
            SDL_Event quit_event;
            if (quit_confirmation.peep(&quit_event, 1, SDL_GETEVENT, SDL_QUIT, SDL_QUIT) > 0) {
                running = false;
                break;
            }
            // AdvanceStep(5) reads a252c; DecodeTaxAnimationScript2 uses 20ms ticks.
            // Retain key/mouse events for the live-minute input boundary.
            const int delay = live_frame_ms_explicit ? live_frame_ms :
                source_delay(5);
            if (delay > 0) SDL_Delay(Uint32(delay));
        }
        if (running) {
            const int final_delay = live_frame_ms_explicit ? live_frame_ms : source_delay(6);
            if (final_delay > 0) SDL_Delay(Uint32(final_delay));
            SDL_SetWindowTitle(window, "OpenBMH - live_matchday");
            present(live_screen.render(frame));
        }
}

// Next callback: a scoring chance, shown as a preview and again as its result.
auto App::animate_chance(const LiveChanceEvent& event, const std::vector<MatchGroup>& groups,
           const GameState& state) -> void {
        if (!running || live_skipped) return;
        // Preview: the chance is already in the event log, but its statistics must not count yet.
        const auto frame = event.stage == LiveChanceStage::preview
            ? ui::live_matchday_frame_before_chance(event.minute, groups, state, event)
            : ui::live_matchday_frame(event.minute, groups, state);
        live_score_reveal.observe(event.stage, frame);
        if (frame.matches.empty()) return;
        const auto chance = ui::live_chance_projection(event, groups, state, season_exe);
        if (event.stage == LiveChanceStage::preview) live_chance_context = chance;
        const std::string title = std::string("OpenBMH - live_chance_") +
            (event.stage == LiveChanceStage::preview ? "preview_" : "result_") +
            std::to_string(event.minute) + "_" + std::to_string(event.group) + "_" +
            std::to_string(event.match) + "_" + std::to_string(event.side) + "_" +
            std::to_string(event.kind) + "_" + std::to_string(int(event.goal));
        SDL_SetWindowTitle(window, title.c_str());
        const Image image = live_screen.render_chance(frame, chance);
        canvas.show(image, palette);
        SDL_PumpEvents();
        SDL_Event quit_event;
        if (quit_confirmation.peep(&quit_event, 1, SDL_GETEVENT, SDL_QUIT, SDL_QUIT) > 0) {
            running = false;
            live_chance_context.reset();
            return;
        }
        // Pause row: 12 for the preview, 1 after a goal, otherwise 2.
        const int pause = event.stage == LiveChanceStage::preview ? 12 : (event.goal ? 1 : 2);
        const uint32_t address = ::bmh::layout::Main_FormatError_ExecutableFieldUnsigned + uint32_t(pause * 9);
        const int saved = state.memory.has(address) ? state.memory.u8(address) : 0;
        const int delay = live_frame_ms_explicit ? live_frame_ms :
            (saved < 6 ? saved / 2 : saved) * 20;
        if (delay > 0) SDL_Delay(Uint32(delay));
        if (event.stage == LiveChanceStage::result) live_chance_context.reset();
}

// Next callback: shot animation, drawn with the shot's own palette. After the frames it
// either waits for a click or applies the saved pause.
auto App::animate_shot(const LiveShotEvent& event, const std::vector<MatchGroup>& groups,
           const GameState& state) -> void {
        if (!running || live_skipped) return;
        const auto frame = live_score_reveal.project(ui::live_matchday_frame(event.minute, groups, state));
        if (frame.matches.empty()) return;
        const auto frames = live_screen.render_shot(frame, event);
        const auto shot_colors = live_screen.shot_palette(palette, event, groups, state);
        const std::string identity = std::to_string(event.minute) + "_" +
            std::to_string(event.group) + "_" + std::to_string(event.match) + "_" +
            std::to_string(event.acting_side) + "_" + std::to_string(int(event.miss));
        for (size_t index = 0; index < frames.size() && running; ++index) {
            const std::string title = "OpenBMH - live_shot_" + identity + "_" + std::to_string(index);
            SDL_SetWindowTitle(window, title.c_str());
            canvas.show(frames[index], shot_colors);
            SDL_PumpEvents();
            SDL_Event input;
            if (quit_confirmation.peep(&input, 1, SDL_GETEVENT, SDL_QUIT, SDL_QUIT) > 0) {
                running = false;
                return;
            }
            // Source waits Main_FormatError_RuntimeField vertical retraces after each frame (70 Hz).
            const int retraces = state.memory.has(::bmh::layout::Main_FormatError_RuntimeField) ?
                std::max(0, int(int8_t(state.memory.u8(::bmh::layout::Main_FormatError_RuntimeField)))) : 0;
            const int delay = live_frame_ms_explicit ? live_frame_ms : retraces * 1000 / 70;
            if (delay > 0) SDL_Delay(Uint32(delay));
        }
        if (!running) return;
        if (!live_frame_ms_explicit) SDL_Delay(200); // LiveMatchdayFrame -> DecodeTaxAnimationScript2(10)
        if (app::match_sound_enabled(state))
            app::original_audio_command(playback, event.miss ? 20 : 19); // SetPieceAnimation, before its ACK.
        if (state.memory.has(::bmh::layout::Main_FormatError_RuntimeField5) && state.memory.u8(::bmh::layout::Main_FormatError_RuntimeField5) == 0) {
            // SDLFlushEvents2 accepts a fresh left/right press, not a keyboard skip.
            const std::string title = "OpenBMH - live_shot_wait_" + identity;
            SDL_SetWindowTitle(window, title.c_str());
            SDL_FlushEvents(SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP);
            SDL_Event input;
            bool acknowledged = false;
            while (running && !acknowledged) {
                while (quit_confirmation.poll(&input)) {
                    if (input.type == SDL_QUIT) { running = false; break; }
                    if (input.type == SDL_MOUSEBUTTONDOWN &&
                        (input.button.button == SDL_BUTTON_LEFT || input.button.button == SDL_BUTTON_RIGHT)) {
                        acknowledged = true;
                        break;
                    }
                }
                if (!acknowledged && running) SDL_Delay(10);
            }
        } else {
            // AdvanceStep(15): the ordinary saved pause instead of a click.
            const int saved = state.memory.has(::bmh::layout::Main_FormatError_RuntimeField7) ? state.memory.u8(::bmh::layout::Main_FormatError_RuntimeField7) : 0;
            const int delay = live_frame_ms_explicit ? live_frame_ms : (saved < 6 ? saved / 2 : saved) * 20;
            if (delay > 0) SDL_Delay(Uint32(delay));
        }
}

// Next callback: set piece animation frames (first frame held 40 retraces at 70 Hz).
auto App::animate_set_piece(const LiveSetPieceEvent& event, const std::vector<MatchGroup>& groups,
           const GameState& state) -> void {
        if (!running || live_skipped) return;
        const auto frame=live_score_reveal.project(ui::live_matchday_frame(event.minute,groups,state));
        if (frame.matches.empty()) return;
        const auto images=live_screen.render_set_piece(frame,event);
        LiveShotEvent palette_event;
        palette_event.group=event.group;palette_event.match=event.match;
        const auto colors=live_screen.shot_palette(palette,palette_event,groups,state);
        for (size_t index=0;index<images.size() && running;++index) {
            const std::string title="OpenBMH - live_set_piece_"+std::to_string(event.minute)+"_"+
                std::to_string(event.group)+"_"+std::to_string(event.match)+"_"+
                std::to_string(event.source_side)+"_"+std::to_string(event.kind)+"_"+
                std::to_string(index)+"_"+std::to_string(images.size());
            SDL_SetWindowTitle(window,title.c_str());
            canvas.show(images[index], colors);
            SDL_PumpEvents();SDL_Event input;
            if (quit_confirmation.peep(&input,1,SDL_GETEVENT,SDL_QUIT,SDL_QUIT)>0) { running=false;return; }
            // FirstThen holds the first frame for 40 retraces; later frames
            // use saved Main_FormatError_RuntimeField2. Keyboard/mouse input remains for the minute hook.
            const int retraces=index==0?40:(state.memory.has(::bmh::layout::Main_FormatError_RuntimeField2)?state.memory.u8(::bmh::layout::Main_FormatError_RuntimeField2):0);
            const int delay=live_frame_ms_explicit?live_frame_ms:retraces*1000/70;
            if (delay>0) SDL_Delay(Uint32(delay));
        }
        if (running && !live_frame_ms_explicit) SDL_Delay(200); // LiveMatchdayFrame
}

// Next callback: booking/injury box; pause row 3 for kind 0, otherwise 4.
auto App::animate_booking(const LiveBookingEvent& event, const std::vector<MatchGroup>& groups,
           const GameState& state) -> void {
        if (!running || live_skipped) return;
        const auto frame=ui::live_matchday_frame(event.minute,groups,state);
        if (frame.matches.empty()) return;
        const auto booking=ui::live_booking_projection(event,groups,state,season_exe);
        const std::string title="OpenBMH - live_booking_"+std::to_string(event.minute)+"_"+
            std::to_string(event.group)+"_"+std::to_string(event.match)+"_"+
            std::to_string(event.side)+"_"+std::to_string(event.kind)+"_"+std::to_string(event.player);
        const auto present=[&](const Image& image) {
            canvas.show(image, palette);
        };
        SDL_SetWindowTitle(window,title.c_str());present(live_screen.render_booking(frame,booking));
        if (event.kind != 2 && app::match_sound_enabled(state))
            app::original_audio_command(playback, 17); // EventImpl/SDLPumpEvents, no injury whistle.
        SDL_PumpEvents();SDL_Event input;
        if (quit_confirmation.peep(&input,1,SDL_GETEVENT,SDL_QUIT,SDL_QUIT)>0) { running=false;return; }
        const int pause=event.kind==0?3:4;
        const uint32_t address=::bmh::layout::Main_FormatError_ExecutableFieldUnsigned+uint32_t(pause*9);
        const int saved=state.memory.has(address)?state.memory.u8(address):0;
        const int delay=live_frame_ms_explicit?live_frame_ms:(saved<6?saved/2:saved)*20;
        if (delay>0) SDL_Delay(Uint32(delay));
        // EventImpl clears the box after its pause; no Source click wait here.
        SDL_SetWindowTitle(window,"OpenBMH - live_matchday");present(live_screen.render(frame));
}

// Next callback: penalty shootout step; the saved pause is repeated 4 times for the
// preview and twice for the result.
auto App::animate_shootout(const LiveShootoutEvent& event, const std::vector<MatchGroup>& groups,
           const GameState& state) -> void {
        if (!running || live_skipped) return;
        const auto frame=ui::live_matchday_frame(event.minute,groups,state);
        if (frame.matches.empty()) return;
        const auto projection=ui::live_shootout_projection(event,groups,state,season_exe);
        const std::string title="OpenBMH - live_shootout_"+std::to_string(int(event.stage))+"_"+
            std::to_string(event.group)+"_"+std::to_string(event.match)+"_"+
            std::to_string(event.side)+"_"+std::to_string(event.round)+"_"+
            std::to_string(event.of)+"_"+std::to_string(event.player);
        SDL_SetWindowTitle(window,title.c_str());
        const auto image=live_screen.render_shootout(frame,projection);
        canvas.show(image, palette);
        SDL_PumpEvents();SDL_Event input;
        if (quit_confirmation.peep(&input,1,SDL_GETEVENT,SDL_QUIT,SDL_QUIT)>0) { running=false;return; }
        const int repeats=event.stage==LiveShootoutStage::preview?4:2;
        const int saved=state.memory.has(::bmh::layout::Main_FormatError_RuntimeField6)?state.memory.u8(::bmh::layout::Main_FormatError_RuntimeField6):0;
        const int delay=live_frame_ms_explicit?live_frame_ms:(saved<6?saved/2:saved)*20;
        for (int i=0;i<repeats && running;++i) if (delay>0) SDL_Delay(Uint32(delay));
}

// Next callback: reality mode (running commentary with a moving field). Stages: begin
// (builds the screens), one call per round, settling and end. Returns true when the
// player ended it with ESC or a right press, or when the program quits.
auto App::present_reality(const LiveRealityEvent& event, const std::vector<MatchGroup>& groups,
           const GameState& state) -> bool {
        if (!running || live_skipped) return false;
        const auto frame = ui::live_matchday_frame(event.minute,groups,state);
        const auto present = [&](const Image& image,const Palette& colors,const std::string& title) {
            SDL_SetWindowTitle(window,title.c_str());
            canvas.show(image, colors);
        };
        const auto delay = [&](uint32_t address) {
            const int saved=state.memory.has(address)?state.memory.u8(address):0;
            const int ms=live_frame_ms_explicit?live_frame_ms:(saved<6?saved/2:saved)*20;
            if (ms>0) SDL_Delay(Uint32(ms));
        };
        const std::string identity=std::to_string(event.minute)+"_"+std::to_string(event.group)+"_"+
            std::to_string(event.match)+"_"+std::to_string(event.side);
        if (event.stage==LiveRealityStage::begin) {
            LiveChanceEvent header_event;header_event.minute=event.minute;
            header_event.group=event.group;header_event.match=event.match;header_event.side=event.side;
            const auto header=ui::live_chance_projection(header_event,groups,state,season_exe);
            reality_pristine=live_screen.render_reality(frame,header);
            reality_surface=reality_pristine;reality_layout={};
            reality_field=std::make_unique<ui::RealityFieldScreen>(data,
                state.memory.has(::bmh::layout::Main_FormatError_RuntimeField3)?state.memory.u8(::bmh::layout::Main_FormatError_RuntimeField3):0);
            LiveShotEvent colors_event;colors_event.group=event.group;colors_event.match=event.match;
            reality_colors=live_screen.shot_palette(palette,colors_event,groups,state);
            present(reality_surface,reality_colors,"OpenBMH - live_reality_begin_"+identity);
            return false;
        }
        if (event.stage==LiveRealityStage::settling) {
            reality_field.reset();
            const int ms=live_frame_ms_explicit?live_frame_ms:200; // DecodeTaxAnimationScript2(10)
            if (ms>0) SDL_Delay(Uint32(ms));
            return false;
        }
        if (event.stage==LiveRealityStage::end) {
            present(live_screen.render(frame),palette,"OpenBMH - live_matchday");
            return false;
        }
        if (!reality_field) throw FormatError("Reality presentation: round without Source header");
        for (const auto& report:event.tick.reports)
            reality_reporter.append(reality_surface,reality_layout,report.text,frame.gradient_text);
        const std::string round_identity=identity+"_"+std::to_string(event.tick.round);
        if (event.tick.motion) {
            size_t index=0;
            for (const auto& motion:event.tick.motion->frames) {
                auto backdrop=reality_pristine;
                // Keep the first 129 rows of the current surface on the fresh backdrop.
                std::copy_n(reality_surface.pixels.begin(),320*129,backdrop.pixels.begin());
                reality_surface=reality_field->render(motion,backdrop);
                present(reality_surface,reality_colors,"OpenBMH - live_reality_round_"+
                    round_identity+"_"+std::to_string(index++));
                delay(::bmh::layout::Main_FormatError_ExecutableField2); // RealityFieldScreen: the saved Reality speed
            }
            delay(::bmh::layout::Main_FormatError_ExecutableField+10*9); // AdvanceStep(10), after all eleven frames
        } else {
            present(reality_surface,reality_colors,"OpenBMH - live_reality_report_"+round_identity);
        }
        // Only ESC or a right press is taken from the queue as the exit; other input stays.
        SDL_PumpEvents();SDL_Event input;
        if (quit_confirmation.peep(&input,1,SDL_GETEVENT,SDL_QUIT,SDL_QUIT)>0) {running=false;return true;}
        // Consume only the Source exit for this scene. Other input remains
        // queued for the existing live-minute/manager intervention boundary.
        if (quit_confirmation.peep(&input,1,SDL_PEEKEVENT,SDL_KEYDOWN,SDL_KEYDOWN)>0 &&
            input.key.keysym.sym==SDLK_ESCAPE) {
            quit_confirmation.peep(&input,1,SDL_GETEVENT,SDL_KEYDOWN,SDL_KEYDOWN);return true;
        }
        if (quit_confirmation.peep(&input,1,SDL_PEEKEVENT,SDL_MOUSEBUTTONDOWN,SDL_MOUSEBUTTONDOWN)>0 &&
            input.button.button==SDL_BUTTON_RIGHT) {
            quit_confirmation.peep(&input,1,SDL_GETEVENT,SDL_MOUSEBUTTONDOWN,SDL_MOUSEBUTTONDOWN);return true;
        }
        return false;
}

// Next callback: results viewer at the end of a match phase; throws if it was stopped
// before being acknowledged.
auto App::present_phase_result(const PhaseResultRequest& request, const std::vector<MatchGroup>& groups,
           const GameState& state) -> void {
        if (!running) throw FormatError("phase results: presentation stopped");
        const auto frame = ui::live_matchday_frame(request.phase_end_minute, groups, state);
        std::string error;
        auto projection = session->open_phase_results(revision, live_screen.render(frame), error);
        if (!projection) throw FormatError(error.empty() ? "phase results: Source viewer unavailable" : error);
        const bool closed = present_running_overview(std::move(projection), state, revision,
            [&](const app::CareerSession::LiveOverviewAction& action, std::string& dispatch_error,
                const app::TransferOfferTeamInfoPresenter& team_info) {
                return session->dispatch_phase_results(action, revision, dispatch_error, team_info);
            }, &request);
        if (!closed) throw FormatError("phase results: viewer stopped before Source ACK");
}
