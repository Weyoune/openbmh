// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#pragma once
// Minute-by-minute match simulation, port of the match loop EndPhase.
//
// A matchday consists of up to six match groups (one per competition). Every minute the
// original walks all groups, all matches of a group and both sides of a match:
//
//   1. End of match: after the regular time (last_minute) each running match ends with
//      probability 1/2 per minute (WatcomRandom), at last_minute + stoppage at the latest.
//      At the end of the first half (minute 45 with no stoppage) this also ends the half.
//   2. Per side, event roll: WatcomRandom::range(0, 220) < rhythm - 30 (indoor:
//      range(0, 10) > 7) calls the foul/card/injury routine EventImpl.
//   3. Per side, chance roll: GameRandom::range(0, 44 + 4 * away_penalty) must not exceed
//      home attitude + away attitude + 3 (+14 indoor). away_penalty is 1 for the away
//      side on league matchdays (descriptor flag bit 1), else 0.
//   4. Chance quality from the side records, the opponent's defence and randomness
//      decides via the table at layout field whether a real chance arises; then the kind of
//      chance (normal, penalty 1/6, free kick 1/7), shooter and assistant (PickTaker),
//      the goalkeeper and finally the goal roll.
//
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "formats/binary.hpp"
#include "game/presentation.hpp"
#include "game/random.hpp"
#include "game/reality_scene.hpp"
#include "game/state.hpp"

namespace bmh {
struct PhaseResultRequest;

// One match group (the original's 0xD0-byte descriptor at MatchdayGroupDescriptors + group * 0xD0 plus the
// heap arrays it points to).
struct MatchGroup {
    Bytes desc;                     // raw descriptor, per-match bytes at 0x10/0x30/0x50
    std::vector<int> fixtures;      // home, away team id per match (descriptor +0x7c)
    std::vector<uint8_t> results;   // league: home << 4 | away per match (descriptor +0x08)
    // Cups have no league result pointer; each match has a u16 instead (descriptor +0x80,
    // one array per leg): bits 0-4 away goals, 5-9 home goals, 10-15 phase
    // (0 regular time, 1 extra time). 0xCCCC = not played. See MatchScore.
    std::vector<uint16_t> cup_results;
    // Two-legged ties: the other leg (descriptor +0x84, its match order follows this leg).
    std::vector<uint16_t> cup_other_leg;
    // Where the result arrays live in the original (LE addresses, 0 = none): +0x80, +0x84.
    uint32_t cup_result_address[2] = {0, 0};
    int phase = 0;                  // param_4 of EndPhase, stored with cup goals
    std::vector<uint8_t> records;   // 18 bytes per match (descriptor +0x0c)
    std::vector<uint8_t> end_minute;  // minute a match ended (descriptor +0x04)
    std::vector<uint32_t> spectators; // attendance per match (descriptor +0xb4)
    std::vector<uint16_t> log;      // event log (Matches, descriptor +0x98)
    uint32_t ended = 0;             // matches ended in the current phase (local_dc)
    uint32_t kickoff_void_mask = 0; // local_70: fixtures excluded from all post-match effects

