// SPDX-License-Identifier: GPL-3.0-only
// App: transfers (incoming offers, decisions, pending offers, market sales, loan returns, poaching).
// (moved out of main.cpp unchanged; the class is declared in app/app.hpp)
#include "app/app.hpp"

// Converts the engine's transfer offer list into the UI's IncomingTransferDecisionView.
auto App::transfer_view(const GameState& state, const TransferOfferListView& offers, int viewer_manager) -> bmh::ui::IncomingTransferDecisionView {
    ui::IncomingTransferDecisionView view;
    view.valid = offers.valid;
    view.list_entry = offers.list_entry;
    view.player = offers.player;
    view.seller = offers.seller;
    view.seller_manager = offers.seller_manager;
    view.viewer_manager = viewer_manager;
    view.preferred_bid_index = offers.preferred_bid_index;
    view.has_preferred_bid = offers.preferred_bid_index >= 0;
    view.preferred_bid_passed_random_check = offers.preferred_bid_passed_random_check;
    if (offers.seller != 0x0fff && offers.seller != kNoTeam) view.negotiator_name = state.club_name(offers.seller);
    view.asking_fee = offers.asking_fee;
    view.asking_monthly_salary = offers.asking_monthly_salary;
    view.quoted_fee = offers.quoted_fee;
    view.quoted_monthly_salary = offers.quoted_monthly_salary;
    view.quoted_years = offers.quoted_years;
    view.notice_text_address = offers.notice_text_address;
    for (size_t row = 0; row < view.offers.size(); ++row) {
        const auto& source = offers.offers[row];
        auto& slot = view.offers[row];
        slot.bid_index = source.bid_index;
        slot.buyer = source.buyer;
        slot.buyer_manager = source.buyer_manager;
        slot.fee = source.fee;
        slot.monthly_salary = source.monthly_salary;
        slot.fee_reference = source.fee_reference;
        slot.salary_reference = source.salary_reference;
        slot.contract_months = source.contract_months;
        slot.loan = source.loan;
        slot.transaction_supported = source.transaction_supported ||
            (source.loan && !source.loan_permitted);
        slot.present = source.present;
        slot.eligible = source.eligible;
        slot.bonuses = source.bonuses;
    }
    return view;
}

// Shows a yes/no/acknowledge dialog over a backdrop and runs a modal loop until answered.
// Questions are bound to the process timer (expiry answers No); timed notices close
// after 70*3*20 ms. Returns `unsupported` when the program is quitting.
auto App::present_pending_dialog(const GameState& state, ui::TaxDialogProjection view,
                                  const Image& backdrop, const char* title,
                                  std::optional<ui::GameEndRenderOptions> effective_options) -> PendingOfferAnswer {
    if (!running) return PendingOfferAnswer::unsupported;
    const bool question = view.kind == ui::TaxDialogKind::question;
    if (question) press.reset(); // PresentManagerDialog owns the caller's input now.
    ui::GameEndTaxDialog screen(data, std::move(view),
        effective_options ? *effective_options : game_end_options(state), backdrop);
    const bool timed = screen.source_timed_notice();
    if (!question) app::original_audio_command(playback, timed ? 13 : 14); // PresentManagerDialog / SystemIdentity.
    const Uint32 started = SDL_GetTicks();
    auto pressed = ui::GameEndTaxDialog::Button::none;
    const auto show = [&] {
        std::string name = title;
        if (pressed == ui::GameEndTaxDialog::Button::yes) name += "_pressed_yes";
        else if (pressed == ui::GameEndTaxDialog::Button::no) name += "_pressed_no";
        else if (pressed == ui::GameEndTaxDialog::Button::acknowledge) name += "_pressed_ack";
        present_season(screen.render(pressed), name.c_str());
    };
    const auto complete = [&](PendingOfferAnswer answer) {
        present_season(screen.backdrop(), "OpenBMH - pending_offer_dialog_restored");
        return answer;
    };
    show();
    if (!state.memory.has(::bmh::layout::Main_Screen_RuntimeField2) || state.memory.u8(::bmh::layout::Main_Screen_RuntimeField2) == 0)
        SDL_FlushEvents(SDL_MOUSEMOTION, SDL_MOUSEBUTTONUP);
    while (running) {
        // PresentManagerDialog polls2de66 after its initial Present.ExpireQuestion keeps
        // a held left control until its UP/leave while the clock runs.
        if (question && poll_process_timer(true,true) &&
            pressed == ui::GameEndTaxDialog::Button::none)
            return complete(PendingOfferAnswer::no);
        if (timed && Uint32(SDL_GetTicks() - started) >= 70 * 3 * 20)
            return complete(PendingOfferAnswer::yes);
        SDL_Event input;
        while (quit_confirmation.poll(&input)) {
            if (input.type == SDL_QUIT) { running = false; return PendingOfferAnswer::unsupported; }
            const bool question_deadline = question && poll_process_timer(true,true);
            if (question_deadline && pressed == ui::GameEndTaxDialog::Button::none)
                return complete(PendingOfferAnswer::no);
            if (input.type == SDL_MOUSEBUTTONDOWN) {
                if (timed && (input.button.button == SDL_BUTTON_LEFT || input.button.button == SDL_BUTTON_RIGHT))
                    return complete(PendingOfferAnswer::yes);
                // ExpireQuestion consumes the paired left release. Right/ESC
                // do not choose a question answer or interrupt that hold.
                if (!question && pressed == ui::GameEndTaxDialog::Button::none &&
                    input.button.button == SDL_BUTTON_RIGHT)
                    return complete(PendingOfferAnswer::yes);
                if (input.button.button != SDL_BUTTON_LEFT) continue;
                pressed = screen.hit_test(input.button.x, input.button.y);
                if (!question && pressed == ui::GameEndTaxDialog::Button::none)
                    return complete(PendingOfferAnswer::yes);
                show();
            } else if (input.type == SDL_MOUSEMOTION &&
                       pressed != ui::GameEndTaxDialog::Button::none &&
                       screen.hit_test(input.motion.x, input.motion.y) != pressed) {
                if (!question) return complete(PendingOfferAnswer::yes);
                pressed = ui::GameEndTaxDialog::Button::none;
                show();
            } else if (input.type == SDL_MOUSEBUTTONUP && input.button.button == SDL_BUTTON_LEFT) {
                //PresentManagerDialog forces button1/No after4885b returns when the
                //signed Source deadline is negative, even after Yes UP.
                if (question_deadline) return complete(PendingOfferAnswer::no);
                const auto released = screen.hit_test(input.button.x, input.button.y);
                if (pressed != ui::GameEndTaxDialog::Button::none && released == pressed)
                    return complete(pressed == ui::GameEndTaxDialog::Button::no
                        ? PendingOfferAnswer::no : PendingOfferAnswer::yes);
                if (!question && pressed != ui::GameEndTaxDialog::Button::none)
                    return complete(PendingOfferAnswer::yes);
                pressed = ui::GameEndTaxDialog::Button::none;
                show();
            }
        }
        SDL_Delay(10);
    }
    return PendingOfferAnswer::unsupported;
}

