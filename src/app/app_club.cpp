// SPDX-License-Identifier: GPL-3.0-only
// App: the club dialogs (club information, player list, player detail, statistics).
// (moved out of main.cpp unchanged; the class is declared in app/app.hpp)
#include "app/app.hpp"

// Callback: club information screen (true if the program is still running).
auto App::show_club_information(app::ClubModalServices& services,int team,const Image&) -> bool {
        return present_club_information(services,team) && running;
}

// Next callback: player detail card.
auto App::show_player_detail(app::ClubModalServices& services,const ClubPlayerDetailSelection& selection,const Image& parent) -> bool {
        // Direct1b556 already owns persistent bbcde=1. The shared
        //ClubPlayerDetailFromGame presenter never restores a temporary club mode here.
        return present_player_detail(services,selection,parent).has_value() && running;
}

// Modal club information screen for `team`. Returns true when it was left normally (or
// the process timer expired), false when the program is quitting.
auto App::club_information_dialog(app::ClubModalServices& services, int team) -> bool {
    if (!running) return false;
    //ClubInfoFromGame owns the opening DOWN and consumes its paired UP inside
    // this synchronous loop. The parent must not retain a held press
    // after that UP has already been consumed by the child.
    // The child loop consumes the matching release itself.
    press.reset();
    ui::ClubInfoView selected_view = ui::ClubInfoView::data_a;
    std::unique_ptr<ui::ClubInfoScreen> club_screen;
    // Rebuilds the club screen from the current game state.
    const auto redraw = [&] {
        std::string error;
        ClubInfoEntry snapshot;
        if (team == kNoTeam || team < season_exe.u16(::bmh::layout::CareerSession_DispatchLineup_RuntimeField)) {
            snapshot = services.enter_club(team);
        }
        auto projection = ui::club_info_from_game(services.state(), team, snapshot,
                                                  season_exe, season_texts);
        projection.view = selected_view;
        club_screen = std::make_unique<ui::ClubInfoScreen>(data, std::move(projection));
    };
    // Presents the club screen; the window title encodes the view state.
    const auto show = [&] {
        auto title = "OpenBMH - cup_club_info_" + std::to_string(team) + "_" +
                     std::to_string(int(selected_view)) + "_category_" +
                     std::to_string(club_screen->projection().category);
        if (club_screen->pressed_button())
            title += "_pressed_" + std::to_string(int(*club_screen->pressed_button()));
        else if (club_screen->hovered_row())
            title += "_hover_" + std::to_string(*club_screen->hovered_row());
        present_season(club_screen->render(), title.c_str());
    };
    redraw(); show();
    SDL_FlushEvents(SDL_MOUSEMOTION, SDL_MOUSEBUTTONUP);
    // True when the process timer ran out and no button is currently held down.
    const auto deadline_expired = [&] {
        return !club_screen->pressed_button() && poll_process_timer(true, true);
    };
    while (running) {
        if (deadline_expired()) return true; //ClubInfoFromGame event8 returns to its owning caller.
        SDL_Event input;
        while (quit_confirmation.poll(&input)) {
            if (input.type == SDL_QUIT) { running = false; return false; }
            if (deadline_expired()) return true;
            ui::ClubInfoRequest request;
            if (input.type == SDL_MOUSEMOTION) {
                if (club_screen->move(input.motion.x, input.motion.y)) show();
            } else if (input.type == SDL_MOUSEBUTTONDOWN) {
                if (input.button.button == SDL_BUTTON_RIGHT) return true;
                if (input.button.button != SDL_BUTTON_LEFT) continue;
                (void)club_screen->move(input.button.x, input.button.y);
                request = club_screen->pointer_down(input.button.x, input.button.y);
                if (request.kind == ui::ClubInfoRequestKind::none) {
                    club_screen->press(input.button.x, input.button.y);
                    show();
                }
            } else if (input.type == SDL_MOUSEBUTTONUP && input.button.button == SDL_BUTTON_LEFT) {
                request = club_screen->release(input.button.x, input.button.y);
                show();
            }
            switch (request.kind) {
            case ui::ClubInfoRequestKind::none: break;
            case ui::ClubInfoRequestKind::close: return true;
            case ui::ClubInfoRequestKind::toggle_view:
                selected_view = selected_view == ui::ClubInfoView::data_a
                    ? ui::ClubInfoView::data_b : ui::ClubInfoView::data_a;
                redraw(); show();
                break;
            case ui::ClubInfoRequestKind::change_category: {
                std::string error;
                const auto applied = services.change_category(team, request.requested_category);
                // Source changes the icon without re-entering2af41 or
                // altering the list's owned pre-cleanup drawing flags.
                auto projection = club_screen->projection();
                projection.category = applied;
                club_screen = std::make_unique<ui::ClubInfoScreen>(data, std::move(projection));
                show();
                break;
            }
            case ui::ClubInfoRequestKind::open_statistics:
                if (!present_club_statistics) throw FormatError("club statistics presenter is unavailable");
                present_club_statistics(services, team, club_screen->render());
                if (!running) return false;
                redraw(); show();
                SDL_FlushEvents(SDL_MOUSEMOTION, SDL_MOUSEBUTTONUP);
                break;
            case ui::ClubInfoRequestKind::open_player_detail:
                if (!present_club_player) throw FormatError("club player presenter is unavailable");
                if (present_club_player(services, request, club_screen->render())) return true;
                if (!running) return false;
                redraw(); show();
                SDL_FlushEvents(SDL_MOUSEMOTION, SDL_MOUSEBUTTONUP);
                break;
            }
        }
        SDL_Delay(10);
    }
    return false;
}

