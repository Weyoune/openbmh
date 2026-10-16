// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Immutable, source-word-preserving input to Sportschau presentation callbacks.
// Presentation callbacks remain immutable; Source replay and award work have
// separate Core owners, independent of whether a UI observer is installed.

#include <cstdint>
#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

#include "game/presentation.hpp"
#include "game/match_engine.hpp"

namespace bmh {

struct GameState;
struct MatchGroup;
class WatcomRandom;
class LeImage;

// MatchdayIntro walks the group's flat 16-bit log in two-word steps. The third
// word it reads is a look-ahead into the next record, and some codes consume
// that next pair as a secondary player/distance. Keep both the decoded fields
// and exact words so a presenter cannot accidentally reinterpret the stream.
struct SportschauLogEvent {
    uint8_t group = 0;
    uint8_t fixture = 0;
    uint16_t event_ordinal = 0;  // ordinal after filtering this fixture's teams
    uint16_t source_word_index = 0;  // index of the first word in MatchGroup::log
    uint16_t source_code_player_word = 0;
    uint16_t source_team_value_word = 0;
    uint8_t source_code = 0;  // high four bits of the first word
    uint16_t player_id = 0x0fff;
    int16_t team_id = -1;
    uint8_t source_value = 0;  // low seven bits of the second word
    bool consumes_following_pair = false;  // source codes 0 (goal) and 1 (miss)
    bool has_following_word = false;
    bool has_following_pair = false;
    uint16_t following_code_player_word = 0;
    uint16_t following_team_value_word = 0;
    std::vector<uint8_t> player_name;  // Original encoded bytes
    std::vector<uint8_t> following_player_name;  // empty when no valid look-ahead player
};

struct SportschauFixtureView {
    uint8_t group = 0;
    uint8_t fixture = 0;
    uint8_t competition_type = 0;
    uint8_t fixture_flags = 0;
    uint8_t source_footer_variant = 0;  // MatchdayIntro local_44: 0 normal,1 extra-time,2 indoor
    bool has_replay_progression_mode = false;
    uint8_t replay_progression_mode = 0;  // saved byte; controls selected-progressive scene path
    bool has_replay_chance_mode = false;
    uint8_t replay_chance_mode = 0;  // saved byte; controls selected scene/panel restoration
    bool source_selection_available = false;
    bool source_selected = false; // ResetSportschauSelection(pair,1), resolved from pre-match state
    uint32_t competition_id = 0;
    int16_t source_setting_index = -1;  // SportschauPresentationOperation(desc,5), or -1 if unavailable
    int16_t home_team = -1;
    int16_t away_team = -1;
    uint8_t source_played_minute = 0;  // descriptor +0x04, indexed by Source fixture ordinal
    uint16_t home_badge_word = 0xffff;  // raw ClubBadgeSelectors u16 consumed by SourceBadgeFallback
    uint16_t away_badge_word = 0xffff;
    bool score_known = false;
    uint8_t home_goals = 0;
    uint8_t away_goals = 0;
    std::vector<uint8_t> home_name;  // Original encoded bytes
    std::vector<uint8_t> away_name;  // Original encoded bytes
    std::vector<uint8_t> home_short_name;  // exact ClubShortNameOffsets substring for team ids < 168
    std::vector<uint8_t> away_short_name;
    bool home_short_name_available = false;
    bool away_short_name_available = false;
    bool home_name_requires_font_fit = true;  // caller trims by two bytes until FNT3 width < 48
    bool away_name_requires_font_fit = true;
    bool home_name_requires_longest_word = false;  // TextWidth foreign-team branch, team >= 168
    bool away_name_requires_longest_word = false;
    std::vector<SportschauLogEvent> log_events;  // Source log order, fixture-filtered
    std::vector<ShowEvent> show_events;  // Distinct live-animation stream, not the report log
};

struct SportschauCompetitionView {
    uint8_t group = 0;
    uint8_t type = 0;
    uint32_t id = 0;
    int16_t source_setting_index = -1;
};

struct SportschauIntroView {
    int group_index = -1;  // -1 post-simulation, otherwise a Source group-entry point
    std::vector<SportschauCompetitionView> groups;
};

// FinishEvent's pre-play KB136 competition list, independent of report selection.
struct MatchdayIntroView {
    std::vector<std::string> competition_names; // Original encoded bytes
};
using MatchdayIntroHook = std::function<void(const GameState&, const MatchdayIntroView&)>;
std::optional<MatchdayIntroView> matchday_intro_view(const GameState&,
    const std::vector<MatchGroup>&);

// Intro gets all group competition ids so App can evaluate Source's global
// setting-matrix eligibility test without guessing from the current fixture.
using SportschauIntroHook = std::function<void(const GameState&, const SportschauIntroView&)>;
using SportschauFixtureHook = std::function<void(const GameState&, const SportschauFixtureView&)>;

struct SportschauAwardPlayer {
    uint16_t squad_slot = 0, player_id = 0x0fff;
    int16_t team_id = -1;
    uint8_t score = 0;
    std::string player_name, club_name; // Original encoded bytes
};
struct SportschauAwardsView {
    uint8_t group = 0;
    uint32_t competition_id = 0;
    std::string competition_title; // Void4, Original encoded bytes
    bool source_backdrop_available = true; // SportschauIntro/5c1c7, independent of C7 animation
    std::array<SportschauAwardPlayer, 11> players;
};
using SportschauAwardsHook = std::function<void(const GameState&, const SportschauAwardsView&)>;
// SportschauEleven: complete-eleven gate, Source manager counter, then pure viewer input.
// The statistic reads the score byte as a slot (confirmed in x86), whereas
// the displayed names use the separate u16 squad-slot array at descriptor+9c.
std::optional<SportschauAwardsView> finish_sportschau_awards(GameState& state,
    const MatchGroup& group, int group_index);

struct SportschauTeamInfoView {
    uint8_t group = 0;
    uint32_t competition_id = 0;
    std::string competition_title;
    bool source_backdrop_available = true;
    // Source descriptor +bc,+c0,+c4,+c8: yellow, red, spectators, goals.
    std::array<int32_t, 4> totals{};
};
using SportschauTeamInfoHook = std::function<void(const GameState&, const SportschauTeamInfoView&)>;
std::string sportschau_competition_title(const GameState& state, const MatchGroup& group);
std::optional<SportschauTeamInfoView> sportschau_team_info_view(const GameState& state,
    const MatchGroup& group, int group_index);

struct SportschauReportStep {
    size_t visible_log_entries = 0;
    uint8_t progress_minute = 0;
    bool minute_wait = false;
    bool chance_wait = false;
    bool terminal_wait = false;
};
std::vector<SportschauReportStep> sportschau_report_steps(const SportschauFixtureView& fixture);

struct SportschauSceneView {
    SportschauReportStep report_step;
    int home_goals_before = 0, away_goals_before = 0;
    std::vector<MatchGroup> groups; // owned single-group context; shot.group ==0
    std::optional<LiveSetPieceEvent> set_piece;
    LiveShotEvent shot;
};
struct SportschauReplayHooks {
    // Return only the Source left=2 latch; Core applies it after a minute wait.
    std::function<bool(const GameState&, const SportschauFixtureView&, const SportschauReportStep&)> step;
    std::function<void(const GameState&, const SportschauFixtureView&, const SportschauSceneView&)> scene;
};

// CompetitionWinnerScreen(param1=0), called at title settlement before title-history writes.
struct CompetitionWinnerView { uint8_t title = 0; uint16_t team = 0; };
using CompetitionWinnerHook = std::function<void(const GameState&, const CompetitionWinnerView&)>;

struct SportschauPresentationHooks {
    CompetitionWinnerHook winner;
    MatchdayIntroHook kickoff;
    SportschauIntroHook intro;
    SportschauFixtureHook fixture;
    SportschauReplayHooks replay;
    SportschauAwardsHook awards;
    SportschauTeamInfoHook team_info;
};

struct SportschauRatingTables {
    std::array<int8_t, 4> position_limit{}, opponent_misses{}, chance_actor{}, own_chances{}, scorer_bonus{};
    std::array<uint8_t, 4> award_begin{}, award_end{};
    uint16_t domestic_teams = 0;
};
SportschauRatingTables sportschau_rating_tables(const LeImage& exe);
// MatchdayIntro, after replay and before morale/development. Updates eleven Source
// award scores and global squad-slot ids in the group's descriptor, not players.
void rate_sportschau_players(const GameState& state, MatchGroup& group,
    const SportschauFixtureView& fixture, WatcomRandom& rand, const SportschauRatingTables& tables);

struct SportschauSourceOptions {
    uint8_t intro_after_matchday = 0;  // unsaved EXE byte CareerSession_SportschauSourceOptions_ExecutableField
    uint8_t intro_each_group = 0;      // unsaved EXE byte CareerSession_DispatchTransferOffer_ExecutableField2
    uint16_t rated_club_threshold = 90; // EXE TeamsWithSquads, ResetSportschauSelection's extra fifty points
    std::optional<SportschauRatingTables> ratings; // loaded EXE policy, not FBM data
};

// Read-only projection for one already-completed fixture. Source's flat log is
// filtered by the two fixture teams, while its pair stride and look-ahead words
// remain intact. This helper does not mutate Native state or either RNG.
SportschauFixtureView sportschau_fixture_view(const GameState& state, const MatchGroup& group,
                                               int group_index, int fixture_index,
                                               const std::vector<ShowEvent>& show_events);
int sportschau_source_setting_index(const GameState& state, const MatchGroup& group);

// ResetSportschauSelection modes0/2/1. Only the pre-match caller may rate candidates; foreign
// teams consume the real Watcom generator even with capacity A8 ==0.
void reset_sportschau_selection(GameState& state);
bool consider_sportschau_fixture(GameState& state, const MatchGroup& group,
                                 int home, int away, WatcomRandom& rand);
bool sportschau_selected(const GameState& state, int home);

// MatchdayIntro/ShotAnimation: owns scene draws between table updates and morale.
// With no UI callbacks, it still consumes the same enabled Source scene work.
void present_sportschau_replay(GameState& state, const MatchGroup& group,
    const SportschauFixtureView& fixture, GameRandom& game, WatcomRandom& rand,
    const SportschauReplayHooks& hooks);

}  // namespace bmh
