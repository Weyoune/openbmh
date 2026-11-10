// SPDX-License-Identifier: GPL-3.0-only
// App: season end and season start (season pictures, contract expiry, tax, promotion, highscore, entrants, cup draw).
// (moved out of main.cpp unchanged; the class is declared in app/app.hpp)
#include "app/app.hpp"

// Paints a 320x240 indexed image through the palette into the texture and presents it.
// Also sets the window title and remembers the image as the backdrop for later dialogs.
auto App::present_season(const Image& image, const char* title,
                          const Palette* palette_override) -> void {
    SDL_SetWindowTitle(window, title);
    season_backdrop = image;
    const Palette& frame_palette = palette_override ? *palette_override : palette;
    const Image painted = process_timer_image(image);
    canvas.show(painted, frame_palette);
}

// Plays the highscore point animation of the desk, one rendered frame per step, if active.
auto App::present_desk_record_points() -> void {
    if(!flow.desk_records_points_active())return;
    while(flow.advance_desk_records_point())
        present_season(flow.render(),"OpenBMH - highscore");
    if(flow.finish_desk_records_points())
        present_season(flow.render(),"OpenBMH - highscore");
    dirty=false;
}

// Modal contracts screen shown when a player's contract expires. Runs its own event loop
// and applies the player's contract actions to a staged copy of the game state.
// Returns 1 if the expired player was renewed, 0 if not, -2 on quit or error (the staged
// state and random generator are then restored).
auto App::contract_expiry_presenter(GameState& staged, WatcomRandom& random,
                                       int manager, int expired_row) -> int {
    const GameState before = staged;
    const WatcomRandom random_before = random;
    try {
        uint64_t local_revision = revision;
        uint64_t next_quote_id = 1;
        auto initial = ui::contracts_from_game(staged, manager, local_revision);
        ui::ContractsScreen screen(data, std::move(initial));
        bool renewed_expired_player = false;
        bool dirty_contracts = true;
        // Modal sub-loop: polls events itself instead of returning to the main loop.
        while (running) {
            if (dirty_contracts) {
                present_season(screen.render(), "OpenBMH - contracts");
                dirty_contracts = false;
            }
            if (screen.close_requested()) return renewed_expired_player ? 1 : 0;
            SDL_Event event;
            bool changed = false;
            while (quit_confirmation.poll(&event)) {
                if (event.type == SDL_QUIT) {
                    running = false;
                    staged = before;
                    random = random_before;
                    return -2;
                }
                // Clicks act on button release; a button held down is drawn pressed meanwhile.
                if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                    if (screen.press(event.button.x, event.button.y)) {
                        present_season(screen.render(), "OpenBMH - contracts");
                    }
                } else if (event.type == SDL_MOUSEBUTTONUP &&
                    (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT)) {
                    changed |= screen.click(event.button.x, event.button.y,
                                            event.button.button == SDL_BUTTON_RIGHT);
                    changed = true;   // also ends a pressed look that did not lead to a change
                } else if (event.type == SDL_MOUSEMOTION) {
                    changed |= screen.move(event.motion.x, event.motion.y);
                } else if (event.type == SDL_TEXTINPUT) {
                    changed |= screen.text(event.text.text);
                } else if (event.type == SDL_KEYDOWN) {
                    if (event.key.keysym.sym == SDLK_ESCAPE) changed |= screen.key("ESC");
                    else if (event.key.keysym.sym == SDLK_BACKSPACE) changed |= screen.key("BACKSPACE");
                }
                // The screen asks for an engine action (quote, offer, bonuses, insurance); it is validated
                // and executed on the staged state, then acknowledged to the screen. Any exception rolls
                // the staged state back.
                if (const ui::ContractRequest* pending = screen.pending()) {
                    const ui::ContractRequest request = *pending;
                    const auto old_game = staged;
                    const auto old_random = random;
                    try {
                        // Reject requests that do not belong to the current club or row range.
                        if (request.manager_slot != manager || request.runtime_club != staged.manager_teams.at(size_t(manager)) ||
                            request.row < 0 || request.row >= 24)
                            throw FormatError("Vertragsaktion gehört nicht zum aktuellen Verein");
                        if (request.kind == ui::ContractActionKind::request_demands) {
                            auto demands = contract_demands(staged, manager, request.row, false, random);
                            auto next = ui::contracts_from_game(staged, manager, ++local_revision);
                            next.demands = demands;
                            next.quote_id = next_quote_id++;
                            next.quote_row = request.row;
                            screen.acknowledge_quote(next);
                        } else if (request.kind == ui::ContractActionKind::submit_offer) {
                            if (request.term_column >= 4 || request.quote_id != screen.state().quote_id ||
                                request.row != screen.state().quote_row || !screen.state().demands ||
                                request.demand != (*screen.state().demands)[request.term_column] ||
                                // Stale quote, wrong row or implausibly high salary: refuse.
                                request.monthly_salary > 200000)
                                throw FormatError("Vertragsangebot ist nicht mehr aktuell");
                            const bool accepted = contract_offer(staged, manager, request.row,
                                request.term_column, request.monthly_salary, request.demand,
                                false, request.row == expired_row, random, &request.proposed_bonuses);
                            auto next = ui::contracts_from_game(staged, manager, ++local_revision);
                            next.feedback = accepted ? "Der Vertrag wurde verlängert." : "Das Angebot wurde abgelehnt.";
                            screen.acknowledge_offer(next);
                            if (accepted && request.row == expired_row) renewed_expired_player = true;
                        } else if (request.kind == ui::ContractActionKind::submit_bonuses) {
                            const bool accepted = contract_bonuses(staged, manager, request.row,
                                                                   request.proposed_bonuses);
                            auto next = ui::contracts_from_game(staged, manager, ++local_revision);
                            next.feedback = accepted ? "Die Prämien wurden übernommen." : "Die Prämien wurden nicht geändert.";
                            screen.acknowledge_bonus_offer(next);
                        } else if (request.kind == ui::ContractActionKind::insure_player) {
                            const auto insured = contract_insurance(staged, manager, request.row,
                                request.term_column, request.insurance_months);
                            if (!insured.accepted) {
                                screen.reject("Die Versicherung konnte nicht übernommen werden.");
                            } else {
                                auto next = ui::contracts_from_game(staged, manager, ++local_revision);
                                next.demands = screen.state().demands;
                                next.quote_id = screen.state().quote_id;
                                next.quote_row = screen.state().quote_row;
                                screen.acknowledge_insurance(next);
                            }
                        } else {
                            throw FormatError("Unbekannte Vertragsaktion");
                        }
                        changed = true;
                    } catch (...) {
                        staged = old_game;
                        random = old_random;
                        throw;
                    }
                }
                if (screen.close_requested()) return renewed_expired_player ? 1 : 0;
                if (renewed_expired_player && !screen.result_page_open()) return 1;
                dirty_contracts |= changed;
            }
            // Keep audio streaming while idle, then yield the CPU briefly.
            if (playback.needs_pump()) playback.pump(audio_error);
            SDL_Delay(10);
        }
        staged = before;
        random = random_before;
        return -2;
    } catch (const std::exception&) {
        staged = before;
        random = random_before;
        return -2;
    }
}