// Opens the player detail card for a row chosen in the club list. The runtime input mode
// byte is switched for the card and restored afterwards (RAII helper below).
auto App::club_player_dialog(app::ClubModalServices& services, const ui::ClubInfoRequest& selected, const Image& parent_image) -> bool {
    if (!running) return false;
    const ClubPlayerDetailSelection selection{selected.runtime_team, selected.player_id,
        selected.rendered_row, selected.physical_row, selected.human_loan_index};
    const uint8_t prior_mode = services.runtime_byte(::bmh::layout::CareerSession_DispatchPoachUi_ExecutableField);
    const uint8_t input_mode = club_player_detail_input_mode(prior_mode);
    // Restores the saved runtime byte when the card is left, including by exception.
    struct PlayerInputModeRestore {
        app::ClubModalServices& services;
        uint8_t prior;
        ~PlayerInputModeRestore() { services.set_runtime_byte(::bmh::layout::CareerSession_DispatchPoachUi_ExecutableField, prior); }
    } mode_restore{services, prior_mode};
    services.set_runtime_byte(::bmh::layout::CareerSession_DispatchPoachUi_ExecutableField, input_mode);
    if(!present_player_detail)throw FormatError("generic player presenter unavailable");
    return present_player_detail(services,selection,parent_image).value_or(false);
}

