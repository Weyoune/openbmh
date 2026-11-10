// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// struct App: the running window program (see the comment on the class). Internal header of the game
// program: included by src/app/main.cpp and the app_*.cpp files that define the member functions.
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

// (this internal header is only included by the files of the game program; they share these two)
namespace fs = std::filesystem;
using namespace bmh;

// Reattach the native editor when an in-game filename field takes focus. Cocoa
// owns a field-editor responder in addition to SDL's TEXTINPUT enable flag.
// Moves the platform text-input rectangle over the save-file name field (logical canvas
// coordinates 106,209 to 262,219 converted to window pixels) and restarts text input.
// Called when an in-game filename field gains focus.
inline void focus_filename_input(SDL_Renderer* renderer) {
    int left, top, right, bottom;
    SDL_RenderLogicalToWindow(renderer, 106, 209, &left, &top);
    SDL_RenderLogicalToWindow(renderer, 262, 219, &right, &bottom);
    const SDL_Rect field{left, top, std::max(1, right - left), std::max(1, bottom - top)};
    SDL_StopTextInput();
    SDL_SetTextInputRect(&field);
    SDL_StartTextInput();
}

// The running game program: everything main() does after the window is set up.
//
// main() used to hold all of this in one function of about 4,300 lines: the state of the window
// program as locals and every presenter (season end, Sportschau, tax, promotion, cup draw, club
// dialogs, live match, ...) as a lambda capturing them. App holds the same things as members: the
// objects main() created (as references under their old names), the state, one member function per
// former lambda in the original order, and run() with the statements that stood between them (the
// wiring of the hooks, the title screen, the event loop, the tear-down). The code inside moved
// unchanged.
//
// The member functions are defined by topic:
//   app_services.cpp  the timers and deadlines the event loop serves between inputs (process timer, table, balance, pairings, stadium, friendly match pickers)
//   app_season.cpp  season end and season start (season pictures, contract expiry, tax, promotion, highscore, entrants, cup draw)
//   app_sportschau.cpp  the Sportschau (intro, replay scenes, fixtures, winner, awards, team info) and its waits
//   app_transfer.cpp  transfers (incoming offers, decisions, pending offers, market sales, loan returns, poaching)
//   app_club.cpp  the club dialogs (club information, player list, player detail, statistics)
//   app_notices.cpp  notices and questions over a screen (construction, season tickets, advertising renewal, national friendly, environment)
//   app_live.cpp  the live match (minute display, the observer with its interventions, the event animations, reality mode, phase results)
//   app_run.cpp  handle() - one request through the controller - and run() - wiring, title screen, event loop, tear-down
struct App {    fs::path& data;
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
    auto process_source_seconds() -> uint64_t;
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
    auto process_timer_image(Image image) -> bmh::Image;
    auto poll_process_timer(bool update_texture, bool present) -> bool;
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
    auto transfer_view(const GameState& state, const TransferOfferListView& offers, int viewer_manager) -> bmh::ui::IncomingTransferDecisionView;
    std::optional<Image> season_backdrop;
    auto present_season(const Image& image, const char* title, const Palette* palette_override = nullptr) -> void;
    auto present_desk_record_points() -> void;
    auto contract_expiry_presenter(GameState& staged, WatcomRandom& random, int manager, int expired_row) -> int;
    auto game_end_options(const GameState& state) -> bmh::ui::GameEndRenderOptions;
    auto wait_season_ack() -> void;
    auto wait_sportschau_ack() -> void;
    auto source_tick_wait(unsigned ticks) -> void;
    auto wait_source_retraces(unsigned count) -> void;
    // --- Sportschau (results show) presenters: the engine calls these hooks synchronously. ---
    SportschauPresentationHooks sportschau_presenter;
    auto sportschau_context(const GameState& state) -> bmh::ui::SportschauReportContext;
    std::optional<ui::SportschauMatchReport> sportschau_replay_screen;
    size_t sportschau_step_index = 0;
    auto show_tax(const GameState& state, const SeasonTaxEvent& event) -> bool;
    // Hooks for the season tax events: question via show_tax, notices and the animation below.
    SeasonTaxHooks tax_presenter;
    auto wait_promotion_time(Uint32 milliseconds) -> void;
    // Presents the promotion/relegation table animation (39 steps) unless the manager's
    // animation-skip flag is set.
    SeasonPromotionHook promotion_presenter;
    auto present_pending_dialog(const GameState& state, ui::TaxDialogProjection view, const Image& backdrop, const char* title, std::optional<ui::GameEndRenderOptions> effective_options = {}) -> PendingOfferAnswer;
    auto loan_return_presenter(GameState& state, WatcomRandom&, int manager, const std::string& question) -> int;
    auto make_pending_offer_hooks(Image backdrop) -> bmh::PendingOfferHooks;
    auto make_market_sale_hooks(Image backdrop) -> bmh::HumanBuyerSaleHooks;
    // Forward declarations: the club, player and statistics presenters call each other, so
    // they are assigned further below.
    std::function<bool(app::ClubModalServices&, int)> present_club_information;
    std::function<std::optional<bool>(app::ClubModalServices&,const ClubPlayerDetailSelection&,const Image&)> present_player_detail;
    auto loan_return_offer_presenter(GameState& staged, GameRandom& game_random, WatcomRandom& watcom_random, app::ClubModalServices& services, int manager, int source_slot, int list_entry, const HumanBuyerSaleHooks& sale_hooks) -> int;
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
    auto show_highscore(const GameState& state, bool display) -> void;
    std::optional<Uint64> timed_notice_presented_at;
    std::optional<Uint8> timed_notice_release;
    // --- the request in service (state of App::handle and of the presenters it passes on) ---
    // handle() is never re-entered: every presenter below runs inside one handle() call.

