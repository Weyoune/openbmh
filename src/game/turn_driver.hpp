// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "game/match_engine.hpp"
#include "game/desk.hpp"
#include "game/random.hpp"
#include "game/state.hpp"
#include "game/season.hpp"
#include "game/sportschau_presentation.hpp"
#include "game/transfer_allocator.hpp"

namespace bmh {

enum class TurnAudioKind { kickoff_whistle, goal, full_time_whistle };

// Controller-level sound events carry stable identities. An original cue is optional:
// only attach one when its concrete BMH call site is mapped to a cue by evidence.
struct TurnAudioEvent {
    uint64_t session_id = 0;
    uint64_t event_id = 0;
    TurnAudioKind kind = TurnAudioKind::kickoff_whistle;
    std::optional<uint8_t> original_id;
};

// WaitingAtDesk has not claimed the driver: its original request may be retried.
enum class TurnDeskKind { NextDesk, SeasonEndDesk, CareerComplete, WaitingAtDesk, Failed };
struct TurnAdvanceRequest {
    int manager_slot = -1;
    uint64_t revision = 0;
};
struct TurnAdvanceResult {
    TurnDeskKind kind = TurnDeskKind::Failed;
    int manager_slot = -1;
    uint32_t date = 0;
    uint64_t revision = 0;
    bool offer_pending = false;
    std::string reason;
    std::vector<TurnAudioEvent> audio_events;
};

// Read-only-ready copy of the most recently completed human matchday. Kept apart
// from `groups`, which is replaced while seeking the next calendar event.
struct TurnResultSnapshot {
    bool valid = false;
    uint32_t date = 0;
    uint16_t calendar_entry = 0;
    uint16_t matchday = 0;
    std::vector<MatchGroup> groups;
    // Owned Original-encoded TEXTD slot TournamentName for the completed matchday.
    // Cup advancement can rename the indoor tournament before the next desk.
    std::vector<uint8_t> tournament_name;
};

// Runtime state between a desk's kickoff request and the next desk. The group objects,
// referee table, presentation settings and random generators remain owned by the caller/
// controller for each synchronous advance; only this cursor survives the desk handoff.
struct TurnDriver {
    enum class Stage { Empty, MatchdayDesks, SeasonEndDesk, CareerComplete, Failed };

