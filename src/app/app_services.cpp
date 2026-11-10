// SPDX-License-Identifier: GPL-3.0-only
// App: the timers and deadlines the event loop serves between inputs (process timer, table, balance, pairings, stadium, friendly match pickers).
// (moved out of main.cpp unchanged; the class is declared in app/app.hpp)
#include "app/app.hpp"

// Seconds since this process started; the time source for the process timer.
auto App::process_source_seconds() -> uint64_t {
    return uint64_t(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now()-process_clock_origin).count());
}

// Returns the image with the process timer bar painted on it while the timer runs.
auto App::process_timer_image(Image image) -> bmh::Image {
    if (!process_timer.paused() && process_timer_frame)
        ui::ProcessTimer::paint(image, *process_timer_frame, season_exe);
    return image;
}

// Sample writes only y0. Nested synchronous presenters poll the
// same Desk-owned clock without re-entering Session/Flow dispatch.
// Samples the process timer. When the bar changed and update_texture is set, only the
// top pixel row of the texture is refreshed (and presented if `present`). Returns true
// once the timer has expired.
auto App::poll_process_timer(bool update_texture, bool present) -> bool {
    const auto sample = process_timer.sample(process_source_seconds());
    const bool changed_bar = sample && !sample->expired &&
        (!process_timer_frame || sample->percent != process_timer_frame->percent);
    process_timer_frame = sample;
    if (changed_bar && update_texture) {
        Image bar(320,240);
        ui::ProcessTimer::paint(bar,*sample,season_exe);
        for (size_t x=0;x<320;++x) pixels[x]=palette.argb[bar.pixels[x]];
        const SDL_Rect top{0,0,320,1};
        if (SDL_UpdateTexture(texture,&top,pixels.data(),320*4)!=0)
            throw std::runtime_error(std::string("Process timer texture: ")+SDL_GetError());
        if (present) {
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer,texture,nullptr,nullptr);
            app::present(renderer);
        }
    }
    return sample && sample->expired;
}

// Flashes the formation-saved feedback image, then shows the restored screen.
auto App::present_formation_save_feedback() -> void {
    for (bool restored : {false, true}) {
        const auto feedback = flow.pitch_formation_save_feedback(restored);
        if (!feedback) break;
        const auto image = process_timer_image(*feedback);
        for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = palette.argb[image.pixels[i]];
        if (SDL_UpdateTexture(texture, nullptr, pixels.data(), 320 * 4) != 0 ||
            SDL_RenderClear(renderer) != 0 || SDL_RenderCopy(renderer, texture, nullptr, nullptr) != 0)
            throw std::runtime_error(std::string("Formation save renderer: ") + SDL_GetError());
        app::present(renderer);
        if (!restored) SDL_Delay(ui::PitchScreen::formation_save_flash_ms);
    }
}

// Called every loop pass. If the left button has been held long enough on the pitch
// screen at the position where it was pressed, triggers the hold action once.
auto App::service_pitch_hold() -> void {
    if (!press || press->pitch_hold_attempted || !flow.in_pitch() ||
        press->screen != flow.screen_name() || press->revision != revision) return;
    const auto ticks = ui::pointer_hold_source_ticks(SDL_GetTicks64()) - press->source_tick;
    if (!ui::pointer_source_hold_ready(ticks)) return;
    int window_x=0,window_y=0;
    if (!(SDL_GetMouseState(&window_x,&window_y) & SDL_BUTTON_LMASK)) return;
    float x=0,y=0;
    SDL_RenderWindowToLogical(renderer,window_x,window_y,&x,&y);
    if (int(x)!=press->x || int(y)!=press->y) { press.reset(); return; }
    press->pitch_hold_attempted = true;
    const auto held = flow.pitch_held_left(press->x,press->y,ticks);
    present_formation_save_feedback();
    handle(held);
}

// Seconds since the loop started in the pairings screen's source units.
auto App::pairings_source_seconds() -> uint64_t {
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - pairings_clock_start).count();
    return ui::league_pairings_source_seconds(uint64_t(milliseconds));
}

// Milliseconds since the same clock started; the base for the table, balance and
// calculator timers below.
auto App::balance_source_milliseconds() -> uint64_t {
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now()-pairings_clock_start).count());
}