// Reads the plain-background and text-gradient switches for the end-of-season dialogs.
auto App::game_end_options(const GameState& state) -> bmh::ui::GameEndRenderOptions {
    return ui::GameEndRenderOptions{
        state.memory.has(::bmh::layout::PlainBackground) && state.memory.u8(::bmh::layout::PlainBackground) != 0,
        (!state.memory.has(::bmh::layout::TextGradientDisabled) || state.memory.u8(::bmh::layout::TextGradientDisabled) == 0) && season_exe.u8(::bmh::layout::TextGradientMode) == 2};
}

// Blocks until a fresh left or right mouse press (pending mouse events are flushed first)
// or quit.
auto App::wait_season_ack() -> void {
    // Source SDLFlushEvents2/SDLFlushEvents flush pending mouse events, then wait for
    // a new left/right press. Physical keyboard ESC is not event8.
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

// Modal dialog for the season tax event (question, notice or outcome). Returns true if the
// player answered Yes. A button counts only when pressed and released on the same button.
auto App::show_tax(const GameState& state, const SeasonTaxEvent& event) -> bool {
    if (!running) return false;
    const size_t base = size_t(event.manager_slot) * 0xb0c;
    ui::TaxDialogProjection view;
    view.kind = event.kind == SeasonTaxEventKind::question
        ? ui::TaxDialogKind::question : ui::TaxDialogKind::acknowledgement;
    view.compact_dialog_frame = state.memory.has(::bmh::layout::CompactDialogFrame) && state.memory.u8(::bmh::layout::CompactDialogFrame) != 0;
    for (size_t i = 0; i < 30 && state.manager_records[base + 0xa8 + i]; ++i)
        view.manager_name += char(state.manager_records[base + 0xa8 + i]);
    view.body = event.text;
    view.portrait = state.manager_records[base + 0x221];
    view.badge_fallback = original_badge_fallback(event.team);
    view.badge_sprite = view.badge_fallback ? 0 : le16(state.club_heap_78, size_t(event.team) * 2) & 0x1ff;
    ui::GameEndTaxDialog screen(data, std::move(view), game_end_options(state),
        event.kind == SeasonTaxEventKind::outcome ? season_backdrop : std::optional<Image>{});
    auto complete = [&](bool answer) {
        present_season(screen.backdrop(), "OpenBMH - season_tax_restored");
        return answer;
    };
    present_season(screen.render(), event.kind == SeasonTaxEventKind::question
        ? "OpenBMH - season_tax_question" : event.kind == SeasonTaxEventKind::notice
        ? "OpenBMH - season_tax_notice" : "OpenBMH - season_tax_outcome");
    auto pressed = ui::GameEndTaxDialog::Button::none;
    // Modal sub-loop (own event polling).
    while (running) {
        SDL_Event input;
        while (quit_confirmation.poll(&input)) {
            if (input.type == SDL_QUIT) { running = false; return false; }
            if (input.type == SDL_MOUSEBUTTONDOWN) {
                if (event.kind != SeasonTaxEventKind::question &&
                    input.button.button == SDL_BUTTON_RIGHT) return complete(false);
                if (input.button.button != SDL_BUTTON_LEFT) continue;
                pressed = screen.hit_test(input.button.x, input.button.y);
                if (event.kind != SeasonTaxEventKind::question &&
                    pressed == ui::GameEndTaxDialog::Button::none) return complete(false);
            } else if (input.type == SDL_MOUSEMOTION &&
                screen.hit_test(input.motion.x, input.motion.y) != pressed) {
                pressed = ui::GameEndTaxDialog::Button::none;
            } else if (input.type == SDL_MOUSEBUTTONUP && input.button.button == SDL_BUTTON_LEFT) {
                const auto button = screen.hit_test(input.button.x, input.button.y);
                if (pressed != ui::GameEndTaxDialog::Button::none && button == pressed) {
                    if (button == ui::GameEndTaxDialog::Button::yes) return complete(true);
                    return complete(false);
                }
                pressed = ui::GameEndTaxDialog::Button::none;
            }
        }
        SDL_Delay(10);
    }
    return false;
}

// Waits without consuming gameplay input; only a quit request is handled.
auto App::wait_promotion_time(Uint32 milliseconds) -> void {
    const auto start = SDL_GetTicks();
    while (running && Uint32(SDL_GetTicks() - start) < milliseconds) {
        // SeasonPromotionScreen does not read gameplay input. Keep queued mouse/keyboard
        // events for the next Original input boundary; only handle SDL exit.
        SDL_PumpEvents();
        SDL_Event quit;
        if (quit_confirmation.peep(&quit, 1, SDL_GETEVENT, SDL_QUIT, SDL_QUIT) > 0) running = false;
        if (running) SDL_Delay(10);
    }
}

// End-of-game highscore presentation: an interactive calculator when the signature is 0,
// otherwise the table is updated on disk and shown when `display` is set; ends with the
// final screen.
auto App::show_highscore(const GameState& state, bool display) -> void {
    if (!running) return;
    const auto options = game_end_options(state);
    const uint16_t signature = state.memory.u16(::bmh::layout::CareerSession_FormatError_RuntimeField7);
    if (signature == 0) {
        // Source GameEndAlternateScoreScreen's alternate branch ignores display: both calls
        // open the calculator, followed by one presentation for each point.
        ui::GameEndAlternateScoreScreen screen(ui::load_game_end_alternate_score_assets(data),
            season_backdrop ? *season_backdrop : flow.render(), options.gradient_text);
        present_season(screen.render(), "OpenBMH - game_end_calculator");
        bool right_cancel = false;
        while (running && screen.phase() == ui::AlternateScorePhase::Calculator) {
            SDL_Event event;
            bool changed = false;
            while (quit_confirmation.poll(&event)) {
                if (event.type == SDL_QUIT) { running = false; return; }
                if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT) {
                    right_cancel = true;
                    changed |= screen.cancel(); // Source mouse cancellation event8
                } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                    const auto button = screen.button_at(event.button.x, event.button.y);
                    if (button && *button < 15) {
                        present_season(screen.render(button), "OpenBMH - game_end_calculator");
                        SDL_Delay(4 * 20); // Source DecodeTaxAnimationScript2(4), 50-Hz ticks
                    }
                    changed |= screen.mouse_down(event.button.x, event.button.y);
                } else if (event.type == SDL_MOUSEMOTION) {
                    changed |= screen.mouse_move(event.motion.x, event.motion.y);
                } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
                    changed |= screen.mouse_up();
                } else if (event.type == SDL_TEXTINPUT) {
                    changed |= screen.text(event.text.text);
                } else if (event.type == SDL_KEYDOWN) {
                    const auto key = event.key.keysym.sym;
                    if (key == SDLK_RETURN) changed |= screen.key("ENTER");
                    else if (key == SDLK_BACKSPACE) changed |= screen.key("BACKSPACE");
                    else if (key == SDLK_SPACE) changed |= screen.key("SPACE");
                }
                if (screen.phase() != ui::AlternateScorePhase::Calculator) break;
            }
            if (changed && screen.phase() == ui::AlternateScorePhase::Calculator)
                present_season(screen.render(), "OpenBMH - game_end_calculator");
            SDL_Delay(10);
        }
        if (!running) return;
        if (!screen.accepted_value()) {
            if (!right_cancel) SDL_Delay(50 * 20); // Source N-key wait; mouse cancel skips it
            present_season(screen.render(), "OpenBMH - game_end_calculator_cancelled");
        } else {
            while (running && screen.advance_point())
                present_season(screen.render(), "OpenBMH - game_end_points");
            screen.finish_points();
            present_season(screen.render(), "OpenBMH - game_end_points_ack");
            wait_season_ack();
        }
    } else {
        const auto file = highscore_file(save_directory, signature, season_texts[889]);
        auto scores = make_highscore_projection(state, load_highscore_table(file), HighscoreContext::SeasonBoundary);
        // The Original stores this table outside the native FBM. Use the
        // configured writable save base rather than a working-directory path.
        std::error_code directory_error;
        fs::create_directories(file.parent_path(), directory_error);
        if (directory_error || !save_highscore_table(file, scores.rows))
            std::fprintf(stderr, "highscore: cannot write %s\n", file.string().c_str());
        if (display) {
            ui::GameEndHighscoreScreen screen(data, std::move(scores), options);
            present_season(screen.render(), "OpenBMH - game_end_highscore");
            wait_season_ack();
        }
    }
    if (display && running) {
        ui::GameEndFinalScreen screen(data, options);
        present_season(screen.render(), "OpenBMH - game_end_final");
        wait_season_ack();
    }
}