// Asks whether a loaned player should be returned. Returns 1 yes, 0 no, -1 cancelled.
auto App::loan_return_presenter(GameState& state, WatcomRandom&, int manager,
                                 const std::string& question) -> int {
    if (!running) return -1;
    const Image backdrop = season_backdrop ? *season_backdrop : flow.render();
    const auto answer = present_pending_dialog(state,
        ui::loan_return_question_dialog(state, manager, question), backdrop,
        "OpenBMH - loan_return_question");
    if (answer == PendingOfferAnswer::yes) return 1;
    if (answer == PendingOfferAnswer::no) return 0;
    return -1;
}

// Builds the hooks that present pending transfer/contract offers over the given backdrop.
auto App::make_pending_offer_hooks(Image backdrop) -> bmh::PendingOfferHooks {
    auto pending_backdrop = std::make_shared<Image>(std::move(backdrop));
    PendingOfferHooks hooks;
    hooks.answer = [&, pending_backdrop](const GameState& staged, const PendingOfferQuestion& question) {
        return present_pending_dialog(staged, ui::pending_offer_question_dialog(staged, question),
            *pending_backdrop, question.kind == PendingOfferQuestionKind::salary_demand
                ? "OpenBMH - pending_offer_salary_question" : "OpenBMH - pending_offer_severance_question");
    };
    // Lets the player pick one of the incoming transfer bids (modal; reacts to mouse press).
    hooks.select_transfer = [&, pending_backdrop](const GameState& staged, const TransferOfferListView& offers) {
        ui::IncomingTransferScreen screen(data,
            ui::pending_offer_transfer_decision_from_game(staged, offers),
            staged.memory.u8(::bmh::layout::TextGradientDisabled) == 0);
        *pending_backdrop = screen.render();
        present_season(*pending_backdrop, "OpenBMH - pending_offer_transfer");
        SDL_FlushEvents(SDL_MOUSEMOTION, SDL_MOUSEBUTTONUP); // DecodeTaxAnimationScript/SDLFlushEvents redraw boundary
        while (running) {
            SDL_Event input;
            while (quit_confirmation.poll(&input)) {
                if (input.type == SDL_QUIT) { running = false; return PendingOfferTransferChoice{}; }
                std::optional<int> bid;
                if (input.type == SDL_MOUSEBUTTONDOWN &&
                    (input.button.button == SDL_BUTTON_LEFT || input.button.button == SDL_BUTTON_RIGHT))
                    bid = screen.click(input.button.x, input.button.y,
                        input.button.button == SDL_BUTTON_RIGHT);
                // IncomingTransferScreen reads event2/8 directly: release and
                // physical ESC never select or decline this row.
                if (bid) return PendingOfferTransferChoice{
                    *bid < 0 ? PendingOfferTransferChoiceKind::decline : PendingOfferTransferChoiceKind::bid, *bid};
            }
            SDL_Delay(10);
        }
        return PendingOfferTransferChoice{};
    };
    hooks.notice = [&, pending_backdrop](const GameState& staged, const PendingOfferNotice& notice) {
        if (present_pending_dialog(staged, ui::pending_offer_notice_dialog(staged, notice),
            *pending_backdrop, "OpenBMH - pending_offer_notice") == PendingOfferAnswer::unsupported)
            throw std::runtime_error("pending offer presenter stopped");
    };
    // Shows a program-error screen and waits for a left or right press; throws on quit.
    hooks.program_error = [&, pending_backdrop](const GameState& staged, uint16_t code) {
        ui::ProgramErrorScreen screen(data,code,game_end_options(staged));
        // The modal loop owns the input now: drop any press recorded by the main loop.
        press.reset();
        SDL_FlushEvents(SDL_MOUSEMOTION,SDL_MOUSEBUTTONUP);
        present_season(screen.render(),"OpenBMH - program_error");
        bool acknowledged=false;
        while(running && !acknowledged) {
            SDL_Event event;
            while(quit_confirmation.poll(&event)) {
                if(event.type==SDL_QUIT){running=false;break;}
                if(event.type==SDL_MOUSEBUTTONDOWN &&
                   (event.button.button==SDL_BUTTON_LEFT || event.button.button==SDL_BUTTON_RIGHT)) {
                    acknowledged=true;break;
                }
            }
            if(!acknowledged && running)SDL_Delay(10);
        }
        if(!running)throw std::runtime_error("program-error acknowledgement interrupted");
        present_season(*pending_backdrop,"OpenBMH - pending_offer_dialog_restored");
    };
    return hooks;
}