    int matches() const { return int(fixtures.size() / 2); }
    uint8_t flags() const { return desc.at(1); }
    uint32_t void_mask() const { return le32(desc, 0xb8); }  // matches not played (b6424)
};

struct MinuteReport {
    bool unsupported = false;
    std::string reason;  // first unsupported path taken
    void fail(const std::string& why) {
        if (!unsupported) reason = why;
        unsupported = true;
    }
};

// Exact Source event which requests the watched manager's live line-up (PitchScreen).
// The identity is captured after the event has changed the slot, before play resumes.
struct LiveMatchEvent {
    size_t group_index = 0;
    int match_index = -1;
    int side = -1;
    int team = -1;
    int opponent = -1;
    int player = -1;
    int kind = -1; // 1 straight red, 2 injury
    int minute = 0; // MatchEngine minute is already 1-based; Source receives minute+1.
    int manager_slot = -1;
};

// BallFrameCount: an already computed Watcom ball position, at the synchronous
// chance presentation boundary. Observers receive no mutable engine references.
struct LiveBallEvent {
    int minute = 0;
    int group = 0;
    int match = 0;
    int displacement = 0; // home positive, away negative; a goal uses +/-52
};

enum class LiveChanceStage { preview, result };
// Read-only Source chance presentation. The preview precedes the score write;
// the result follows the optional shot/set-piece RNG path. Neither hook decides play.
struct LiveChanceEvent {
    LiveChanceStage stage = LiveChanceStage::preview;
    int minute = 0, group = 0, match = 0, side = 0, kind = 0;
    int shooter = 0xfff, assistant = 0xfff, distance = 0;
    bool goal = false, ticker = false, deflected = false;
};

// SetPieceAnimation/Range4: retain the picture values from the existing
// Watcom draws. A presenter cannot change the shot or draw additional randomness.
struct LiveShotEvent {
    int minute = 0, group = 0, match = 0, acting_side = 0;
    int lane = 0, height = 0, end_lane = 0, end_height = 0, keeper_delay = 0;
    bool miss = false, mirror_sprites = false;
};

struct LiveSetPieceDraw { int low = 0, high = 0, value = 0; };
// An owned observation of the existing LiveSetPieceFrames draws. The UI replays these
// values to reconstruct figures; it has no GameRandom/WatcomRandom reference.
struct LiveSetPieceEvent {
    int minute = 0, group = 0, match = 0, source_side = 0, kind = 0, distance = 0;
    std::vector<LiveSetPieceDraw> draws;
};

// EventImpl/SDLPumpEvents presentation, after the card/injury is recorded and before
// substitution decisions. Source kind: 0 yellow, 1 red, 2 injury, 3 second yellow.
struct LiveBookingEvent {
    int minute = 0, group = 0, match = 0, side = 0, kind = 0;
    int team = -1, player = 0xfff, season_yellows = 0, ban = 0;
};

enum class LiveShootoutStage { start, preview, end };
// CupTie2/PenaltyShootout: header/text before each existing kick and after the lottery.
// Scores are owned presentation values; observers cannot write cup results.
struct LiveShootoutEvent {
    LiveShootoutStage stage = LiveShootoutStage::start;
    int minute = 0x79, group = 0, match = 0, side = 3, player = 0xfff;
    int home_goals = 0, away_goals = 0, round = 0, of = 5;
};

// EventImpl(slot,1,side) stores this request in bdb14/16/18 rather than opening
// a screen or substituting during a Reality scene. The scene owner must drain
// it after the scene ends (RealityReporterPanel). No request means those globals stay put.
struct DeferredRealityChange {
    bool human = false;
    int team = -1;       // opponent for a human request, own team for automatic
    int slot = -1;       // absolute squad slot; Source 0xff for a human request
    int mode = -1;       // position+20 for automatic; manager index for human
};
struct ForcedMatchEvent {
    uint8_t code = 0;    // Source return: yellow=1, straight red=3, second yellow=2;
                         // bit7 requests the watched human's post-scene screen
    std::optional<DeferredRealityChange> change;
};

enum class LiveRealityStage { begin, round, settling, end };
// Owned Source observation: presentation receives neither RNG nor mutable scene.
// A true round response requests Source's post-animation exit boundary.
struct LiveRealityEvent {
    LiveRealityStage stage = LiveRealityStage::begin;
    int minute = 0, group = 0, match = 0, side = 0;
    RealitySceneTick tick;
};

struct MatchEngine {
    MatchEngine(GameState& s, std::vector<MatchGroup>& gr, GameRandom& gm, WatcomRandom& r, std::vector<uint8_t> refs)
        : state(s), groups(gr), game(gm), rand(r), referees(std::move(refs)), last_pick_quality(s.last_pick_quality) {}

    GameState& state;
    std::vector<MatchGroup>& groups;
    GameRandom& game;
    WatcomRandom& rand;
    std::vector<uint8_t> referees;  // RefereeProperties, bits 3-4: strictness (from M3D.MAN)
    bool indoor = false;            // CareerSessionRuntimeField: adds 14 to the chance threshold
    bool alternate_event_roll = false;             // AlternateEventRoll: alternative event roll range(0, 10) > 7
    bool match_events_disabled = false;             // MatchEventsDisabled: disables the foul/event routine
    bool live_scene_visible = false;             // LiveSceneVisible (live scene enabled)
    bool live_desk_disabled = false;             // LiveDeskDisabled: no live desk (both live modes off)
    int month = 0;                  // CareerSession_PairingsRuntimeDraw_ExecutableField5, for the climate effect when re-rating
    bool trace = false;             // print events and chances to stderr (debugging)
    // What the original would show (presentation.hpp); filled only when set. Appending
    // draws no random numbers and changes nothing else.
    std::vector<ShowEvent>* show = nullptr;
    // The substitution screen (PitchScreen) a watched human team gets after a red card or an
    // injury when the assistant may not substitute. An installed live_event presenter handles
    // that boundary synchronously; leave_screens preserves the legacy headless path.
    bool leave_screens = false;
    // Synchronous Source presenter for an actual red-card/injury event. While this runs,
    // MatchEngine::event and the outer minute loop retain references into `groups`;
    // implementations must not replace, resize, reorder, or otherwise invalidate that
    // vector or its MatchGroup objects. The supported live scope mutates only match/player
    // state and restores group contents in place on abort. Empty keeps the existing
    // leave_screens/headless behavior.
    std::function<void(const LiveMatchEvent&)> live_event;
    std::function<void(const LiveBallEvent&)> live_ball;
    std::function<void(const LiveChanceEvent&)> live_chance;
    std::function<void(const LiveShotEvent&)> live_shot;
    std::function<void(const LiveSetPieceEvent&)> live_set_piece;
    std::function<void(const LiveBookingEvent&)> live_booking;
    std::function<void(const LiveShootoutEvent&)> live_shootout;
    // Source sound IDs only, emitted at actual presentation boundaries. This
    // observer has no access to game/random generators and never decides play.
    std::function<void(uint8_t)> original_sound;
    const LeImage* reality_rules = nullptr; // borrowed executable, stable during minute()
    std::function<bool(const LiveRealityEvent&)> live_reality;
    // EndPhase's existing group result barrier, before its single Source ACK.
    // A presenter receives owned scores and const context, never either RNG.
    std::function<void(const PhaseResultRequest&, const std::vector<MatchGroup>&,
                       const GameState&)> phase_results;
    // Button settings of the desk (block LeaguePresentationOptions, 0xb4 bytes, saved in the .FBM and the .EIN
    // files); several switch live-match animations that draw random numbers.
    std::vector<uint8_t> buttons;
    int button(uint32_t address) const {
        return buttons.empty() ? 0 : buttons.at(address - ::bmh::layout::LeaguePresentationOptions);
    }