    Stage stage = Stage::Empty;
    uint16_t calendar_entry = 0;
    int manager_position = 0;
    uint64_t revision = 0;
    uint64_t session_id = 0;
    uint64_t next_event_id = 1;
    // Unsaved program-image flags, supplied by the session's loaded EXE.
    // These are independent of the saved indoor/live settings.
    bool alternate_match_event_roll = false;  // AlternateEventRoll
    bool disable_match_events = false;       // MatchEventsDisabled
    // This unsaved EXE setting also governs the allocator used by weekly and
    // season-end transfer-list expiry.
    TransferAllocatorContext transfer_allocator_context = TransferAllocatorContext::unsupported;
    // Set only when a Native FBM restores an already prepared matchday desk loop.
    // It is provenance, not behavior: legacy/headless callers keep their old path.
    bool loaded_matchday_resume = false;
    // The normal-interactive App sets this after the loaded outgoing desk's staged
    // Source batch completes. It suppresses only pre-match offers at later desks.
    bool suppress_remaining_pre_match_offers = false;
    // Policy is explicitly selected by the normal-interactive App. Legacy and
    // headless drivers leave it false, preserving their previous calendar path.
    bool enable_national_nomination_desks = false;
    // Provenance for the one-manager Source national-only flag nomination desk. This is
    // classified from the canonical saved checkpoint or set by fresh next_event;
    // empty groups alone never imply a nomination desk.
    bool national_nomination_desk = false;
    // No further manager desk uses these flags. Consume them at the end of the
    // manager-desk phase, before match simulation and annual/future-event processing.
    void complete_loaded_manager_loop() noexcept {
        loaded_matchday_resume = false;
        suppress_remaining_pre_match_offers = false;
    }
    std::vector<MatchGroup> groups;
    TurnResultSnapshot last_results;
    std::vector<uint8_t> referee_source;
    std::vector<uint8_t> referees;
    // Optional: called after every minute of a matchday (play_matchday's after_minute) with
    // the running engine, groups and state. The app uses it for a manager's intervention in
    // his match (EndPhase: the line-up or tactics screen opened from the match, then
    // BuildSideRecords3): change the state (e.g. manual_substitution), then
    // engine.rerate_team(team, report). Empty = the matchday runs through.
    std::function<void(int minute, MatchEngine& engine, std::vector<MatchGroup>& groups, GameState& g)> live_minute;
    std::function<void(uint8_t, const GameState&)> original_sound;
    // Source PitchScreen pause, emitted synchronously from the event routine before
    // the current side/minute resumes. The caller must preserve the size/order and
    // MatchGroup object addresses in `groups` until this callback returns because the
    // current event and minute iterators hold references into that vector.
    std::function<void(const LiveMatchEvent&, MatchEngine& engine,
                       std::vector<MatchGroup>& groups, GameState& g)> live_event;
    std::function<void(const LiveBallEvent&, const std::vector<MatchGroup>&,
                       const GameState&)> live_ball;
    std::function<void(const LiveChanceEvent&, const std::vector<MatchGroup>&,
                       const GameState&)> live_chance;
    std::function<void(const LiveShotEvent&, const std::vector<MatchGroup>&,
                       const GameState&)> live_shot;
    std::function<void(const LiveSetPieceEvent&, const std::vector<MatchGroup>&,
                       const GameState&)> live_set_piece;
    std::function<void(const LiveBookingEvent&, const std::vector<MatchGroup>&,
                       const GameState&)> live_booking;
    std::function<void(const LiveShootoutEvent&, const std::vector<MatchGroup>&,
                       const GameState&)> live_shootout;
    // Legacy programmatic drivers have no executable provider. CareerSession
    // supplies its loaded image for the actual Source Reality match path.
    const LeImage* reality_rules = nullptr;
    std::function<bool(const LiveRealityEvent&, const std::vector<MatchGroup>&,
                       const GameState&)> live_reality;
    // Exact league/cup half/full-time requests from the existing phase barrier.
    std::function<void(const PhaseResultRequest&, const std::vector<MatchGroup>&,
                       const GameState&)> phase_results;
    // Empty by default. These callbacks observe the Source intro/group/fixture
    // presentation boundaries and receive immutable projections only.
    SportschauPresentationHooks sportschau;
    SportschauSourceOptions sportschau_source;
    DeskAutosaveHook desk_autosave;
    // Presentation pauses at the existing Source season-transition checkpoints.
    // The callback borrows const state and cannot run desk actions while advancing.
    std::function<void(const char* screen, const GameState&)> season_pause;
    // GameEndAlternateScoreScreen update-only before duration decrement, then update/display on completion.
    std::function<void(const GameState&, bool display)> season_highscore;
    SeasonTaxHooks season_tax;
    SeasonPromotionHook season_promotion;
    SeasonCupDrawHook season_cup_draw;
};

// Bind an event already prepared by prepare_event and with its first human desk open.
// No generators are drawn here; the object becomes the only accepted continuation for it.
bool begin_turn_driver(TurnDriver& driver, const GameState& g, std::vector<MatchGroup> groups,
                       uint16_t calendar_entry, std::vector<uint8_t> referees,
                       uint64_t session_id, uint64_t revision, std::string& error);

// Accept the current desk's advance request, show the next manager desk when applicable,
// then play the event in the background and advance through calendar days to the next desk.
// Rejected/stale requests leave state and both random generators untouched.
TurnAdvanceResult advance_to_next_desk(TurnDriver& driver, GameState& g,
                                       const TurnAdvanceRequest& request,
                                       GameRandom& game, WatcomRandom& rand,
                                       MinuteReport& report);

}  // namespace bmh