// Builds the hooks for transfer-market sales to a human buyer (only notices are shown).
auto App::make_market_sale_hooks(Image backdrop) -> bmh::HumanBuyerSaleHooks {
    auto market_backdrop = std::make_shared<Image>(std::move(backdrop));
    HumanBuyerSaleHooks hooks;
    hooks.notice = [&, market_backdrop](const GameState& staged, const HumanBuyerSaleNotice& notice) {
        const PendingOfferNotice projection_notice{notice.manager, notice.raw_offer_slot,
            notice.source_text_address, notice.original_message};
        if (present_pending_dialog(staged, ui::pending_offer_notice_dialog(staged, projection_notice),
            *market_backdrop, "OpenBMH - transfer_market_notice") == PendingOfferAnswer::unsupported)
            throw std::runtime_error("transfer market presenter stopped");
    };
    // PresentManagerDialog yes/no question: poaching asks the selling manager whether the offered fee is
    // fine and the buyer whether he pays the fee the DFB sets. Without this hook the engine refuses a poaching
    // offer (the ANGEBOT button of the Abwerben dialog then does nothing).
    hooks.confirm = [&, market_backdrop](const GameState& staged, int manager, const std::string& text) {
        const auto answer = present_pending_dialog(staged, ui::loan_return_question_dialog(staged, manager, text),
            *market_backdrop, "OpenBMH - transfer_market_question");
        if (answer == PendingOfferAnswer::unsupported) throw std::runtime_error("transfer market presenter stopped");
        return answer == PendingOfferAnswer::yes;
    };
    return hooks;
}