    // Simulates one minute of the phase [first_minute, last_minute + stoppage].
    MinuteReport minute(int minute, int last_minute, int stoppage);
    // CupTie2 for match m of a group whose tie is level after extra time: the
    // penalty shoot-out (PenaltyShootout, kicks Range5); the result is stored with
    // phase 2. A match shown live (or in the ticker) animates every kick.
    void penalty_shootout(MatchGroup& g, int m, MinuteReport& report);
    // EndPhase after the line-up or substitution screen the manager opened during his live
    // match (RerateTeam / PitchScreen): his side of that match is rated again
    // (BuildSideRecords3). The changes themselves are made on the state before.
    void rerate_team(int team, MinuteReport& report);
    // Real card effects for the selected Reality offender. Mode1 skips the
    // injury roll and random slot selection, suppresses booking message boxes,
    // and defers changes. `g` must be a stable member of `groups` as for minute().
    ForcedMatchEvent forced_event(MatchGroup& g, int m, int side, int minute,
                                 int absolute_slot, MinuteReport& report);
    // Source scene generators shared by live chances and ShotAnimation replay.
    // They own the Watcom draws; UI consumers receive immutable observations.
    void shot_animation(bool miss, bool follow_up, int acting_side, int group, int match, int minute); // SetPieceAnimation
    void set_piece_animation(int kind, int distance, int side, int group, int match, int minute); // LiveSetPieceFrames

private:
    void side(MatchGroup& g, int m, int& side, int minute, int& own_attitude, int& opp_attitude, MinuteReport& report);
    void event(MatchGroup& g, int m, int side, int minute, MinuteReport& report);  // EventImpl
    ForcedMatchEvent event_impl(MatchGroup& g, int m, int side, int minute,
                               int fixed_slot, MinuteReport& report);
    bool substitute(int team, int off, int mode, MinuteReport& report);             // Injure
    bool injure(int slot, bool match, int manager, MinuteReport& report);          // InjurePlayer
    void rerate(MatchGroup& g, int m, int side, MinuteReport& report);             // BuildSideRecords3
    int pick_player(int team, int mode, int exclude, MinuteReport& report, int preset = -1);  // PickTaker
    int pick_taker(int team, int mode, int exclude, int kind, MinuteReport& report);        // DesignatedSlot
    int designated_slot(int team, int offset, int exclude) const;                          // Emit
    // the match the next ShowEvent belongs to (set where a side, an event or a shoot-out
    // starts)
    int show_group = 0, show_match = 0, show_minute = 0;
    void emit(ShowEvent e) {
        if (!show) return;
        e.group = uint8_t(show_group), e.match = uint8_t(show_match), e.minute = int16_t(show_minute);
        show->push_back(std::move(e));
    }
    void show_at(const MatchGroup& g, int m, int minute) {
        show_group = int(&g - groups.data()), show_match = m, show_minute = minute;
    }
    int& last_pick_quality;  // MatchEngineRuntimeField (a global: GameState::last_pick_quality)
    int last_shooter_slot = 0;  // MatchEngineRuntimeField2
    int last_assist_slot = 0;   // MatchEngineRuntimeField3
    // Stack slot local_1c of PlayerValue as seen from PickTaker (see
    // RatingContext::stale_condition): the condition of the last candidate rated with it.
    int pick_stack_condition = 0;
    // local_84 of EndPhase: quality of the last assistant picked (free kicks use it).
    int stale_assistant_quality = 0;
    int stale_set_piece_score = 0; // local_9c, retained for Source Reality restart2
    RealityScene reality_runtime; // persistent grid, cue slots and MatchEngine_Minute_ExecutableField during matchday
};

}  // namespace bmh