// DecodeTaxAnimationScript2 multiplies its argument by 20 before host timer's
// millisecond conversion. This is the host unit scale, not a claim
// of parity with the Original DOS clock's quantization.
// Elapsed time in 20 ms source units.
auto App::family_table_source_units() -> uint64_t {
    return balance_source_milliseconds() / 20;
}

// Tells the flow the table frame has reached the screen; it may answer with a request.
auto App::mark_family_table_presented() -> void {
    const auto acknowledged = flow.in_toto_calculator()
        ? flow.after_toto_present(family_table_source_units())
        : flow.after_family_table_present(family_table_source_units());
    if (acknowledged.request != ui::FlowRequest::none) handle(acknowledged);
    else dirty |= acknowledged.changed;
}

// Redraws the league table or toto screen when dirty, then marks it as presented.
auto App::present_family_table_redraw() -> void {
    if ((!flow.in_family_table() && flow.screen_name() != "toto") || !dirty) return;
    const auto image = process_timer_image(flow.render());
    for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = palette.argb[image.pixels[i]];
    if (SDL_UpdateTexture(texture, nullptr, pixels.data(), 320 * 4) != 0 ||
        SDL_RenderClear(renderer) != 0 || SDL_RenderCopy(renderer, texture, nullptr, nullptr) != 0)
        throw std::runtime_error(std::string("Table renderer: ") + SDL_GetError());
    app::present(renderer);
    dirty = false;
    mark_family_table_presented();
}

// Collects typed digits, ENTER, BACKSPACE and SPACE for those screens. Returns true when
// the event belongs to them (unsupported keys are swallowed too).
auto App::queue_table_range_event(const SDL_Event& event) -> bool {
    if (!flow.in_toto_calculator() &&
        (!flow.in_family_table_range_dialog() || !flow.league_family_identity())) return false;
    const auto queue = [&](std::string key) {
        if (flow.in_toto_calculator())
            table_range_keys.push_back({std::move(key), {}, flow.toto()->state().manager_slot,
                                        flow.toto()->state().revision});
        else table_range_keys.push_back({std::move(key), *flow.league_family_identity()});
    };
    if (event.type == SDL_TEXTINPUT) {
        const std::string_view text(event.text.text);
        if (std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; }))
            for (char c : text) queue(std::string(1, c));
        return true;
    }
    if (event.type != SDL_KEYDOWN) return false;
    if (event.key.keysym.sym == SDLK_RETURN) queue("ENTER");
    else if (event.key.keysym.sym == SDLK_BACKSPACE) queue("BACKSPACE");
    else if (event.key.keysym.sym == SDLK_SPACE) queue("SPACE");
    // No SDL Escape/right-button origin for Source raw event 8 has
    // been established for this modal. Unsupported keys stay owned.
    return true;
}

// Called every loop pass while a table or toto screen is open: advances its timers,
// redraws, and feeds queued keys whenever the screen is ready for input.
auto App::service_family_table() -> void {
    if (!flow.in_family_table() && flow.screen_name() != "toto") { table_range_keys.clear(); return; }
    const auto tick = flow.in_toto_calculator()
        ? flow.tick_toto_source_units(family_table_source_units())
        : flow.tick_family_table_source_units(family_table_source_units());
    if (tick.request != ui::FlowRequest::none) handle(tick);
    else dirty |= tick.changed;
    present_family_table_redraw();
    if (!flow.in_family_table_range_dialog() && !flow.in_toto_calculator()) table_range_keys.clear();
    while (!table_range_keys.empty() &&
           (flow.family_table_range_input_ready() || flow.toto_calculator_input_ready())) {
        auto input = std::move(table_range_keys.front());
        table_range_keys.pop_front();
        if ((input.toto_manager >= 0 && flow.in_toto_calculator() &&
             input.toto_manager == flow.toto()->state().manager_slot &&
             input.toto_revision == flow.toto()->state().revision) ||
            (input.toto_manager < 0 && flow.league_family_identity() && input.identity == *flow.league_family_identity())) {
            handle(flow.send_key(input.key));
            present_family_table_redraw();
        }
    }
}

