// SPDX-License-Identifier: GPL-3.0-only
// App: handle() - one request through the controller - and run() - wiring, title screen, event loop, tear-down.
// (moved out of main.cpp unchanged; the class is declared in app/app.hpp)
#include "app/app.hpp"

// =====================================================================================
// Handling FlowResults (requests from the UI flow)
// =====================================================================================
// Called by the main loop for every ui::FlowResult. It handles quitting and the process
// timer, then passes the request to app::serve_flow_request() together with the
// presenter lambdas defined inside, which the engine may call back synchronously.
auto App::handle(const ui::FlowResult& r) -> void {
    pitch_pointer.show({}, renderer);
    // A quit request must be confirmed by the player; otherwise the desk reports it as cancelled.
    if (r.request == ui::FlowRequest::end_session && !quit_confirmation.confirm()) {
        flow.reject_desk_action(english_game ? "Quit cancelled." : "Beenden abgebrochen.");
        dirty = true;
        return;
    }
    // Play the sounds the desk queued during this action.
    if (const auto* desk = flow.desktop())
        for (const auto id : desk->take_sound_ids()) app::original_audio_command(playback, id);
    // Process timer: kickoff and quitting stop it; opening the menus 10, 11, 19 and 29
    // (save/load, settings, new manager) pauses it.
    if (r.request == ui::FlowRequest::kickoff || r.request == ui::FlowRequest::end_session) {
        // DeskDate clears CareerSession_DispatchStrengths_ExecutableField2 before returning to the match caller.
        process_timer.stop(); process_timer_owner.reset(); process_timer_frame.reset();
    } else if (r.desk_action.kind == ui::DeskRequestKind::open_menu &&
               (r.desk_action.menu_kind == 10 || r.desk_action.menu_kind == 11 ||
                r.desk_action.menu_kind == 19 || r.desk_action.menu_kind == 29)) {
        // ApplySettingsFile suspends save/load, settings and new-manager waits.
        process_timer.pause(process_source_seconds()); process_timer_frame.reset();
    }
    // Confirmed quit: end the program at once, without advancing the calendar or saving.
    if (r.request == ui::FlowRequest::end_session) {
        // DeskDate's confirmed socket exit tears down the running
        // session without a calendar turn, RNG draw, or implicit save.
        running = false;
        dirty = false;
        return;
    }
    dirty |= r.changed;
    live_skipped = false;   // (a member now; fresh for every request like the former local)
    live_overview_sequence = 0;   // (a member now; fresh for every request like the former local)
    outcome_backdrop = std::optional<Image>{};   // (a member now; fresh for every request like the former local)
    // State used by the live-match presenters below.
    live_score_reveal = ui::LiveScoreReveal{};   // (a member now; fresh for every request like the former local)
    live_chance_context = std::optional<ui::LiveChanceProjection>{};   // (a member now; fresh for every request like the former local)
    reality_field.reset();   // (a member now; fresh for every request like the former local)
    reality_layout = ui::RealityReporterLayoutState{};   // (a member now; fresh for every request like the former local)
    reality_pristine = Image{}; reality_surface = Image{};   // (a member now; fresh for every request like the former local)
    reality_colors = Palette{};   // (a member now; fresh for every request like the former local)
    // Hand the flow result to the controller. The arguments after the audio ones are the
    // presenter callbacks the engine may call synchronously (live frame, incoming transfers,
    // transfer decisions, season checkpoints, tax, highscore, promotion, cup draw, offers,
    // club/player screens, notices, poaching, contracts, ...). The returned message is
    // printed to stderr by the caller; "career_complete" ends the program.
    const std::string message = app::serve_flow_request(flow, r, session, data, save_directory, seed, revision,
        playback, audio_session, music_enabled,
        [this](auto&&... a) -> decltype(auto) { return present_live_minute(std::forward<decltype(a)>(a)...); },
        [this](auto&&... a) -> decltype(auto) { return choose_transfer_offer(std::forward<decltype(a)>(a)...); },
        [this](auto&&... a) -> decltype(auto) { return present_transfer_decision(std::forward<decltype(a)>(a)...); },
        [this](auto&&... a) -> decltype(auto) { return present_season_checkpoint(std::forward<decltype(a)>(a)...); },
        // Further callbacks: tax, highscore, promotion and cup draw presenters (defined above).
        tax_presenter, [this](auto&&... a) -> decltype(auto) { return show_highscore(std::forward<decltype(a)>(a)...); }, promotion_presenter, cup_draw_presenter,
        // Pending-offer hooks exist only for kickoff; market sale hooks for kickoff and for
        // transfer market / outgoing transfer submenu actions.
        r.request == ui::FlowRequest::kickoff
            ? make_pending_offer_hooks(flow.render()) : PendingOfferHooks{},
        app::PendingOfferEntryPolicy::normal_interactive,
        (r.request == ui::FlowRequest::kickoff || (r.request == ui::FlowRequest::submenu_action &&
            (std::holds_alternative<ui::TransferMarketRequest>(r.submenu_action) ||
             std::holds_alternative<ui::OutgoingTransferRequest>(r.submenu_action))
           )) ? make_market_sale_hooks(flow.render()) : HumanBuyerSaleHooks{},
        app::HumanBuyerMarketEntryPolicy::normal_interactive,
        [this](auto&&... a) -> decltype(auto) { return show_club_information(std::forward<decltype(a)>(a)...); },
        [this](auto&&... a) -> decltype(auto) { return show_player_detail(std::forward<decltype(a)>(a)...); },
        [this](auto&&... a) -> decltype(auto) { return present_construction_notice(std::forward<decltype(a)>(a)...); },
        [this](auto&&... a) -> decltype(auto) { return present_ticket_compensation_notice(std::forward<decltype(a)>(a)...); }, sportschau_presenter,
        [this](auto&&... a) -> decltype(auto) { return ask_advertising_renewal(std::forward<decltype(a)>(a)...); }, poach_player_selector,
        [this](auto&&... a) -> decltype(auto) { return present_poach_notices(std::forward<decltype(a)>(a)...); }, [this](auto&&... a) -> decltype(auto) { return contract_expiry_presenter(std::forward<decltype(a)>(a)...); },
        [this](auto&&... a) -> decltype(auto) { return ask_national_friendly(std::forward<decltype(a)>(a)...); },
        // The loan-return presenters (defined above), then the live match observer.
        [this](auto&&... a) -> decltype(auto) { return loan_return_presenter(std::forward<decltype(a)>(a)...); }, [this](auto&&... a) -> decltype(auto) { return loan_return_offer_presenter(std::forward<decltype(a)>(a)...); },
        [this](auto&&... a) -> decltype(auto) { return observe_live_minute(std::forward<decltype(a)>(a)...); },
        [this](auto&&... a) -> decltype(auto) { return present_environment_notice(std::forward<decltype(a)>(a)...); },
        [this](auto&&... a) -> decltype(auto) { return present_live_event_lineup(std::forward<decltype(a)>(a)...); },
        [this](auto&&... a) -> decltype(auto) { return animate_ball(std::forward<decltype(a)>(a)...); },
        [this](auto&&... a) -> decltype(auto) { return animate_chance(std::forward<decltype(a)>(a)...); },
        [this](auto&&... a) -> decltype(auto) { return animate_shot(std::forward<decltype(a)>(a)...); },
        [this](auto&&... a) -> decltype(auto) { return animate_set_piece(std::forward<decltype(a)>(a)...); },
        [this](auto&&... a) -> decltype(auto) { return animate_booking(std::forward<decltype(a)>(a)...); },
        [this](auto&&... a) -> decltype(auto) { return animate_shootout(std::forward<decltype(a)>(a)...); },
        [this](auto&&... a) -> decltype(auto) { return present_reality(std::forward<decltype(a)>(a)...); },
        [this](auto&&... a) -> decltype(auto) { return present_phase_result(std::forward<decltype(a)>(a)...); }, true);
        // In -debug mode every request and its outcome is logged.
        if (app::debug::enabled()) app::debug::log("request " + std::to_string(int(r.request)) + (message.empty() ? std::string(" ok") : " -> " + message));
    // The controller reports the end of the career by this message: leave the main loop.
    if (message == "career_complete") { running = false; return; }
    if (!message.empty()) std::fprintf(stderr, "engine: %s\n", message.c_str());
    // Autosave problems are reported but do not stop the game.
    if (session) {
        const auto autosave_error = session->take_autosave_error();
        if (!autosave_error.empty()) std::fprintf(stderr, "autosave: %s\n", autosave_error.c_str());
    }
    // Table requests own their redraw boundary. In particular a
    // same-league map ACK and a forecast Present ACK retain the frame.
    if (!flow.in_family_table() || !message.empty()) dirty = true;
    // DeskDate checks the pending mailbox latch after the normal desk
    // request has completed. Poll only on the returned, idle desk so
    // load/weekly/close acknowledgements can open the Source notice
    // immediately without interrupting another modal or read-only view.
    if (running && session && flow.desktop() && !flow.desktop()->pending() &&
        !flow.in_preparation_menu() && !flow.in_results_view() &&
        !flow.in_readonly_view() && !flow.in_mailbox() && flow.screen_name() == "desktop") {
        if (auto mailbox = session->pending_mailbox_notification(revision)) {
            flow.show_notified_mailbox(std::move(*mailbox));
            dirty = true;
        }
    }
    // Forget the presentation time once no timed notice is open.
    if(!flow.timed_notice_open())timed_notice_presented_at.reset();
    SDL_SetWindowTitle(window, ("OpenBMH - " + flow.screen_name()).c_str());
}