// Callback at season-transition checkpoints; only the one for the German entrants does
// anything: it shows the entrant screens, each closed by a mouse release.
auto App::present_season_checkpoint(const char* checkpoint, const GameState& state) -> void {
        if (!running || std::strcmp(checkpoint, "the German entrants (CompetitionWinnerScreen)") != 0) return;
        // SeasonEntrantsFromGame / CompetitionWinnerScreen: show the three owned Original
        // projections while the transition is suspended at its checkpoint.
        // Other season animations remain separate implementation work.
        for (auto view : ui::season_entrants_from_game(state, season_exe, season_texts)) {
            ui::SeasonEntrantsScreen entrants_screen(data, std::move(view));
            SDL_SetWindowTitle(window, "OpenBMH - season_entrants");
            const Image image = entrants_screen.render();
            canvas.show(image, palette);
            bool dismissed = false;
            while (running && !dismissed) {
                SDL_Event event;
                while (quit_confirmation.poll(&event)) {
                    if (event.type == SDL_QUIT) { running = false; return; }
                    if (event.type == SDL_MOUSEBUTTONUP &&
                        (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT)) {
                        dismissed = true;
                        break;
                    }
                }
                if (!dismissed) SDL_Delay(10);
            }
        }
}


auto App::present_tax_event(const GameState& state, const SeasonTaxEvent& event) -> void {
    if (event.kind == SeasonTaxEventKind::notice || event.kind == SeasonTaxEventKind::outcome)
        (void)show_tax(state, event);
    if (running && event.kind == SeasonTaxEventKind::animation) {
        ui::GameEndTaxAnimationScreen animation(ui::load_game_end_tax_animation_assets(data),
            unsigned(event.animation_variant), season_backdrop ? *season_backdrop : flow.render(),
            state.memory.has(::bmh::layout::SolidPanelBackground) && state.memory.u8(::bmh::layout::SolidPanelBackground) != 0);
        while (running && animation.advance_step()) {
            present_season(animation.render(), "OpenBMH - season_tax_animation");
            const auto started = SDL_GetTicks();
            while (running && Uint32(SDL_GetTicks() - started) < animation.wait_milliseconds()) {
                SDL_Event input;
                while (quit_confirmation.poll(&input)) if (input.type == SDL_QUIT) running = false;
                // Source param4=0: clicks/keys cannot change this script or RNG.
                SDL_Delay(10);
            }
        }
        // Source marker150 flips buffers without copying the visible frame.
        // Outcome ACK must start on the older drawing frame in this case.
        season_backdrop = animation.drawing_frame();
    }
}