// Presents the balance screen only when its identity or notice state changed.
auto App::present_balance_redraw() -> void {
    if (!flow.in_balance()) { presented_balance.reset();presented_balance_notice=false; return; }
    const auto identity=flow.balance()->state().identity;
    const bool notice=flow.balance()->notice_open();
    if(presented_balance&&*presented_balance==identity&&notice==presented_balance_notice)return;
    const auto image=process_timer_image(flow.render());
    canvas.show(image, palette);
    flow.mark_balance_presented_source_milliseconds(balance_source_milliseconds());
    presented_balance=identity;presented_balance_notice=notice;dirty=false;
}

// Called every loop pass: advances the balance screen's millisecond timer.
auto App::service_balance_timer() -> void {
    present_balance_redraw();
    const auto tick=flow.tick_balance_source_milliseconds(balance_source_milliseconds());
    if(tick.request!=ui::FlowRequest::none){handle(tick);present_balance_redraw();}
    else if(tick.changed){dirty=true;presented_balance.reset();}
}

// Queues a key for the current stadium calculator owner.
auto App::queue_calculator_key(std::string key) -> void {
    const auto& owner = flow.stadium()->state();
    calculator_keys.push_back({std::move(key), owner.revision, owner.scope, owner.sequence});
}

// Collects typed digits, ENTER, BACKSPACE and SPACE for the stadium calculator; returns
// true when the event is consumed.
auto App::queue_calculator_event(const SDL_Event& event) -> bool {
    if (event.type == SDL_TEXTINPUT) {
        const std::string_view text(event.text.text);
        if (!std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; }))
            return true;
        for (const char c : text) queue_calculator_key(std::string(1, c));
        return true;
    }
    if (event.type != SDL_KEYDOWN) return false;
    if (event.key.keysym.sym == SDLK_RETURN) queue_calculator_key("ENTER");
    else if (event.key.keysym.sym == SDLK_BACKSPACE) queue_calculator_key("BACKSPACE");
    else if (event.key.keysym.sym == SDLK_SPACE) queue_calculator_key("SPACE");
    // The calculator owns every keyboard event. Unsupported keys,
    // including Escape, must not reach the outer application shortcut.
    // Digits arrive separately through SDL_TEXTINPUT.
    return true;
}

// Starts the calculator's wait clock once its frame has reached the screen.
auto App::mark_stadium_calculator_presented() -> void {
    if (!flow.in_stadium() || !flow.stadium()->calculator_source_waiting()) {
        calculator_tick_anchor.reset();
        return;
    }
    flow.mark_stadium_calculator_presented();
    if (!calculator_tick_anchor) calculator_tick_anchor = balance_source_milliseconds();
}

// Called every loop pass while the stadium screen is open: converts elapsed time into
// source ticks for the calculator's waits and feeds queued keys.
auto App::service_stadium_calculator() -> void {
    if (!flow.in_stadium()) {
        calculator_release.reset(); calculator_tick_anchor.reset();
        calculator_keys.clear();
        return;
    }
    if (flow.stadium()->calculator_source_waiting() && calculator_tick_anchor) {
        const uint64_t now = balance_source_milliseconds();
        const uint64_t ticks = (now - *calculator_tick_anchor) / 20;
        if (ticks != 0) {
            // No wait exceeds fifty ticks, so bound narrowing and keep
            // the unused remainder for the next physical-clock sample.
            const unsigned step = unsigned(std::min<uint64_t>(ticks, 50));
            *calculator_tick_anchor += uint64_t(step) * 20;
            const auto tick = flow.tick_stadium_calculator_source_ticks(step);
            if (tick.request != ui::FlowRequest::none) handle(tick);
            else dirty |= tick.changed;
        }
    }
    if (!flow.in_stadium()) {
        calculator_release.reset(); calculator_tick_anchor.reset();
        calculator_keys.clear();
        return;
    }
    if (!flow.stadium()->calculator_source_waiting()) {
        calculator_tick_anchor.reset();
        // The remembered release is delivered only if the calculator owner is unchanged and still
        // waits for the key release.
        if (calculator_release) {
            const auto release = *calculator_release;
            calculator_release.reset();
            const auto& owner = flow.stadium()->state();
            if (owner.scope == release.scope && owner.sequence == release.sequence &&
                flow.stadium()->calculator_waiting_for_key_release())
                handle(flow.click(release.button.x, release.button.y));
            press.reset();
        }
    }
    if (!flow.stadium()->calculator_open()) calculator_keys.clear();
    while (!calculator_keys.empty() && !flow.stadium()->calculator_source_waiting() &&
           !flow.stadium()->calculator_waiting_for_key_release() && !flow.stadium()->pending()) {
        auto input = std::move(calculator_keys.front());
        calculator_keys.pop_front();
        const auto& owner = flow.stadium()->state();
        if (owner.revision == input.revision && owner.scope == input.scope &&
            owner.sequence == input.sequence)
            handle(flow.send_key(input.key));
    }
}