// Modal offer editor for a returned loan player. Returns -1 if unsupported or quitting,
// 0 if cancelled, 1 once the offer was submitted/closed.
auto App::loan_return_offer_presenter(GameState& staged, GameRandom& game_random,
        WatcomRandom& watcom_random, app::ClubModalServices& services,
        int manager, int source_slot, int list_entry,
        const HumanBuyerSaleHooks& sale_hooks) -> int {
    // Only valid for manager slots 0..3 and list entries 40..43; anything else is unsupported.
    if (!running || manager < 0 || manager >= 4 || list_entry < 40 || list_entry >= 44 ||
        source_slot != 2928 + list_entry || !staged.memory.has(::bmh::layout::EditorList + uint32_t(list_entry) * 0x58 + 0x57))
        return -1;
    const uint32_t row = ::bmh::layout::EditorList + uint32_t(list_entry) * 0x58;
    const int player = staged.squad_slots.at(size_t(source_slot)) & 0x0fff;
    const uint16_t seller_word = staged.memory.u16(row + 8);
    int bid_slot = -1;
    for (int i = 0; i < 4; ++i) {
        const uint16_t word = staged.memory.u16(row + uint32_t(i) * 2);
        if (word == 0xffff || word == staged.manager_teams.at(size_t(manager))) { bid_slot = i; break; }
    }
    TransferOfferEditorRequest input;
    input.manager = manager;
    input.runtime_club = staged.manager_teams.at(size_t(manager));
    input.list_entry = list_entry;
    input.source_slot = source_slot;
    input.expected_player = player;
    input.expected_seller_word = seller_word;
    input.expected_bid_slot = bid_slot;
    input.loan = false;
    input.loan_return_entry = true;
    auto owner = open_transfer_offer_editor_source(staged, input, game_random, watcom_random);
    if (!owner.supported || owner.closed) return -1;
    ui::OutgoingTransferScreen screen(data, app::loan_return_offer_editor_projection(
        staged, owner, revision, (!staged.memory.has(::bmh::layout::TextGradientDisabled) || staged.memory.u8(::bmh::layout::TextGradientDisabled) == 0) && season_exe.u8(::bmh::layout::TextGradientMode) == 2));
    const Image backdrop = flow.render();
    const auto draw = [&] { present_season(screen.render(), "OpenBMH - loan_return_offer"); };
    SDL_FlushEvents(SDL_MOUSEMOTION, SDL_MOUSEBUTTONUP);
    draw();
    while (running) {
        SDL_Event event;
        while (quit_confirmation.poll(&event)) {
            if (event.type == SDL_QUIT) { running = false; return -1; }
            ui::OutgoingTransferRequest action;
            if (event.type == SDL_MOUSEMOTION) continue;
            if (event.type == SDL_MOUSEBUTTONDOWN) {
                action = screen.click(event.button.x, event.button.y,
                    // clicks > 1 tells the screen it is a double click.
                    event.button.button == SDL_BUTTON_RIGHT, event.button.clicks > 1);
            } else if (event.type == SDL_KEYDOWN) {
                const char* name = SDL_GetKeyName(event.key.keysym.sym);
                std::string key = name ? name : "";
                std::transform(key.begin(), key.end(), key.begin(),
                    [](unsigned char c) { return char(std::toupper(c)); });
                action = screen.key(key);
            } else if (event.type == SDL_TEXTINPUT) {
                (void)screen.text(event.text.text);
            }
            if (action.kind == ui::OutgoingTransferRequestKind::none) { draw(); continue; }
            if (action.kind == ui::OutgoingTransferRequestKind::cancel) {
                (void)cancel_transfer_offer_editor_source(staged, owner);
                return 0;
            }
            if (action.kind == ui::OutgoingTransferRequestKind::edit_field ||
                action.kind == ui::OutgoingTransferRequestKind::quote_change) {
                owner = app::edit_transfer_offer_from_ui(staged, owner, action,
                    game_random, watcom_random);
                if (!owner.supported) return -1;
                screen.acknowledge(app::loan_return_offer_editor_projection(staged, owner, revision,
                    (!staged.memory.has(::bmh::layout::TextGradientDisabled) || staged.memory.u8(::bmh::layout::TextGradientDisabled) == 0) && season_exe.u8(::bmh::layout::TextGradientMode) == 2));
                draw();
                continue;
            }
            if (action.kind == ui::OutgoingTransferRequestKind::team_info) {
                const auto validated = cancel_transfer_offer_editor_source(staged, owner);
                if (!validated.supported || !present_club_information ||
                    !present_club_information(services, int(seller_word & 0x7fff))) return -1;
                owner = resume_transfer_offer_team_info_source(staged, owner, watcom_random);
                if (!owner.supported) return -1;
                screen.acknowledge(app::loan_return_offer_editor_projection(staged, owner, revision,
                    (!staged.memory.has(::bmh::layout::TextGradientDisabled) || staged.memory.u8(::bmh::layout::TextGradientDisabled) == 0) && season_exe.u8(::bmh::layout::TextGradientMode) == 2));
                draw();
                continue;
            }
            if (action.kind == ui::OutgoingTransferRequestKind::player_info) {
                if (!present_player_detail) return -1;
                const auto selection = transfer_offer_player_info_selection_source(staged, owner);
                services.set_runtime_byte(::bmh::layout::CareerSession_DispatchPoachUi_ExecutableField, 1);
                if (!present_player_detail(services, selection, screen.render()).has_value()) return -1;
                owner = resume_transfer_offer_player_info_source(staged, owner,
                    services.runtime_byte(::bmh::layout::CareerSession_DispatchTransferOffer_ExecutableField), services.runtime_byte(::bmh::layout::CareerSession_DispatchTransferOffer_ExecutableField2), watcom_random);
                if (!owner.supported) return -1;
                screen.acknowledge(app::loan_return_offer_editor_projection(staged, owner, revision,
                    (!staged.memory.has(::bmh::layout::TextGradientDisabled) || staged.memory.u8(::bmh::layout::TextGradientDisabled) == 0) && season_exe.u8(::bmh::layout::TextGradientMode) == 2));
                draw();
                continue;
            }
            if (action.kind == ui::OutgoingTransferRequestKind::submit) {
                MinuteReport report;
                auto submitted = submit_transfer_offer_editor_source(staged, owner, watcom_random, sale_hooks, report);
                if (report.unsupported || submitted.action.status == TransferActionStatus::unsupported) return -1;
                if (submitted.editor.closed || submitted.action.status == TransferActionStatus::pending ||
                    submitted.action.status == TransferActionStatus::rejected ||
                    submitted.action.status == TransferActionStatus::insufficient_funds) return 1;
                owner = std::move(submitted.editor);
                screen.acknowledge(app::loan_return_offer_editor_projection(staged, owner, revision,
                    (!staged.memory.has(::bmh::layout::TextGradientDisabled) || staged.memory.u8(::bmh::layout::TextGradientDisabled) == 0) && season_exe.u8(::bmh::layout::TextGradientMode) == 2));
                draw();
            }
        }
        // Keep audio streaming while idle, then yield the CPU briefly.
        if (playback.needs_pump()) playback.pump(audio_error);
        SDL_Delay(10);
    }
    (void)backdrop;
    return -1;
}

