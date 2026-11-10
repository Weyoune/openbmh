// SPDX-License-Identifier: GPL-3.0-only
// Entry point of the SDL front end (openbmh_play) and the owner of everything the player
// sees and hears: window, renderer, 320x240 indexed canvas, audio Playback, quit handling.
// It parses the command line, resolves the game data (src/launch), builds ui::StartupFlow and
// then runs the event loops. The request/acknowledge model works like this:
//   - Input events go to the StartupFlow; a click or key may return a FlowResult carrying a
//     FlowRequest (kickoff, desk action, transfer, save/load, ...).
//   - main.cpp's handle() passes that result to app::serve_flow_request() (controller.cpp),
//     together with the shared revision counter and a large set of presenter lambdas.
//   - The controller asks the CareerSession (career_session.cpp, the engine side) to do the
//     work and acknowledges the outcome to the flow; the engine may call a presenter lambda
//     back, synchronously, to show a modal dialog or a live match frame, which runs its own
//     nested event loop here until the player answers.
//   - A non-empty message from the controller is printed to stderr; "career_complete" ends
//     the program; the screen is redrawn only when the flow marks it dirty.
// The engine never draws and the UI never advances the engine on its own.
#include "formats/badge_policy.hpp"
#include "formats/executable_layout.hpp"
#include "app/start_screen.hpp"
#include "ui/live_timing.hpp"
// openbmh_play - the UI of src/ui connected to the engine: options, manager dialogs, a new
// career initialised by the engine (app/career_session.hpp) and its desk.
//
// Usage: openbmh_play [<game>] [--seed <Watcom seed>] [--save-directory <directory>] [--mute] [--volume <0..1>] [--music] [--live-frame-ms <0..1000>]
//   <game> (also as --data <game>): the game directory, its BMH.EXE, or a ZIP/7z archive with
//   exactly one game (src/launch). Without a game the system's file dialog asks for one.
#include <SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "app/canvas.hpp"
#include "app/controller.hpp"
#include "app/quit_confirmation.hpp"
#include "launch/archive.hpp"
#include "launch/file_dialog.hpp"
#include "launch/game_data.hpp"
#include "version.hpp"
#include "app/video_session.hpp"
#include "app/live_matchday_events.hpp"
#include "app/debug_log.hpp"
#include "app/pitch_pointer.hpp"
#include "audio/playback.hpp"
#include "app/audio.hpp"
#include "formats/palette.hpp"
#include "formats/textd.hpp"
#include "ui/flow.hpp"
#include "ui/source_hold.hpp"
#include "ui/live_matchday.hpp"
#include "ui/live_reality.hpp"
#include "ui/reality_reporter.hpp"
#include "ui/league_table.hpp"
#include "ui/league_pairings.hpp"
#include "ui/other_games.hpp"
#include "ui/process_timer.hpp"
#include "ui/strengths.hpp"
#include "ui/league_table_range_dialog.hpp"
#include "ui/settings.hpp"
#include "ui/lineup.hpp"
#include "ui/tactics.hpp"
#include "ui/sportschau.hpp"
#include "ui/incoming_transfer.hpp"
#include "ui/outgoing_transfer.hpp"
#include "ui/pending_offer.hpp"
#include "ui/season_entrants.hpp"
#include "ui/season_promotion.hpp"
#include "ui/cup_draw.hpp"
#include "ui/club_info.hpp"
#include "ui/team_statistics.hpp"
#include "ui/club_player_detail.hpp"
#include "ui/game_end.hpp"
#include "ui/game_end_alternate.hpp"
#include "ui/game_end_tax_animation.hpp"
#include "ui/contracts.hpp"
#include "game/month.hpp"
#include "game/driver.hpp"
#include "game/messages.hpp"

namespace fs = std::filesystem;
using namespace bmh;

