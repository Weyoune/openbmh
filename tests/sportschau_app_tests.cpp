// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "app/career_session.hpp"
#include "app/controller.hpp"
#include "game/cup.hpp"
#include "game/matchday.hpp"
#include "game/random.hpp"
#include "game/sportschau_presentation.hpp"
#include "game/turn.hpp"
#include "game/save.hpp"
#include "ui/flow.hpp"
#include "ui/sportschau.hpp"
#include "audio/playback.hpp"

using namespace bmh;

namespace {

void check(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

void put32(Bytes& bytes, size_t at, uint32_t value) {
    for (size_t i = 0; i < 4; ++i) bytes.at(at + i) = uint8_t(value >> (i * 8));
}

GameState source_game() {
    GameState state;
    state.club_ids = {0, 6, 12};
    state.club_names = {'A','l','p','h','a',0,'B','r','a','v','o',0,'C','h','a','r','l','i','e',0};
    state.player_name_offsets.assign(8, 0);
    state.player_name_offsets[6] = 8;
    state.player_name_offsets[7] = 16;
    state.player_names = {'P','l','a','y','e','r','A',0,'P','l','a','y','e','r','B',0,
                          'P','l','a','y','e','r','C',0};
    state.club_heap_78.assign(3 * 2, 0);
    state.club_heap_78[0] = 0x18;
    state.club_heap_78[2] = 0x27;
    state.club_heap_78[4] = 0x38;
    state.memory.add_block(::bmh::layout::ClubShortNameOffsets, 0xa8);
    state.memory.set8(::bmh::layout::ClubShortNameOffsets, 0x10); // offset 1, length 1: exact domestic abbreviation slice
    state.memory.set8(::bmh::layout::FixtureSportschauAdjacentStateByte, 0x10);
    state.memory.add_block(::bmh::layout::ReplayProgressionMode, 1);
    state.memory.set8(::bmh::layout::ReplayProgressionMode, 1);
    state.memory.add_block(::bmh::layout::ReplayChanceMode, 1);
    state.memory.set8(::bmh::layout::ReplayChanceMode, 0);
    state.memory.add_block(kCupTable, kCupEntries * kCupEntrySize);
    return state;
}

void test_fixture_projection_preserves_source_log_and_live_stream() {
    GameState state = source_game();
    MatchGroup group;
    group.desc.assign(0xd0, 0);
    group.desc[0] = 0; // league group
    group.desc[1] = 0;
    put32(group.desc, 0xcc, 0);
    group.fixtures = {0, 1, 2, 0};
    group.results = {0x12, 0x30};
    group.end_minute = {87, 90};
    // Each source log item is two words. Goal/miss rows consume the following
    // pair for their secondary player/detail; other rows advance by one pair.
    group.log = {0x0005, 0 * 0x80 + 7,
                 0x8006, 0 * 0x80 + 9,
                 0x3007, 1 * 0x80 + 12,
                 0x2fff, 1 * 0x80 + 121,
                 0x4005, 2 * 0x80 + 20};
    std::vector<ShowEvent> show{
        ShowEvent{Show::Chance, 8, 0, 0, 0, 0, 0, 5},
        ShowEvent{Show::Booking, 12, 0, 1, 1, 0, 1, 7},
        ShowEvent{Show::SetPiece, 20, 1, 0, 0, 0, 0, 6}};

    GameRandom game(0x12345678);
    WatcomRandom watcom(0x87654321);
    const auto game_before = game.seed();
    const auto game_calls = game.calls();
    const auto watcom_before = watcom.seed();
    const auto watcom_calls = watcom.calls();
    const uint8_t saved_abbreviation = state.memory.u8(::bmh::layout::ClubShortNameOffsets);
    const auto saved_badges = state.club_heap_78;

    const SportschauFixtureView view = sportschau_fixture_view(state, group, 0, 0, show);
    check(view.group == 0 && view.fixture == 0 && view.competition_type == 0 &&
          view.source_setting_index == 0, "fixture identity carries the Source league setting index");
    check(view.home_team == 0 && view.away_team == 1 && view.home_name == std::vector<uint8_t>({'A','l','p','h','a'}) &&
          view.away_name == std::vector<uint8_t>({'B','r','a','v','o'}),
          "fixture club names remain Original bytes");
    check(view.source_played_minute == 87,
          "fixture report receives the descriptor's Source played-minute byte");
    check(view.has_replay_progression_mode && view.replay_progression_mode == 1 &&
          view.has_replay_chance_mode && view.replay_chance_mode == 0,
          "fixture view preserves both saved scene-control bytes without interpreting missing memory");
    check(view.home_short_name == std::vector<uint8_t>({'l'}) &&
          view.away_short_name == std::vector<uint8_t>({'r'}) &&
          view.home_short_name_available && view.away_short_name_available &&
          view.home_name_requires_font_fit && view.away_name_requires_font_fit &&
          !view.home_name_requires_longest_word,
          "domestic Source abbreviations use b5d24 substring before the caller's FNT3 trim");
    check(view.home_badge_word == 0x0018 && view.away_badge_word == 0x0027,
          "badge projection preserves the live ba378 words for legacy routine");
    check(view.score_known && view.home_goals == 1 && view.away_goals == 2,
          "fixture carries the already-computed Source result");
    check(view.log_events.size() == 4 && view.log_events[0].event_ordinal == 0 &&
          view.log_events[3].event_ordinal == 3,
          "group log is filtered by fixture teams without reordering source pairs");
    check(view.log_events[0].source_code == 0 && view.log_events[0].player_id == 5 &&
          view.log_events[0].team_id == 0 && view.log_events[0].source_value == 7 &&
          view.log_events[0].consumes_following_pair && view.log_events[0].has_following_pair &&
          view.log_events[0].following_code_player_word == 0x8006 &&
          view.log_events[0].following_team_value_word == 9 &&
          view.log_events[0].following_player_name == std::vector<uint8_t>({'P','l','a','y','e','r','B'}),
          "goal look-ahead preserves the next raw pair and its Original player name");
    check(view.log_events[1].source_word_index == 2 && view.log_events[1].source_code == 8 &&
          view.log_events[2].source_word_index == 4 && view.log_events[2].team_id == 1 &&
          view.log_events[3].player_id == 0x0fff && view.log_events[3].has_following_pair,
          "secondary/source-code rows remain available without assuming a fixed triple record");
    check(view.show_events.size() == 1 && view.show_events[0].what == Show::Chance &&
          view.show_events[0].group == 0 && view.show_events[0].match == 0,
          "live ShowEvents are a separate stream filtered to this fixture");
    check(game.seed() == game_before && game.calls() == game_calls && watcom.seed() == watcom_before &&
          watcom.calls() == watcom_calls && state.memory.u8(::bmh::layout::ClubShortNameOffsets) == saved_abbreviation &&
          state.club_heap_78 == saved_badges,
          "projection performs no Native writes or RNG draws");
}

void test_source_selection_priority_and_reset() {
    GameState g;
    g.memory.add_block(::bmh::layout::LeaguePresentationOptions, 0xb4);
    g.memory.set8(::bmh::layout::CareerSession_FormatError_RuntimeField, 2);
    g.team_morale.assign(6, 25);
    g.league_rounds.assign(9, 0);
    g.squad_slots.assign(6 * 24, kEmptySlot);
    for (int team = 0; team < 6; ++team)
        for (int i = 0; i < 11; ++i) {
            Player p;
            p.w35c = 50u | (80u << 7) | (80u << 14);
            g.squad_slots[size_t(team * 24 + i)] = uint16_t(kSlotInLineup | g.players.size());
            g.players.push_back(p);
        }
    MatchGroup group;
    group.desc.assign(0xd0, 0);
    WatcomRandom rand(8128);
    reset_sportschau_selection(g);
    check(consider_sportschau_fixture(g, group, 0, 1, rand) &&
          consider_sportschau_fixture(g, group, 2, 3, rand) &&
          consider_sportschau_fixture(g, group, 4, 5, rand), "three eligible domestic candidates rated");
    check(rand.calls() == 0 && g.sportschau_priorities[0] == 20 &&
          g.sportschau_priorities[1] == 20 && g.sportschau_homes[0] == 0 &&
          g.sportschau_homes[1] == 2 && !sportschau_selected(g, 4),
          "equal byte priorities preserve the earlier two candidates, without random draws");
    g.manager_teams = {4};
    consider_sportschau_fixture(g, group, 4, 5, rand);
    check(g.sportschau_homes[0] == 4 && g.sportschau_priorities[0] == 1 &&
          g.sportschau_homes[1] == 0, "Source managed candidate minus300 clamps signed priority to1");
    const auto homes = g.sportschau_homes;
    reset_sportschau_selection(g);
    check(g.sportschau_homes == homes && g.sportschau_priorities[0] == 255 &&
          g.sportschau_priorities[1] == 255 && sportschau_selected(g, 4),
          "Original mode0 resets priorities, preserving stale home words used by mode1");
    g.memory.set8(::bmh::layout::Controller_FormatError_ExecutableField2, 1);
    check(!consider_sportschau_fixture(g, group, 2, 3, rand) && g.sportschau_homes == homes &&
          rand.calls() == 0, "disabled report competition consumes no candidate work");
    g.memory.set8(::bmh::layout::Controller_FormatError_ExecutableField2, 0);
    g.memory.set8(::bmh::layout::CareerSession_FormatError_RuntimeField, 0);
    reset_sportschau_selection(g);
    consider_sportschau_fixture(g, group, 2, 3, rand);
    check(g.sportschau_homes[0] == 2 && !sportschau_selected(g, 2),
          "capacity0 still rates and writes the insertion slot but selects no home");
    g.memory.set8(::bmh::layout::CareerSession_FormatError_RuntimeField, 10);
    bool rejected = false;
    try { reset_sportschau_selection(g); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "corrupt A8 cannot overrun the Original ten slots");
}

void test_source_report_steps() {
    SportschauFixtureView f;
    f.source_played_minute = 14;
    SportschauLogEvent first;
    first.source_value = 12;
    first.source_code = 0;
    first.consumes_following_pair = true;
    first.source_word_index = 0;
    SportschauLogEvent companion;
    companion.source_value = 100;
    companion.source_word_index = 2;
    SportschauLogEvent backwards;
    backwards.source_value = 4;
    backwards.source_code = 2;
    backwards.source_word_index = 4;
    f.log_events = {first, companion, backwards};
    const auto steps = ui::sportschau_report_steps(f);
    check(steps.front().visible_log_entries == 0 && steps.front().progress_minute == 0,
          "progress starts with no future score or complete-time strip");
    check(steps.size() == 17 && steps[11].progress_minute == 11 && steps[11].minute_wait &&
          steps[12].visible_log_entries == 2 && steps[12].chance_wait &&
          steps[13].visible_log_entries == 3 && steps[13].progress_minute == 11 &&
          steps.back().progress_minute == 14 && steps.back().visible_log_entries == 3 &&
          steps.back().terminal_wait && !steps[11].terminal_wait,
          "old minute, consumed companion, backwards event and inclusive final minute follow Source");
    f.log_events.clear();
    const auto empty = ui::sportschau_report_steps(f);
    check(empty.size() == 15 && empty[1].progress_minute == 1 && empty.back().progress_minute == 14,
          "scoreless empty logs still progress through the played-minute byte");
}

void test_cup_setting_row_comes_from_live_descriptor() {
    GameState state = source_game();
    const uint32_t entry = cup_entry_address(4);
    state.memory.set8(entry + 0x18, 12);
    state.memory.set8(entry + 0x1d, 3); // conflicting later byte must not be read
    MatchGroup group;
    group.desc.assign(0xd0, 0);
    group.desc[0] = 1;
    put32(group.desc, 0xcc, entry);
    check(sportschau_source_setting_index(state, group) == 17,
          "cup setting index adds argument 5 to the cup-entry byte at +0x18");
    state.memory.set8(entry + 0x18, 0);
    check(sportschau_source_setting_index(state, group) == 5,
          "cup setting index preserves the low-entry boundary at Source row 5");
    put32(group.desc, 0xcc, 20);
    check(sportschau_source_setting_index(state, group) == 19,
          "Source setting rows for league-style IDs 8..20 saturate at row 19");
}

void test_missing_domestic_abbreviation_does_not_use_foreign_fallback() {
    GameState state;
    state.club_ids = {0, 1};
    state.club_names = {'A','l','p','h','a',0,'B','r','a','v','o',0};
    MatchGroup group;
    group.desc.assign(0xd0, 0);
    group.fixtures = {0, 1};
    group.results = {0x00};
    const auto view = sportschau_fixture_view(state, group, 0, 0, {});
    check(!view.home_short_name_available && !view.away_short_name_available &&
          !view.home_name_requires_longest_word && !view.away_name_requires_longest_word,
          "missing domestic b5d24 data remains explicitly unavailable instead of using the foreign longest-word path");
}

void test_stale_and_nul_crossing_domestic_abbreviations(const std::filesystem::path& data) {
    GameState stale = source_game();
    stale.club_names = {'R','W',' ','E','S','S','E','N',0,'X',0};
    stale.club_ids = {0, 9};
    stale.memory.set8(::bmh::layout::ClubShortNameOffsets, 0xa5); // offset 10, length 6: beyond "RW ESSEN".
    MatchGroup group;
    group.desc.assign(0xd0, 0);
    group.fixtures = {0, 1};
    group.results = {0x10};
    const auto stale_view = sportschau_fixture_view(stale, group, 0, 0, {});
    check(!stale_view.home_short_name_available && stale_view.home_short_name.empty() &&
          stale_view.home_name_requires_font_fit && !stale_view.home_name_requires_longest_word,
          "stale domestic descriptor is represented as unavailable instead of inventing a fallback label");

    const auto exe = load_le(data / "BMH.exe");
    ui::SportschauReportContext options;
    options.solid_panel_background = true;
    options.gradient_text = stale.memory.has(::bmh::layout::TextGradientDisabled) && stale.memory.u8(::bmh::layout::TextGradientDisabled) == 0 &&
                            exe.u8(::bmh::layout::TextGradientMode) == 2;
    const ui::SportschauMatchReport stale_report(data, stale_view, options);
    const auto stale_image = stale_report.render_report(stale_view.log_events.size());
    SportschauFixtureView blank_label = stale_view;
    blank_label.home_short_name_available = true;
    const ui::SportschauMatchReport blank_report(data, blank_label, options);
    check(stale_image.pixels == blank_report.render_report(blank_label.log_events.size()).pixels,
          "stale domestic table bytes render as a blank bounded label without aborting the report");

    GameState crossing = source_game();
    crossing.club_names = {'A','B',0,'X','Y',0};
    crossing.club_ids = {0, 3};
    crossing.memory.set8(::bmh::layout::ClubShortNameOffsets, 0x13); // begin at B; encoded length crosses the NUL.
    const auto crossing_view = sportschau_fixture_view(crossing, group, 0, 0, {});
    check(crossing_view.home_name == std::vector<uint8_t>({'A','B'}) &&
          crossing_view.home_short_name_available &&
          crossing_view.home_short_name == std::vector<uint8_t>({'B'}),
          "a domestic slice crossing the source NUL keeps only the bounded C-string prefix");
    const ui::SportschauMatchReport crossing_report(data, crossing_view, options);
    SportschauFixtureView bounded_label = crossing_view;
    bounded_label.home_short_name = {'B'};
    const ui::SportschauMatchReport bounded_report(data, bounded_label, options);
    check(crossing_report.render_report(crossing_view.log_events.size()).pixels ==
              bounded_report.render_report(bounded_label.log_events.size()).pixels,
          "bytes after a crossed NUL do not become invented abbreviation glyphs");
}

void test_foreign_club_name_uses_source_longest_word_path() {
    GameState state;
    state.club_ids.assign(169, 0);
    state.club_names = {'N','e','w',' ','C','o','u','n','t','r','y',0};
    state.club_heap_78.assign(169 * 2, 0);
    MatchGroup group;
    group.desc.assign(0xd0, 0);
    group.fixtures = {168, 0};
    group.results = {0x00};
    const auto view = sportschau_fixture_view(state, group, 0, 0, {});
    check(view.home_name == std::vector<uint8_t>({'N','e','w',' ','C','o','u','n','t','r','y'}) &&
          view.home_short_name == view.home_name && view.home_short_name_available &&
          view.home_name_requires_longest_word && view.home_name_requires_font_fit,
          "foreign Source club names are available as full names with longest-word then FNT3-fit flags");
}

void test_process_group_callback_is_after_table_before_morale_and_rng() {
    GameState base;
    base.club_ids = {0, 1};
    base.club_names = {'A', 0, 'B', 0};
    base.club_table.assign(2 * 0x4a, 0);
    base.team_morale = {50, 50};
    base.team_flags = {0, 0};
    base.squad_slots.assign(48, kEmptySlot);

    MatchGroup group;
    group.desc.assign(0xd0, 0);
    group.desc[0] = 0;
    put32(group.desc, 0xcc, 0);
    group.fixtures = {0, 1};
    group.results = {0x21}; // home 2, away 1
    group.end_minute = {87};
    group.log.clear(); // A played fixture can have an empty report log.

    auto without_hook = base;
    auto with_hook = base;
    GameRandom no_hook_game(0x12345678), observed_game(0x12345678);
    WatcomRandom no_hook_rand(0x87654321), observed_rand(0x87654321);
    MinuteReport no_hook_report, observed_report;
    const auto game_seed = observed_game.seed();
    const auto game_calls = observed_game.calls();
    const auto rand_seed = observed_rand.seed();
    const auto rand_calls = observed_rand.calls();
    int fixture_callbacks = 0;
    SportschauFixtureHook hook = [&](const GameState& state, const SportschauFixtureView& view) {
        ++fixture_callbacks;
        check(view.fixture == 0 && view.source_played_minute == 87 && view.log_events.empty(),
              "fixture boundary carries the played minute even when its log is empty");
        check(state.club_table.at(0x20) == 2 && state.club_table.at(0x22) == 1 &&
              state.club_table.at(0x4a + 0x20) == 1 && state.club_table.at(0x4a + 0x22) == 2,
              "fixture callback observes the already-booked league score");
        check(state.team_morale == std::vector<uint8_t>({50, 50}),
              "fixture callback runs before either side's morale update");
        check(observed_game.seed() == game_seed && observed_game.calls() == game_calls &&
              observed_rand.seed() == rand_seed && observed_rand.calls() == rand_calls,
              "Source report projection boundary itself consumes no random draws");
    };
    process_group(without_hook, group, no_hook_game, no_hook_rand, no_hook_report);
    process_group(with_hook, group, observed_game, observed_rand, observed_report, 0, hook);
    check(fixture_callbacks == 1 && !no_hook_report.unsupported && !observed_report.unsupported,
          "one nonvoid played fixture emits one immutable callback");
    check(with_hook.club_table == without_hook.club_table && with_hook.team_morale == without_hook.team_morale &&
          with_hook.team_flags == without_hook.team_flags && with_hook.squad_slots == without_hook.squad_slots &&
          with_hook.development_balance == without_hook.development_balance &&
          with_hook.trend_rotation == without_hook.trend_rotation &&
          observed_game.seed() == no_hook_game.seed() && observed_game.calls() == no_hook_game.calls() &&
          observed_rand.seed() == no_hook_rand.seed() && observed_rand.calls() == no_hook_rand.calls(),
          "enabled/disabled fixture observer produces identical post-group model fields and both RNG streams");

    group.kickoff_void_mask = 1;
    int void_callbacks = 0;
    process_group(with_hook, group, observed_game, observed_rand, observed_report, 0,
        [&](const GameState&, const SportschauFixtureView&) { ++void_callbacks; });
    check(void_callbacks == 0, "void fixture with an empty log does not produce a report callback");
}

void test_finish_event_intro_order_and_tactics_reset() {
    GameState base;
    base.manager_teams = {0};
    base.tactics.assign(0x840, 0);
    base.manager_records.assign(0xb0c, 0);
    base.manager_options.assign(5, 0);
    base.league_rounds.assign(20, 0);
    base.results.assign(20 * 0x17c, 0);
    base.memory.add_block(::bmh::layout::CalendarState_FormatError_RuntimeField2, 1);
    base.memory.add_block(::bmh::layout::CalendarState_FormatError_RuntimeField, 1);
    base.memory.add_block(::bmh::layout::DeskPosition, 1);
    base.memory.set8(::bmh::layout::DeskPosition, 9);

    MatchGroup empty_group;
    empty_group.desc.assign(0xd0, 0);
    empty_group.desc[0] = 0;
    put32(empty_group.desc, 0xcc, 0);
    std::vector<MatchGroup> baseline_groups{empty_group};
    std::vector<MatchGroup> observed_groups{empty_group};
    auto baseline = base;
    auto observed = base;
    GameRandom baseline_game(0x11223344), observed_game(0x11223344);
    WatcomRandom baseline_rand(0x55667788), observed_rand(0x55667788);
    MinuteReport baseline_report, observed_report;
    std::vector<uint8_t> buttons(180, 0);
    finish_event(baseline, baseline_groups, 0, buttons, {}, {}, baseline_game, baseline_rand, baseline_report);

    std::vector<int> intro_order;
    SportschauPresentationHooks hooks;
    hooks.intro = [&](const GameState& state, const SportschauIntroView& view) {
        intro_order.push_back(view.group_index);
        check(view.groups.size() == 1 && view.groups[0].group == 0,
              "intro projection owns the source-ordered group list");
        for (size_t slot = 0; slot < 24; ++slot) {
            const size_t at = slot * 0x58 + 0x44;
            check(state.tactics.at(at) == 0xff && state.tactics.at(at + 1) == 0x0f &&
                  state.tactics.at(at + 2) == 0xff && state.tactics.at(at + 3) == 0x0f,
                  "intro runs after every saved tactics marker is reset");
        }
        if (view.group_index == 0)
            check(state.memory.u8(::bmh::layout::DeskPosition) == 0, "per-group intro runs after the Source group cursor is set");
    };
    finish_event(observed, observed_groups, 0, buttons, {}, {}, observed_game, observed_rand,
                 observed_report, {}, &hooks);
    check(intro_order == std::vector<int>({-1, 0}),
          "post-matchday intro precedes the per-group intro in Source order");
    check(observed.tactics == baseline.tactics && observed.manager_records == baseline.manager_records &&
          observed.manager_options == baseline.manager_options && observed.league_rounds == baseline.league_rounds &&
          observed.results == baseline.results && observed.memory.u8(::bmh::layout::DeskPosition) == baseline.memory.u8(::bmh::layout::DeskPosition) &&
          observed_game.seed() == baseline_game.seed() && observed_game.calls() == baseline_game.calls() &&
          observed_rand.seed() == baseline_rand.seed() && observed_rand.calls() == baseline_rand.calls(),
          "intro callbacks enabled/disabled leave Native fields and both RNG streams identical");
}

class CareerHarness {
public:
    explicit CareerHarness(const std::filesystem::path& data)
        : data_(data), flow(data) {
        flow.click(150, 215);
        flow.click(5, 225, true);
        flow.click(5, 225, true);
        const auto started = flow.click(90, 169);
        const std::string error = serve(started);
        check(error.empty() && session && flow.screen_name() == "desktop",
              "Sportschau test starts a real German career through the App controller");
    }

    std::string serve(const ui::FlowResult& request, SportschauPresentationHooks hooks = {}) {
        return app::serve_flow_request(flow, request, session, data_, data_, 0x1789abcd,
            revision, playback, audio_session, false,
            {}, {}, {}, {}, {}, {}, {}, {}, {}, app::PendingOfferEntryPolicy::legacy_unpresented,
            {}, app::HumanBuyerMarketEntryPolicy::legacy_unpresented,
            {}, {}, {}, {}, std::move(hooks));
    }

    ui::FlowResult kickoff() {
        check(flow.desktop() != nullptr, "active production career remains at its desk boundary");
        const auto& items = flow.desktop()->state().items;
        const auto it = std::find_if(items.begin(), items.end(), [](const ui::DeskItem& item) {
            return item.kind == 12 && !item.disabled;
        });
        check(it != items.end(), "real German career exposes its kickoff action");
        return flow.click(it->x + it->width / 2, it->y + it->height / 2);
    }

    void set_source_settings(uint8_t intro_animation, bool eligible) {
        GameState& state = session->state();
        // This harness observes individual events, including AI-only reports.
        state.memory.set8(::bmh::layout::MatchdaySetup_MatchdayNeedsManagerDesks_RuntimeField,1);
        for (const uint32_t address : {::bmh::layout::Controller_FormatError_RuntimeFieldUnsigned, ::bmh::layout::Controller_FormatError_RuntimeField2Unsigned, ::bmh::layout::Controller_FormatError_RuntimeField3Unsigned})
            check(state.memory.has(address), "saved Sportschau setting byte exists in German Native memory");
        check(state.memory.has(::bmh::layout::Controller_FormatError_ExecutableField2 + 19 * 9), "full 20-row match-display matrix exists in German Native memory");
        state.memory.set8(::bmh::layout::Controller_FormatError_RuntimeField, 0); // full report setting enabled
        state.memory.set8(::bmh::layout::Controller_FormatError_RuntimeField2, intro_animation); // intro movie gate only
        state.memory.set8(::bmh::layout::Controller_FormatError_RuntimeField3, 0); // show the fixture report
        for (int row = 0; row < 20; ++row)
            state.memory.set8(::bmh::layout::Controller_FormatError_ExecutableField2 + uint32_t(row) * 9, eligible ? 0 : 1);
    }

    void advance_to_first_matchday(const SportschauPresentationHooks& hooks = {}) {
        const auto has_completed_matchday = [&] {
            const auto& result = session->last_turn_results();
            return result.valid && !result.groups.empty();
        };
        for (int n = 0; n < 32 && !has_completed_matchday(); ++n) {
            const auto request = kickoff();
            check(request.request == ui::FlowRequest::kickoff, "real desk click creates a kickoff request");
            const auto error = serve(request, hooks);
            check(error.empty(), "real matchday completes without unsupported engine path: " + error);
        }
        check(session->last_turn_results().valid && !session->last_turn_results().groups.empty(),
              "real German career reaches a completed matchday within the bounded test window");
    }

    std::filesystem::path data_;
    ui::StartupFlow flow;
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision = 0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
};

class FixtureCopy {
public:
    explicit FixtureCopy(const std::filesystem::path& input) {
        static unsigned serial = 0;
        path = std::filesystem::temp_directory_path() /
            ("openbmh-sportschau-app-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
             std::to_string(++serial));
        check(std::filesystem::create_directory(path), "create isolated Sportschau save directory");
        std::filesystem::copy_file(input, path / "AUTOSAVE.FBM");
    }
    ~FixtureCopy() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    std::filesystem::path path;
};

class LoadedSportschauHarness {
public:
    LoadedSportschauHarness(const std::filesystem::path& data, const std::filesystem::path& input)
        : data_(data), saves(input), flow(data, saves.path) {
        check(load_savegame(input).checksum_ok, "controlled WEEK135 fixture has a valid Native checksum");
        playback.set_muted(true);
        flow.click(214, 215);
        const auto load = flow.send_key("ENTER");
        check(load.request == ui::FlowRequest::file_action &&
              load.file_action.action == ui::SaveLoadAction::load,
              "ordinary loaded-career UI requests the prepared WEEK135 Save");
        const std::string error = serve(load);
        check(error.empty() && session && revision == 1 && flow.screen_name() == "desktop",
              "Main Controller loads the controlled WEEK135 career to its actual desk");
    }

    std::string serve(const ui::FlowResult& request, SportschauPresentationHooks hooks = {}) {
        return app::serve_flow_request(flow, request, session, data_, saves.path, 0x1789abcd,
            revision, playback, audio_session, false,
            {}, {}, {}, {}, {}, {}, {}, {}, {}, app::PendingOfferEntryPolicy::normal_interactive,
            {}, app::HumanBuyerMarketEntryPolicy::legacy_unpresented,
            {}, {}, {}, {}, std::move(hooks));
    }

    void apply_shared_source_settings() {
        GameState& state = session->state();
        check(state.memory.has(::bmh::layout::Controller_FormatError_RuntimeField) && state.memory.u8(::bmh::layout::Controller_FormatError_RuntimeField) == 0,
              "WEEK135 already has the controlled full-report Source setting a2506=0");
        check(state.memory.has(::bmh::layout::Controller_FormatError_RuntimeField2) && state.memory.has(::bmh::layout::Controller_FormatError_RuntimeField3) &&
              state.memory.has(::bmh::layout::Controller_FormatError_ExecutableField2 + 19 * 9),
              "WEEK135 contains the intro, report, and full 20-row source settings matrix");
        state.memory.set8(::bmh::layout::Controller_FormatError_RuntimeField2, 0);
        state.memory.set8(::bmh::layout::Controller_FormatError_RuntimeField3, 0);
        for (int row = 0; row < 20; ++row)
            state.memory.set8(::bmh::layout::Controller_FormatError_ExecutableField2 + uint32_t(row) * 9, 0);
    }

    ui::FlowResult kickoff() {
        check(flow.desktop() && !flow.desktop()->pending(),
              "loaded WEEK135 desk has no unconsumed action before kickoff");
        const auto& items = flow.desktop()->state().items;
        const auto item = std::find_if(items.begin(), items.end(), [](const ui::DeskItem& value) {
            return value.kind == 12 && !value.disabled;
        });
        check(item != items.end(), "loaded WEEK135 exposes its real enabled advance-turn desk item");
        const auto request = flow.click(item->x + item->width / 2, item->y + item->height / 2);
        check(request.request == ui::FlowRequest::kickoff && flow.desktop()->pending() &&
              *flow.desktop()->pending() == request.desk_action,
              "physical-equivalent Flow click creates the owned kickoff request");
        return request;
    }

    std::filesystem::path data_;
    FixtureCopy saves;
    ui::StartupFlow flow;
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision = 0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
};

bool same_match_group(const MatchGroup& a, const MatchGroup& b) {
    return a.desc == b.desc && a.fixtures == b.fixtures && a.results == b.results &&
           a.cup_results == b.cup_results && a.cup_other_leg == b.cup_other_leg &&
           a.cup_result_address[0] == b.cup_result_address[0] &&
           a.cup_result_address[1] == b.cup_result_address[1] && a.phase == b.phase &&
           a.records == b.records && a.end_minute == b.end_minute &&
           a.spectators == b.spectators && a.log == b.log && a.ended == b.ended &&
           a.kickoff_void_mask == b.kickoff_void_mask;
}

void test_source_selection_foreign_draws(const std::filesystem::path& data,
                                        const std::filesystem::path& input) {
    app::CareerSession session(data, 8128, input);
    GameState g = session.state();
    g.memory.set8(::bmh::layout::CareerSession_FormatError_RuntimeField, 1);
    g.memory.set8(::bmh::layout::Controller_FormatError_ExecutableField2, 0);
    g.sportschau_club_threshold = session.sportschau_source_options().rated_club_threshold;
    MatchGroup group;
    group.desc.assign(0xd0, 0);
    WatcomRandom expected(8128), actual(8128);
    int priority = 0;
    for (int team : {168, 169}) {
        const int value = team_average(g, team, 0x1ff, &expected).value;
        priority += ((100 - int(uint16_t(value))) >> 1) + 2 + 50 + 90;
    }
    reset_sportschau_selection(g);
    const auto native = save_stream(g);
    check(consider_sportschau_fixture(g, group, 168, 169, actual), "foreign pair is eligible");
    check(priority > 255 && g.sportschau_priorities[0] == uint8_t(priority) &&
          g.sportschau_homes[0] == 168 && actual.calls() > 0 &&
          actual.calls() == expected.calls() && actual.seed() == expected.seed() && save_stream(g) == native,
          "foreign priority truncates to low byte and consumes both real ratings without Native edits");
    g.memory.set8(::bmh::layout::CareerSession_FormatError_RuntimeField, 0);
    actual = WatcomRandom(8128);
    reset_sportschau_selection(g);
    consider_sportschau_fixture(g, group, 168, 169, actual);
    check(actual.seed() == expected.seed() && actual.calls() == expected.calls() && !sportschau_selected(g, 168),
          "zero capacity still consumes the Original foreign-rating draws");
}

void test_controlled_week135_actual_renderers_and_reload(const std::filesystem::path& data,
                                                         const std::filesystem::path& input) {
    const auto original_bytes = load_savegame(input).stream;
    LoadedSportschauHarness plain(data, input);
    LoadedSportschauHarness observed(data, input);
    plain.apply_shared_source_settings();
    observed.apply_shared_source_settings();
    // The renderer counts one event's frames/reports; use a declared F6-off control.
    plain.session->state().memory.set8(::bmh::layout::MatchdaySetup_MatchdayNeedsManagerDesks_RuntimeField,1);
    observed.session->state().memory.set8(::bmh::layout::MatchdaySetup_MatchdayNeedsManagerDesks_RuntimeField,1);
    check(save_stream(plain.session->state()) == save_stream(observed.session->state()) &&
          plain.session->rand().seed() == observed.session->rand().seed() &&
          plain.session->rand().calls() == observed.session->rand().calls() &&
          plain.session->game().seed() == observed.session->game().seed() &&
          plain.session->game().calls() == observed.session->game().calls(),
          "baseline and renderer sessions start from the same report settings plus F6-off WEEK135 controls and RNG state");

    const SportschauSourceOptions source_options = observed.session->sportschau_source_options();
    check(source_options.intro_after_matchday == 1 && source_options.intro_each_group == 0,
          "controlled WEEK135 uses the German Original's post-matchday Sportschau intro gate");
    const LeImage exe = load_le(data / "BMH.exe");
    ui::SportschauIntro intro(data);
    size_t intro_callbacks = 0;
    size_t intro_frames = 0;
    size_t report_callbacks = 0;
    size_t report_pixels = 0;
    std::vector<std::pair<int, int>> report_ordinals;
    SportschauPresentationHooks renderers;
    renderers.intro = [&](const GameState& state, const SportschauIntroView& view) {
        ++intro_callbacks;
        check(view.group_index == -1 && !view.groups.empty() &&
              state.memory.u8(::bmh::layout::Controller_FormatError_RuntimeField) == 0 && state.memory.u8(::bmh::layout::Controller_FormatError_RuntimeField2) == 0,
              "intro renderer receives the actual enabled post-matchday Source view");
        for (uint32_t frame_index = 0; frame_index < ui::SportschauIntro::frame_count; ++frame_index) {
            const Image frame = intro.render_frame(frame_index);
            check(frame.width == 320 && frame.height == 240 && frame.pixels.size() == 320u * 240u,
                  "each actual Sportschau intro callback renders a complete logical frame");
            ++intro_frames;
        }
    };
    renderers.fixture = [&](const GameState& state, const SportschauFixtureView& fixture) {
        check(fixture.source_setting_index >= 0 && fixture.source_setting_index < 20 &&
              state.memory.u8(::bmh::layout::Controller_FormatError_ExecutableField2 + uint32_t(fixture.source_setting_index) * 9) == 0 &&
              state.memory.u8(::bmh::layout::Controller_FormatError_RuntimeField3) == 0,
              "report renderer is reached only through the selected Original saved-matrix gates");
        ui::SportschauReportContext context;
        context.solid_panel_background = state.memory.has(::bmh::layout::SolidPanelBackground) && state.memory.u8(::bmh::layout::SolidPanelBackground) != 0;
        context.gradient_text = state.memory.has(::bmh::layout::TextGradientDisabled) && state.memory.u8(::bmh::layout::TextGradientDisabled) == 0 &&
                                exe.u8(::bmh::layout::TextGradientMode) == 2;
        const ui::SportschauMatchReport report(data, fixture, context);
        const Image image = report.render_report(fixture.log_events.size());
        check(image.width == 320 && image.height == 240 && image.pixels.size() == 320u * 240u,
              "every actual played-fixture callback renders a complete static report");
        ++report_callbacks;
        report_pixels += image.pixels.size();
        report_ordinals.emplace_back(fixture.group, fixture.fixture);
    };

    const ui::FlowResult plain_request = plain.kickoff();
    check(plain.serve(plain_request).empty(), "unobserved loaded WEEK135 matchday completes through Controller");
    const ui::FlowResult observed_request = observed.kickoff();
    check(observed.serve(observed_request, renderers).empty(),
          "actual Sportschau render callbacks do not abort the loaded WEEK135 matchday");
    check(plain.session->last_turn_results().valid && !plain.session->last_turn_results().groups.empty() &&
          observed.session->last_turn_results().valid && !observed.session->last_turn_results().groups.empty() &&
          plain.session->completed_matchday_results().has_value() &&
          observed.session->completed_matchday_results().has_value(),
          "both baseline and observed paths finish the complete matchday and own its results modal");
    check(intro_callbacks == 1 && intro_frames == ui::SportschauIntro::frame_count,
          "the Main-equivalent Source gates present all 40 frames exactly once");

    size_t expected_report_callbacks = 0;
    const auto& final_state = plain.session->state();
    for (const MatchGroup& group : plain.session->last_turn_results().groups) {
        const int setting_index = sportschau_source_setting_index(final_state, group);
        if (source_options.intro_after_matchday == 0 || setting_index < 0 || setting_index >= 20 ||
            final_state.memory.u8(::bmh::layout::Controller_FormatError_ExecutableField2 + uint32_t(setting_index) * 9) != 0 ||
            final_state.memory.u8(::bmh::layout::Controller_FormatError_RuntimeField3) != 0) continue;
        for (int fixture = 0; fixture < group.matches(); ++fixture) {
            if (group.kickoff_void_mask & (1u << fixture)) continue;
            if (!group.results.empty() && (group.results.at(size_t(fixture)) & 0xcc) == 0xcc) continue;
            ++expected_report_callbacks;
        }
    }
    check(expected_report_callbacks > 0 && report_callbacks == expected_report_callbacks &&
          report_pixels == report_callbacks * 320u * 240u,
          "actual report callback count covers every eligible non-void Source fixture, including the stale-name case");
    check(std::is_sorted(report_ordinals.begin(), report_ordinals.end()) &&
          std::adjacent_find(report_ordinals.begin(), report_ordinals.end()) == report_ordinals.end(),
          "all rendered group/fixture report callbacks appear once in Source order");
    const auto& plain_result = plain.session->last_turn_results();
    const auto& observed_result = observed.session->last_turn_results();
    check(plain_result.date == observed_result.date &&
          plain_result.calendar_entry == observed_result.calendar_entry &&
          plain_result.matchday == observed_result.matchday &&
          plain_result.groups.size() == observed_result.groups.size(),
          "presentation leaves the completed matchday identity unchanged");
    for (size_t i = 0; i < plain_result.groups.size(); ++i)
        check(same_match_group(plain_result.groups[i], observed_result.groups[i]),
              "presentation leaves every complete played fixture/log/result group unchanged");
    check(plain.revision == observed.revision &&
          save_stream(plain.session->state()) == save_stream(observed.session->state()) &&
          plain.session->game().seed() == observed.session->game().seed() &&
          plain.session->game().calls() == observed.session->game().calls() &&
          plain.session->rand().seed() == observed.session->rand().seed() &&
          plain.session->rand().calls() == observed.session->rand().calls(),
          "all render callbacks preserve complete Native bytes and both RNG seed/call-count pairs");
    check(load_savegame(input).stream == original_bytes,
          "the read-only controlled WEEK135 fixture remains byte-identical on disk");

    const Bytes before_save = save_stream(observed.session->state());
    const uint64_t w_calls_before_save = observed.session->rand().calls();
    const auto result_path = observed.saves.path / "RESULTS.FBM";
    std::string save_error;
    const int manager_slot = observed.session->state().memory.u8(::bmh::layout::ActiveManager);
    check(observed.session->save_game(result_path, manager_slot, false, save_error) && save_error.empty(),
          "completed rendered matchday saves through the actual CareerSession save codec");
    check(save_stream(observed.session->state()) == before_save &&
          observed.session->rand().calls() == w_calls_before_save + 4,
          "RESULTS.FBM Save preserves gameplay Native and consumes only the codec's four W draws");
    check(load_savegame(result_path).checksum_ok && load_savegame(result_path).stream == before_save,
          "saved matchday result has a valid checksum and exact full Native stream");
    app::CareerSession fresh(data, 0x1789abcd, result_path);
    check(save_stream(fresh.state()) == before_save && fresh.state().date == observed.session->state().date,
          "a fresh CareerSession reload resumes at the complete rendered result's saved date");
    const auto resumed_desk = fresh.desktop_projection(1);
    check(resumed_desk.manager_slot == manager_slot &&
          std::any_of(resumed_desk.items.begin(), resumed_desk.items.end(), [](const ui::DeskItem& item) {
              return item.kind == 12 && !item.disabled;
          }), "fresh reload reconstructs the next enabled desk advance action");
}

void test_source_option_memory_runtime_exe_precedence(const std::filesystem::path& data) {
    CareerHarness harness(data);
    const auto original = harness.session->sportschau_source_options();
    check(original.intro_after_matchday == 1 && original.intro_each_group == 0,
          "fresh German career resolves the mapped EXE/runtime Sportschau defaults");
    GameState& state = harness.session->state();
    check(!state.memory.has(::bmh::layout::CareerSession_SportschauSourceOptions_ExecutableField) && !state.memory.has(::bmh::layout::CareerSession_DispatchTransferOffer_ExecutableField2),
          "both source option bytes are runtime/EXE values rather than saved FBM fields");

    auto runtime = harness.session->club_modal_runtime_checkpoint();
    runtime[::bmh::layout::CareerSession_SportschauSourceOptions_ExecutableField] = 0;
    runtime[::bmh::layout::CareerSession_DispatchTransferOffer_ExecutableField2] = 1;
    harness.session->restore_club_modal_runtime(runtime);
    const auto from_runtime = harness.session->sportschau_source_options();
    check(from_runtime.intro_after_matchday == 0 && from_runtime.intro_each_group == 1,
          "current club-modal runtime bytes override conflicting EXE defaults");

    runtime.erase(::bmh::layout::CareerSession_SportschauSourceOptions_ExecutableField);
    runtime.erase(::bmh::layout::CareerSession_DispatchTransferOffer_ExecutableField2);
    harness.session->restore_club_modal_runtime(runtime);
    const auto from_exe = harness.session->sportschau_source_options();
    check(from_exe.intro_after_matchday == original.intro_after_matchday &&
          from_exe.intro_each_group == original.intro_each_group,
          "missing runtime bytes fall back to the mapped EXE values");

    state.memory.add_block(::bmh::layout::CareerSession_SportschauSourceOptions_ExecutableField, 1);
    state.memory.add_block(::bmh::layout::CareerSession_DispatchTransferOffer_ExecutableField2, 1);
    state.memory.set8(::bmh::layout::CareerSession_SportschauSourceOptions_ExecutableField, 1);
    state.memory.set8(::bmh::layout::CareerSession_DispatchTransferOffer_ExecutableField2, 0);
    runtime[::bmh::layout::CareerSession_SportschauSourceOptions_ExecutableField] = 0;
    runtime[::bmh::layout::CareerSession_DispatchTransferOffer_ExecutableField2] = 1;
    harness.session->restore_club_modal_runtime(runtime);
    const auto from_memory = harness.session->sportschau_source_options();
    check(from_memory.intro_after_matchday == 1 && from_memory.intro_each_group == 0,
          "saved Native memory takes precedence over a conflicting runtime/EXE pair");
}

void test_real_career_observer_gate_and_rng_neutrality(const std::filesystem::path& data) {
    CareerHarness plain(data), observed(data);
    const auto plain_options = plain.session->sportschau_source_options();
    const auto observed_options = observed.session->sportschau_source_options();
    check(plain_options.intro_after_matchday == 1 && observed_options.intro_after_matchday == 1 &&
          plain_options.intro_each_group == 0 && observed_options.intro_each_group == 0,
          "the loaded German supported edition EXE source options are pinned to one post-matchday intro");
    plain.set_source_settings(0, true);
    observed.set_source_settings(0, true);
    check(save_stream(plain.session->state()) == save_stream(observed.session->state()) &&
          plain.session->game().seed() == observed.session->game().seed() &&
          plain.session->rand().seed() == observed.session->rand().seed(),
          "normal observer comparison starts from the same saved German career and seeds");

    const auto original_table = observed.session->state().club_table;
    const auto original_morale = observed.session->state().team_morale;
    std::vector<int> intro_groups;
    std::vector<int> report_groups;
    std::vector<int> report_fixtures;
    std::vector<int> expected_gf(observed.session->state().club_ids.size());
    std::vector<int> expected_ga(observed.session->state().club_ids.size());
    SportschauPresentationHooks hooks;
    hooks.intro = [&](const GameState& state, const SportschauIntroView& view) {
        intro_groups.push_back(view.group_index);
        for (size_t manager = 0; manager < state.manager_teams.size(); ++manager)
            for (size_t slot = 0; slot < 24; ++slot) {
                const size_t at = manager * 0x840 + slot * 0x58 + 0x44;
                check(state.tactics.at(at) == 0xff && state.tactics.at(at + 1) == 0x0f &&
                      state.tactics.at(at + 2) == 0xff && state.tactics.at(at + 3) == 0x0f,
                      "actual intro callback sees the post-simulation tactics reset");
            }
    };
    hooks.fixture = [&](const GameState& state, const SportschauFixtureView& view) {
        report_groups.push_back(view.group);
        report_fixtures.push_back(view.fixture);
        check(view.source_setting_index >= 0 && view.source_setting_index < 20 &&
              state.memory.u8(::bmh::layout::Controller_FormatError_ExecutableField2 + uint32_t(view.source_setting_index) * 9) == 0,
              "actual report callback is admitted only by the selected saved column-3 setting");
        check(view.has_replay_progression_mode && view.has_replay_chance_mode,
              "actual report view preserves both saved scene-control bytes");
        if ((view.competition_type == 0 || view.competition_type == 10) && view.score_known) {
            for (const auto [team, goals_for, goals_against] : {
                    std::tuple<int,int,int>{view.home_team, view.home_goals, view.away_goals},
                    std::tuple<int,int,int>{view.away_team, view.away_goals, view.home_goals}}) {
                const size_t base = size_t(team) * 0x4a;
                check(base + 0x22 < original_table.size() && size_t(team) < original_morale.size(),
                      "fixture report Source table/morale offsets are in range");
                check(state.club_table.at(base + 0x20) == original_table.at(base + 0x20) +
                          expected_gf.at(size_t(team)) + goals_for &&
                      state.club_table.at(base + 0x22) == original_table.at(base + 0x22) +
                          expected_ga.at(size_t(team)) + goals_against,
                      "actual report is after the league result was booked into the table");
                check(state.team_morale.at(size_t(team)) == original_morale.at(size_t(team)),
                      "actual report is before morale changes for the current fixture");
            }
            expected_gf.at(size_t(view.home_team)) += view.home_goals;
            expected_ga.at(size_t(view.home_team)) += view.away_goals;
            expected_gf.at(size_t(view.away_team)) += view.away_goals;
            expected_ga.at(size_t(view.away_team)) += view.home_goals;
        }
    };

    plain.advance_to_first_matchday();
    observed.advance_to_first_matchday(hooks);
    check(!intro_groups.empty() && intro_groups.front() == -1 && !report_groups.empty() &&
          !report_fixtures.empty(),
          "normal German saved settings present the post-matchday intro and fixture reports");
    check(plain.revision == observed.revision &&
          save_stream(plain.session->state()) == save_stream(observed.session->state()) &&
          plain.session->game().seed() == observed.session->game().seed() &&
          plain.session->game().calls() == observed.session->game().calls() &&
          plain.session->rand().seed() == observed.session->rand().seed() &&
          plain.session->rand().calls() == observed.session->rand().calls(),
          "actual Source intro/report callbacks leave the entire Native stream and both RNGs identical");
}

void test_real_controller_gates_and_throw_terminal(const std::filesystem::path& data) {
    CareerHarness movie_disabled(data);
    movie_disabled.set_source_settings(1, true);
    int disabled_intro = 0, visible_reports = 0;
    SportschauPresentationHooks disabled_hooks;
    disabled_hooks.intro = [&](const GameState&, const SportschauIntroView&) { ++disabled_intro; };
    disabled_hooks.fixture = [&](const GameState&, const SportschauFixtureView&) { ++visible_reports; };
    movie_disabled.advance_to_first_matchday(disabled_hooks);
    check(disabled_intro == 0 && visible_reports > 0,
          "a250f disables only the intro movie while report fixtures remain visible");

    CareerHarness no_eligible(data);
    no_eligible.set_source_settings(0, false);
    int no_eligible_intro = 0, no_eligible_reports = 0;
    SportschauPresentationHooks hidden_hooks;
    hidden_hooks.intro = [&](const GameState&, const SportschauIntroView&) { ++no_eligible_intro; };
    hidden_hooks.fixture = [&](const GameState&, const SportschauFixtureView&) { ++no_eligible_reports; };
    no_eligible.advance_to_first_matchday(hidden_hooks);
    check(no_eligible_intro == 0 && no_eligible_reports == 0,
          "nonzero column-3 selectors suppress both intro and fixture reports");

    CareerHarness throwing(data);
    throwing.set_source_settings(0, true);
    int prior_hook_calls = 0, throwing_calls = 0;
    SportschauPresentationHooks prior;
    prior.intro = [&](const GameState&, const SportschauIntroView&) { ++prior_hook_calls; };
    throwing.session->set_sportschau_presentation_hooks(prior);
    SportschauPresentationHooks throw_hooks;
    throw_hooks.intro = [&](const GameState&, const SportschauIntroView&) {
        ++throwing_calls;
        throw std::runtime_error("intentional Sportschau presenter failure");
    };
    ui::FlowResult failed_request;
    for (int n = 0; n < 32 && throwing_calls == 0; ++n) {
        failed_request = throwing.kickoff();
        check(failed_request.request == ui::FlowRequest::kickoff, "throw fixture uses real kickoff request");
        check(throwing.serve(failed_request, throw_hooks).empty(),
              "turn-driver exception rejects the owned pending desk without resimulating");
    }
    check(throwing_calls == 1 && throwing.session->sportschau_presentation_hooks().intro,
          "throwing callback runs once and controller RAII restores its previous hook");
    GameState probe;
    SportschauIntroView probe_view;
    throwing.session->sportschau_presentation_hooks().intro(probe, probe_view);
    check(prior_hook_calls == 1, "the restored prior intro callback remains installed after terminal failure");
    const auto terminal_native = save_stream(throwing.session->state());
    const auto terminal_game = throwing.session->game();
    const auto terminal_rand = throwing.session->rand();
    const uint64_t terminal_revision = throwing.revision;
    std::string error;
    const auto retry = throwing.session->dispatch_kickoff(failed_request.desk_action,
                                                          throwing.revision, error);
    check(retry.kind == TurnDeskKind::Failed && !error.empty() && throwing_calls == 1 &&
          save_stream(throwing.session->state()) == terminal_native &&
          throwing.session->game().seed() == terminal_game.seed() &&
          throwing.session->game().calls() == terminal_game.calls() &&
          throwing.session->rand().seed() == terminal_rand.seed() &&
          throwing.session->rand().calls() == terminal_rand.calls() && throwing.revision == terminal_revision,
          "failed presenter claims a terminal turn cursor; direct stale replay cannot repeat simulation or callbacks");
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2 && argc != 3)
            throw std::runtime_error("expected BMH_DATA directory and optional controlled WEEK135 fixture");
        const std::filesystem::path data = argv[1];
        test_fixture_projection_preserves_source_log_and_live_stream();
        test_source_selection_priority_and_reset();
        test_source_report_steps();
        test_cup_setting_row_comes_from_live_descriptor();
        test_missing_domestic_abbreviation_does_not_use_foreign_fallback();
        test_stale_and_nul_crossing_domestic_abbreviations(data);
        test_foreign_club_name_uses_source_longest_word_path();
        test_process_group_callback_is_after_table_before_morale_and_rng();
        test_finish_event_intro_order_and_tactics_reset();
        test_source_option_memory_runtime_exe_precedence(data);
        test_real_career_observer_gate_and_rng_neutrality(data);
        test_real_controller_gates_and_throw_terminal(data);
        if (argc == 3) {
            test_source_selection_foreign_draws(data, argv[2]);
            test_controlled_week135_actual_renderers_and_reload(data, argv[2]);
        }
        std::puts("sportschau_app_tests: Source projection, callback ordering, Career gates, and Native/RNG neutrality passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "sportschau_app_tests: %s\n", error.what());
        return 1;
    }
}