// Callback for an incoming transfer offer: modal decision screen. Returns the chosen bid
// index, or -1 for decline, nothing to choose or quit.
auto App::choose_transfer_offer(const GameState& state, const TransferOfferListView& offers) -> int {
        if (!running) return -1;
        auto view = transfer_view(state, offers, offers.seller_manager);
        ui::IncomingTransferScreen offer_screen(data,
            ui::incoming_transfer_decision_from_game(state, view),
            state.memory.u8(::bmh::layout::TextGradientDisabled) == 0);
        SDL_SetWindowTitle(window, "OpenBMH - incoming_transfer");
        const Uint32 notice_started = SDL_GetTicks();
        bool offer_dirty = true;
        // Keep the original synchronous decision stack suspended here.
        // Only this modal's input is dispatched; desk actions and saves
        // cannot run against a partially processed calendar day.
        while (running) {
            // The introductory notice disappears after 4260 ms or on any key.
            if (offer_screen.notice_active() && SDL_GetTicks() - notice_started >= 4260)
                offer_dirty |= offer_screen.dismiss_notice();
            if (!offer_screen.notice_active() && offers.offer_count == 0) return -1;
            if (offer_dirty) {
                const auto image = offer_screen.render();
                outcome_backdrop = image;
                canvas.show(image, palette);
                offer_dirty = false;
            }
            SDL_Event event;
            while (quit_confirmation.poll(&event)) {
                if (event.type == SDL_QUIT) { running = false; return -1; }
                std::optional<int> answer;
                if (event.type == SDL_MOUSEBUTTONDOWN &&
                    (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT)) {
                    offer_screen.pointer_down(event.button.button == SDL_BUTTON_RIGHT);
                } else if (event.type == SDL_MOUSEBUTTONUP &&
                    (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT)) {
                    answer = offer_screen.pointer_up(event.button.x, event.button.y,
                                                     event.button.button == SDL_BUTTON_RIGHT);
                    offer_dirty = true;
                } else if (event.type == SDL_KEYDOWN) {
                    if (offer_screen.notice_active()) {
                        offer_dirty |= offer_screen.dismiss_notice();
                    } else if (event.key.keysym.sym == SDLK_ESCAPE) {
                        answer = offer_screen.key("ESC");
                    }
                }
                if (answer) return *answer;
            }
            if (playback.needs_pump()) playback.pump(audio_error);
            SDL_Delay(10);
        }
        return -1;
}