    bool live_skipped;
    uint64_t live_overview_sequence;
    std::optional<Image> outcome_backdrop;
    auto present_live_lineup(int minute, MatchEngine& engine, std::vector<MatchGroup>& groups, GameState& state, uint64_t& live_revision, int selected_team, int selected_opponent, const app::LiveLineupDispatcher& dispatch, const app::LiveTacticsDispatcher& dispatch_tactic, const app::LivePitchDispatcher& dispatch_pitch, const app::LiveInterventionScopeDispatcher& dispatch_scope, bool live_skipped, bool event_origin) -> bool;
    // Callback type through which the overview screen asks the engine for its actions.
    using RunningOverviewDispatcher = std::function<std::optional<app::CareerSession::LiveOverviewDispatch>(
        const app::CareerSession::LiveOverviewAction&, std::string&,
        const app::TransferOfferTeamInfoPresenter&)>;
    auto present_running_overview(std::optional<app::CareerSession::LiveOverviewProjection> projection, const GameState& state, uint64_t& live_revision, const RunningOverviewDispatcher& dispatch_overview, const PhaseResultRequest* phase_result) -> bool;
    // State used by the live-match presenters below.
    ui::LiveScoreReveal live_score_reveal;
    std::optional<ui::LiveChanceProjection> live_chance_context;
    std::unique_ptr<ui::RealityFieldScreen> reality_field;
    ui::RealityReporterLayoutState reality_layout;
    Image reality_pristine, reality_surface;
    Palette reality_colors;
    auto present_live_minute(int minute, const std::vector<MatchGroup>& groups, const GameState& state) -> void;
    auto choose_transfer_offer(const GameState& state, const TransferOfferListView& offers) -> int;
    auto present_transfer_decision(const GameState& state, const TransferDecisionEvent& decision) -> void;
    auto present_season_checkpoint(const char* checkpoint, const GameState& state) -> void;
    auto show_club_information(app::ClubModalServices& services,int team,const Image&) -> bool;
    auto show_player_detail(app::ClubModalServices& services,const ClubPlayerDetailSelection& selection,const Image& parent) -> bool;
    auto present_construction_notice(const GameState& staged, const ConstructionNotice& notice) -> void;
    auto present_ticket_compensation_notice(const GameState& staged, const SeasonTicketCompensationNotice& notice) -> void;
    auto ask_advertising_renewal(const GameState& staged, const AdvertisingRenewal& renewal, const Image& backdrop) -> app::CareerSession::AdvertisingRenewalPresentation;
    auto present_poach_notices(const GameState& staged,const PoachStart& attempt,const Image& backdrop) -> void;
    auto ask_national_friendly(const GameState& staged,ui::TaxDialogProjection view,const Image& backdrop) -> std::optional<bool>;
    auto observe_live_minute(int minute, MatchEngine& engine, std::vector<MatchGroup>& groups, GameState& state, uint64_t& live_revision, const app::LiveLineupDispatcher& dispatch, const app::LiveTacticsDispatcher& dispatch_tactic, const app::LivePitchDispatcher& dispatch_pitch, const app::LiveInterventionScopeDispatcher& dispatch_scope) -> void;
    auto present_environment_notice(const GameState& staged, ui::TaxDialogProjection view, const Image& backdrop) -> bool;
    auto present_live_event_lineup(const LiveMatchEvent& event, MatchEngine& engine, std::vector<MatchGroup>& groups, GameState& state, uint64_t& live_revision, const app::LiveLineupDispatcher& dispatch, const app::LiveTacticsDispatcher& dispatch_tactic, const app::LivePitchDispatcher& dispatch_pitch, const app::LiveInterventionScopeDispatcher& dispatch_scope) -> void;
    auto animate_ball(const LiveBallEvent& event, const std::vector<MatchGroup>& groups, const GameState& state) -> void;
    auto animate_chance(const LiveChanceEvent& event, const std::vector<MatchGroup>& groups, const GameState& state) -> void;
    auto animate_shot(const LiveShotEvent& event, const std::vector<MatchGroup>& groups, const GameState& state) -> void;
    auto animate_set_piece(const LiveSetPieceEvent& event, const std::vector<MatchGroup>& groups, const GameState& state) -> void;
    auto animate_booking(const LiveBookingEvent& event, const std::vector<MatchGroup>& groups, const GameState& state) -> void;
    auto animate_shootout(const LiveShootoutEvent& event, const std::vector<MatchGroup>& groups, const GameState& state) -> void;
    auto present_reality(const LiveRealityEvent& event, const std::vector<MatchGroup>& groups, const GameState& state) -> bool;
    auto present_phase_result(const PhaseResultRequest& request, const std::vector<MatchGroup>& groups, const GameState& state) -> void;
    auto handle(const ui::FlowResult& r) -> void;
    auto present_formation_save_feedback() -> void;
    auto service_pitch_hold() -> void;
    const std::chrono::steady_clock::time_point pairings_clock_start = std::chrono::steady_clock::now();
    auto pairings_source_seconds() -> uint64_t;
    auto balance_source_milliseconds() -> uint64_t;
    auto family_table_source_units() -> uint64_t;
    auto mark_family_table_presented() -> void;
    auto present_family_table_redraw() -> void;
    // A key typed for the table range dialog or toto calculator, tagged with its owner so
    // that keys for a screen that has since changed are dropped.
    struct TableRangeKey {
        std::string key;
        ui::StrengthsIdentity identity;
        int toto_manager = -1;
        uint64_t toto_revision = 0;
    };
    std::deque<TableRangeKey> table_range_keys;
    auto queue_table_range_event(const SDL_Event& event) -> bool;
    auto service_family_table() -> void;
    // Which balance screen state was last presented (to avoid redrawing identical frames).
    std::optional<ui::BalanceIdentity> presented_balance;
    bool presented_balance_notice=false;
    auto present_balance_redraw() -> void;
    auto service_balance_timer() -> void;
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
    auto queue_calculator_key(std::string key) -> void;
    auto queue_calculator_event(const SDL_Event& event) -> bool;
    auto mark_stadium_calculator_presented() -> void;
    auto service_stadium_calculator() -> void;
    // Which pairings page was last presented (to avoid redrawing identical frames).
    std::optional<ui::LeaguePairingsIdentity> presented_pairings;
    auto present_pairings_redraw() -> void;
    auto service_pairings_timer() -> void;
    //PresentManagerDialog presents its question before polling2de66. Bind the actual
    //freshly drawn frame, never an old texture or a preceding modal ACK.
    // Frame identities: each tuple names the owner state (session, manager, scope, sequence,
    // revision, ...) of a modal question that was actually drawn. A process-timer deadline
    // is applied only to a frame that has been presented, never to a stale one.
    using StadiumQuestionFrame = std::array<uint64_t,5>;
    std::optional<StadiumQuestionFrame> presented_stadium_question;
    auto stadium_question_frame() -> std::optional<StadiumQuestionFrame>;
    auto mark_stadium_question_presented() -> void;
    auto service_stadium_deadline() -> bool;
    auto service_idle_stadium_deadline() -> void;
    // Same idea for the national friendly picker.
    using NationalPickerFrame = std::pair<uint64_t,ui::NationalFriendlyUiOwner>;
    std::optional<NationalPickerFrame> presented_national_picker;
    auto national_picker_frame() -> std::optional<NationalPickerFrame>;
    auto mark_national_picker_presented() -> void;
    using FriendlyPickerFrame = std::array<uint64_t,5>;
    std::optional<FriendlyPickerFrame> presented_friendly_picker;
    auto friendly_picker_frame() -> std::optional<FriendlyPickerFrame>;
    auto mark_friendly_picker_presented() -> void;
    // Same idea for the friendly match calendar.
    using FriendlyCalendarFrame = std::array<uint64_t,5>;
    std::optional<FriendlyCalendarFrame> presented_friendly_calendar;
    auto friendly_calendar_frame() -> std::optional<FriendlyCalendarFrame>;
    auto mark_friendly_calendar_presented() -> void;
    // Same idea for the friendly match question.
    using FriendlyQuestionFrame = std::array<uint64_t,8>;
    std::optional<FriendlyQuestionFrame> presented_friendly_question;
    auto friendly_question_frame() -> std::optional<FriendlyQuestionFrame>;
    auto mark_friendly_question_presented() -> void;
    auto service_friendly_question_deadline() -> bool;
    auto service_process_timer() -> void;
    // filename_input_active: the save/load filename field already has focus.
    // setup_button_down: [0] left, [1] right button pressed while on the setup screen.
    bool filename_input_active = false;
    std::array<bool, 2> setup_button_down{};
    auto present_sportschau_intro(const GameState&, const SportschauIntroView&) -> void;
    auto present_matchday_intro(const GameState&, const MatchdayIntroView& view) -> void;
    auto present_sportschau_replay_step(const GameState& state, const SportschauFixtureView& fixture, const SportschauReportStep& step) -> bool;
    auto present_sportschau_scene(const GameState& state, const SportschauFixtureView& fixture, const SportschauSceneView& scene) -> void;
    auto present_sportschau_fixture(const GameState& state, const SportschauFixtureView& fixture) -> void;
    auto present_competition_winner(const GameState& state, const CompetitionWinnerView& view) -> void;
    auto present_sportschau_awards(const GameState& state, const SportschauAwardsView& view) -> void;
    auto present_sportschau_team_info(const GameState& state, const SportschauTeamInfoView& view) -> void;
    auto present_tax_event(const GameState& state, const SeasonTaxEvent& event) -> void;
    auto present_promotion(const GameState& state, const SeasonPromotionEvent& event) -> void;
    auto club_information_dialog(app::ClubModalServices& services, int team) -> bool;
    auto club_player_dialog(app::ClubModalServices& services, const ui::ClubInfoRequest& selected, const Image& parent_image) -> bool;
    auto player_detail_dialog(app::ClubModalServices& services,const ClubPlayerDetailSelection& selection,const Image& parent_image) -> std::optional<bool>;
    auto club_statistics_dialog(app::ClubModalServices& services, int team, const Image& parent_frame) -> void;
    auto select_poach_player(app::CareerSession& staged,app::ClubModalServices&,int manager, uint64_t& selection_revision,const Image& transfer_backdrop) -> std::optional<ClubPlayerDetailSelection>;
    auto present_cup_draw(const GameState& state, const SeasonCupDrawEvent& event) -> void;
    auto run() -> int;
};