// Presents the league pairings screen when its page identity changed.
auto App::present_pairings_redraw() -> void {
    if (!flow.in_league_pairings()) { presented_pairings.reset(); return; }
    const auto identity = flow.league_pairings()->state().identity;
    if (presented_pairings && *presented_pairings == identity) return;
    const auto image = process_timer_image(flow.render());
    canvas.show(image, palette);
    flow.mark_league_pairings_presented_source_seconds(pairings_source_seconds());
    presented_pairings = identity;
    dirty = false;
}

// Called every loop pass: advances the pairings screen's second timer.
auto App::service_pairings_timer() -> void {
    present_pairings_redraw();
    const auto tick = flow.tick_league_pairings_source_seconds(pairings_source_seconds());
    if (tick.request != ui::FlowRequest::none) {
        handle(tick);
        // Move anchors its clock after each accepted full redraw.
        present_pairings_redraw();
    }
}

// Identity of the stadium tip/quote question currently open, if any.
auto App::stadium_question_frame() -> std::optional<StadiumQuestionFrame> {
    if (!session || !flow.in_stadium()) return {};
    const auto& question=flow.stadium()->state();
    if (!question.construction_tip_dialog && !question.periphery_quote_dialog) return {};
    const auto& state=flow.stadium()->state();
    return StadiumQuestionFrame{session->session_id(),uint64_t(state.manager_slot),state.scope,state.sequence,state.revision};
}

// Records the current stadium question frame as presented.
auto App::mark_stadium_question_presented() -> void { presented_stadium_question=stadium_question_frame(); }

// If the presented stadium question has expired, hands the expiry to the flow. Returns
// true when it did.
auto App::service_stadium_deadline() -> bool {
    const auto owner=stadium_question_frame();
    if (!owner || owner!=presented_stadium_question) return false;
    if (!session->stadium_tip_deadline_expired() && !poll_process_timer(false,false)) return false;
    const auto expired = flow.expire_stadium_tip();
    if (expired.request == ui::FlowRequest::none) return false;
    handle(expired);
    return true;
}

// Same check from the idle path, but not while the question's button is being held.
auto App::service_idle_stadium_deadline() -> void {
    // OriginalMouseSound/Sample can synthesize event8 on expiry, including idle.
    // ExpireQuestion's held-button helper must return before57ef7 answers.
    if (flow.in_stadium() && !flow.stadium()->construction_tip_held())
        service_stadium_deadline();
}


auto App::national_picker_frame() -> std::optional<NationalPickerFrame> {
    if (!session || flow.screen_name() != "national_friendly_picker" || !flow.national_friendly_owner()) return {};
    return NationalPickerFrame{session->session_id(),*flow.national_friendly_owner()};
}


auto App::mark_national_picker_presented() -> void { presented_national_picker=national_picker_frame(); }

// Identity of the friendly opponent picker currently open, if any.
auto App::friendly_picker_frame() -> std::optional<FriendlyPickerFrame> {
    const auto* picker=flow.friendly_opponent_picker();
    if (!session || !picker || !flow.desktop()) return {};
    const auto& desk=flow.desktop()->state();
    return FriendlyPickerFrame{session->session_id(),uint64_t(desk.manager_slot),
        uint64_t(desk.runtime_club),desk.revision,uint64_t(picker->selected_league())};
}


auto App::mark_friendly_picker_presented() -> void { presented_friendly_picker=friendly_picker_frame(); }


auto App::friendly_calendar_frame() -> std::optional<FriendlyCalendarFrame> {
    if (!session || !flow.friendly_calendar_quote()) return {};
    const auto& quote=*flow.friendly_calendar_quote();
    return FriendlyCalendarFrame{session->session_id(),uint64_t(quote.manager_slot),
        uint64_t(quote.runtime_team),quote.revision,quote.request_id};
}


auto App::mark_friendly_calendar_presented() -> void { presented_friendly_calendar=friendly_calendar_frame(); }