auto App::present_promotion(const GameState& state, const SeasonPromotionEvent& event) -> void {
    if (!running) return;
    auto view = ui::season_promotion_from_game(state, event, season_exe, season_texts);
    ui::SeasonPromotionScreen screen(data, std::move(view));
    const auto show_step = [&](int step) {
        const auto title = "OpenBMH - season_promotion_" + std::to_string(event.group) + "_" + std::to_string(step);
        present_season(screen.render(step), title.c_str());
    };
    show_step(0);
    // Original checks the manager's stored animation-skip flag once,
    // after presenting the starting table, before both 50-tick waits.
    if (state.memory.has(::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField) && state.memory.u8(::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField) != 0) return;
    wait_promotion_time(50 * 20);
    for (int step = 1; running && step <= 38; ++step) show_step(step);
    if (running) wait_promotion_time(50 * 20);
}


auto App::present_cup_draw(const GameState& state, const SeasonCupDrawEvent& event) -> void {
    if (!running) return;
    if (event.phase == SeasonCupDrawPhase::draw) {
        if (!event.group_stage || event.group_index == 0)
            group_reveal_skipped = false;
        if (event.group_stage && group_reveal_skipped) {
            // SDLFlushEvents still runs before the native overview.
            SDL_FlushEvents(SDL_MOUSEMOTION, SDL_MOUSEBUTTONUP);
            return;
        }
    }
    auto view = ui::cup_draw_from_game(state, event, season_exe, season_texts);
    if (!view.source_eligible) return;
    ui::CupDrawScreen screen(data, std::move(view));
    bool first_draw_frame = true;
    // Presents the current draw frame; the first frame or flagged steps flush queued mouse events.
    const auto show_draw = [&] {
        const auto title = "OpenBMH - cup_draw_" + std::to_string(screen.revealed_count());
        present_season(screen.render(), title.c_str());
        // DecodeTaxAnimationScript(1) flushes outside script execution (Main_Screen_RuntimeField2=0).
        // Script 0/1 presentations retain queued input (Main_Screen_RuntimeField2=1).
        if (first_draw_frame || screen.queue_flush_needed())
            SDL_FlushEvents(SDL_MOUSEMOTION, SDL_MOUSEBUTTONUP);
        first_draw_frame = false;
    };
    if (event.phase == SeasonCupDrawPhase::draw) {
        // Source ignores the first OriginalMouseSound poll's gameplay result, then
        // presents the starting scene before its ten option-delay polls.
        SDL_Event initial;
        if (quit_confirmation.poll(&initial) && initial.type == SDL_QUIT) { running = false; return; }
        show_draw();
        while (running && screen.advance_step()) {
            if (screen.present_needed()) show_draw();
            wait_promotion_time(Uint32(screen.option_delay_milliseconds()));
            if (!running) return;
            if (screen.input_poll_needed()) {
                SDL_Event input;
                while (quit_confirmation.poll(&input)) {
                    if (input.type == SDL_QUIT) { running = false; return; }
                    if (input.type == SDL_MOUSEBUTTONDOWN &&
                        (input.button.button == SDL_BUTTON_LEFT || input.button.button == SDL_BUTTON_RIGHT)) {
                        screen.source_input(input.button.button == SDL_BUTTON_LEFT ? 2 : 8);
                        break;
                    }
                }
                // OriginalMouseSound synthetic left press at its normal poll
                // boundary; missing transient blocks are not zero values.
                if ((state.memory.has(::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField) && state.memory.u8(::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField) != 0) ||
                    (state.memory.has(::bmh::layout::CareerSession_SportschauSourceOptions_ExecutableField) && state.memory.u8(::bmh::layout::CareerSession_SportschauSourceOptions_ExecutableField) == 0))
                    screen.source_input(2);
            }
            if (screen.held_mouse_check_needed()) {
                SDL_PumpEvents();
                const auto held = SDL_GetMouseState(nullptr, nullptr);
                screen.source_held_mouse(uint8_t((held & SDL_BUTTON_LMASK ? 1 : 0) |
                                               (held & SDL_BUTTON_RMASK ? 2 : 0)));
            }
            if (!screen.reveal_aborted()) wait_promotion_time(Uint32(screen.wait_milliseconds()));
        }
        if (running && screen.reveal_complete() && !screen.reveal_aborted()) {
            present_season(screen.render(), "OpenBMH - cup_draw_ack");
            wait_season_ack();
        }
        if (event.group_stage && screen.reveal_aborted())
            group_reveal_skipped = true;
        SDL_FlushEvents(SDL_MOUSEMOTION, SDL_MOUSEBUTTONUP); // SDLFlushEvents before native sort
        return;
    }
    present_season(screen.render(), "OpenBMH - cup_overview");
    SDL_FlushEvents(SDL_MOUSEMOTION, SDL_MOUSEBUTTONUP);
    while (running) {
        SDL_Event input;
        while (quit_confirmation.poll(&input)) {
            if (input.type == SDL_QUIT) { running = false; return; }
            if (input.type != SDL_MOUSEBUTTONDOWN) continue;
            if (input.button.button == SDL_BUTTON_RIGHT) return;
            if (input.button.button == SDL_BUTTON_LEFT) {
                const auto club = screen.hit_test_team(input.button.x, input.button.y);
                if (club) {
                    auto services=session->live_club_modal_services(revision);
                    (void)present_club_information(services,*club);
                    if (!running) return;
                    // Native category/squad edits belong to the clicked
                    // Source modal. Rebuild the overview from the current
                    // state while retaining its owned sorted round field.
                    auto current = ui::cup_draw_from_game(session->state(), event, season_exe, season_texts);
                    screen = ui::CupDrawScreen(data, std::move(current));
                    present_season(screen.render(), "OpenBMH - cup_overview");
                    SDL_FlushEvents(SDL_MOUSEMOTION, SDL_MOUSEBUTTONUP);
                }
            }
        }
        SDL_Delay(10);
    }
}