// Callback that presents a transfer decision already settled by the engine: last-week
// dialog, confirmation, or the animated evaluation of the bids. Clicks cannot change it.
auto App::present_transfer_decision(const GameState& state, const TransferDecisionEvent& decision) -> void {
        if (!running) return;
        if (decision.kind == TransferDecisionEventKind::last_week) {
            auto view = ui::transfer_last_week_dialog(state, decision);
            const Image backdrop = outcome_backdrop ? *outcome_backdrop :
                ui::GameEndTaxDialog(data, view, game_end_options(state)).backdrop();
            if (present_pending_dialog(state, std::move(view), backdrop,
                                       "OpenBMH - transfer_last_week") ==
                PendingOfferAnswer::unsupported)
                running = false;
            return;
        }
        auto view = transfer_view(state, decision.offers, decision.viewer_manager);
        if (decision.kind == TransferDecisionEventKind::confirmation)
            view.notice_text_address = decision.result_text_address;
        ui::IncomingTransferScreen decision_screen(data,
            ui::incoming_transfer_decision_from_game(state, view),
            state.memory.u8(::bmh::layout::TextGradientDisabled) == 0);
        const bool confirmation = decision.kind == TransferDecisionEventKind::confirmation;
        if (confirmation) {
            const Image backdrop = outcome_backdrop ? *outcome_backdrop : decision_screen.render();
            decision_screen.show_confirmation(decision.result_text_address, backdrop);
        } else {
            decision_screen.begin_outcome(decision.offers.offer_count,
                decision.accepted ? decision.selected_bid_index : -1, state.memory.u8(::bmh::layout::OutcomeDelayDisabled));
        }
        SDL_SetWindowTitle(window, confirmation ? "OpenBMH - transfer_confirmation" : "OpenBMH - transfer_evaluation");
        const Uint32 started = SDL_GetTicks();
        uint64_t rendered_phase = UINT64_MAX;
        // The observer only presents the engine's settled decision. Keep
        // calendar/desk/save dispatch suspended until the original modal
        // input completes; clicks cannot select or reroll a bid here.
        while (running) {
            const uint64_t elapsed = Uint32(SDL_GetTicks() - started);
            const uint64_t phase = confirmation ? 0 : std::min(elapsed / ui::IncomingTransferScreen::outcome_phase_ms,
                decision_screen.outcome_duration_ms() / ui::IncomingTransferScreen::outcome_phase_ms);
            if (phase != rendered_phase) {
                const auto image = decision_screen.render_at_elapsed(elapsed);
                if (!confirmation) outcome_backdrop = image;
                canvas.show(image, palette);
                rendered_phase = phase;
            }
            SDL_Event event;
            while (quit_confirmation.poll(&event)) {
                if (event.type == SDL_QUIT) { running = false; return; }
                if (event.type == SDL_MOUSEBUTTONDOWN &&
                    (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT)) {
                    decision_screen.pointer_down(event.button.button == SDL_BUTTON_RIGHT);
                    // OK of the confirmation looks pressed while the mouse button is held
                    if (event.button.button == SDL_BUTTON_LEFT && confirmation &&
                        decision_screen.press(event.button.x, event.button.y))
                        rendered_phase = UINT64_MAX;
                } else if (event.type == SDL_MOUSEBUTTONUP &&
                    (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT)) {
                    (void)decision_screen.pointer_up(event.button.x, event.button.y,
                        event.button.button == SDL_BUTTON_RIGHT, elapsed);
                    if (confirmation && !decision_screen.confirmation_dismissed()) rendered_phase = UINT64_MAX;
                }
            }
            if (confirmation ? decision_screen.confirmation_dismissed() : decision_screen.outcome_dismissed()) return;
            SDL_Delay(10);
        }
}

// Callback: shows the poaching attempt's notices one after another.
auto App::present_poach_notices(const GameState& staged,const PoachStart& attempt,const Image& backdrop) -> void {
        for(const auto& message:attempt.messages)
            if(present_pending_dialog(staged,ui::poach_notice_from_game(staged,attempt,message),backdrop,
                "OpenBMH - poach_notice")==PendingOfferAnswer::unsupported)
                throw FormatError("poach: notice presenter stopped");
}