// Modal player detail card. Returns the card's close-parent flag, or std::nullopt when
// the program is quitting.
auto App::player_detail_dialog(app::ClubModalServices& services,const ClubPlayerDetailSelection& selection,const Image& parent_image) -> std::optional<bool> {
    if(!running)return std::nullopt;
    press.reset(); //ClubPlayerDetailFromGame now owns physical hold/release through its own Source wait.
    const uint8_t input_mode=services.runtime_byte(::bmh::layout::CareerSession_DispatchPoachUi_ExecutableField);
    size_t rebuild_count = 0;
    // Asks the engine to redraw the player's data and projects it for the card.
    const auto rebuild = [&] {
        std::string error;
        auto redraw = services.redraw_player(selection);
        ++rebuild_count;
        return ui::club_player_detail_from_game(services.state(), std::move(redraw), season_exe);
    };
    ui::ClubPlayerDetailScreen card(data, rebuild(), parent_image);
    // Presents the card; the window title encodes selection and statistics state.
    const auto show = [&] {
        const auto title = "OpenBMH - cup_player_detail_" + std::to_string(selection.runtime_team) +
            "_" + std::to_string(selection.player_id) + (card.in_statistics() ? "_player_statistics_" : "_card_") +
            "_statistics_flags_" + std::to_string(card.projection().statistics_flags) +
            "_rebuild_" + std::to_string(rebuild_count);
        present_season(card.render(), title.c_str());
    };
    // Waits until the left button is up, but only if the engine recorded a long hold before
    // the card opened.
    const auto wait_prior_left_release = [&] {
        if (input_mode == 0) return;
        // Source waits only for an already set CareerSession_InitializeClubModalRuntime_ExecutableField (>25 held ticks),
        // then clears it. A fresh list DOWN is not an already held flag.
        const bool prior_long_hold = services.runtime_byte(::bmh::layout::CareerSession_InitializeClubModalRuntime_ExecutableField) != 0;
        if (!prior_long_hold) return;
        SDL_PumpEvents();
        while (running && (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK)) {
            SDL_Event input;
            while (quit_confirmation.poll(&input))
                if (input.type == SDL_QUIT) { running = false; return; }
            SDL_PumpEvents();
            if (running) SDL_Delay(10);
        }
        services.set_runtime_byte(::bmh::layout::CareerSession_InitializeClubModalRuntime_ExecutableField, 0); // Source consumes the owned hold flag.
    };
    show(); wait_prior_left_release();
    // Source tick at which a left button hold began (for the card's hold shortcut).
    std::optional<uint64_t> held_left_since;
    SDL_PumpEvents();
    // ClubPlayerDetailFromGame consumes the opening DOWN when bbcde!=0. Only its
    // selection mode retains that inherited DOWN as a hold candidate.
    if (input_mode == 0 && (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK))
        held_left_since = ui::pointer_hold_source_ticks(SDL_GetTicks64());
    // Executes a request coming from the card. A returned value means the card is left
    // (true/false = close the parent screens too).
    const auto apply_request = [&](ui::ClubPlayerDetailRequest request) -> std::optional<bool> {
        switch (request.kind) {
        case ui::ClubPlayerDetailRequestKind::none: return std::nullopt;
        case ui::ClubPlayerDetailRequestKind::close:
            if(poach_selection_active && input_mode==0 && request.source_return>0)
                poach_card_selection=request.selection;
            present_season(parent_image, "OpenBMH - cup_club_player_restored");
            return request.close_parent();
        case ui::ClubPlayerDetailRequestKind::redraw_card: {
            // CopyOpaqueSlice saves active-manager preferences only on exit.
            const int manager = card.projection().redraw.context.active_manager;
            const uint32_t flags = ::bmh::layout::Flags + uint32_t(manager) * 3 + 2;
            if (!services.state().memory.has(flags))
                throw FormatError("club player statistics: active preference block missing");
            services.state().memory.set8(flags, request.statistics_flags);
            card.replace_redraw(rebuild()); // One actual42dbd rebuild, never per presentation.
            held_left_since.reset();
            show(); wait_prior_left_release();
            return std::nullopt;
        }
        case ui::ClubPlayerDetailRequestKind::national_action: {
            present_season(parent_image, "OpenBMH - cup_club_player_restored");
            std::string error;
            (void)services.national_action(request.selection);
            // Source returnsff even when a full national squad prevents an add.
            return request.close_parent();
        }
        case ui::ClubPlayerDetailRequestKind::offer_action: {
            // ClubPlayerDetailFromGame restores its saved parent before3140f; the
            // question overlays the club list, not the departed card.
            present_season(parent_image, "OpenBMH - pending_offer_parent_restored");
            const auto hooks = make_pending_offer_hooks(parent_image);
            std::string error;
            try { (void)services.pending_offers(request.selection, hooks); }
            catch(const std::exception& e) {
                if(services.transactional)throw;
                if(running) {std::fprintf(stderr,"pending offer: %s\n",e.what());show();}
                return std::nullopt;
            }
            present_season(parent_image, "OpenBMH - cup_club_player_restored");
            return request.close_parent();
        }
        }
        return std::nullopt;
    };
    // Applies the process-timer deadline to the card unless a mouse release is still awaited.
    const auto expire_card = [&]() -> std::optional<bool> {
        if (card.source_mouse_wait_release_pending()) return std::nullopt;
        if (poll_process_timer(true, true) && card.process_timer_expired())
            return apply_request(card.take_request());
        return std::nullopt;
    };
    while (running) {
        if (const auto complete = expire_card()) return *complete;
        SDL_Event input;
        while (quit_confirmation.poll(&input)) {
            if (input.type == SDL_QUIT) { running = false; return std::nullopt; }
            if (const auto complete = expire_card()) return *complete;
            bool changed = false;
            if (input.type == SDL_MOUSEMOTION) {
                changed = card.move(input.motion.x, input.motion.y);
                if (changed) held_left_since.reset(); // ExpireQuestion move-out cancels its held press.
            } else if (input.type == SDL_MOUSEBUTTONDOWN) {
                if (input.button.button == SDL_BUTTON_RIGHT) {
                    held_left_since.reset();
                    changed = card.press(input.button.x, input.button.y, true);
                } else if (input.button.button == SDL_BUTTON_LEFT) {
                    // ExpireQuestion waits for the paired release itself;
                    // its button hold does not run42dbd's hold shortcut.
                    held_left_since.reset();
                    changed = card.press(input.button.x, input.button.y);
                }
            } else if (input.type == SDL_MOUSEBUTTONUP && input.button.button == SDL_BUTTON_LEFT) {
                held_left_since.reset();
                changed = card.release(input.button.x, input.button.y);
            }
            const auto request = card.take_request();
            if (const auto complete = apply_request(request)) return *complete;
            if (!running) return std::nullopt;
            if (changed && request.kind == ui::ClubPlayerDetailRequestKind::none) show();
        }
        // While the button is held, report the elapsed source ticks to the card; forget the hold
        // when the physical button is up.
        if (held_left_since) {
            SDL_PumpEvents();
            if (!(SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK)) held_left_since.reset();
            else if (card.held_left_ticks(uint32_t(ui::pointer_hold_source_ticks(SDL_GetTicks64()) - *held_left_since))) {
                if (const auto complete = apply_request(card.take_request())) return *complete;
            }
        }
        SDL_Delay(10);
    }
    return std::nullopt;
}