// The statements of the former main(): hook wiring, title screen, event loop and tear-down.
auto App::run() -> int {
    sportschau_presenter.intro = [this](auto&&... a) -> decltype(auto) { return present_sportschau_intro(std::forward<decltype(a)>(a)...); };
    sportschau_presenter.kickoff = [this](auto&&... a) -> decltype(auto) { return present_matchday_intro(std::forward<decltype(a)>(a)...); };
    sportschau_presenter.replay.step = [this](auto&&... a) -> decltype(auto) { return present_sportschau_replay_step(std::forward<decltype(a)>(a)...); };
    sportschau_presenter.replay.scene = [this](auto&&... a) -> decltype(auto) { return present_sportschau_scene(std::forward<decltype(a)>(a)...); };
    sportschau_presenter.fixture = [this](auto&&... a) -> decltype(auto) { return present_sportschau_fixture(std::forward<decltype(a)>(a)...); };
    sportschau_presenter.winner = [this](auto&&... a) -> decltype(auto) { return present_competition_winner(std::forward<decltype(a)>(a)...); };
    sportschau_presenter.awards = [this](auto&&... a) -> decltype(auto) { return present_sportschau_awards(std::forward<decltype(a)>(a)...); };
    sportschau_presenter.team_info = [this](auto&&... a) -> decltype(auto) { return present_sportschau_team_info(std::forward<decltype(a)>(a)...); };

    tax_presenter.choose = [this](auto&&... a) -> decltype(auto) { return show_tax(std::forward<decltype(a)>(a)...); };
    tax_presenter.observe = [this](auto&&... a) -> decltype(auto) { return present_tax_event(std::forward<decltype(a)>(a)...); };
    promotion_presenter = [this](auto&&... a) -> decltype(auto) { return present_promotion(std::forward<decltype(a)>(a)...); };
    present_club_information = [this](auto&&... a) -> decltype(auto) { return club_information_dialog(std::forward<decltype(a)>(a)...); };
    present_club_player = [this](auto&&... a) -> decltype(auto) { return club_player_dialog(std::forward<decltype(a)>(a)...); };
    present_player_detail = [this](auto&&... a) -> decltype(auto) { return player_detail_dialog(std::forward<decltype(a)>(a)...); };
    present_club_statistics = [this](auto&&... a) -> decltype(auto) { return club_statistics_dialog(std::forward<decltype(a)>(a)...); };
    poach_player_selector = [this](auto&&... a) -> decltype(auto) { return select_poach_player(std::forward<decltype(a)>(a)...); };
    cup_draw_presenter = [this](auto&&... a) -> decltype(auto) { return present_cup_draw(std::forward<decltype(a)>(a)...); };

    // =====================================================================================
    // Opening screen
    // =====================================================================================
    // OpenBMH deviates from the original: display the repository-owned title
    // at its native resolution until a complete left/right click, without loading balls.
    {
        app::StartScreen title_screen(renderer);
        title_screen.present();
        bool opening_confirmed = false;
        std::optional<Uint8> opening_press;
        // Only a complete click confirms: press and release of the same button. Losing focus
        // forgets the press; expose and resize events redraw the title picture.
        while (running && !opening_confirmed) {
            SDL_Event opening_event;
            while (quit_confirmation.poll(&opening_event)) {
                if (opening_event.type == SDL_QUIT) running = false;
                if (opening_event.type == SDL_KEYDOWN && opening_event.key.keysym.sym == SDLK_ESCAPE &&
                    quit_confirmation.confirm()) running = false;
                if (opening_event.type == SDL_WINDOWEVENT &&
                    (opening_event.window.event == SDL_WINDOWEVENT_EXPOSED ||
                     opening_event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED))
                    title_screen.present();
                if (opening_event.type == SDL_WINDOWEVENT &&
                    opening_event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
                    opening_press.reset();
                if (opening_event.type == SDL_MOUSEBUTTONDOWN &&
                    (opening_event.button.button == SDL_BUTTON_LEFT ||
                     opening_event.button.button == SDL_BUTTON_RIGHT))
                    opening_press = opening_event.button.button;
                if (opening_event.type == SDL_MOUSEBUTTONUP && opening_press &&
                    opening_event.button.button == *opening_press)
                    opening_confirmed = true;
            }
            SDL_Delay(10);
        }
    }
 // Restore the indexed game canvas and its logical input mapping.
    // Opening input must not also change an option on the first setup frame.
    SDL_FlushEvents(SDL_KEYDOWN, SDL_KEYUP);

    SDL_FlushEvents(SDL_MOUSEMOTION, SDL_MOUSEBUTTONUP);

    season_backdrop.reset();

    dirty = true;

    // Text input stays on for the whole session so SDL_TEXTINPUT events arrive.
    SDL_StartTextInput();

    // =====================================================================================
    // Main event loop
    // =====================================================================================
    // Each pass: keep the filename field focused on the save/load screen, expire timed
    // notices, service the timers, then drain the SDL event queue through
    // quit_confirmation.poll(). SDL_QUIT ends the loop. Mouse down, up and motion, text and
    // key events are translated into StartupFlow calls (pointer_down, click or hold,
    // move_action, text, send_key); every FlowResult is passed to handle(), which serves
    // the request. After the queue is empty the loop services holds and timers again, plays
    // queued sounds, pumps audio, redraws the 320x240 canvas if dirty, presents it and
    // sleeps 10 ms.
    while (running) {
        const bool filename_open = flow.screen_name() == "save_load";
        if (filename_open && !filename_input_active) focus_filename_input(renderer);
        filename_input_active = filename_open;
        // A timed notice closes by itself after 70*3*20 ms.
        if(timed_notice_presented_at && flow.timed_notice_open() &&
           SDL_GetTicks64()-*timed_notice_presented_at>=70*3*20)
            handle(flow.expire_timed_notice());
        service_process_timer();
        if (!running) break;
        SDL_Event e;
        while (quit_confirmation.poll(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
                break;
            }
            // Losing focus forgets the pressed setup buttons; regaining it restores text input.
            if (e.type == SDL_WINDOWEVENT) {
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) setup_button_down.fill(false);
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                    if (flow.screen_name() == "save_load") focus_filename_input(renderer);
                    else SDL_StartTextInput();
                }
            }
            // A native picker/window activation can leave an unmatched UP.
            // Only a press which began on setup may toggle its options.
            if ((e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) &&
                (e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT)) {
                const size_t button = e.button.button == SDL_BUTTON_LEFT ? 0 : 1;
                if (e.type == SDL_MOUSEBUTTONDOWN) setup_button_down[button] = flow.screen_name() == "setup";
                else {
                    const bool owned = setup_button_down[button];
                    setup_button_down[button] = false;
                    if (flow.screen_name() == "setup" && !owned) continue;
                }
            }
            // Swallow the release of the press that dismissed a timed notice.
            if(e.type==SDL_MOUSEBUTTONUP&&timed_notice_release&&e.button.button==*timed_notice_release) {
                timed_notice_release.reset();continue;
            }
            if(flow.timed_notice_open()&&e.type==SDL_MOUSEBUTTONDOWN&&
               (e.button.button==SDL_BUTTON_LEFT||e.button.button==SDL_BUTTON_RIGHT)) {
                // PresentManagerDialog consumes DOWN. Do not deliver its release to
                // the restored caller as a second booking/purchase.
                timed_notice_release=e.button.button;press.reset();
                handle(flow.expire_timed_notice());continue;
            }
            // Keyboard input is ignored while a timed notice is open.
            if(flow.timed_notice_open()&&(e.type==SDL_KEYDOWN||e.type==SDL_TEXTINPUT))continue;
            // InLeaguePairings handles a changed league selector before5147b's
            // timer. Preserve that precedence without moving other inputs.
            bool selector_consumed = false;
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                flow.in_league_pairings()) {
                const auto selection = flow.league_pairings()->selector_event(e.button.x,e.button.y);
                if (selection && (selection->kind == LeaguePairingsAction::toggle_group ||
                    selection->value != flow.league_pairings()->state().source.owner.league)) {
                    handle(flow.pointer_down(e.button.x,e.button.y));
                    present_pairings_redraw();
                    selector_consumed = true;
                }
            }
            // Other polled input keeps the Original timer precedence;
            // held controls remain blocked by release ownership.
            service_process_timer();
            service_pairings_timer();
            service_balance_timer();
            service_family_table();
            service_stadium_calculator();
            service_idle_stadium_deadline();
            // Keys for the table range dialog or toto calculator are queued, not dispatched at once.
            if (queue_table_range_event(e)) {
                service_family_table();
                continue;
            }
            // While the stadium calculator is in a timed wait, its keys are queued and its release
            // is remembered; no other input is processed (release barrier).
            if (flow.in_stadium() && flow.stadium()->calculator_source_waiting()) {
                // DecodeTaxAnimationScript2 delays without consuming keyboard input. Retain
                // FIFO keys for this accepted calculator owner, including
                // every digit of one SDL text-input packet.
                queue_calculator_event(e);
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT &&
                    flow.stadium()->calculator_waiting_for_key_release() && !calculator_release) {
                    const auto& owner = flow.stadium()->state();
                    calculator_release = CalculatorRelease{e.button, owner.scope, owner.sequence};
                }
                // The Source calculator helper owns input until its wait
                // completes. SDL_QUIT was serviced above this barrier.
                continue;
            }
            // The open calculator owns all keyboard input.
            if (flow.in_stadium() && flow.stadium()->calculator_open() && queue_calculator_event(e)) {
                service_stadium_calculator();
                continue;
            }
            // ESC asks to quit (after confirmation) unless it is a close key in a menu or view.
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE &&
                !flow.in_preparation_menu() && !flow.in_results_view() && !flow.in_readonly_view() &&
                quit_confirmation.confirm())
                running = false;
            // Remember the left press so its release can be judged as a click or a 2 second hold.
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
                press = Press{e.button.x, e.button.y, SDL_GetTicks(), flow.screen_name(),
                              ui::pointer_hold_source_ticks(SDL_GetTicks64()), revision};
            // Remember where the right press began, to detect releases without a matching press.
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT)
                right_press_screen = flow.screen_name();
            // Buttons without a lasting state look pressed while the mouse button is held (bank, offer editor,
            // lineup, pitch). The pitch runs its action at the press itself, so the pressed look is held briefly.
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                flow.press_hold(e.button.x, e.button.y)) {
                const auto image = process_timer_image(flow.render());
                canvas.show(image, palette);
                dirty = false;
                if (flow.in_pitch()) SDL_Delay(90);
            }
            // Screens that react to the press itself get pointer_down() now.
            if(!selector_consumed&&e.type==SDL_MOUSEBUTTONDOWN&&(flow.desktop()||flow.in_balance()||flow.in_desk_records()||flow.in_achievements()||flow.in_indoor_standings()||flow.in_reschedule_calendar()||flow.in_league_pairings()||flow.in_family_table()||flow.in_toto_calculator()||flow.in_stadium()||flow.in_environment()||flow.in_strengths()||flow.in_top_scorers()||flow.in_opportunities()||flow.in_statistics_menu()||flow.in_statistics_page()||flow.in_analytics()||flow.in_other_games()||flow.in_pitch()||flow.in_settings())&&
                (e.button.button==SDL_BUTTON_LEFT||e.button.button==SDL_BUTTON_RIGHT)) {
                handle(flow.pointer_down(e.button.x,e.button.y,e.button.button==SDL_BUTTON_RIGHT));
                if (!running) break;
                if(flow.in_family_table()) {
                    // This path also owns pressed KB6 frames. Upload and
                    // copy must succeed before their Present starts a wait
                    // or commits a staged forecast random stream.
                    present_family_table_redraw();
                // Screens with held controls are redrawn at once, so the pressed state is visible before
                // the release arrives.
                } else if(flow.in_balance()||flow.in_desk_records()||flow.in_achievements()||flow.in_indoor_standings()||flow.in_reschedule_calendar()||flow.in_league_pairings()||flow.in_stadium()||flow.in_environment()||flow.in_strengths()||flow.in_top_scorers()||flow.in_opportunities()||flow.in_statistics_menu()||flow.in_statistics_page()||flow.in_analytics()||flow.in_other_games()||flow.in_pitch()||flow.in_settings()||flow.friendly_question_screen()) {
                    //48529/ExpireQuestion/67e80 present the held control before waiting
                    //for its release. The existing UP ownership remains intact.
                    const auto image=process_timer_image(flow.render());
                    for(size_t i=0;i<pixels.size();++i)pixels[i]=palette.argb[image.pixels[i]];
                    if (SDL_UpdateTexture(texture,nullptr,pixels.data(),320*4)!=0 ||
                        SDL_RenderClear(renderer)!=0 || SDL_RenderCopy(renderer,texture,nullptr,nullptr)!=0)
                        throw std::runtime_error(std::string("Held controls renderer: ")+SDL_GetError());
                    app::present(renderer);
                    mark_stadium_question_presented();
                    mark_national_picker_presented();
                    mark_friendly_picker_presented();
                    mark_friendly_calendar_presented();
                    mark_friendly_question_presented();
                    if(flow.in_balance())flow.mark_balance_presented_source_milliseconds(balance_source_milliseconds());
                    mark_stadium_calculator_presented();
                    dirty=false;
                } else if (flow.desktop() && flow.desktop()->socket_confirmation_armed()) {
                    // SDLSetWindowTitle paints its pressed socket sprite before
                    // DeskDate waits for the physical release.
                    SDL_SetWindowTitle(window, "OpenBMH - desktop");
                    const auto image=process_timer_image(flow.render());
                    canvas.show(image, palette);
                    dirty=false;
                }
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT &&
                flow.file_info_press(e.button.x, e.button.y)) {
                // InfoPress→Centered paints immediately and waits for mouse UP.
                // Present before polling a queued release, which restores the list.
                const auto image = process_timer_image(flow.render());
                canvas.show(image, palette);
                dirty = false;
            }
            // Mouse release. Order: an expired question that owns this release, then right-button
            // releases whose press was consumed elsewhere (orphaned releases), then click or hold.
            if (e.type == SDL_MOUSEBUTTONUP && (e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT)) {
                if(e.button.button==SDL_BUTTON_LEFT&&service_friendly_question_deadline()) {
                    press.reset();continue; // The expired question owns this UP, not its returned calendar/desk.
                }
                if (e.button.button == SDL_BUTTON_LEFT && service_stadium_deadline()) {
                    // The release belongs to the expired question, never
                    // to the newly accepted stadium scene underneath it.
                    press.reset();
                    continue;
                }
                if (e.button.button == SDL_BUTTON_RIGHT) {
                    // ClubInfoFromGame consumes the right press and clears CareerSession_BeginFriendlyOpponentTable_ExecutableField2
                    // before returning. Its release is not another Source8
                    // cancel event in the parent OpenTransferEditor input loop.
                    // owned: the right press began on this same screen.
                    const bool owned = right_press_screen && *right_press_screen == flow.screen_name();
                    const bool source_wait_release = flow.source_mouse_wait_release_pending();
                    // Menu6's right DOWN closes1e06b immediately. Its real
                    // UP must still consume the existing national barrier.
                    const bool national_picker_release = right_press_screen &&
                        *right_press_screen == "national_friendly_picker";
                    right_press_screen.reset();
                    if (!owned && flow.settings_readme_release_pending()) {
                        handle(flow.click(e.button.x, e.button.y, true));
                        press.reset();
                        continue;
                    }
                    if (!owned && (source_wait_release || national_picker_release)) {
                        handle(flow.click(e.button.x, e.button.y, true));
                        press.reset();
                        continue;
                    }
                    if (!owned) { flow.consume_orphaned_pitch_release(); continue; }
                }
                // A left release is a hold (2000 ms or more, same position, same screen) only on screens
                // that support it; everywhere else it is a click.
                const bool held = e.button.button == SDL_BUTTON_LEFT && !flow.in_pitch() && !flow.friendly_question_screen() && !flow.friendly_question_release_pending() && !flow.in_family_table() && !flow.in_balance() && !flow.in_desk_records() && !flow.in_achievements() && !flow.in_indoor_standings() && !flow.in_reschedule_calendar() && !flow.in_top_scorers() && !flow.in_opportunities() && !flow.in_statistics_menu() && !flow.in_statistics_page() && !flow.in_analytics() && !flow.in_other_games() && press && e.button.x == press->x && e.button.y == press->y &&
                                  press->screen == flow.screen_name() && SDL_GetTicks() - press->time >= 2000;
                const auto released = held ? flow.hold(e.button.x, e.button.y, 2000)
                    // clicks >= 2 reports a double click.
                    : flow.click(e.button.x, e.button.y, e.button.button == SDL_BUTTON_RIGHT, e.button.clicks >= 2);
                present_formation_save_feedback();
                // JA of the manager confirmation is shown pushed in briefly before the
                // (possibly slow) next step starts, as in the original.
                if (flow.screen_name() == "manager_accepted") {
                    present_season(flow.render(), "OpenBMH - manager_accepted");
                    SDL_Delay(150);
                }
                handle(released);
                present_desk_record_points();
                press.reset();
            }
            // Moving away from the pressed position cancels the hold candidate.
            if (e.type == SDL_MOUSEMOTION) {
                if (press && (e.motion.x != press->x || e.motion.y != press->y)) press.reset();
                const auto moved = flow.move_action(e.motion.x, e.motion.y);
                if (moved.request != ui::FlowRequest::none) handle(moved);
                else dirty |= moved.changed;
                // BalanceFromGame presents a drag move before sampling the next
                // keyboard/mouse event, even when both are already queued.
                present_family_table_redraw();
            }
            // Typed characters go to the flow; a FormatError only produces a message.
            if (e.type == SDL_TEXTINPUT) {
                try {
                    dirty |= flow.text(e.text.text);
                    present_desk_record_points();
                } catch (const FormatError& err) {
                    std::fprintf(stderr, "input: %s\n", err.what());
                }
            }
            // Ctrl/Cmd+A selects all in the filename field.
            if (e.type == SDL_KEYDOWN && flow.screen_name() == "save_load" &&
                e.key.keysym.sym == SDLK_a && (e.key.keysym.mod & (KMOD_CTRL | KMOD_GUI))) {
                handle(flow.send_key("SELECT_ALL"));
                continue;
            }
            // Only keys the current screen type uses are forwarded to the flow (by name); on the
            // desktop the letters A-Z are shortcuts.
            if (e.type == SDL_KEYDOWN && (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_BACKSPACE ||
                                          (flow.screen_name() == "save_load" && (e.key.keysym.sym == SDLK_UP ||
                                           e.key.keysym.sym == SDLK_DOWN || e.key.keysym.sym == SDLK_PAGEUP || e.key.keysym.sym == SDLK_PAGEDOWN)) ||
                                          ((flow.in_preparation_menu() || flow.in_results_view() || flow.in_readonly_view()) && e.key.keysym.sym == SDLK_ESCAPE) ||
                                          ((flow.in_results_view() || flow.in_readonly_view() || flow.screen_name() == "friendly_match") && (e.key.keysym.sym == SDLK_LEFT ||
                                           e.key.keysym.sym == SDLK_RIGHT || e.key.keysym.sym == SDLK_SPACE)) ||
                                          ((flow.in_balance() || flow.in_stadium()) && e.key.keysym.sym == SDLK_SPACE) ||
                                          (flow.desktop() && !flow.in_balance() && e.key.keysym.sym >= SDLK_a && e.key.keysym.sym <= SDLK_z))) {
                const std::string key = e.key.keysym.sym == SDLK_RETURN      ? "ENTER"
                                        : e.key.keysym.sym == SDLK_BACKSPACE ? "BACKSPACE"
                                        : e.key.keysym.sym == SDLK_ESCAPE    ? "ESC"
                                        : e.key.keysym.sym == SDLK_LEFT      ? "LEFT"
                                        : e.key.keysym.sym == SDLK_RIGHT     ? "RIGHT"
                                        : e.key.keysym.sym == SDLK_SPACE     ? "SPACE"
                                        : e.key.keysym.sym == SDLK_UP        ? "UP"
                                        : e.key.keysym.sym == SDLK_DOWN      ? "DOWN"
                                        : e.key.keysym.sym == SDLK_PAGEUP    ? "PAGEUP"
                                        : e.key.keysym.sym == SDLK_PAGEDOWN  ? "PAGEDOWN"
                                                                             : std::string(1, char('A' + e.key.keysym.sym - SDLK_a));
                handle(flow.send_key(key));
                present_desk_record_points();
            }
            if (!running) break;
        }
        // Queue drained: service holds, timers and sounds, then draw.
        if (!running) break;
        service_pitch_hold();
        if (!running) break;
        service_process_timer();
        service_pairings_timer();
        service_balance_timer();
        service_family_table();
        service_stadium_calculator();
        service_idle_stadium_deadline();
        if (const auto* desk = flow.desktop())
            for (const auto id : desk->take_sound_ids()) app::original_audio_command(playback, id);
        // Keep the audio options in step with the game's settings.
        if (session) playback.set_original_options(app::original_audio_options(session->state(), music_enabled));
        if (playback.needs_pump()) {
            playback.pump(audio_error);
            if (!audio_error.empty()) std::fprintf(stderr, "audio: %s\n", audio_error.c_str());
        }
        // Present the frame. Tables redraw through their own path so that their acknowledgement
        // is tied to a freshly drawn frame.
        if (flow.in_family_table()) {
            // Family ACKs belong to a freshly rendered, successfully
            // copied image. Do not ACK an old texture at the routine
            // end-of-loop Present after an earlier ACK changed the Flow.
            present_family_table_redraw();
        } else {
            // A preceding service Present can request the calculator's
            // restored-underlay frame. This fresh end-of-loop frame owns
            // its own ACK; an unchanged texture never advances the modal.
            const bool toto_calculator_redraw = dirty && flow.in_toto_calculator();
            const bool stadium_redraw = dirty && flow.in_stadium();
            const bool national_picker_redraw = dirty && national_picker_frame().has_value();
            const bool friendly_picker_redraw = dirty && friendly_picker_frame().has_value();
            const bool friendly_calendar_redraw = dirty && friendly_calendar_frame().has_value();
            const bool friendly_question_redraw = dirty && friendly_question_frame().has_value();
            const bool source_modal_redraw = toto_calculator_redraw || stadium_redraw ||
                national_picker_redraw || friendly_picker_redraw || friendly_calendar_redraw || friendly_question_redraw;
            if (dirty) {
                const auto image = process_timer_image(flow.render());
                for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = palette.argb[image.pixels[i]];
                if (SDL_UpdateTexture(texture, nullptr, pixels.data(), 320 * 4) != 0 && source_modal_redraw)
                    throw std::runtime_error(std::string("Source modal texture: ") + SDL_GetError());
                dirty = false;
            }
            // Update the custom pitch cursor, then present the canvas texture.
            pitch_pointer.show(flow.pitch_pointer_cursor(palette), renderer);
            const int cleared = SDL_RenderClear(renderer);
            const int copied = SDL_RenderCopy(renderer, texture, nullptr, nullptr);
            if (source_modal_redraw && (cleared != 0 || copied != 0))
                throw std::runtime_error(std::string("Source modal renderer: ") + SDL_GetError());
            app::present(renderer);
            if(flow.timed_notice_open()&&!timed_notice_presented_at)
                timed_notice_presented_at=SDL_GetTicks64();
            if (stadium_redraw) mark_stadium_question_presented();
            if (national_picker_redraw) mark_national_picker_presented();
            if (friendly_picker_redraw) mark_friendly_picker_presented();
            if (friendly_calendar_redraw) mark_friendly_calendar_presented();
            if (friendly_question_redraw) mark_friendly_question_presented();
            if (toto_calculator_redraw) mark_family_table_presented();
            mark_stadium_calculator_presented();
            if(flow.in_balance()) {
                const auto identity=flow.balance()->state().identity;
                const bool notice=flow.balance()->notice_open();
                flow.mark_balance_presented_source_milliseconds(balance_source_milliseconds());
                presented_balance=identity;presented_balance_notice=notice;
            }
        }
        // Limit the loop to roughly 100 passes per second.
        SDL_Delay(10);
    }

    // =====================================================================================
    // Shutdown
    // =====================================================================================
    // Stop text input, release the SDL objects, detach the mouse sound and close audio.
    SDL_StopTextInput();

    pitch_pointer.clear();

    SDL_DestroyTexture(texture);

    SDL_DestroyRenderer(renderer);

    SDL_DestroyWindow(window);

    mouse_sound.detach();
 // Remove event producers before closing the audio device.
    playback.shutdown();

    return 0;
}