// Why the game cannot start: a message box, and the same text on stderr.
// Reports a fatal startup problem: prints it to stderr and shows it in a message box.
// Called for failures before or while the window is being set up.
static void show_start_error(const std::string& text) {
    std::fprintf(stderr, "%s\n", text.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenBMH", text.c_str(), nullptr);
}

// Reattach the native editor when an in-game filename field takes focus. Cocoa
// owns a field-editor responder in addition to SDL's TEXTINPUT enable flag.
// Moves the platform text-input rectangle over the save-file name field (logical canvas
// coordinates 106,209 to 262,219 converted to window pixels) and restarts text input.
// Called when an in-game filename field gains focus.
static void focus_filename_input(SDL_Renderer* renderer) {
    int left, top, right, bottom;
    SDL_RenderLogicalToWindow(renderer, 106, 209, &left, &top);
    SDL_RenderLogicalToWindow(renderer, 262, 219, &right, &bottom);
    const SDL_Rect field{left, top, std::max(1, right - left), std::max(1, bottom - top)};
    SDL_StopTextInput();
    SDL_SetTextInputRect(&field);
    SDL_StartTextInput();
}

#ifdef _WIN32
// The game is a Windows GUI program (no console window on double-click). Started from a
// console, it writes --version, --help and start errors there (kernel32 AttachConsole).
extern "C" __declspec(dllimport) int __stdcall AttachConsole(unsigned long process_id);
// Windows only: reconnects stdout/stderr to the parent console when they are not valid streams.
static void attach_parent_console() {
    // (an output redirected to a file or pipe stays there; a GUI program without one has no
    //  valid stream, fileno < 0)
    const bool out_free = _fileno(stdout) < 0, err_free = _fileno(stderr) < 0;
    if ((out_free || err_free) && AttachConsole(static_cast<unsigned long>(-1))) {  // ATTACH_PARENT_PROCESS
        std::FILE* f = nullptr;
        if (out_free) freopen_s(&f, "CONOUT$", "w", stdout);
        if (err_free) freopen_s(&f, "CONOUT$", "w", stderr);
    }
}
#else
static void attach_parent_console() {}
#endif

// =====================================================================================
// Startup and option parsing
// =====================================================================================
// Program entry. Order of work: parse the command line, find and validate the game data,
// start the optional debug log, then build the window/renderer/audio and the presenter
// lambdas, and finally run the main event loop.
// The running game program: everything main() does after the window is set up.
//
// main() used to hold all of this in one function of about 4,300 lines: the state of the window
// program as locals and every presenter (season end, Sportschau, tax, promotion, cup draw, club
// dialogs, live match, ...) as a lambda capturing them. App holds the same things as members: the
// objects main() created (as references under their old names), the state, one member function per
// former lambda in the original order, and run() with the statements that stood between them (the
// wiring of the hooks, the title screen, the event loop, the tear-down). The code inside moved
// unchanged.
struct App {
    fs::path& data;
    fs::path& save_directory;
    uint32_t& seed;
    bool& music_enabled;
    int& live_frame_ms;
    bool& live_frame_ms_explicit;
    bool& english_game;
    const Palette& palette;
    ui::StartupFlow& flow;
    std::unique_ptr<app::CareerSession>& session;
    uint64_t& revision;
    openbmh::audio::Playback& playback;
    std::string& audio_error;
    std::optional<uint64_t>& audio_session;
    SDL_Window*& window;
    SDL_Renderer*& renderer;
    SDL_Texture*& texture;
    app::PitchPointer& pitch_pointer;
    std::vector<uint32_t>& pixels;
    app::Canvas& canvas;
    ui::LiveMatchdayScreen& live_screen;
    ui::RealityReporterPanel& reality_reporter;
    ui::SportschauIntro& sportschau_intro;
    ui::MatchdayIntro& matchday_intro;
    ui::SportschauEleven& sportschau_eleven;
    ui::SportschauTeamInfo& sportschau_team_info;
    ui::CompetitionWinnerScreen& competition_winner;
    const LeImage& season_exe;
    app::QuitConfirmation& quit_confirmation;
    app::OriginalMouseSound& mouse_sound;
    const TextDb& season_texts;
    const std::chrono::steady_clock::time_point& process_clock_origin;

    // Seconds since this process started; the time source for the process timer.
    auto process_source_seconds() {
        return uint64_t(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now()-process_clock_origin).count());
    }

    ui::ProcessTimer process_timer;

    // Identifies which session/manager/day a process timer belongs to.
    struct ProcessTimerOwner {
        uint64_t session;
        int manager;
        uint32_t day;
        bool operator==(const ProcessTimerOwner&) const = default;
    };

    std::optional<ProcessTimerOwner> process_timer_owner;

    std::optional<ui::ProcessTimer::Sample> process_timer_frame;

    // Returns the image with the process timer bar painted on it while the timer runs.
    auto process_timer_image(Image image) {
        if (!process_timer.paused() && process_timer_frame)
            ui::ProcessTimer::paint(image, *process_timer_frame, season_exe);
        return image;
    }

    // Sample writes only y0. Nested synchronous presenters poll the
    // same Desk-owned clock without re-entering Session/Flow dispatch.
    // Samples the process timer. When the bar changed and update_texture is set, only the
    // top pixel row of the texture is refreshed (and presented if `present`). Returns true
    // once the timer has expired.
    auto poll_process_timer(bool update_texture, bool present) {
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

    // `running` ends every loop below; `dirty` requests a redraw of the flow screen.
    bool running = true, dirty = true;

    // Remembers the current left mouse press (position, time, screen, source tick, revision)
    // so the matching release can be judged against it later.
    struct Press {
        int x, y;
        uint32_t time;
        std::string screen;
        uint64_t source_tick, revision;
        bool pitch_hold_attempted = false;
    };

    std::optional<Press> press;

    std::optional<std::string> right_press_screen;

    // Converts the engine's transfer offer list into the UI's IncomingTransferDecisionView.
    auto transfer_view(const GameState& state, const TransferOfferListView& offers, int viewer_manager) {
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

    std::optional<Image> season_backdrop;

    // Paints a 320x240 indexed image through the palette into the texture and presents it.
    // Also sets the window title and remembers the image as the backdrop for later dialogs.
    auto present_season(const Image& image, const char* title,
                              const Palette* palette_override = nullptr) {
        SDL_SetWindowTitle(window, title);
        season_backdrop = image;
        const Palette& frame_palette = palette_override ? *palette_override : palette;
        const Image painted = process_timer_image(image);
        canvas.show(painted, frame_palette);
    }

    // Plays the highscore point animation of the desk, one rendered frame per step, if active.
    auto present_desk_record_points() {
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
    auto contract_expiry_presenter(GameState& staged, WatcomRandom& random,
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
    auto game_end_options(const GameState& state) {
        return ui::GameEndRenderOptions{
            state.memory.has(::bmh::layout::PlainBackground) && state.memory.u8(::bmh::layout::PlainBackground) != 0,
            (!state.memory.has(::bmh::layout::TextGradientDisabled) || state.memory.u8(::bmh::layout::TextGradientDisabled) == 0) && season_exe.u8(::bmh::layout::TextGradientMode) == 2};
    }

    // Blocks until a fresh left or right mouse press (pending mouse events are flushed first)
    // or quit.
    auto wait_season_ack() {
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

    // Same acknowledgement wait as above, used by the Sportschau screens.
    auto wait_sportschau_ack() {
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
    auto source_tick_wait(unsigned ticks) {
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
    auto wait_source_retraces(unsigned count) {
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

    // --- Sportschau (results show) presenters: the engine calls these hooks synchronously. ---
    SportschauPresentationHooks sportschau_presenter;

    // Builds the render options (panel background, gradient text) for Sportschau screens.
    auto sportschau_context(const GameState& state) {
        ui::SportschauReportContext context;
        context.solid_panel_background = state.memory.has(::bmh::layout::SolidPanelBackground) && state.memory.u8(::bmh::layout::SolidPanelBackground) != 0;
        context.gradient_text = state.memory.has(::bmh::layout::TextGradientDisabled) && state.memory.u8(::bmh::layout::TextGradientDisabled) == 0 &&
                                season_exe.u8(::bmh::layout::TextGradientMode) == 2;
        return context;
    }

    std::optional<ui::SportschauMatchReport> sportschau_replay_screen;

    size_t sportschau_step_index = 0;

    // Modal dialog for the season tax event (question, notice or outcome). Returns true if the
    // player answered Yes. A button counts only when pressed and released on the same button.
    auto show_tax(const GameState& state, const SeasonTaxEvent& event) -> bool {
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

    // Hooks for the season tax events: question via show_tax, notices and the animation below.
    SeasonTaxHooks tax_presenter;

    // Waits without consuming gameplay input; only a quit request is handled.
    auto wait_promotion_time(Uint32 milliseconds) {
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

    // Presents the promotion/relegation table animation (39 steps) unless the manager's
    // animation-skip flag is set.
    SeasonPromotionHook promotion_presenter;

    // Shows a yes/no/acknowledge dialog over a backdrop and runs a modal loop until answered.
    // Questions are bound to the process timer (expiry answers No); timed notices close
    // after 70*3*20 ms. Returns `unsupported` when the program is quitting.
    auto present_pending_dialog(const GameState& state, ui::TaxDialogProjection view,
                                      const Image& backdrop, const char* title,
                                      std::optional<ui::GameEndRenderOptions> effective_options = {}) -> PendingOfferAnswer {
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
    auto loan_return_presenter(GameState& state, WatcomRandom&, int manager,
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
    auto make_pending_offer_hooks(Image backdrop) {
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
    auto make_market_sale_hooks(Image backdrop) {
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

    // Forward declarations: the club, player and statistics presenters call each other, so
    // they are assigned further below.
    std::function<bool(app::ClubModalServices&, int)> present_club_information;

    std::function<std::optional<bool>(app::ClubModalServices&,const ClubPlayerDetailSelection&,const Image&)> present_player_detail;

    // Modal offer editor for a returned loan player. Returns -1 if unsupported or quitting,
    // 0 if cancelled, 1 once the offer was submitted/closed.
    auto loan_return_offer_presenter(GameState& staged, GameRandom& game_random,
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

    // Assigned to the genuine Source submodal presenters below before the
    // controller advances a new career or annual transition.
    std::function<bool(app::ClubModalServices&, const ui::ClubInfoRequest&, const Image&)> present_club_player;

    std::function<void(app::ClubModalServices&, int, const Image&)> present_club_statistics;

    bool poach_selection_active=false;

    std::optional<ClubPlayerDetailSelection> poach_card_selection;

    // Selector the engine calls while the player chooses a player to poach: shows league table
    // pages, the range dialog, club info and player cards in nested modal loops. Returns the
    // chosen player, or std::nullopt when closed or the process timer expired.
    app::CareerSession::PoachPlayerSelector poach_player_selector;

    // CupDrawFromGame local_14 belongs to the whole linked-record traversal. A
    // skipped reveal still presents every overview; a normal reveal ACK or
    // the right press leaving an overview does not skip subsequent groups.
    bool group_reveal_skipped = false;

    // Presents a cup draw: the reveal animation (draw phase) or the overview of a round.
    // In the overview a left click on a club opens its club info, a right click leaves.
    SeasonCupDrawHook cup_draw_presenter;

    // End-of-game highscore presentation: an interactive calculator when the signature is 0,
    // otherwise the table is updated on disk and shown when `display` is set; ends with the
    // final screen.
    auto show_highscore(const GameState& state, bool display) {
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

    std::optional<Uint64> timed_notice_presented_at;

    std::optional<Uint8> timed_notice_release;

    // =====================================================================================
    // Handling FlowResults (requests from the UI flow)
    // =====================================================================================
    // Called by the main loop for every ui::FlowResult. It handles quitting and the process
    // timer, then passes the request to app::serve_flow_request() together with the
    // presenter lambdas defined inside, which the engine may call back synchronously.
    auto handle(const ui::FlowResult& r) {
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
        bool live_skipped = false;
        uint64_t live_overview_sequence = 0;
        std::optional<Image> outcome_backdrop;
        // Live-match intervention: opens the lineup/pitch screen for the clicked team and runs it
        // until closed. Returns true if the intervention was completed successfully.
        auto present_live_lineup = [&](int minute, MatchEngine& engine, std::vector<MatchGroup>& groups,
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
        };
        // Callback type through which the overview screen asks the engine for its actions.
        using RunningOverviewDispatcher = std::function<std::optional<app::CareerSession::LiveOverviewDispatch>(
            const app::CareerSession::LiveOverviewAction&, std::string&,
            const app::TransferOfferTeamInfoPresenter&)>;
        // Presents the overview shown while the matchday is running (league table, other games,
        // statistics) in its own loop, driven by the engine's overview dispatcher.
        const auto present_running_overview = [&](std::optional<app::CareerSession::LiveOverviewProjection> projection,
                const GameState& state, uint64_t& live_revision,
                const RunningOverviewDispatcher& dispatch_overview,
                const PhaseResultRequest* phase_result) {
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
        };
        // State used by the live-match presenters below.
        ui::LiveScoreReveal live_score_reveal;
        std::optional<ui::LiveChanceProjection> live_chance_context;
        std::unique_ptr<ui::RealityFieldScreen> reality_field;
        ui::RealityReporterLayoutState reality_layout;
        Image reality_pristine, reality_surface;
        Palette reality_colors;
        // Hand the flow result to the controller. The arguments after the audio ones are the
        // presenter callbacks the engine may call synchronously (live frame, incoming transfers,
        // transfer decisions, season checkpoints, tax, highscore, promotion, cup draw, offers,
        // club/player screens, notices, poaching, contracts, ...). The returned message is
        // printed to stderr by the caller; "career_complete" ends the program.
        const std::string message = app::serve_flow_request(flow, r, session, data, save_directory, seed, revision,
            playback, audio_session, music_enabled,
            // Callback per simulated match minute: shows the live matchday frame (unless skipped)
            // and waits frame_delay ms, taken from --live-frame-ms or from the game's own speed setting.
            [&](int minute, const std::vector<MatchGroup>& groups, const GameState& state) {
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
            },
            // Callback for an incoming transfer offer: modal decision screen. Returns the chosen bid
            // index, or -1 for decline, nothing to choose or quit.
            [&](const GameState& state, const TransferOfferListView& offers) -> int {
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
            },
            // Callback that presents a transfer decision already settled by the engine: last-week
            // dialog, confirmation, or the animated evaluation of the bids. Clicks cannot change it.
            [&](const GameState& state, const TransferDecisionEvent& decision) {
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
            },
            // Callback at season-transition checkpoints; only the one for the German entrants does
            // anything: it shows the entrant screens, each closed by a mouse release.
            [&](const char* checkpoint, const GameState& state) {
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
            // Further callbacks: tax, highscore, promotion and cup draw presenters (defined above).
            }, tax_presenter, [this](auto&&... a) -> decltype(auto) { return show_highscore(std::forward<decltype(a)>(a)...); }, promotion_presenter, cup_draw_presenter,
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
            // Callback: club information screen (true if the program is still running).
            [&](app::ClubModalServices& services,int team,const Image&) {
                return present_club_information(services,team) && running;
            // Next callback: player detail card.
            }, [&](app::ClubModalServices& services,const ClubPlayerDetailSelection& selection,const Image& parent) {
                // Direct1b556 already owns persistent bbcde=1. The shared
                //ClubPlayerDetailFromGame presenter never restores a temporary club mode here.
                return present_player_detail(services,selection,parent).has_value() && running;
            // Next callback: stadium construction payment notice, shown over the investment detail.
            }, [&](const GameState& staged, const ConstructionNotice& notice) {
                if (!flow.stadium()) throw std::runtime_error("construction notice has no stadium owner");
                // The Original payment notices restore the investment detail,
                // after the tip question has returned, before the outer ACK.
                const Image backdrop = flow.stadium()->construction_notice_backdrop();
                auto view = ui::construction_notice_dialog(staged, season_exe, notice);
                std::optional<ui::GameEndRenderOptions> effective_options;
                if (flow.stadium()->state().block_expansion || flow.stadium()->state().quoted_object) {
                    const auto& owner = flow.stadium()->state();
                    view.compact_dialog_frame = owner.compact_dialog_frame;
                    effective_options = ui::GameEndRenderOptions{owner.plain_background, owner.gradient_text};
                }
                const char* title = notice.kind == ConstructionNoticeKind::InsufficientFunds
                    ? "OpenBMH - stadium_funds_notice" : "OpenBMH - stadium_sponsor_notice";
                if (present_pending_dialog(staged, view, backdrop, title, effective_options) == PendingOfferAnswer::unsupported)
                    throw std::runtime_error("construction notice presenter stopped");
            // Next callback: season ticket compensation notice.
            }, [&](const GameState& staged, const SeasonTicketCompensationNotice& notice) {
                if (!flow.stadium()) throw std::runtime_error("compensation notice has no stadium owner");
                const Image backdrop = flow.stadium()->construction_notice_backdrop();
                auto view = ui::season_ticket_compensation_notice_dialog(staged, season_exe, notice);
                const auto& owner = flow.stadium()->state();
                view.compact_dialog_frame = owner.compact_dialog_frame;
                if (present_pending_dialog(staged, view, backdrop,
                        "OpenBMH - stadium_ticket_compensation_notice",
                        ui::GameEndRenderOptions{owner.plain_background, owner.gradient_text}) == PendingOfferAnswer::unsupported)
                    throw std::runtime_error("compensation notice presenter stopped");
            }, sportschau_presenter,
            // Callback: advertising contract renewal. A question gives renew/decline; a sponsor's
            // refusal is a notice that gives acknowledged; anything else aborts.
            [&](const GameState& staged, const AdvertisingRenewal& renewal, const Image& backdrop)
                -> app::CareerSession::AdvertisingRenewalPresentation {
                if (!running) return app::CareerSession::AdvertisingRenewalPresentation::abort;
                const int manager = int(staged.memory.u8(::bmh::layout::ActiveManager));
                if (renewal.status == AdvertisingRenewal::Status::offered) {
                    const auto view = ui::advertising_renewal_question_dialog(staged, manager, renewal);
                    const auto answer = present_pending_dialog(staged, view, backdrop,
                        "OpenBMH - advertising_renewal_question");
                    if (answer == PendingOfferAnswer::yes) return app::CareerSession::AdvertisingRenewalPresentation::renew;
                    if (answer == PendingOfferAnswer::no) return app::CareerSession::AdvertisingRenewalPresentation::decline;
                    return app::CareerSession::AdvertisingRenewalPresentation::abort;
                }
                if (renewal.status == AdvertisingRenewal::Status::declined_by_sponsor) {
                    const auto view = ui::advertising_renewal_notice_dialog(staged, manager, renewal);
                    if (present_pending_dialog(staged, view, backdrop,
                            "OpenBMH - advertising_renewal_notice") == PendingOfferAnswer::yes)
                        return app::CareerSession::AdvertisingRenewalPresentation::acknowledged;
                }
                return app::CareerSession::AdvertisingRenewalPresentation::abort;
            }, poach_player_selector,
            // Callback: shows the poaching attempt's notices one after another.
            [&](const GameState& staged,const PoachStart& attempt,const Image& backdrop) {
                for(const auto& message:attempt.messages)
                    if(present_pending_dialog(staged,ui::poach_notice_from_game(staged,attempt,message),backdrop,
                        "OpenBMH - poach_notice")==PendingOfferAnswer::unsupported)
                        throw FormatError("poach: notice presenter stopped");
            }, [this](auto&&... a) -> decltype(auto) { return contract_expiry_presenter(std::forward<decltype(a)>(a)...); },
            // Callback: national friendly question. Returns yes/no, or std::nullopt when unsupported
            // or the program is quitting.
            [&](const GameState& staged,ui::TaxDialogProjection view,const Image& backdrop)
                ->std::optional<bool> {
                if(!running)return {};
                const auto answer=present_pending_dialog(staged,std::move(view),backdrop,
                    "OpenBMH - national_friendly_question");
                flow.acknowledge_national_friendly_modal_release();
                // The synchronous questions already consumed their UP;
                // do not retain the picker DOWN as a held parent press.
                press.reset();
                if(answer==PendingOfferAnswer::unsupported)return {};
                return answer==PendingOfferAnswer::yes;
            // The loan-return presenters (defined above), then the live match observer.
            }, [this](auto&&... a) -> decltype(auto) { return loan_return_presenter(std::forward<decltype(a)>(a)...); }, [this](auto&&... a) -> decltype(auto) { return loan_return_offer_presenter(std::forward<decltype(a)>(a)...); },
            // =====================================================================================
            // Live match observer
            // =====================================================================================
            // Called by the engine once per simulated minute of a live match. Besides pausing, it is
            // where the player interacts: ESC or window close skips the live display, P pauses, the
            // keys for overview/table/settings open their modal screens (key letters come from the
            // game executable), clicks on the top strip open an overview or the settings, and clicks
            // on the scoreboard rows open the lineup (left) or tactics (right) of that team.
            [&](int minute, MatchEngine& engine, std::vector<MatchGroup>& groups,
                GameState& state, uint64_t& live_revision,
                const app::LiveLineupDispatcher& dispatch,
                const app::LiveTacticsDispatcher& dispatch_tactic,
                const app::LivePitchDispatcher& dispatch_pitch,
                const app::LiveInterventionScopeDispatcher& dispatch_scope) {
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
            // Next callback: environment/settings notice dialog. Returns false when the program is
            // quitting or the dialog could not be shown.
            }, [&](const GameState& staged, ui::TaxDialogProjection view, const Image& backdrop) {
                if (!running) return false;
                const bool settings_notice = flow.in_settings() && flow.pending_settings();
                const bool shown = present_pending_dialog(staged, std::move(view), backdrop,
                    settings_notice ? "OpenBMH - settings_notice" : "OpenBMH - environment_notice")
                    != PendingOfferAnswer::unsupported;
                if (settings_notice) {
                    flow.acknowledge_settings_notice_release();
                    press.reset();
                    SDL_FlushEvents(SDL_MOUSEMOTION, SDL_MOUSEBUTTONUP);
                }
                return shown;
            // Next callback: a lineup change forced by a live match event; runs the same lineup
            // screen as the click on the scoreboard. It must complete unless the program quits.
            }, [&](const LiveMatchEvent& event, MatchEngine& engine,
                   std::vector<MatchGroup>& groups, GameState& state, uint64_t& live_revision,
                   const app::LiveLineupDispatcher& dispatch,
                   const app::LiveTacticsDispatcher& dispatch_tactic,
                   const app::LivePitchDispatcher& dispatch_pitch,
                   const app::LiveInterventionScopeDispatcher& dispatch_scope) {
                if (!running) return;
                const bool completed = present_live_lineup(event.minute, engine, groups, state, live_revision,
                    event.team, event.opponent, dispatch, dispatch_tactic, dispatch_pitch,
                    dispatch_scope, false, true);
                if (!running) return; // SDL_QUIT aborts the Source scope without failing the event.
                if (!completed)
                    throw FormatError("The Source event line-up did not complete.");
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
            }, [&](const LiveBallEvent& event, const std::vector<MatchGroup>& groups,
                   const GameState& state) {
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
            // Next callback: a scoring chance, shown as a preview and again as its result.
            }, [&](const LiveChanceEvent& event, const std::vector<MatchGroup>& groups,
                   const GameState& state) {
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
            // Next callback: shot animation, drawn with the shot's own palette. After the frames it
            // either waits for a click or applies the saved pause.
            }, [&](const LiveShotEvent& event, const std::vector<MatchGroup>& groups,
                   const GameState& state) {
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
            // Next callback: set piece animation frames (first frame held 40 retraces at 70 Hz).
            }, [&](const LiveSetPieceEvent& event, const std::vector<MatchGroup>& groups,
                   const GameState& state) {
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
            // Next callback: booking/injury box; pause row 3 for kind 0, otherwise 4.
            }, [&](const LiveBookingEvent& event, const std::vector<MatchGroup>& groups,
                   const GameState& state) {
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
            // Next callback: penalty shootout step; the saved pause is repeated 4 times for the
            // preview and twice for the result.
            }, [&](const LiveShootoutEvent& event, const std::vector<MatchGroup>& groups,
                   const GameState& state) {
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
            // Next callback: reality mode (running commentary with a moving field). Stages: begin
            // (builds the screens), one call per round, settling and end. Returns true when the
            // player ended it with ESC or a right press, or when the program quits.
            }, [&](const LiveRealityEvent& event, const std::vector<MatchGroup>& groups,
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
            // Next callback: results viewer at the end of a match phase; throws if it was stopped
            // before being acknowledged.
            }, [&](const PhaseResultRequest& request, const std::vector<MatchGroup>& groups,
                   const GameState& state) {
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
            }, true);
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

    // Flashes the formation-saved feedback image, then shows the restored screen.
    auto present_formation_save_feedback() {
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
    auto service_pitch_hold() {
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

    const std::chrono::steady_clock::time_point pairings_clock_start = std::chrono::steady_clock::now();

    // Seconds since the loop started in the pairings screen's source units.
    auto pairings_source_seconds() {
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - pairings_clock_start).count();
        return ui::league_pairings_source_seconds(uint64_t(milliseconds));
    }

    // Milliseconds since the same clock started; the base for the table, balance and
    // calculator timers below.
    auto balance_source_milliseconds() {
        return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now()-pairings_clock_start).count());
    }

    // DecodeTaxAnimationScript2 multiplies its argument by 20 before host timer's
    // millisecond conversion. This is the host unit scale, not a claim
    // of parity with the Original DOS clock's quantization.
    // Elapsed time in 20 ms source units.
    auto family_table_source_units() {
        return balance_source_milliseconds() / 20;
    }

    // Tells the flow the table frame has reached the screen; it may answer with a request.
    auto mark_family_table_presented() {
        const auto acknowledged = flow.in_toto_calculator()
            ? flow.after_toto_present(family_table_source_units())
            : flow.after_family_table_present(family_table_source_units());
        if (acknowledged.request != ui::FlowRequest::none) handle(acknowledged);
        else dirty |= acknowledged.changed;
    }

    // Redraws the league table or toto screen when dirty, then marks it as presented.
    auto present_family_table_redraw() {
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

    // A key typed for the table range dialog or toto calculator, tagged with its owner so
    // that keys for a screen that has since changed are dropped.
    struct TableRangeKey {
        std::string key;
        ui::StrengthsIdentity identity;
        int toto_manager = -1;
        uint64_t toto_revision = 0;
    };

    std::deque<TableRangeKey> table_range_keys;

    // Collects typed digits, ENTER, BACKSPACE and SPACE for those screens. Returns true when
    // the event belongs to them (unsupported keys are swallowed too).
    auto queue_table_range_event(const SDL_Event& event) {
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
    auto service_family_table() {
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

    // Which balance screen state was last presented (to avoid redrawing identical frames).
    std::optional<ui::BalanceIdentity> presented_balance;

    bool presented_balance_notice=false;

    // Presents the balance screen only when its identity or notice state changed.
    auto present_balance_redraw() {
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
    auto service_balance_timer() {
        present_balance_redraw();
        const auto tick=flow.tick_balance_source_milliseconds(balance_source_milliseconds());
        if(tick.request!=ui::FlowRequest::none){handle(tick);present_balance_redraw();}
        else if(tick.changed){dirty=true;presented_balance.reset();}
    }

    // KB6 waits start after the pressed/final calculator image reaches SDL.
    // Keep a matching physical UP until the four-tick press is over; N's
    // last calculator frame stays visible through its fifty-tick wait.
    // Pending release of a button pressed on the stadium calculator.
    struct CalculatorRelease {
        SDL_MouseButtonEvent button;
        uint64_t scope = 0, sequence = 0;
    };

    std::optional<CalculatorRelease> calculator_release;

    std::optional<uint64_t> calculator_tick_anchor;

    // A calculator key tagged with the owner state it was typed for.
    struct CalculatorKey {
        std::string key;
        uint64_t revision = 0, scope = 0, sequence = 0;
    };

    std::deque<CalculatorKey> calculator_keys;

    // Queues a key for the current stadium calculator owner.
    auto queue_calculator_key(std::string key) {
        const auto& owner = flow.stadium()->state();
        calculator_keys.push_back({std::move(key), owner.revision, owner.scope, owner.sequence});
    }

    // Collects typed digits, ENTER, BACKSPACE and SPACE for the stadium calculator; returns
    // true when the event is consumed.
    auto queue_calculator_event(const SDL_Event& event) {
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
    auto mark_stadium_calculator_presented() {
        if (!flow.in_stadium() || !flow.stadium()->calculator_source_waiting()) {
            calculator_tick_anchor.reset();
            return;
        }
        flow.mark_stadium_calculator_presented();
        if (!calculator_tick_anchor) calculator_tick_anchor = balance_source_milliseconds();
    }

    // Called every loop pass while the stadium screen is open: converts elapsed time into
    // source ticks for the calculator's waits and feeds queued keys.
    auto service_stadium_calculator() {
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

    // Which pairings page was last presented (to avoid redrawing identical frames).
    std::optional<ui::LeaguePairingsIdentity> presented_pairings;

    // Presents the league pairings screen when its page identity changed.
    auto present_pairings_redraw() {
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
    auto service_pairings_timer() {
        present_pairings_redraw();
        const auto tick = flow.tick_league_pairings_source_seconds(pairings_source_seconds());
        if (tick.request != ui::FlowRequest::none) {
            handle(tick);
            // Move anchors its clock after each accepted full redraw.
            present_pairings_redraw();
        }
    }

    //PresentManagerDialog presents its question before polling2de66. Bind the actual
    //freshly drawn frame, never an old texture or a preceding modal ACK.
    // Frame identities: each tuple names the owner state (session, manager, scope, sequence,
    // revision, ...) of a modal question that was actually drawn. A process-timer deadline
    // is applied only to a frame that has been presented, never to a stale one.
    using StadiumQuestionFrame = std::array<uint64_t,5>;

    std::optional<StadiumQuestionFrame> presented_stadium_question;

    // Identity of the stadium tip/quote question currently open, if any.
    auto stadium_question_frame() -> std::optional<StadiumQuestionFrame> {
        if (!session || !flow.in_stadium()) return {};
        const auto& question=flow.stadium()->state();
        if (!question.construction_tip_dialog && !question.periphery_quote_dialog) return {};
        const auto& state=flow.stadium()->state();
        return StadiumQuestionFrame{session->session_id(),uint64_t(state.manager_slot),state.scope,state.sequence,state.revision};
    }

    // Records the current stadium question frame as presented.
    auto mark_stadium_question_presented() { presented_stadium_question=stadium_question_frame(); }

    // If the presented stadium question has expired, hands the expiry to the flow. Returns
    // true when it did.
    auto service_stadium_deadline() {
        const auto owner=stadium_question_frame();
        if (!owner || owner!=presented_stadium_question) return false;
        if (!session->stadium_tip_deadline_expired() && !poll_process_timer(false,false)) return false;
        const auto expired = flow.expire_stadium_tip();
        if (expired.request == ui::FlowRequest::none) return false;
        handle(expired);
        return true;
    }

    // Same check from the idle path, but not while the question's button is being held.
    auto service_idle_stadium_deadline() {
        // OriginalMouseSound/Sample can synthesize event8 on expiry, including idle.
        // ExpireQuestion's held-button helper must return before57ef7 answers.
        if (flow.in_stadium() && !flow.stadium()->construction_tip_held())
            service_stadium_deadline();
    }

    // Same idea for the national friendly picker.
    using NationalPickerFrame = std::pair<uint64_t,ui::NationalFriendlyUiOwner>;

    std::optional<NationalPickerFrame> presented_national_picker;

    auto national_picker_frame() -> std::optional<NationalPickerFrame> {
        if (!session || flow.screen_name() != "national_friendly_picker" || !flow.national_friendly_owner()) return {};
        return NationalPickerFrame{session->session_id(),*flow.national_friendly_owner()};
    }

    auto mark_national_picker_presented() { presented_national_picker=national_picker_frame(); }

    using FriendlyPickerFrame = std::array<uint64_t,5>;

    std::optional<FriendlyPickerFrame> presented_friendly_picker;

    // Identity of the friendly opponent picker currently open, if any.
    auto friendly_picker_frame() -> std::optional<FriendlyPickerFrame> {
        const auto* picker=flow.friendly_opponent_picker();
        if (!session || !picker || !flow.desktop()) return {};
        const auto& desk=flow.desktop()->state();
        return FriendlyPickerFrame{session->session_id(),uint64_t(desk.manager_slot),
            uint64_t(desk.runtime_club),desk.revision,uint64_t(picker->selected_league())};
    }

    auto mark_friendly_picker_presented() { presented_friendly_picker=friendly_picker_frame(); }

    // Same idea for the friendly match calendar.
    using FriendlyCalendarFrame = std::array<uint64_t,5>;

    std::optional<FriendlyCalendarFrame> presented_friendly_calendar;

    auto friendly_calendar_frame() -> std::optional<FriendlyCalendarFrame> {
        if (!session || !flow.friendly_calendar_quote()) return {};
        const auto& quote=*flow.friendly_calendar_quote();
        return FriendlyCalendarFrame{session->session_id(),uint64_t(quote.manager_slot),
            uint64_t(quote.runtime_team),quote.revision,quote.request_id};
    }

    auto mark_friendly_calendar_presented() { presented_friendly_calendar=friendly_calendar_frame(); }

    // Same idea for the friendly match question.
    using FriendlyQuestionFrame = std::array<uint64_t,8>;

    std::optional<FriendlyQuestionFrame> presented_friendly_question;

    auto friendly_question_frame() -> std::optional<FriendlyQuestionFrame> {
        const auto* question=flow.friendly_question_screen();
        if(!session||!question||!flow.desktop())return {};
        const auto& desk=flow.desktop()->state();
        return FriendlyQuestionFrame{session->session_id(),uint64_t(desk.manager_slot),
            uint64_t(desk.runtime_club),desk.revision,uint64_t(question->stage()),
            uint64_t(question->selected_opponent()),question->quote()?question->quote()->request_id:0,
            uint64_t(question->active_question()->source_manager_flags)};
    }

    auto mark_friendly_question_presented() { presented_friendly_question=friendly_question_frame(); }

    // If the presented friendly question has expired, hands the expiry to the flow. Returns
    // true when it did.
    auto service_friendly_question_deadline() {
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
    auto service_process_timer() {
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

    // filename_input_active: the save/load filename field already has focus.
    // setup_button_down: [0] left, [1] right button pressed while on the setup screen.
    bool filename_input_active = false;

    std::array<bool, 2> setup_button_down{};

    // The statements of the former main(): hook wiring, title screen, event loop and tear-down.
    int run() {
        // Plays the Sportschau intro animation frames with fanfare, then waits for a click.
        sportschau_presenter.intro = [&](const GameState&, const SportschauIntroView&) {
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
        };

        // Shows the matchday intro screen for a fixed time; a left click skips it.
        sportschau_presenter.kickoff = [&](const GameState&, const MatchdayIntroView& view) {
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
        };

        // Shows one step of the report replay (log entries growing minute by minute).
        // Returns true when the replay should stop (quit) or be skipped by a left click.
        sportschau_presenter.replay.step = [&](const GameState& state,
                const SportschauFixtureView& fixture, const SportschauReportStep& step) {
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
        };

        // Plays one highlight (header, optional set piece, shot animation) of the replay, using
        // the memory-driven delays, then restores the report screen.
        sportschau_presenter.replay.scene = [&](const GameState& state,
                const SportschauFixtureView& fixture, const SportschauSceneView& scene) {
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
        };

        // Shows the full match report of a fixture and waits for acknowledgement.
        sportschau_presenter.fixture = [&](const GameState& state, const SportschauFixtureView& fixture) {
            if (!running) return;
            ui::SportschauMatchReport screen(data, fixture, sportschau_context(state));
            present_season(screen.render_report(fixture.log_events.size()), "OpenBMH - sportschau_report");
            wait_sportschau_ack();
            sportschau_replay_screen.reset();
        };

        // Shows the competition winner screen.
        sportschau_presenter.winner = [&](const GameState& state, const CompetitionWinnerView& view) {
            if (!running) return;
            const auto& colors = competition_winner.palette_override(view.title);
            present_season(competition_winner.render(state, view),
                ("OpenBMH - competition_winner_" + std::to_string(view.title) + "_" +
                 std::to_string(view.team)).c_str(), colors ? &*colors : nullptr);
            wait_sportschau_ack(); // SDLFlushEvents2 waits for a mouse press; ESC is ignored.
        };

        // Shows the team-of-the-matchday (eleven) screen.
        sportschau_presenter.awards = [&](const GameState& state, const SportschauAwardsView& view) {
            if (!running) return;
            present_season(sportschau_eleven.render(view, sportschau_context(state).gradient_text),
                ("OpenBMH - sportschau_eleven_" + std::to_string(view.group)).c_str());
            wait_sportschau_ack();
        };

        // Shows the team information screen.
        sportschau_presenter.team_info = [&](const GameState& state, const SportschauTeamInfoView& view) {
            if (!running) return;
            present_season(sportschau_team_info.render(view, sportschau_context(state).gradient_text),
                ("OpenBMH - sportschau_teaminfo_" + std::to_string(view.group)).c_str());
            wait_sportschau_ack();
        };

        tax_presenter.choose = [this](auto&&... a) -> decltype(auto) { return show_tax(std::forward<decltype(a)>(a)...); };

        tax_presenter.observe = [&](const GameState& state, const SeasonTaxEvent& event) {
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
        };

        promotion_presenter = [&](const GameState& state, const SeasonPromotionEvent& event) {
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
        };

        // Modal club information screen for `team`. Returns true when it was left normally (or
        // the process timer expired), false when the program is quitting.
        present_club_information = [&](app::ClubModalServices& services, int team) -> bool {
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
        };

        // Opens the player detail card for a row chosen in the club list. The runtime input mode
        // byte is switched for the card and restored afterwards (RAII helper below).
        present_club_player = [&](app::ClubModalServices& services, const ui::ClubInfoRequest& selected, const Image& parent_image) {
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
        };

        // Modal player detail card. Returns the card's close-parent flag, or std::nullopt when
        // the program is quitting.
        present_player_detail = [&](app::ClubModalServices& services,const ClubPlayerDetailSelection& selection,const Image& parent_image) -> std::optional<bool> {
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
        };

        // Modal team statistics screen with a mode toggle; leaves on close, right press or timer.
        present_club_statistics = [&](app::ClubModalServices& services, int team, const Image& parent_frame) {
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
        };

        poach_player_selector =
            [&](app::CareerSession& staged,app::ClubModalServices&,int manager,
                uint64_t& selection_revision,const Image& transfer_backdrop)
                -> std::optional<ClubPlayerDetailSelection> {
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
        };

        cup_draw_presenter = [&](const GameState& state, const SeasonCupDrawEvent& event) {
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
        };

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
};

int main(int argc, char** argv) {
    attach_parent_console();
    const std::vector<std::string> args = launch::utf8_arguments(argc, argv);  // (file names beyond the code page)
    fs::path data, save_directory;
    uint32_t seed = uint32_t(std::chrono::system_clock::now().time_since_epoch().count());
    bool muted = false, music_enabled = true, debug_mode = false;
    int live_frame_ms = 0;
    bool live_frame_ms_explicit = false;
    // Optional environment default for the pause per live-match frame; --live-frame-ms below
    // may override it.
    if (const char* configured = std::getenv("BMH_LIVE_FRAME_MS")) {
        live_frame_ms = std::clamp(std::atoi(configured), 0, 1000);
        live_frame_ms_explicit = true;
    }
    float volume = 1.0F;
    // Option loop (index 0 is the program name). Options that take a value only match when
    // another argument follows ("value"); a first plain argument that does not start with
    // "--" is taken as the game location unless --data already set it.
    for (size_t i = 1; i < args.size(); ++i) {
        const auto is = [&](const char* name) { return args[i] == name; };
        const bool value = i + 1 < args.size();
        if (is("--mute")) {
            muted = true;
        } else if (is("--music")) {
            music_enabled = true;
        } else if (is("--no-music")) {
            music_enabled = false;
        } else if (is("--live-frame-ms") && value) {
            live_frame_ms = std::clamp(std::atoi(args[++i].c_str()), 0, 1000);
            live_frame_ms_explicit = true;
        } else if (is("--data") && value) {
            data = launch::path_from_utf8(args[++i]);
        } else if (is("--seed") && value) {
            seed = uint32_t(std::strtoul(args[++i].c_str(), nullptr, 0));
        } else if (is("-debug") || is("--debug")) {
            debug_mode = true;
        } else if (is("--save-directory") && value) {
            save_directory = launch::path_from_utf8(args[++i]);
        } else if (is("--volume") && value) {
            volume = std::strtof(args[++i].c_str(), nullptr);
        } else if (is("--version")) {
            std::printf("OpenBMH %s\n", bmh::kOpenBmhVersion);
            return 0;
        } else if (is("--help") || is("-h")) {
            std::printf("usage: OpenBMH [<game directory | BMH.EXE | game.zip | game.7z>] [--seed <Watcom seed>] "
                        "[--save-directory <directory>] [--mute] [--volume <0..1>] [--no-music] [--live-frame-ms <0..1000>] [-debug]\n");
            return 0;
        } else if (args[i].rfind("--", 0) != 0 && data.empty()) {
            data = launch::path_from_utf8(args[i]);
        }
    }
    // Start the video session now; a failure is reported to the user and ends the program.
    std::optional<app::VideoSession> video;
    try { video.emplace(); }
    catch (const std::exception& error) { show_start_error(error.what()); return 1; }
    // the game: as given (directory, EXE or archive); else the directory OpenBMH itself lies in,
    // when it holds a playable game (the simplest install: openbmh next to BMH.EXE); else
    // chosen in the file dialog
    if (data.empty()) {
        if (char* base = SDL_GetBasePath()) {
            const fs::path here = launch::path_from_utf8(base);
            SDL_free(base);
            try {
                const launch::ResolvedGame game = launch::resolve_game(here, launch::default_cache_directory());
                if (game.error.empty() && game.check.playable) data = here;
            } catch (const std::exception&) {
                // (not a game directory: fall through to the file dialog)
            }
        }
    }
    if (data.empty()) {
        try {
            const auto chosen = launch::choose_game_file("OpenBMH");
            if (!chosen) return 0;  // cancelled
            data = *chosen;
        } catch (const std::exception& e) {
            show_start_error(std::string(e.what()) +
                             "\nStart OpenBMH with the game as argument: openbmh_play <game directory | BMH.EXE | game.zip>");
            return 1;
        }
    }
    // Validate the chosen game data; the resolved directory (possibly an unpacked cache copy)
    // replaces `data` from here on.
    bool english_game = false;  // (the window title marks the English version as experimental)
    {
        const launch::ResolvedGame game = launch::resolve_game(data, launch::default_cache_directory());
        if (!game.error.empty() || !game.check.playable) {
            show_start_error("The game cannot be started:\n" + launch::describe(game));
            return 1;
        }
        english_game = game.check.family == launch::GameFamily::english;
        // (from an archive: its unpacked copy in the cache, which then also keeps the saves)
        data = game.check.directory;
    }
    if (save_directory.empty()) save_directory = data;
    if (debug_mode) {
        // -debug: log in <saves>/debug, with a savegame after every match day (app/debug_log.hpp).
        if (!app::debug::init(save_directory / "debug")) std::fprintf(stderr, "debug folder cannot be created\n");
        app::debug::log(std::string("OpenBMH ") + bmh::kOpenBmhVersion + ", data " + data.string() + ", saves " + save_directory.string() + ", seed " + std::to_string(seed));
        SDL_version linked; SDL_GetVersion(&linked);
        app::debug::log("SDL " + std::to_string(linked.major) + "." + std::to_string(linked.minor) + "." + std::to_string(linked.patch) + ", platform " + SDL_GetPlatform());
    }
    // =====================================================================================
    // Window, renderer, audio and shared UI state
    // =====================================================================================
    // Everything below lives inside this try block; an exception thrown during setup or the
    // main loop ends in the catch handler at the end of main.
    try {
        const Palette palette = load_palette(ui::asset_path(data, "M3PC.COL"));
        ui::StartupFlow flow(data, save_directory);
        std::unique_ptr<app::CareerSession> session;
        uint64_t revision = 0;
        openbmh::audio::Playback playback;
        std::string audio_error;
        playback.initialize(audio_error);  // Audio is optional; the game continues without a device.
        playback.set_muted(muted);
        playback.set_volume(volume);
        std::optional<uint64_t> audio_session;
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        const std::string window_title = std::string("OpenBMH ") + bmh::kOpenBmhVersion +
                                  (english_game ? " - Football Limited (experimental)" : "");
        // 960x720 initial window = 3 times the 320x240 game canvas; resizable and high-DPI aware.
        SDL_Window* window = SDL_CreateWindow(window_title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        if (!window) throw FormatError(SDL_GetError());
        // Prefer a vsynced renderer, fall back to the software renderer.
        SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
        if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        if (!renderer) throw FormatError(SDL_GetError());
        // The game always draws a 320x240 canvas; SDL scales it to the window and maps mouse
        // coordinates back to these logical units.
        SDL_RenderSetLogicalSize(renderer, 320, 240);
        // Streaming texture that holds the current canvas as ARGB (filled from `pixels`).
        SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 320, 240);
        if (!texture) throw FormatError(SDL_GetError());
        app::PitchPointer pitch_pointer;
        std::vector<uint32_t> pixels(320 * 240);
        // The picture on the window (app/canvas.hpp): canvas.show(image, palette) converts, uploads and presents.
        app::Canvas canvas{renderer, texture, pixels};
        ui::LiveMatchdayScreen live_screen(data);
        ui::RealityReporterPanel reality_reporter(data);
        ui::SportschauIntro sportschau_intro(data);
        ui::MatchdayIntro matchday_intro(data);
        ui::SportschauEleven sportschau_eleven(data);
        ui::SportschauTeamInfo sportschau_team_info(data);
        ui::CompetitionWinnerScreen competition_winner(data);
        ui::set_settings_product_version(bmh::kOpenBmhVersion);
        const auto season_exe = load_le(ui::asset_path(data, "BMH.exe"));
        english_game = english_game || season_exe.u8(layout::LeagueSizes) == 20;
        {
            // The pointer of the original is its default 11x11 bitmap (index 0 transparent, hot spot
            // top left, set when a window object is created); the pitch page draws the same bitmap
            // with its own fill colour. The edition's own bitmap is used for the English version.
            std::array<uint32_t, 121> bitmap{};
            for (uint32_t i = 0; i < bitmap.size(); ++i) {
                const auto index = season_exe.u8(layout::OriginalPointerPixels + i);
                bitmap[i] = index == 0 ? 0 : palette.argb[index];
            }
            pitch_pointer.set_default(bitmap);
            pitch_pointer.show({}, renderer);
        }
        // A robot run (app/robot.hpp: OPENBMH_ROBOT, a test hook) plays a script and answers the quit question itself.
        app::Robot::instance().start(window);
        app::QuitConfirmation quit_confirmation(window, english_game,
            app::Robot::instance().active() ? app::QuitConfirmation::Presenter(app::Robot::confirm_quit)
                                            : app::QuitConfirmation::Presenter(SDL_ShowMessageBox));
        try {
            if (!playback.initialize_original(app::original_audio_assets(data, season_exe), audio_error))
                std::fprintf(stderr, "audio: %s\n", audio_error.c_str());
            playback.set_original_options({music_enabled, true, true});
            app::original_audio_command(playback, 0);
        } catch (const std::exception& error) {
            std::fprintf(stderr, "audio: %s\n", error.what());
        }
        app::OriginalMouseSound mouse_sound(playback);
        const auto season_texts = load_textd(ui::asset_path(data, "TEXTD.BLK"));
        const auto process_clock_origin = std::chrono::steady_clock::now();
        // Everything from here on is App (above): the presenters and the event loop.
        App app{data, save_directory, seed, music_enabled, live_frame_ms, live_frame_ms_explicit, english_game, palette, flow, session, revision, playback, audio_error, audio_session, window, renderer, texture, pitch_pointer, pixels, canvas, live_screen, reality_reporter, sportschau_intro, matchday_intro, sportschau_eleven, sportschau_team_info, competition_winner, season_exe, quit_confirmation, mouse_sound, season_texts, process_clock_origin};
        return app.run();
    // Any exception from setup or the main loop is printed and ends with exit code 1.
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