// Modal team statistics screen with a mode toggle; leaves on close, right press or timer.
auto App::club_statistics_dialog(app::ClubModalServices& services, int team, const Image& parent_frame) -> void {
    if (!running) return;
    auto mode = ui::TeamStatisticsMode::combined;
    std::unique_ptr<ui::TeamStatisticsScreen> statistics;
    // Rebuilds the statistics screen for the current mode.
    const auto rebuild = [&] {
        auto projection = ui::team_statistics_from_game(services.state(), team, mode,
                                                         season_exe, season_texts);
        statistics = std::make_unique<ui::TeamStatisticsScreen>(data,
            std::move(projection), parent_frame);
    };
    // Presents the statistics screen; the window title encodes team, mode and pressed button.
    const auto show = [&] {
        auto title = "OpenBMH - cup_team_statistics_" + std::to_string(team) +
            "_mode_" + std::to_string(int(mode));
        if (statistics->pressed_button())
            title += "_pressed_" + std::to_string(int(*statistics->pressed_button()));
        present_season(statistics->render(), title.c_str());
    };
    rebuild(); show();
    SDL_FlushEvents(SDL_MOUSEMOTION, SDL_MOUSEBUTTONUP);
    // True when the process timer ran out and no button is currently held down.
    const auto deadline_expired = [&] {
        return !statistics->pressed_button() && poll_process_timer(true, true);
    };
    while (running) {
        if (deadline_expired()) return; //LeagueCalculations frees its child before4f548 polls again.
        SDL_Event input;
        while (quit_confirmation.poll(&input)) {
            if (input.type == SDL_QUIT) { running = false; return; }
            if (deadline_expired()) return;
            // A right press leaves at once without waiting for its release.
            if (input.type == SDL_MOUSEBUTTONDOWN && input.button.button == SDL_BUTTON_RIGHT)
                return; // ExpireQuestion right DOWN: do not wait for release.
            if (input.type == SDL_MOUSEMOTION) {
                if (statistics->move(input.motion.x, input.motion.y)) show();
            } else if (input.type == SDL_MOUSEBUTTONDOWN && input.button.button == SDL_BUTTON_LEFT) {
                statistics->press(input.button.x, input.button.y);
                show();
            } else if (input.type == SDL_MOUSEBUTTONUP && input.button.button == SDL_BUTTON_LEFT) {
                const auto request = statistics->release(input.button.x, input.button.y);
                if (request.kind == ui::TeamStatisticsRequestKind::close) return;
                if (request.kind == ui::TeamStatisticsRequestKind::change_mode) {
                    mode = request.mode;
                    rebuild(); // Pure Source table projection; neither RNG advances.
                    SDL_FlushEvents(SDL_MOUSEMOTION, SDL_MOUSEBUTTONUP);
                }
                show();
            }
            // OriginalMouseSound keys, including ESC, do not select a button.
        }
        SDL_Delay(10);
    }
}