auto App::select_poach_player(app::CareerSession& staged,app::ClubModalServices&,int manager,
        uint64_t& selection_revision,const Image& transfer_backdrop) -> std::optional<ClubPlayerDetailSelection> {
    using Phase=app::CareerSession::TableDispatchPhase;
    press.reset(); // The synchronous Source player-selection loop owns its input.
    std::string error;
    auto page=staged.current_league_table_dispatch(error);
    if(!page)throw FormatError(error);
    // Local variant of present_season that throws if an SDL render call fails.
    const auto present=[&](const Image& frame,const std::string& title) {
        SDL_SetWindowTitle(window,title.c_str());season_backdrop=frame;
        const auto painted=process_timer_image(frame);
        for(size_t i=0;i<pixels.size();++i)pixels[i]=palette.argb[painted.pixels[i]];
        if(SDL_UpdateTexture(texture,nullptr,pixels.data(),320*4)!=0 ||
           SDL_RenderClear(renderer)!=0 || SDL_RenderCopy(renderer,texture,nullptr,nullptr)!=0)
            throw FormatError(std::string("poach renderer: ")+SDL_GetError());
        app::present(renderer);
    };
    std::unique_ptr<ui::LeagueTableScreen> table;
    // Rebuilds the league table screen from the current engine page.
    const auto rebuild=[&] {
        auto tables=ui::league_tables_from_game(staged.state(),manager);
        tables[size_t(page->identity.league)]=page->page;
        table=std::make_unique<ui::LeagueTableScreen>(data,std::move(tables),page->identity.league,
            ui::LeagueTableScreen::FamilyMode{page->identity,1});
    };
    // Presents the table; a freshly forecast page is acknowledged to the engine once shown.
    const auto show_table=[&] {
        present(table->render(),"OpenBMH - poach_table_"+std::to_string(page->identity.league));
        if(page->phase==Phase::forecast_pending_present) {
            ui::LeagueFamilyRequest ack;
            ack.identity=page->identity;ack.kind=ui::LeagueFamilyRequestKind::table_forecast_presented;
            ack.from_child=1;
            page=staged.acknowledge_league_forecast_present(ack,selection_revision,error);
            if(!page)throw FormatError(error);
            rebuild(); // Retain forecast pixels, refresh only the stale-request token.
        }
    };
    // Modal table range dialog. Digit, ENTER, BACKSPACE and SPACE keys are queued and fed to
    // the dialog only while it is ready for input; returns its accept/cancel outcome.
    const auto run_range=[&](Image backdrop,ui::TableRangeDialogProfile profile)
        -> ui::TableRangeDialogOutcome {
        ui::LeagueTableRangeDialog dialog(data,std::move(backdrop),std::move(profile));
        std::deque<std::string> keys;
        Uint32 clock_anchor=SDL_GetTicks();
        std::optional<ui::TableRangeDialogOutcome> outcome;
        const auto show=[&] {
            present(dialog.render(),"OpenBMH - poach_table_range");
            outcome=dialog.after_present();clock_anchor=SDL_GetTicks();
        };
        const auto expire_range=[&] {
            //BalanceFromGame polls raw event8 before its next input. Keep its
            //key/N waits and accepted value owner; expose cancellation
            //only after the restored backdrop has actually been shown.
            if(!dialog.source_input_ready() || !poll_process_timer(true,true) ||
               !dialog.cancel_from_source_event())return false;
            show();
            return true;
        };
        show();
        while(running&&!outcome) {
            if(expire_range())break;
            SDL_Event input;
            while(quit_confirmation.poll(&input)) {
                if(input.type==SDL_QUIT){running=false;break;}
                if(expire_range())break;
                bool changed=false;
                if(input.type==SDL_TEXTINPUT) {
                    for(char c:std::string_view(input.text.text))if(c>='0'&&c<='9')keys.emplace_back(1,c);
                } else if(input.type==SDL_KEYDOWN) {
                    if(input.key.keysym.sym==SDLK_RETURN)keys.push_back("ENTER");
                    else if(input.key.keysym.sym==SDLK_BACKSPACE)keys.push_back("BACKSPACE");
                    else if(input.key.keysym.sym==SDLK_SPACE)keys.push_back("SPACE");
                } else if(input.type==SDL_MOUSEMOTION)changed=dialog.move(input.motion.x,input.motion.y);
                else if(input.type==SDL_MOUSEBUTTONDOWN&&
                        (input.button.button==SDL_BUTTON_LEFT||input.button.button==SDL_BUTTON_RIGHT))
                    changed=dialog.pointer_down(input.button.x,input.button.y,input.button.button==SDL_BUTTON_RIGHT);
                else if(input.type==SDL_MOUSEBUTTONUP&&
                        (input.button.button==SDL_BUTTON_LEFT||input.button.button==SDL_BUTTON_RIGHT))
                    changed=dialog.pointer_up(input.button.x,input.button.y,input.button.button==SDL_BUTTON_RIGHT);
                // A drag-only release does not restart an existing Source wait.
                if(changed&&input.type!=SDL_MOUSEBUTTONUP)show();
                if(outcome)break;
            }
            if(!running||outcome)break;
            while(!keys.empty()&&dialog.source_input_ready()) {
                if(expire_range())break;
                const auto key=std::move(keys.front());keys.pop_front();
                if(dialog.key(key))show();
                if(outcome)break;
            }
            // Feed the dialog whole 20 ms source units elapsed since the last anchor.
            const Uint32 now=SDL_GetTicks(),units=Uint32(now-clock_anchor)/20;
            if(units) {
                clock_anchor+=units*20;
                if(dialog.tick_source_units(units))show();
            }
            SDL_Delay(10);
        }
        if(!outcome)throw FormatError("poach: range presenter stopped");
        return *outcome;
    };
    rebuild();show_table();
    poach_selection_active=true;poach_card_selection.reset();
    struct ResetSelection { bool& active;~ResetSelection(){active=false;} } reset{poach_selection_active};
    // When the process timer runs out and no table action or release is pending, closes the
    // selection as if the close control had been used. Returns true if it did.
    const auto deadline_expired=[&] {
        if(table->pending_family_action() || table->source_mouse_wait_release_pending() ||
           !poll_process_timer(true,true))return false;
        //Move/4ffe0 selection owner must close before19c1c resumes.
        if(!table->pointer_down(0,0,true) || !table->pending_family_action() ||
           table->pending_family_action()->kind!=ui::LeagueFamilyRequestKind::close)
            throw FormatError("poach: timer close has no table owner");
        if(!staged.close_poach_table_selection(*table->pending_family_action(),error))
            throw FormatError(error);
        present(transfer_backdrop,"OpenBMH - poach_transfer_restored");
        return true;
    };
    while(running) {
        if(deadline_expired())return std::nullopt;
        SDL_Event input;
        while(quit_confirmation.poll(&input)) {
            if(input.type==SDL_QUIT){running=false;break;}
            if(deadline_expired())return std::nullopt;
            bool changed=false;
            if(input.type==SDL_MOUSEBUTTONDOWN&&
               (input.button.button==SDL_BUTTON_LEFT||input.button.button==SDL_BUTTON_RIGHT))
                changed=table->pointer_down(input.button.x,input.button.y,input.button.button==SDL_BUTTON_RIGHT);
            else if(input.type==SDL_MOUSEBUTTONUP&&input.button.button==SDL_BUTTON_LEFT)
                changed=table->click(input.button.x,input.button.y);
            else if(input.type==SDL_MOUSEMOTION)changed=table->move(input.motion.x,input.motion.y);
            if(!table->pending_family_action()) {if(changed)show_table();continue;}
            const auto request=*table->pending_family_action();
            if(request.kind==ui::LeagueFamilyRequestKind::close) {
                if(!staged.close_poach_table_selection(request,error))throw FormatError(error);
                present(transfer_backdrop,"OpenBMH - poach_transfer_restored");
                return std::nullopt;
            }
            if(request.kind==ui::LeagueFamilyRequestKind::select_league ||
               request.kind==ui::LeagueFamilyRequestKind::toggle_league_group) {
                const int prior_league=page->identity.league;
                if(!staged.select_league_family_page(request,selection_revision,error))throw FormatError(error);
                page=staged.current_league_table_dispatch(error);if(!page)throw FormatError(error);
                if(page->identity.league==prior_league) {
                    table->acknowledge_same_league_selector(request,page->identity);
                    if(request.kind==ui::LeagueFamilyRequestKind::toggle_league_group)show_table();
                } else {rebuild();show_table();}
            } else if(request.kind==ui::LeagueFamilyRequestKind::team_info) {
                poach_card_selection.reset();
                const auto identity=staged.dispatch_family_table_team_info(request,selection_revision,error,
                    [&](app::ClubModalServices& services,int team,const Image&) {
                        return present_club_information(services,team)&&running;
                    },&poach_card_selection);
                if(!identity)throw FormatError(error);
                if(poach_card_selection) {
                    present(transfer_backdrop,"OpenBMH - poach_transfer_restored");
                    return poach_card_selection;
                }
                page=staged.current_league_table_dispatch(error);if(!page)throw FormatError(error);
                rebuild();show_table();
            } else if(request.kind==ui::LeagueFamilyRequestKind::control) {
                const auto modal_backdrop=table->render();
                page=staged.dispatch_league_table_control(request,selection_revision,error);
                if(!page)throw FormatError(error);
                while(page->dialog) {
                    const auto stage=page->phase==Phase::range_start?ui::TableDialogStage::start:ui::TableDialogStage::end;
                    const auto outcome=run_range(modal_backdrop,*page->dialog);
                    ui::LeagueFamilyRequest reply;
                    reply.identity=page->identity;reply.kind=ui::LeagueFamilyRequestKind::table_range_result;
                    reply.from_child=1;reply.control_index=9;
                    reply.table_dialog_reply=ui::TableDialogReply{stage,
                        outcome.result==ui::TableRangeDialogResult::accept?ui::TableDialogOutcome::accept:ui::TableDialogOutcome::cancel,
                        outcome.result==ui::TableRangeDialogResult::accept?int(outcome.value):0,outcome.x,outcome.y};
                    page=staged.dispatch_league_table_range_reply(reply,selection_revision,error);
                    if(!page)throw FormatError(error);
                }
                rebuild();show_table();
            }
        }
        SDL_Delay(10);
    }
    throw FormatError("poach: player selection presenter stopped");
}