auto App::friendly_question_frame() -> std::optional<FriendlyQuestionFrame> {
    const auto* question=flow.friendly_question_screen();
    if(!session||!question||!flow.desktop())return {};
    const auto& desk=flow.desktop()->state();
    return FriendlyQuestionFrame{session->session_id(),uint64_t(desk.manager_slot),
        uint64_t(desk.runtime_club),desk.revision,uint64_t(question->stage()),
        uint64_t(question->selected_opponent()),question->quote()?question->quote()->request_id:0,
        uint64_t(question->active_question()->source_manager_flags)};
}


auto App::mark_friendly_question_presented() -> void { presented_friendly_question=friendly_question_frame(); }

// If the presented friendly question has expired, hands the expiry to the flow. Returns
// true when it did.
auto App::service_friendly_question_deadline() -> bool {
    const auto owner=friendly_question_frame();
    if(!owner||owner!=presented_friendly_question||!poll_process_timer(false,false))return false;
    const auto expired=flow.expire_friendly_question(true);
    if(expired.request==ui::FlowRequest::none&&!expired.changed)return false;
    handle(expired);return true;
}

// Called every loop pass and before each event. On the desktop it starts or resumes the
// process timer for the current session/manager/day and polls it. It does nothing until
// the modal frames above have been presented and while a button is held. On expiry the
// flow is told with the current logical mouse position.
auto App::service_process_timer() -> void {
    // SDLFlushEvents3's notice owns its wait. Keep the desk clock polling,
    // but hand its outer expiry back only after this notice restores.
    if(flow.timed_notice_open()) { poll_process_timer(!dirty,false); return; }
    if (!session || !flow.desktop()) {
        process_timer.stop(); process_timer_owner.reset(); process_timer_frame.reset();
        return;
    }
    if (flow.screen_name() == "desktop") {
        const auto& desk = flow.desktop()->state();
        const ProcessTimerOwner owner{session->session_id(), desk.manager_slot, desk.day_counter};
        const auto minutes = session->state().memory.u8(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField);
        if (!process_timer_owner || *process_timer_owner != owner) {
            process_timer.start(minutes, process_source_seconds()); process_timer_owner = owner;
        } else if (process_timer.paused()) {
            process_timer.resume(minutes, process_source_seconds());
        }
    }
    // ExpireQuestion's held controls and Source calculator waits own their
    // release. The outer event8 is serviced after that owner returns.
    //1e06b paints its initial nation bank before the first2de66 poll.
    const auto picker_owner=national_picker_frame();
    if (picker_owner && picker_owner!=presented_national_picker) return;
    //50775 paints its accepted league before its first51135/OriginalMouseSound poll.
    const auto friendly_picker_owner=friendly_picker_frame();
    if (!friendly_picker_owner) presented_friendly_picker.reset();
    if (friendly_picker_owner && friendly_picker_owner!=presented_friendly_picker) return;
    //CalendarCell presents the initial calendar before its input loop.
    const auto calendar_owner=friendly_calendar_frame();
    if (calendar_owner && calendar_owner!=presented_friendly_calendar) return;
    const auto question_owner=stadium_question_frame();
    if (question_owner && question_owner!=presented_stadium_question) return;
    const auto friendly_question_owner=friendly_question_frame();
    if(!friendly_question_owner)presented_friendly_question.reset();
    if(friendly_question_owner&&friendly_question_owner!=presented_friendly_question)return;
    if (press || (flow.friendly_question_screen()&&flow.friendly_question_screen()->question_held())) {
        //ExpireQuestion still polls the clock while holding its button. Only
        //the timer row advances; the answer and frame ACK stay owned.
        if (question_owner || friendly_picker_owner || friendly_question_owner) poll_process_timer(!dirty,false);
        return;
    }
    // A clock repaint never re-enters a table/forecast draw or ACK.
    if (poll_process_timer(!dirty, false)) {
        int window_x=0,window_y=0;
        SDL_GetMouseState(&window_x,&window_y);
        float logical_x=0,logical_y=0;
        SDL_RenderWindowToLogical(renderer,window_x,window_y,&logical_x,&logical_y);
        const auto expired = flow.process_timer_expired(int(logical_x),int(logical_y));
        if (expired.request != ui::FlowRequest::none || expired.changed) handle(expired);
    }
}
