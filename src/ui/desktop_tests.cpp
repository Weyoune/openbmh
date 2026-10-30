#include "../../tests/runtime_fixture_tables.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "../../tests/test_inputs.hpp"
#include "ui/flow.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <fstream>
#include <iostream>
#include <numeric>
#include <string_view>

#include "formats/fbm.hpp"
#include "formats/savestate.hpp"
#include "formats/vga.hpp"
#include "game/messages.hpp"
#include "game/matchday_setup.hpp"
#include "game/save.hpp"
#include "game/state.hpp"
#include "game/turn_driver.hpp"
#include "ui/calendar_state.hpp"
#include "ui/strengths_state.hpp"
#include "ui/desk_records.hpp"
#include "ui/desk_drawers.hpp"

using namespace bmh;
using namespace bmh::ui;

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void rejected(const std::function<void()>& action) {
    static int index = 0; ++index;
    try { action(); } catch (const FormatError&) { return; }
    throw std::runtime_error("expected rejected action #" + std::to_string(index));
}
void write16(Bytes& b, size_t at, uint16_t value) { b.at(at) = uint8_t(value); b.at(at + 1) = uint8_t(value >> 8); }
void write32(Bytes& b, size_t at, uint32_t value) { write16(b, at, uint16_t(value)); write16(b, at + 2, uint16_t(value >> 16)); }
int calendar_rng_draws = 0;
void count_calendar_rng_draw(int) { ++calendar_rng_draws; }
void paired_closing_selection() {
    DeskState state;state.runtime_club=29;state.next_opponent=-1;
    state.items={{40,69,22,42,42,66,0,0,true,false},
                 {41,213,22,42,42,67,0,0,true,false}};
    check(source_drawer_team(state,16)==29 && !source_drawer_team(state,17),
          "Source closing drawers keep own badge first and no-opponent computer second");
}
void paired_selection_matrix(const LeImage& exe) {
    const std::array<int,8> table{3,4,11,12,16,17,20,21};
    for(size_t i=0;i<table.size();++i)
        check(exe.u8(::bmh::layout::FixtureDesktopPairCases+uint32_t(i))==table[i],"pair cases match the actual Original EXE table");
    for(bool gate:{false,true})for(int rival:{-1,13})for(int shape=0;shape<6;++shape) {
        DeskState state;state.runtime_club=12;state.next_opponent=rival;
        state.paired_drawers_away_order=gate;
        for(size_t pair=0;pair<4;++pair) {
            state.items.clear();
            // Four open/closed combinations, a visible disabled pair, and a
            // duplicate third record. Source uses exactly count2, not >=2.
            const int mask=shape<4?shape:3;
            state.items.push_back({1,0,0,42,42,table[pair*2]+50,0,0,bool(mask&1),shape==4});
            state.items.push_back({2,50,0,42,42,table[pair*2+1]+50,0,0,bool(mask&2),shape==4});
            if(shape==5)state.items.push_back(state.items[0]);
            int source_count=0;
            if(gate && rival!=-1)for(const auto& item:state.items)if(item.open) {
                for(size_t i=0;i<table.size();++i)
                    if(item.kind-50==table[i] && i/2==pair)++source_count;
            }
            const int source21=source_count==2?12:rival;
            const int source_first=source21!=12?12:rival;
            check(source_drawer_team(state,table[pair*2])==
                  (source_first<0?std::nullopt:std::optional<int>{source_first}) &&
                  source_drawer_team(state,table[pair*2+1])==
                  (source21<0?std::nullopt:std::optional<int>{source21}),
                  "all four pairs follow Source count/gate/opponent/open/disabled rules");
        }
    }
    DeskState unknown;unknown.runtime_club=12;unknown.paired_drawers_away_order=true;
    unknown.items={{1,0,0,42,42,66,0,0,true,false},{2,50,0,42,42,67,0,0,true,false}};
    check(source_drawer_team(unknown,16)==12 && !source_drawer_team(unknown,17),
          "unknown opponent does not become a guessed away-side swap");
}
void paired_saved_projection(const std::filesystem::path& path,const LeImage& exe) {
    const auto fbm=load_savegame(path);check(fbm.checksum_ok,"real closing fixture checksum");
    const auto original=GameState::from_save(SaveState(fbm.stream));
    check(original.date==1461 && original.manager_teams==std::vector<int>{29},"actual SIM/Bayern closing context");
    const std::vector<MatchGroup> groups;
    for(int option:{0,1})for(int away:{0,1}) {
        auto g=original;g.memory.set8(::bmh::layout::DeskState_FormatError_RuntimeField2,uint8_t(option));g.manager_records.at(0x28a)=uint8_t(away);
        const auto native=save_stream(g);const bool expected=option==0 && away!=0;
        const auto saved=desk_from_save(SaveState(native),exe,1);
        const DeskMatchCallargs closing{32,-1};
        const auto live=desk_from_game(g,exe,1,&groups,32,&closing);
        check(saved.paired_drawers_away_order==expected && live.paired_drawers_away_order==expected &&
              !live.next_match_away,"saved/raw pair gate is independent of absent closing match labels");
        check(source_drawer_team(live,16)==29 && !source_drawer_team(live,17),
              "real closing badge remains own first for every option/raw-away combination");
        const DeskMatchCallargs match{34,13};
        const auto matched=desk_from_game(g,exe,1,&groups,34,&match);
        check(matched.paired_drawers_away_order==expected && matched.next_match_away==bool(away),
              "pair setting does not change actual match-label home/away semantics");
        check(save_stream(g)==native,"all Source gate projections preserve the complete Native state");
    }
    auto g=original;g.memory.set8(::bmh::layout::DeskState_FormatError_RuntimeField2,0);g.manager_records.at(0x28a)=1;
    const DeskMatchCallargs match{34,13};
    const auto both=desk_from_game(g,exe,1,&groups,34,&match);
    check(source_drawer_team(both,16)==13 && source_drawer_team(both,17)==29,
          "genuine away pair with two visible open badges reverses exactly once");
    const auto first=std::find_if(both.items.begin(),both.items.end(),[](const auto& i){return i.kind==66;});
    check(first!=both.items.end() && first->open,"real first badge exists and is open");
    const uint32_t at=::bmh::layout::Desktops+uint32_t(first->id)*16;
    auto hidden=g;hidden.memory.set16(at,uint16_t(hidden.memory.u16(at)|0x1000));
    const auto hidden_desk=desk_from_game(hidden,exe,1,&groups,34,&match);
    check(std::none_of(hidden_desk.items.begin(),hidden_desk.items.end(),[](const auto& i){return i.kind==66;}) &&
          source_drawer_team(hidden_desk,17)==13,"native hidden badge is omitted before Source pair counting");
    auto collapsed=g;collapsed.memory.set8(at+13,uint8_t(collapsed.memory.u8(at+13)&0x7f));
    const auto collapsed_desk=desk_from_game(collapsed,exe,1,&groups,34,&match);
    check(source_drawer_team(collapsed_desk,17)==13,"native collapsed first badge leaves count1, not count2");
}

GameState fixture() {
    GameState g;
    for (auto [address, size] : std::vector<std::pair<uint32_t, size_t>>{{::bmh::layout::CareerDuration,1},{::bmh::layout::GameMode,1},{::bmh::layout::ManagerCount,1},
         {::bmh::layout::CompetitionSelection,4},{::bmh::layout::ActiveManager,1},{::bmh::layout::CareerSession_Save_RuntimeField,4},{::bmh::layout::LeaguePresentationOptions,0xb4},{::bmh::layout::FundBlock,0xa8},{::bmh::layout::Loans,0x1e0},{::bmh::layout::Desktops,0x10e0},
         {::bmh::layout::Difficulty,1},{::bmh::layout::CalendarDay,4},{::bmh::layout::CoachActive,1},{::bmh::layout::CareerSession_AnyOf_ExecutableField,10},{::bmh::layout::ClubShortNameOffsets,512},
         {::bmh::layout::Coupon,0x14},{::bmh::layout::CareerSession_DispatchToto_RuntimeField,1},{::bmh::layout::DeskRecords_Teams_RuntimeField,138*2}}) g.memory.add_block(address, size);
    g.memory.set8(::bmh::layout::CareerDuration,5); g.memory.set8(::bmh::layout::GameMode,3); g.memory.set8(::bmh::layout::ManagerCount,1); g.memory.set32(::bmh::layout::CompetitionSelection,1);
    g.level = 2; g.memory.set8(::bmh::layout::Difficulty,9); // typed level intentionally differs from stale memory copy
    g.memory.set32(::bmh::layout::CareerSession_Save_RuntimeField,0x12345678);
    g.manager_teams = {12}; g.manager_records.resize(5 * 0xb0c);
    const std::string name = "Nobody"; std::copy(name.begin(), name.end(), g.manager_records.begin() + 0xa8);
    write16(g.manager_records,0x290,2); // authoritative runtime club comes from manager_teams
    g.club_ids.resize(419); std::iota(g.club_ids.begin(),g.club_ids.end(),0);
    g.club_names.resize(419,0); std::copy_n("UNITFC",6,g.club_names.begin()+12); g.club_names[18]=0;
    g.club_heap_78.resize(419 * 2);
    g.stocks.resize(0x450); g.manager_options.resize(20); g.league_rounds.resize(9);
    g.squad_slots.resize(3096,0x0fff); g.players.resize(24); g.team_morale.resize(419,25);
    g.player_cards.resize(24); g.player_bonus.resize(24); g.player_name_offsets.resize(24,0);
    g.player_names={'P','L','A','Y','E','R',0};
    g.tactics.resize(0x2940); g.formations.resize(0x160);
    for (int row = 0; row < 24; ++row) {
        g.squad_slots.at(12 * 24 + row) = uint16_t(row | (row < 11 ? 0x8000 : row < 16 ? 0x4000 : 0));
        g.players[row].w35c = 50 | (90 << 7) | (90 << 14);
        g.tactics.at(size_t(row) * 0x58 + 0x0c) = 4;
    }
    // Layout: calendar, lineup, training, tactics, kickoff; no screen coordinates in controller code.
    const std::array<int,5> kinds = {58,3,16,18,12};
    g.memory.set8(::bmh::layout::Desktops + 5,1);
    for (int i = 0; i < 5; ++i) {
        const auto at = ::bmh::layout::DesktopsUnsigned + uint32_t(i + 1) * 16;
        g.memory.set16(at,uint16_t(i == 0 ? 4 : i * 40)); g.memory.set8(at + 2,uint8_t(i == 0 ? 22 : 120));
        g.memory.set8(at + 3,uint8_t(i == 0 ? 61 : 32)); g.memory.set8(at + 4,uint8_t(i == 0 ? 62 : 25));
        g.memory.set8(at + 5,uint8_t(i == 4 ? 0 : i + 2)); g.memory.set8(at + 12,uint8_t(kinds[i]));
        g.memory.set8(at + 13,uint8_t(i == 0 ? 0x80 : 0));
    }
    return g;
}

GameState calendar_fixture(const LeImage& exe, uint32_t season_start, uint32_t day_offset) {
    auto game = fixture();
    game.season_start = season_start;
    game.date = season_start + day_offset;
    game.season = 0;
    game.memory.add_block(::bmh::layout::CalendarState_FormatError_RuntimeField3, 2);
    game.memory.set16(::bmh::layout::CalendarState_FormatError_RuntimeField3, uint16_t(season_start));
    game.memory.add_block(::bmh::layout::DaysIntoSeason, 2);
    game.memory.set16(::bmh::layout::DaysIntoSeason, uint16_t(day_offset));
    game.memory.add_block(::bmh::layout::CompetitionCalendar, 366 * 2);
    game.memory.add_block(::bmh::layout::CalendarState_FormatError_RuntimeField2, 1);
    game.memory.add_block(::bmh::layout::CalendarState_FormatError_RuntimeField, 1);
    game.memory.set8(::bmh::layout::CalendarState_FormatError_RuntimeField2, 0);
    game.memory.set8(::bmh::layout::CalendarState_FormatError_RuntimeField, 0);
    for (int league = 0; league < 9; ++league)
        check(exe.u8(::bmh::layout::LeagueCalendarBits + uint32_t(league)) <= 31,"league calendar class fits Source bitmap");
    return game;
}

void calendar_projection_tests(const LeImage& exe) {
    auto regular = calendar_fixture(exe, 0, 364);
    regular.memory.set16(::bmh::layout::CompetitionCalendar, uint16_t(1u << exe.u8(::bmh::layout::LeagueCalendarBits)));
    regular.memory.set16(::bmh::layout::CompetitionCalendar + 4, uint16_t(1u << exe.u8(::bmh::layout::LeagueCalendarBits)));
    regular.memory.set16(::bmh::layout::CompetitionCalendar + 6, uint16_t(1u << exe.u8(::bmh::layout::FixtureCalendarSecondLeagueBit)));
    const Bytes before_native = save_stream(regular);
    GameRandom game_rng(1234);
    WatcomRandom watcom_rng(5678);
    const auto game_seed = game_rng.seed();
    const auto game_calls = game_rng.calls();
    const auto watcom_seed = watcom_rng.seed();
    const auto watcom_calls = watcom_rng.calls();
    calendar_rng_draws = 0;
    auto previous_hook = draw_hook;
    draw_hook = count_calendar_rng_draw;
    const auto state = calendar_from_game(regular,exe);
    draw_hook = previous_hook;
    check(state.days.size() == 365 && state.current_day == 364 && state.days.back().current_day,
          "regular season ends at June 30 with its final day included");
    check(state.days[0].date.month == 7 && state.days[0].date.day == 1 &&
          state.days[364].date.month == 6 && state.days[364].date.day == 30,
          "calendar rows use the Source July-to-June date conversion");
    check(state.days[0].league_matchdays[0] == 1 && state.days[1].league_matchdays[0] == 0 &&
          state.days[2].league_matchdays[0] == 2,
          "league matchday ordinals count effective Source class bits inclusively");
    check(state.days[0].raw_entry == uint16_t(1u << exe.u8(::bmh::layout::LeagueCalendarBits)) &&
          state.days[0].competitions == (1u << exe.u8(::bmh::layout::LeagueCalendarBits)),
          "projection preserves the saved calendar word and its effective competition bitmap");
    check(calendar_rng_draws == 0 && game_rng.seed() == game_seed && game_rng.calls() == game_calls &&
          watcom_rng.seed() == watcom_seed && watcom_rng.calls() == watcom_calls &&
          save_stream(regular) == before_native,
          "calendar projection leaves the complete serialized native state and RNG untouched");

    // legacy routine starts native calendar word zero on the first Wednesday, while
    // legacy routine draws from July 1. The two leading display cells have no word.
    auto first_wednesday = calendar_fixture(exe,1463,262);
    first_wednesday.memory.set16(::bmh::layout::CompetitionCalendar + 262 * 2, uint16_t(1u << exe.u8(::bmh::layout::LeagueCalendarBits)));
    const auto wednesday_state = calendar_from_game(first_wednesday,exe);
    check(wednesday_state.leading_days == 2 && wednesday_state.current_day == 264 &&
          wednesday_state.season_start_counter == 1461,
          "cold-loaded first-Wednesday season keeps display and native cursors distinct");
    check(wednesday_state.days[0].date.month == 7 && wednesday_state.days[0].date.day == 1 &&
          wednesday_state.days[0].season_day == -2 && wednesday_state.days[0].raw_entry == 0 &&
          wednesday_state.days[2].date.day == 3 && wednesday_state.days[2].season_day == 0 &&
          wednesday_state.days[264].date.month == 3 && wednesday_state.days[264].date.day == 22 &&
          wednesday_state.days[264].season_day == 262 && wednesday_state.days[264].current_day &&
          wednesday_state.days[264].raw_entry == uint16_t(1u << exe.u8(::bmh::layout::LeagueCalendarBits)),
          "native day 262 maps to displayed March 22 after two empty July cells");

    auto marked = calendar_fixture(exe,0,0);
    marked.memory.set16(::bmh::layout::CompetitionCalendar,uint16_t(0x8001));
    const auto marked_state = calendar_from_game(marked,exe);
    check(marked_state.days[0].raw_entry == 0x8001 && marked_state.days[0].competitions == 1,
          "saved high marker remains in raw word while the effective bitmap follows Source masking");

    auto leap = calendar_fixture(exe,365,365);
    leap.date = leap.season_start + 365;
    const auto leap_state = calendar_from_game(leap,exe);
    check(leap_state.days.size() == 366 && leap_state.current_day == 365 &&
          leap_state.days.back().season_day == 365 && leap_state.days.back().date.month == 6 &&
          leap_state.days.back().date.day == 30 && leap_state.days.back().current_day,
          "leap season includes valid June 30 at season index 365");

    for (int season = 0; season < 4; ++season) {
        auto rotated = calendar_fixture(exe,0,0);
        rotated.season = season;
        rotated.memory.set16(::bmh::layout::CompetitionCalendar,0x1e00);
        const auto rotated_state = calendar_from_game(rotated,exe);
        const uint32_t expected = uint32_t(1) << (12 - season);
        check(rotated_state.days[0].competitions == expected,
              "rotating calendar competition follows the four-season Source cycle");
    }
    auto one_rotation_bit = calendar_fixture(exe,0,0);
    one_rotation_bit.memory.set16(::bmh::layout::CompetitionCalendar,0x0200);
    auto one_rotation_state = calendar_from_game(one_rotation_bit,exe);
    check(one_rotation_state.days[0].competitions == 0,
          "a single rotation bit is absent when the current season selects another class");
    one_rotation_bit.season = 3;
    one_rotation_state = calendar_from_game(one_rotation_bit,exe);
    check(one_rotation_state.days[0].competitions == 0x0200,
          "the same rotation bit appears in its selected Source season");

    auto all_leagues = calendar_fixture(exe,0,0);
    all_leagues.memory.set8(::bmh::layout::CompetitionSelection,3);
    for (int league = 0; league < 9; ++league) {
        const uint32_t mask = uint32_t(1) << exe.u8(::bmh::layout::LeagueCalendarBits + uint32_t(league));
        uint16_t raw = 0;
        bool found = false;
        for (uint32_t candidate = 0; candidate <= 0xffff; ++candidate) {
            const auto word = uint16_t(candidate);
            if (day_competitions(all_leagues,word) == mask) { raw = word; found = true; break; }
        }
        check(found,"each executable league class has a representable calendar entry");
        all_leagues.memory.set16(::bmh::layout::CompetitionCalendar + uint32_t(league) * 2,raw);
    }
    const auto all_league_state = calendar_from_game(all_leagues,exe);
    for (int league = 0; league < 9; ++league) {
        const uint32_t mask = uint32_t(1) << exe.u8(::bmh::layout::LeagueCalendarBits + uint32_t(league));
        int inclusive_ordinal = 0;
        for (int day = 0; day < 9; ++day) {
            const uint16_t raw = all_leagues.memory.u16(::bmh::layout::CompetitionCalendar + uint32_t(day) * 2);
            const bool plays = (day_competitions(all_leagues,raw) & mask) != 0;
            if (plays) ++inclusive_ordinal;
            check(all_league_state.days[size_t(day)].league_matchdays[size_t(league)] ==
                      (plays ? inclusive_ordinal : 0),
                  "each league ordinal is the inclusive count of its class through a playing day");
        }
    }

    auto specials = calendar_fixture(exe,0,0);
    specials.memory.set16(::bmh::layout::CompetitionCalendar,0x4001);
    specials.memory.set16(::bmh::layout::CompetitionCalendar+2,0x4002);
    specials.memory.set16(::bmh::layout::CompetitionCalendar+4,0x4004);
    specials.memory.set16(::bmh::layout::CompetitionCalendar+6,0x4006);
    auto special_state = calendar_from_game(specials,exe);
    check(special_state.days[0].competitions == 0x8000 &&
          special_state.days[1].competitions == 0x10000 &&
          special_state.days[2].competitions == 0x40000 &&
          special_state.days[3].competitions == 0,
          "special calendar words preserve Source masks and disable tournament without its flag");
    specials.memory.set8(::bmh::layout::CompetitionSelection,3);
    special_state = calendar_from_game(specials,exe);
    check(special_state.days[3].competitions == 0x800000,
          "0x4006 tournament entry is effective only when the saved tournament flag is set");
    specials.memory.set8(::bmh::layout::CalendarState_FormatError_RuntimeField,1);
    special_state = calendar_from_game(specials,exe);
    check(special_state.days[1].competitions == 0,
          "saved special-event disable flag is applied by the Source competition projection");
    auto filtered = calendar_fixture(exe,0,0);
    filtered.memory.set16(::bmh::layout::CompetitionCalendar,0x8010);
    auto filtered_state = calendar_from_game(filtered,exe);
    check(filtered_state.days[0].raw_entry == 0x8010 && filtered_state.days[0].competitions == 0x10,
          "calendar raw high marker is retained while effective flags omit that marker");
    filtered.memory.set8(::bmh::layout::CalendarState_FormatError_RuntimeField2,1);
    filtered_state = calendar_from_game(filtered,exe);
    check(filtered_state.days[0].raw_entry == 0x8010 && filtered_state.days[0].competitions == 0,
          "saved calendar mask 0xa2594 clears effective bit 0x10");

    auto malformed = calendar_fixture(exe,0,0);
    malformed.memory.set16(::bmh::layout::CalendarState_FormatError_RuntimeField3,1);
    rejected([&]{ calendar_from_game(malformed,exe); });
    auto outside = calendar_fixture(exe,0,0);
    outside.date = 365;
    rejected([&]{ calendar_from_game(outside,exe); });
    auto missing = fixture();
    rejected([&]{ calendar_from_game(missing,exe); });
    auto present_table = calendar_fixture(exe,0,0);
    LeImage missing_executable;
    rejected([&]{ calendar_from_game(present_table,missing_executable); });
}

void source_sort_reference(std::vector<std::pair<uint32_t, int>>& rows,
                           size_t begin, size_t count, bool ascending) {
    if (count <= 1) return;
    std::ptrdiff_t left = -1, right = std::ptrdiff_t(count);
    const uint32_t pivot = rows[begin + count / 2].first;
    for (;;) {
        if (!ascending) {
            do { ++left; } while (pivot < rows[begin + size_t(left)].first);
            do { --right; } while (rows[begin + size_t(right)].first < pivot);
        } else {
            do { ++left; } while (rows[begin + size_t(left)].first < pivot);
            do { --right; } while (pivot < rows[begin + size_t(right)].first);
        }
        if (right <= left) break;
        std::swap(rows[begin + size_t(left)], rows[begin + size_t(right)]);
    }
    source_sort_reference(rows, begin, size_t(left), ascending);
    source_sort_reference(rows, begin + size_t(left), count - size_t(left), ascending);
}

void strengths_projection_tests() {
    const auto none = strengths_draw_words_source(0x0000);
    check(none.raw_settings_word == 0 && none.row_settings_word == 0x00ff &&
          none.effective_average_flags == 0x00ff && none.post_draw_settings_word == 0x01ff,
          "Strengths defaults row criteria and normalizes the persisted word after the draw");
    const auto full = strengths_draw_words_source(0x00ff);
    check(full.row_settings_word == 0x00ff && full.post_draw_settings_word == 0x01ff,
          "all eight filters set saved word bit 8 without changing row calculation flags");
    const auto header = strengths_draw_words_source(0x8000);
    check(header.effective_average_flags == 0x01ff && header.post_draw_settings_word == 0x81ff,
          "post-draw high-byte mask preserves Source header bit 15 before the zero-word default");
    const auto age_draw = strengths_draw_words_source(0x0200);
    check(age_draw.row_settings_word == 0x00ff && age_draw.post_draw_settings_word == 0x0200,
          "age-sort word is retained by the Source 0xf2 high-byte mask");

    check(strengths_control_word_source(0x00ff, StrengthsControl::condition) == 0x00fe,
          "filter control toggles its exact bit from normalized settings");
    check(strengths_control_word_source(0x01ff, StrengthsControl::condition) == 0x00fe,
          "a changed filter clears Source's line-up bit unless the full low nine bits remain set");
    check(!strengths_control_word_source(0x02ff, StrengthsControl::condition),
          "Source locks the eight filters while age sorting is active");
    check(strengths_control_word_source(0x02ff, StrengthsControl::age) == 0x0000,
          "age control applies Source high-byte mask and toggle");
    check(strengths_control_word_source(0x8000, StrengthsControl::lineup) == 0x0000,
          "line-up header control toggles word bit 15");
    check(strengths_control_word_source(0x2200, StrengthsControl::reset) == 0x01ff,
          "zero-value control writes the full Source 0x1ff word");
    check(strengths_family_word_source(0xffff, 0) == 0xfd7f &&
          strengths_family_word_source(0xffff, 1) == 0xfdff,
          "family entry preserves Source 0xfc0f bits and sets values from points mode");

    auto game = fixture();
    game.memory.add_block(::bmh::layout::ManagerSettings, 10);
    game.memory.add_block(::bmh::layout::DeskPosition, 1);
    game.manager_records[0xa8] = 'A';
    game.manager_teams[0] = 12;
    game.memory.set16(::bmh::layout::ManagerSettings, 0x8000);
    game.memory.set8(::bmh::layout::ActiveManager, 0);
    game.memory.set8(::bmh::layout::DeskPosition, 0);
    for (size_t i = 0; i < game.players.size(); ++i)
        game.players[i].w364 = (uint32_t(i % 24) << 21) | (i == 0 ? 0x00010000u : 0);
    constexpr std::array<std::string_view, 4> foreign_titles{"ENGLAND", "ITALY", "SPAIN", "FRANCE"};
    for (size_t i = 0; i < foreign_titles.size(); ++i) {
        const size_t title_team = size_t(0x166) + i;
        const size_t title_offset = 300 + i * 16;
        game.club_ids[title_team] = uint16_t(title_offset);
        std::copy(foreign_titles[i].begin(), foreign_titles[i].end(),
                  game.club_names.begin() + std::ptrdiff_t(title_offset));
        game.club_names[title_offset + foreign_titles[i].size()] = 0;
    }
    static TextDb texts;
    if (texts.strings.empty()) texts.strings.resize(TEXT_COUNT);
    texts.strings.at((::bmh::layout::Achievements_FormatError_TextPointer3 - ::bmh::layout::TextPointerTable) / 4) = "League";
    texts.strings.at((::bmh::layout::Manager_FormatError_TextPointer2 - ::bmh::layout::TextPointerTable) / 4) = " ";
    game.texts = &texts;

    const Bytes before = save_stream(game);
    calendar_rng_draws = 0;
    auto previous_hook = draw_hook;
    draw_hook = count_calendar_rng_draw;
    const auto projection = strengths_from_game(game, 0, 0);
    draw_hook = previous_hook;
    check(projection.rows.size() == 18 && projection.first_team == 0 &&
          projection.manager_slot == 0 && projection.age_scratch.basis ==
              StrengthsAgeBasis::entry_or_league_change_bit10 &&
          !projection.age_scratch.lineup_only,
          "league zero projects its exact eighteen teams and uses raw bit 10 on entry");
    check(projection.rows.front().runtime_team == 12 &&
          projection.rows.front().team_name == "UNITFC" &&
          projection.rows.front().name_color == 0x7a,
          "league projection carries save-remapped name and Source active-manager color");
    check(projection.rows.front().criterion_values == std::array<uint16_t, 4>{90,90,50,50} &&
          projection.rows.front().aggregate_value == 90,
          "strength columns and aggregate honor the Source eleven-player lineup divisor");
    check(projection.age_scratch.tenths.at(12) == 115,
          "Source age scratch averages all squad slots in byte tenths with integer truncation");
    check(save_stream(game) == before,
          "Strengths projection leaves the complete serialized game state unchanged");
    check(calendar_rng_draws == 0,
          "domestic Strengths projection does not call the random draw hook");
    auto high_identity = game;
    high_identity.manager_records[0xa8] = 0;
    high_identity.manager_teams[0] = 0x0100;
    const auto high_identity_projection = strengths_from_game(high_identity, 0, 0);
    const auto low_byte_alias = std::find_if(high_identity_projection.rows.begin(),
        high_identity_projection.rows.end(), [](const StrengthsRow& row){ return row.runtime_team == 0; });
    check(low_byte_alias != high_identity_projection.rows.end() && low_byte_alias->name_color == 0x5f,
          "Source current-club name highlight compares the complete 16-bit team identity");
    auto fallback_color = game;
    fallback_color.manager_records[0xa8] = 0;
    const auto fallback_projection = strengths_from_game(fallback_color, 0, 0);
    const auto active_team_row = std::find_if(fallback_projection.rows.begin(),
        fallback_projection.rows.end(), [](const StrengthsRow& row){ return row.runtime_team == 12; });
    check(active_team_row != fallback_projection.rows.end() && active_team_row->name_color == 0xf7,
          "Source favorite-club fallback uses the active manager's complete runtime team ID");

    const auto header_age = strengths_from_game(game, 0, 0,
        StrengthsProjectionContext{std::nullopt, StrengthsAgeBasis::header_toggle_bit15});
    check(header_age.age_scratch.lineup_only && header_age.age_scratch.tenths.at(12) == 50,
          "header-toggle age scratch uses bit 15 and only the eleven starting players");
    StrengthsProjectionContext cached_context{projection.age_scratch,
        StrengthsAgeBasis::entry_or_league_change_bit10};
    const auto cached = strengths_from_game(game, 0, 0, cached_context);
    check(cached.age_scratch.basis == StrengthsAgeBasis::entry_or_league_change_bit10 &&
          cached.age_scratch.tenths == projection.age_scratch.tenths,
          "same-league redraw retains the original age-source basis and cached bytes");
    auto stale_context = cached_context;
    stale_context.cached_age->tenths[12]++;
    rejected([&]{ strengths_from_game(game, 0, 0, stale_context); });

    auto hole = game;
    const size_t squad = squad_range(12).first;
    hole.squad_slots[squad + 2] = kEmptySlot;
    const auto hole_projection = strengths_from_game(hole, 0, 0);
    check(hole_projection.age_scratch.tenths.at(12) == 5,
          "age scratch stops at the first empty slot rather than counting later squad entries");
    auto byte_wrap = game;
    for (size_t i = 0; i < byte_wrap.players.size(); ++i)
        byte_wrap.players[i].w364 = 31u << 21;
    const auto wrapped_age = strengths_from_game(byte_wrap, 0, 0);
    check(wrapped_age.age_scratch.tenths.at(12) == uint8_t(310),
          "age scratch preserves Source byte narrowing after the tenths division");
    auto empty = game;
    for (size_t i = 0; i < squad_range(12).count; ++i) empty.squad_slots[squad + i] = kEmptySlot;
    const auto empty_projection = strengths_from_game(empty, 0, 0);
    check(empty_projection.age_scratch.tenths.at(12) == 0x22,
          "clubs with no qualifying players preserve Source age sentinel 0x22");

    for (int league = 0; league < 9; ++league) {
        const int expected_first = league < 5 ? league * 18 : 90 + (league - 5) * 12;
        const int expected_count = league < 5 ? 18 : 12;
        const auto rows = strengths_from_game(game, 0, league);
        check(rows.first_team == expected_first && rows.rows.size() == size_t(expected_count),
              "each of the nine Source domestic league bounds is exact");
        if (league >= 5)
            check(rows.league_name == iso646de_to_utf8(
                      game.club_name(0x165 + league - 4).c_str()),
                  "foreign league title comes from its saved country-club name, not domestic TEXTD");
    }
    auto foreign_without_textd = game;
    foreign_without_textd.texts = nullptr;
    check(strengths_from_game(foreign_without_textd, 0, 5).league_name == "ENGLAND",
          "foreign league projection does not index the domestic league-name table");
    rejected([&]{ strengths_from_game(game, 0, -1); });
    rejected([&]{ strengths_from_game(game, 0, 9); });
    rejected([&]{ strengths_from_game(game, 5, 0); });
    rejected([&]{ strengths_from_game(GameState{}, 0, 0); });
    auto five_managers = game;
    five_managers.memory.set8(::bmh::layout::ManagerCount, 5);
    rejected([&]{ strengths_from_game(five_managers, 0, 0); });
    auto invalid_basis = StrengthsProjectionContext{
        std::nullopt, static_cast<StrengthsAgeBasis>(0xff)};
    rejected([&]{ strengths_from_game(game, 0, 0, invalid_basis); });
    auto no_morale = game;
    no_morale.team_morale.clear();
    rejected([&]{ strengths_from_game(no_morale, 0, 0); });

    auto age_sorted_game = game;
    age_sorted_game.memory.set16(::bmh::layout::ManagerSettings, 0x8200);
    const auto age_sorted = strengths_from_game(age_sorted_game, 0, 0);
    std::vector<std::pair<uint32_t, int>> two_equal{{5, 10}, {5, 11}};
    source_sort_reference(two_equal, 0, two_equal.size(), true);
    check(two_equal[0].second == 11 && two_equal[1].second == 10,
          "literal Source quicksort swaps a two-element equal-key pair");
    std::vector<std::pair<uint32_t, int>> expected;
    for (int team = 0; team < 18; ++team) {
        const auto row = std::find_if(age_sorted.rows.begin(), age_sorted.rows.end(),
            [team](const StrengthsRow& r){ return r.runtime_team == team; });
        check(row != age_sorted.rows.end(), "age-sort rows retain every source team");
        expected.emplace_back(row->source_sort_key, team);
    }
    source_sort_reference(expected, 0, expected.size(), true);
    for (size_t i = 0; i < expected.size(); ++i)
        check(age_sorted.rows[i].runtime_team == expected[i].second,
              "age ordering matches the independent literal Source quicksort including ties");

    auto all_equal_game = game;
    all_equal_game.memory.set16(::bmh::layout::ManagerSettings, 0x8000);
    for (int team = 0; team < 18; ++team) {
        const SquadRange range = squad_range(team);
        for (size_t slot = 0; slot < range.count; ++slot)
            all_equal_game.squad_slots[range.first + slot] = kEmptySlot;
    }
    const auto all_equal = strengths_from_game(all_equal_game, 0, 0);
    constexpr std::array<int, 18> source_equal_order{
        11,12,9,10,16,17,13,15,14,2,3,0,1,7,8,4,6,5
    };
    for (size_t index = 0; index < all_equal.rows.size(); ++index)
        check(all_equal.rows[index].runtime_team == source_equal_order[index],
              "Source quicksort follows its exact recursive 18-row equal-key order");
}

void begin(StartupFlow& flow) {
    flow.click(150,215); flow.click(5,225,true); flow.click(5,225,true);
    check(flow.click(90,169).request == FlowRequest::new_career,"one career request");
}
void confirm_managers(StartupFlow& flow, int count, bool national = false) {
    if (national) flow.click(95,215); // league off; world remains enabled, one manager only
    else flow.click(150,215); // world off
    for (int i = 1; i < count; ++i) flow.click(260,135);
    flow.click(5,225,true);
    for (int i = 0; i < count; ++i) {
        flow.click(5,225,true);
        const auto result = flow.click(90,169);
        if (i + 1 < count) {
            check(result.request == FlowRequest::manager_confirmed,"manager selection awaits the next club order");
            SelectionContext context; context.revision = uint64_t(i + 1); context.source = "test_fixture";
            context.club_order.resize(168); std::iota(context.club_order.begin(),context.club_order.end(),0);
            flow.resume_manager_selection(context);
        } else check(result.request == FlowRequest::new_career,"last manager confirms career request");
    }
}
GameState career_game(const LeImage& exe, const NewCareerRequest& request, bool national = false) {
    auto game = fixture();
    const auto count = request.managers.size();
    game.memory.set8(::bmh::layout::ManagerCount,uint8_t(count));
    game.memory.set32(::bmh::layout::CompetitionSelection,national ? 2 : (request.options.league ? 1 : 0) | (request.options.world ? 2 : 0));
    game.manager_teams.resize(count);
    const auto template_record = Bytes(game.manager_records.begin(),game.manager_records.begin()+0xb0c);
    for(size_t i=0;i<count;++i) {
        const auto base=i*0xb0c;
        std::copy(template_record.begin(),template_record.end(),game.manager_records.begin()+base);
        auto& choice=request.managers[i];
        game.manager_teams[i]=national ? kNoTeam : int(12+i);
        std::fill(game.manager_records.begin()+base+0xa8,game.manager_records.begin()+base+0xa8+26,0);
        std::copy_n(choice.name.begin(),std::min<size_t>(25,choice.name.size()),game.manager_records.begin()+base+0xa8);
        game.manager_records[base+0x210]=uint8_t(choice.start_league);
        game.manager_records[base+0x221]=uint8_t(choice.portrait);
        write16(game.manager_records,base+0x290,uint16_t(game.manager_teams[i]));
    }
    game.memory.set8(::bmh::layout::ActiveManager,0);
    return game;
}
void set_preparation_revision_source(PreparationState& state, uint64_t revision, const std::string& source) {
    state.desktop.revision=revision; state.desktop.source=source;
    state.lineup.revision=state.tactics.revision=revision;
    state.lineup.source=state.tactics.source=source;
    if(state.training){state.training->revision=revision;state.training->source=source;}
    if(state.training_camp){state.training_camp->revision=revision;state.training_camp->source=source;}
    if(state.bank){state.bank->revision=revision;state.bank->source=source;}
    if(state.stadium){state.stadium->revision=revision;state.stadium->source=source;}
    if(state.toto){state.toto->revision=revision;state.toto->source=source;}
    if(state.pitch){state.pitch->revision=revision;state.pitch->source=source;}
    for(auto& card:state.player_cards){card.lineup.revision=revision;card.lineup.source=source;}
}
} // namespace

int main(int argc, char** argv) {
    try {
        paired_closing_selection();
        if (argc < 2) throw std::runtime_error("data directory required");
        const std::filesystem::path data = argv[1]; const auto exe = bmh::test::load_executable(asset_path(data,"BMH.exe"));
        paired_selection_matrix(exe);
        if(argc>3)paired_saved_projection(argv[3],exe);
        // The date arithmetic is checked against a controlled epoch aligned with
        // the original leap cycle, independently of the installation's start year.
        auto oracle_exe = exe;
        const uint8_t oracle_year[] = {0xea,0x07,0,0};
        const uint8_t oracle_weekday = 2;
        oracle_exe.patch(layout::CalendarDateSetup+13,oracle_year,4);
        oracle_exe.patch(layout::CalendarDateSetup+4,&oracle_weekday,1);
        using namespace std::chrono;
        const sys_days epoch = year{2026}/7/1;
        for (uint32_t i = 0; i < 1461; ++i) {
            const auto d = desk_date(i,oracle_exe); const year_month_day oracle(epoch + days{i});
            check(d.year == int(oracle.year()) && d.month == int(unsigned(oracle.month())) &&
                  d.day == int(unsigned(oracle.day())),"calendar/leap boundary mismatch");
        }
        rejected([&]{ desk_date(0xffff,exe); });
        calendar_projection_tests(exe);
        strengths_projection_tests();
        auto g = fixture(); const auto before_records = g.manager_records; const auto before_squads = g.squad_slots;
        auto d = desk_from_game(g,exe,7);
        DesktopScreen timed_desk(data,d);
        const auto deadline=timed_desk.process_timer_expired();
        check(deadline.kind==DeskRequestKind::advance_turn&&deadline.manager_slot==d.manager_slot&&
              deadline.revision==d.revision&&deadline.item_id==-1&&timed_desk.pending()==deadline,
              "Original deadline owns one typed turn request without a guessed icon hit");
        check(timed_desk.process_timer_expired().kind==DeskRequestKind::none,
              "repeated expired polls cannot duplicate the pending calendar turn");
        check(d.options.difficulty == 2 && d.runtime_club == 12 && d.strength == 90,"typed projection / strength");
        check(!d.table_position,"table rank stays absent before any league round is recorded");
        check(d.warnings[2] == true && d.warnings[5] == false && d.warnings[6] == false,"initial warnings");
        check(before_records == g.manager_records && before_squads == g.squad_slots,"projection must be read-only");
        check(desk_menu_name(25)=="fair_play"&&desk_menu_name(26)=="highscore",
              "ordinary menu 25/26 names match their Original desk functions");
        auto fair_fixture=fixture();
        for(int team=0;team<18;++team) fair_fixture.memory.set16(::bmh::layout::DeskRecords_Teams_RuntimeField+uint32_t(team)*2,uint16_t(team));
        fair_fixture.memory.set16(::bmh::layout::DeskRecords_Teams_RuntimeField+uint32_t(4)*2,0); // avoid a tied four-point team in this ordering assertion
        fair_fixture.memory.set16(::bmh::layout::DeskRecords_Teams_RuntimeField,0x0100); // one red card: four points, after scores 1..3
        const auto fair= fair_play_state_from_game(fair_fixture,0,{},0);
        check(fair.fair_play_rows.size()==18&&fair.fair_play_rows.front().team==4&&
              fair.fair_play_rows.front().points==0&&fair.fair_play_rows[4].team==0&&
              fair.fair_play_rows[4].points==4&&fair.fair_play_rows.back().team==17&&
              fair.fair_play_rows.back().points==17,
              "legacy routine ascending fair-play ordering uses four points per red card");
        auto fair_view=fair;fair_view.underlay=Image(320,240);
        DeskRecordsScreen fair_screen(data,std::move(fair_view));
        check(fair_screen.render().pixels.size()==320u*240u&&!fair_screen.closed(),
              "menu-25 Original panel renders the full Source league row set");
        DeskRecordsState high_view;high_view.kind=DeskRecordKind::highscore;high_view.underlay=Image(320,240);
        DeskRecordsScreen high_screen(data,std::move(high_view));
        check(high_screen.render().pixels.size()==320u*240u&&!high_screen.closed(),
              "menu-26 uses the five-row highscore presentation, not the game-end calculator");
        const auto high_backdrop=high_screen.render();
        check(high_screen.key_input("ENTER"),"Source calculator ENTER accepts its value");
        while(high_screen.advance_point()) {}
        check(high_screen.finish_points()&&high_screen.render().pixels!=high_backdrop.pixels,
              "signature-zero acceptance advances every Original points frame and retains the points image");
        check(high_screen.pointer_down(4,4)&&high_screen.closed(),
              "signature-zero points screen waits for its own mouse-down acknowledgement");
        DeskRecordsState ranked_high_view;ranked_high_view.kind=DeskRecordKind::highscore;
        ranked_high_view.highscore.ranked=true;
        ranked_high_view.underlay=Image(320,240);
        DeskRecordsScreen ranked_high_screen(data,std::move(ranked_high_view));
        const auto ranked_high_frame=ranked_high_screen.render();
        const auto source_ranked_art=load_vga(asset_path(data/"KB","51.VGA"));
        check(ranked_high_frame.pixels[size_t(50)*size_t(ranked_high_frame.width)+150]==
              source_ranked_art.pixels[size_t(25)*size_t(source_ranked_art.width)+50],
              "ranked menu-26 copies Source decimal asset 51 (legacy routine argument 0x33)");
        auto menu_game = fixture();
        menu_game.memory.set8(::bmh::layout::Desktops + 44 * 16 + 12, 63); // mailbox occurs before the menu shortcut in this scan
        menu_game.memory.set8(::bmh::layout::Desktops + 53 * 16 + 12, 72); // final compact-bar cell dispatches MEN
        auto menu_desk = desk_from_game(menu_game, exe, 6);
        check(menu_desk.mailbox_shortcut && menu_desk.menu_layout_shortcut &&
              menu_desk.menu_layout_shortcut->id == 53,
              "Source final compact-bar cell resolves record 53/kind 72 independently of linked drawers");
        menu_desk.shortcut_bar_always = false;
        DesktopScreen edge_bar(data, menu_desk);
        const auto edge_hidden = edge_bar.render();
        check(!edge_bar.pointer_move(160, 238), "Source quickbar edge requires exactly y239");
        check(edge_bar.pointer_move(160, 239) && edge_bar.render().pixels != edge_hidden.pixels &&
              !edge_bar.pending() && edge_bar.state().layout_records == menu_desk.layout_records,
              "lower-edge motion shows quickbar without an action or Native layout edits");
        const auto edge_shown = edge_bar.render();
        const auto quickbar_art = load_vga(asset_path(data / "KB", "3.VGA"));
        for (int y = 0; y < 20; ++y) for (int x = 0; x < 20; ++x)
            check(edge_shown.pixels[size_t(207 + y) * 320 + 299 + x] ==
                  quickbar_art.pixels[size_t(75 + y) * quickbar_art.width + exe.u8(::bmh::layout::Desktop_Render_RuntimeField3) + x],
                  "visible Source quickbar copies the complete normal socket sprite before any press");
        check(!edge_bar.pointer_move(250, 239) && edge_bar.click(250, 239).kind == DeskRequestKind::none &&
              edge_bar.render().pixels == edge_shown.pixels,
              "motion along edge and click at the same edge do not toggle the Source y latch twice");
        check(edge_bar.pointer_move(292, 221), "MEN quickbar cell motion selects its Source hover label");
        const auto hover_frame = edge_bar.render();
        int hover_pixels = 0;
        for (int y = 0; y < 240; ++y) for (int x = 0; x < 320; ++x)
            if (hover_frame.pixels[size_t(y) * 320 + x] != edge_shown.pixels[size_t(y) * 320 + x]) {
                check(y >= 230 && y <= 235, "Source quickbar label changes only the footer Font3 rows");
                ++hover_pixels;
            }
        check(hover_pixels > 0 && !edge_bar.pointer_move(291, 221),
              "Source hover title renders once and stays stable inside the same cell");
        check(edge_bar.pointer_move(160, 238) && edge_bar.render().pixels == edge_shown.pixels,
              "leaving the cell restores the footer without hiding the quickbar");
        check(edge_bar.pointer_move(160, 239) && edge_bar.render().pixels == edge_hidden.pixels,
              "re-entering the lower edge toggles the quickbar off without a click");
        auto always_desk = menu_desk; always_desk.shortcut_bar_always = true;
        DesktopScreen always_bar(data, always_desk);
        const auto always_frame = always_bar.render();
        always_bar.pointer_move(160, 239); always_bar.pointer_move(160, 238); always_bar.pointer_move(160, 239);
        check(always_bar.render().pixels == always_frame.pixels && !always_bar.pending(),
              "format field bit1 keeps Always quickbar visible through both bit0 edge transitions");
        DesktopScreen menu_bar(data, menu_desk);
        check(menu_bar.click(292,221).kind == DeskRequestKind::none,
              "MEN shortcut is unavailable before the original compact bar is shown");
        menu_bar.pointer_move(64,239);
        auto menu_save = menu_bar.click(292,221);
        check(menu_save.kind == DeskRequestKind::open_menu && menu_save.menu_kind == 53 &&
              menu_save.item_id == 53 && menu_save.value == 1,
              "compact last-cell left click opens the Original menu-layout save path from record 53");
        menu_bar.resolve_local_menu();
        auto menu_load = menu_bar.click(292,221,true);
        check(menu_load.kind == DeskRequestKind::open_menu && menu_load.menu_kind == 53 &&
              menu_load.item_id == 53 && menu_load.value == 0,
              "compact last-cell right click opens the Original menu-layout load path from record 53");
        auto quickbar_drawer = menu_desk;
        quickbar_drawer.layout_records[50][5]=0xff;
        quickbar_drawer.layout_records[50][6]=0xff;
        quickbar_drawer.layout_records[50][12]=50;
        DesktopScreen quickbar_open(data,quickbar_drawer);
        quickbar_open.pointer_move(64,239);
        const auto append_drawer=quickbar_open.click(22,221);
        check(quickbar_open.take_sound_ids()==std::vector<uint8_t>{15} && quickbar_open.take_sound_ids().empty(),
              "Source drawer append emits ID15 exactly once through the application boundary");
        quickbar_open.render(); quickbar_open.pointer_move(23,221); quickbar_open.click(22,221);
        check(quickbar_open.take_sound_ids().empty(),"render/hover/pending input never replay a drawer sound");
        check(append_drawer.kind==DeskRequestKind::change_layout&&
              append_drawer.layout_operation==DeskLayoutOperation::toggle_quickbar_drawer&&
              append_drawer.layout_after[50*16+5]==0&&append_drawer.layout_after[50*16+13]&0x80,
              "Original quickbar LMB appends a hidden drawer record and sets its open bit");
        auto quickbar_close_state=menu_desk;
        quickbar_close_state.layout_records[0][5]=50;
        quickbar_close_state.layout_records[50][5]=0;
        quickbar_close_state.layout_records[50][6]=0;
        quickbar_close_state.layout_records[50][12]=50;
        DesktopScreen quickbar_close(data,quickbar_close_state);
        quickbar_close.pointer_move(64,239);
        const auto unlink_drawer=quickbar_close.click(22,221,true);
        check(quickbar_close.take_sound_ids()==std::vector<uint8_t>{12},"Source drawer unlink emits ID12");
        check(unlink_drawer.kind==DeskRequestKind::change_layout&&
              unlink_drawer.layout_operation==DeskLayoutOperation::toggle_quickbar_drawer&&
              unlink_drawer.layout_after[0*16+5]==0&&unlink_drawer.layout_after[50*16+5]==0xff&&
              unlink_drawer.layout_after[50*16+6]==0xff,
              "Original quickbar RMB unlinks a visible drawer record with cleared links");
        auto child_desk=menu_desk;
        child_desk.layout_records[3][5]=child_desk.layout_records[3][6]=0xff;
        child_desk.layout_records[3][12]=2;
        child_desk.layout_records[3][13]=0;
        child_desk.layout_records[49][12]=50;
        child_desk.layout_records[49][13]=0x80;
        child_desk.layout_records[0][5]=49;
        child_desk.layout_records[49][5]=child_desk.layout_records[49][6]=0;
        child_desk.folder_icon_slots[0]={3};
        child_desk.drawer_handles=true;
        child_desk.items.push_back(DeskItem{49,40,40,100,65,50,0,0,true,false});
        DesktopScreen folder_click(data,child_desk);
        const auto child_action=folder_click.click(50,49);
        check(child_action.kind==DeskRequestKind::open_menu&&child_action.item_id==3&&child_action.menu_kind==2,
              "Source folder child uses its native record/kind on ordinary LMB activation");
        DesktopScreen folder_drag(data,child_desk);
        folder_drag.pointer_down(50,49,true);
        check(folder_drag.take_sound_ids()==std::vector<uint8_t>({15,12,15}),
              "folder-child RMB start preserves Source restore/unlink/append ordering");
        folder_drag.pointer_move(176,100);
        const auto child_return=folder_drag.pointer_up(176,100,true);
        check(child_return.kind==DeskRequestKind::change_layout&&
              child_return.layout_operation==DeskLayoutOperation::return_folder_child&&
              child_return.layout_before[3*16+5]==0xff&&child_return.layout_after[0*16+5]==49&&
              child_return.layout_after[49*16+5]==3&&child_return.layout_after[3*16+5]==0&&
              child_return.layout_after[3*16+6]==49,
              "RMB drag from the rendered 32×25 folder tile returns it to the visible chain");
        check(folder_drag.take_sound_ids().empty(),"folder-child release cannot replay its down-event append");
        DesktopScreen folder_drop(data,child_desk);
        folder_drop.pointer_down(50,49,true); folder_drop.take_sound_ids();
        folder_drop.pointer_move(55,52);
        const auto dropped_child=folder_drop.pointer_up(55,52,true);
        check(dropped_child.layout_operation==DeskLayoutOperation::set_folder &&
              folder_drop.take_sound_ids()==std::vector<uint8_t>{12},
              "Source fully enclosed drop unlinks the dragged child exactly once");
        DesktopScreen unchanged_handle(data,child_desk);
        unchanged_handle.pointer_down(41,41);
        check(!unchanged_handle.pending() && unchanged_handle.take_sound_ids().empty(),
              "existing LMB handle gesture has no invented RMB sound mapping");
        unchanged_handle.pointer_up(41,41);
        check(unchanged_handle.take_sound_ids().empty(),"handle release remains silent at this Source boundary");
        auto covered_child=child_desk;
        covered_child.items.push_back(DeskItem{6,60,48,32,25,21,0,0,false,false});
        covered_child.layout_records[49][5]=6;
        covered_child.layout_records[6][5]=0;
        covered_child.layout_records[6][6]=49;
        covered_child.layout_records[6][12]=21;
        DesktopScreen stacked_icon(data,covered_child);
        const auto exposed_folder_frame=folder_click.render();
        const auto covered_folder_frame=stacked_icon.render();
        const auto desktop_atlas=load_vga(asset_path(data/"KB","3.VGA"));
        for(int y=0;y<25;++y) for(int x=0;x<32;++x)
            check(covered_folder_frame.pixels[size_t(48+y)*320+60+x]==
                  desktop_atlas.pixels[size_t(50+y)*desktop_atlas.width+160+x],
                  "top icon renders over the folder child using its own native KB3 tile");
        check(covered_folder_frame.pixels[size_t(49)*320+50]==exposed_folder_frame.pixels[size_t(49)*320+50],
              "the uncovered part of a folder child remains visible below the stacked icon");
        const auto covering_icon_action=stacked_icon.click(65,49);
        check(covering_icon_action.kind==DeskRequestKind::open_menu&&covering_icon_action.item_id==6&&
              covering_icon_action.menu_kind==21&&stacked_icon.state().layout_records==covered_child.layout_records,
              "LMB activates the rendered top icon rather than the covered folder child without layout edits");
        DesktopScreen stacked_drag(data,covered_child);
        stacked_drag.pointer_down(65,49,true);
        check(stacked_drag.take_sound_ids()==std::vector<uint8_t>({12,15}),
              "visible RMB drag starts with the Source unlink/append pair");
        stacked_drag.pointer_move(200,120);
        const auto covering_icon_drag=stacked_drag.pointer_up(200,120,true);
        check(covering_icon_drag.kind==DeskRequestKind::change_layout&&covering_icon_drag.item_id==6&&
              covering_icon_drag.layout_operation==DeskLayoutOperation::move_item&&
              covering_icon_drag.layout_after[3*16+5]==0xff&&covering_icon_drag.layout_after[3*16+6]==0xff,
              "RMB drags the rendered top icon and leaves the covered child outside the visible link chain");
        check(stacked_drag.take_sound_ids().empty(),"ordinary move release adds no structural sound");
        DesktopScreen still_drag(data,covered_child);
        still_drag.pointer_down(65,49,true); still_drag.pointer_up(65,49,true);
        check(still_drag.take_sound_ids()==std::vector<uint8_t>({12,15}),
              "Source RMB reordering sounds happen even without pointer motion");
        DesktopScreen exposed_child(data,covered_child);
        check(exposed_child.click(50,49).item_id==3,"an exposed folder tile still opens its native child action");
        auto status_cover=child_desk;
        status_cover.items.push_back(DeskItem{48,48,48,42,35,56,0,0,true,false});
        DesktopScreen stacked_status(data,status_cover);
        check(stacked_status.render().pixels!=exposed_folder_frame.pixels&&
              stacked_status.pointer_down(50,49).kind==DeskRequestKind::none&&
              stacked_status.pointer_up(50,49).kind==DeskRequestKind::none&&!stacked_status.pending(),
              "an open status drawer covers a folder child and its inert body cannot activate that child");
        auto empty_folder_cover=child_desk;
        empty_folder_cover.items.push_back(DeskItem{48,48,48,45,35,51,0,1,true,false});
        DesktopScreen stacked_empty_folder(data,empty_folder_cover);
        check(stacked_empty_folder.click(50,49).kind==DeskRequestKind::none&&!stacked_empty_folder.pending(),
              "an empty part of the top folder blocks a lower folder child");
        DesktopScreen covered_bin_restore(data,empty_folder_cover);
        covered_bin_restore.pointer_move(160,239);
        covered_bin_restore.click(10,215);
        check(covered_bin_restore.pointer_down(50,49,true).kind==DeskRequestKind::none&&
              covered_bin_restore.pointer_up(50,49,true).kind==DeskRequestKind::none&&
              !covered_bin_restore.pending()&&covered_bin_restore.state().layout_records==empty_folder_cover.layout_records,
              "bin-mode RMB on a covering empty folder cannot restore the lower child or change links");
        check(covered_bin_restore.take_sound_ids().empty(),"covered bin child cannot emit a restore sound");
        DesktopScreen exposed_bin_restore(data,covered_child);
        exposed_bin_restore.pointer_move(160,239);
        exposed_bin_restore.click(10,215);
        const auto exposed_restore=exposed_bin_restore.click(50,49,true);
        check(exposed_bin_restore.take_sound_ids()==std::vector<uint8_t>{15},"bin-mode restore emits only append ID15");
        check(exposed_restore.layout_operation==DeskLayoutOperation::restore_folder_child&&exposed_restore.item_id==3,
              "bin-mode RMB still restores an exposed folder child using its native owner");
        DesktopScreen bin_send(data,covered_child);
        bin_send.pointer_move(160,239); bin_send.click(10,215);
        const auto sent_icon=bin_send.click(65,49,true);
        check(sent_icon.layout_operation==DeskLayoutOperation::send_to_folder &&
              bin_send.take_sound_ids()==std::vector<uint8_t>{12},"bin-mode send emits only unlink ID12");
        auto closed_cover=child_desk;
        closed_cover.items.push_back(DeskItem{48,48,48,14,14,63,0,0,false,false});
        DesktopScreen stacked_closed_drawer(data,closed_cover);
        check(stacked_closed_drawer.click(50,49).kind==DeskRequestKind::none&&!stacked_closed_drawer.pending(),
              "a closed drawer blocks hits inside its Source dimensions minus eight");
        DesktopScreen outside_closed_drawer(data,closed_cover);
        check(outside_closed_drawer.click(55,49).item_id==3,
              "the eight-pixel closed drawer inset allows hits on the exposed lower folder child");
        const auto menu_test_dir = std::filesystem::temp_directory_path() /
            ("openbmh-menu-layout-picker-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(menu_test_dir / "load");
        std::filesystem::create_directories(menu_test_dir / "save");
        { std::ofstream(menu_test_dir / "load" / "LAYOUT.MEN") << "fixture";
          std::ofstream(menu_test_dir / "load" / "NOTGAME.FBM") << "fixture"; }
        SaveLoadScreen menu_load_picker(data, menu_test_dir / "load", SaveLoadMode::load, 0, 6,
            menu_bar.render(), true, SaveLoadFileKind::menu_layout, 52);
        check(menu_load_picker.entries().size() == 1 &&
              menu_load_picker.entries()[0].path.extension() == ".MEN" &&
              !menu_load_picker.info_press(60,45),
              "menu-layout picker filters to MEN and never interprets a layout as an FBM information header");
        menu_load_picker.click(60,45); menu_load_picker.click(60,45);
        check(menu_load_picker.pending() && menu_load_picker.pending()->file_kind == SaveLoadFileKind::menu_layout &&
              menu_load_picker.pending()->origin_item_id == 52 &&
              menu_load_picker.pending()->path.extension() == ".MEN",
              "MEN load request retains its typed file kind and source drawer identity");
        SaveLoadScreen menu_save_picker(data, menu_test_dir / "save", SaveLoadMode::save, 0, 6,
            menu_bar.render(), true, SaveLoadFileKind::menu_layout, 52);
        menu_save_picker.click(120,214); menu_save_picker.text("LAYOUT"); menu_save_picker.key("ENTER");
        check(menu_save_picker.pending() && menu_save_picker.pending()->file_kind == SaveLoadFileKind::menu_layout &&
              menu_save_picker.pending()->path.filename() == "LAYOUT.MEN" &&
              save_load_request_json(*menu_save_picker.pending()).find("\"file_kind\":\"menu_layout\"") != std::string::npos,
              "MEN save request uses the native extension and an explicit JSON file-kind discriminator");
        std::filesystem::remove_all(menu_test_dir);
        TextDb desk_texts = load_textd(asset_path(data,"TEXTD.BLK"));
        auto match_game = g; match_game.texts = &desk_texts;
        match_game.club_names.assign(32,0);
        std::copy_n("UNITFC",6,match_game.club_names.begin());
        std::copy_n("TESTCITY",8,match_game.club_names.begin()+7);
        std::copy_n("BREMEN",6,match_game.club_names.begin()+16);
        match_game.club_ids[12]=0; match_game.club_ids[13]=7; match_game.club_ids[14]=16;
        match_game.results.assign(9 * 0x17c, 0xcc);
        check(desk_texts[(::bmh::layout::FixtureDesktopFirstLabelPointer-::bmh::layout::TextPointerTable)/4]==bmh::test::expected_text("desktop_tests.0") &&
              desk_texts[(::bmh::layout::FixtureDesktopSecondLabelPointer-::bmh::layout::TextPointerTable)/4]==bmh::test::expected_text("desktop_tests.1"),
              "kind-61 rank suffix and strength label resolve from the Source TEXTD addresses");
        check(format_number(match_game,6,2)=="%6" && format_number(match_game,95,2)=="95",
              "legacy routine width 2 keeps a transparent leading cell only for one-digit strength");
        const Font small_font=load_font(data/"F"/"BMH3.FNT");
        check(small_font.glyph('%').bit_width==0 && small_font.glyph('%').advance==5,
              "FNT3 renders legacy routine's percent width placeholder as a leading space");
        auto ranked_game = match_game;
        ranked_game.league_rounds[0] = 1;
        ranked_game.position_history.resize(340);
        write16(ranked_game.position_history, 12 * 13 * 2, 6);
        write16(ranked_game.position_history, 13 * 13 * 2, 2);
        auto ranked_desk = desk_from_game(ranked_game,exe,8);
        check(ranked_desk.table_position==6 && ranked_desk.strength==90,
              "desk projects Core table_position as a zero-based rank beside strength");
        auto current_club_game = ranked_game;
        current_club_game.manager_teams[0] = 13;
        write16(current_club_game.manager_records,0x290,13);
        auto current_club_desk = desk_from_game(current_club_game,exe,8);
        check(current_club_desk.runtime_club==13 && current_club_desk.club_name=="TESTCITY" &&
              current_club_desk.table_position==2,
              "table rank follows the current runtime club identity after club remapping");
        auto rank_item_desk = ranked_desk;
        rank_item_desk.items.clear();
        rank_item_desk.items.push_back({42,213,16,94,15,61,0,0,false,false});
        const auto ranked_image = DesktopScreen(data,rank_item_desk).render();
        auto unranked_item_desk = rank_item_desk;
        unranked_item_desk.table_position.reset();
        const auto unranked_image = DesktopScreen(data,unranked_item_desk).render();
        int min_x=320, min_y=240, max_x=-1, max_y=-1;
        for (int y=0;y<240;++y) for (int x=0;x<320;++x)
            if (ranked_image.pixels[size_t(y)*320+x]!=unranked_image.pixels[size_t(y)*320+x]) {
                min_x=std::min(min_x,x); min_y=std::min(min_y,y);
                max_x=std::max(max_x,x); max_y=std::max(max_y,y);
            }
        check(max_x>=0 && min_x>=214 && max_x<307 && min_y==17 && max_y==22,
              "rank changes only the centered Source kind-61 text line at its saved position and clip");
        auto flat_text_desk = rank_item_desk;
        flat_text_desk.gradient_text=false;
        check(DesktopScreen(data,flat_text_desk).render().pixels!=ranked_image.pixels,
              "kind-61 rank and strength use the existing Source gradient text renderer");
        check(desk_json(ranked_desk).find("\"table_position\":6")!=std::string::npos &&
              desk_json(d).find("\"table_position\":null")!=std::string::npos,
              "desk JSON reports the optional zero-based table position");
        MatchGroup league_fixture; league_fixture.desc.assign(0xd0,0); league_fixture.desc[0]=0;
        write32(league_fixture.desc,0xcc,0); league_fixture.fixtures={12,13}; league_fixture.results={0xcc};
        std::vector<MatchGroup> prepared{league_fixture};
        auto matched = desk_from_game(match_game,exe,8,&prepared,34);
        check(matched.next_opponent==13 && matched.next_competition==exe.u8(::bmh::layout::LeagueCalendarBits) &&
              matched.next_opponent_name=="TESTCITY" && matched.next_opponent_badge.has_value() &&
              !matched.next_opponent_badge_fallback,
              "current prepared league fixture maps opponent, Source competition and badge");
        check(!matched.next_competition_label.empty(),"league fixture resolves Source label and matchday");
        auto setup_group = league_fixture; setup_group.results.clear();
        std::vector<MatchGroup> setup_groups{setup_group};
        auto setup_desk = desk_from_game(match_game,exe,8,&setup_groups,34);
        check(setup_desk.next_opponent==13,
              "fresh setup_matchday uses typed raw 0xCC season result when MatchGroup results are empty");
        auto played_game = match_game; played_game.results[0] = 0;
        setup_desk = desk_from_game(played_game,exe,8,&setup_groups,34);
        check(setup_desk.next_opponent==-1 && setup_desk.next_competition==34,
              "fresh setup_matchday suppresses a fixture whose typed raw season result is played");
        matched.items.push_back({40,100,149,33,29,15,0,0,false,false});
        matched.items.push_back({41,140,149,33,29,17,0,0,false,false});
        check(DesktopScreen(data,matched).render().pixels.size()==320*240,
              "resolved league opponent renders its Source competition, names and badge");
        auto played_league = league_fixture;
        played_league.results={0}; played_league.kickoff_void_mask=0;
        std::vector<MatchGroup> played_group{played_league};
        auto played_desk=desk_from_game(match_game,exe,8,&played_group,34);
        check(played_desk.next_competition==34 && played_desk.next_opponent==-1 &&
              !played_desk.next_opponent_badge,
              "Source played league result keeps competition default 34 and clears the opponent");
        auto later_played = played_league; later_played.fixtures={12,14};
        write32(later_played.desc,0xcc,2);
        std::vector<MatchGroup> mixed_results{league_fixture,later_played};
        auto mixed_desk=desk_from_game(match_game,exe,8,&mixed_results,34);
        check(mixed_desk.next_competition==exe.u8(::bmh::layout::LeagueCalendarBits) && mixed_desk.next_opponent==-1,
              "later played fixture clears opponent while preserving the earlier competition write");
        auto away_game = match_game;
        away_game.manager_records.at(0x28a) = 1;
        auto away_fixture = league_fixture; away_fixture.fixtures={13,12};
        std::vector<MatchGroup> away_groups{away_fixture};
        auto away_desk = desk_from_game(away_game,exe,8,&away_groups,34);
        check(away_desk.next_opponent==13 && away_desk.next_match_away,
              "Source manager record +0x28a marks the manager's fixture as away");
        away_desk.items.push_back({40,100,149,33,29,15,0,0,false,false});
        auto rendered_away = DesktopScreen(data,away_desk).render();
        auto expected_order = away_desk;
        std::swap(expected_order.club_name, expected_order.next_opponent_name);
        expected_order.next_match_away = false;
        check(rendered_away.pixels == DesktopScreen(data,expected_order).render().pixels,
              "Source desk renders the home side before the away side for an away manager");
        MatchGroup cup_fixture; cup_fixture.desc.assign(0xd0,0); cup_fixture.desc[0]=6;
        cup_fixture.fixtures={12,14}; cup_fixture.cup_results={0};
        prepared.push_back(cup_fixture);
        matched = desk_from_game(match_game,exe,9,&prepared,34);
        check(matched.next_opponent==14 && matched.next_competition==10 &&
              matched.next_opponent_name=="BREMEN",
              "later matching group deterministically overrides earlier Source values");
        auto special_game=match_game;special_game.memory.add_block(::bmh::layout::CompetitionCalendar,2);
        special_game.memory.set16(::bmh::layout::CompetitionCalendar,0x4006);
        auto special=desk_from_game(special_game,exe,10,&prepared,34);
        check(special.next_competition_label==desk_texts[(::bmh::layout::DeskState_FormatError_RuntimeField4-::bmh::layout::TextPointerTable)/4],
              "Source calendar entry 4006 selects the special competition title");
        MatchGroup fallback_badge_group=cup_fixture;fallback_badge_group.fixtures={12,100};
        std::vector<MatchGroup> fallback_badge_groups{fallback_badge_group};
        auto fallback_badge=desk_from_game(match_game,exe,10,&fallback_badge_groups,34);
        check(fallback_badge.next_opponent==100&&fallback_badge.next_opponent_badge_fallback&&
              !fallback_badge.next_opponent_badge,
              "legacy routine special team range uses its computer-portrait fallback");
        prepared.clear();
        auto other_fixture = league_fixture; other_fixture.fixtures={13,14}; prepared.push_back(other_fixture);
        auto unmatched = desk_from_game(match_game,exe,10,&prepared,34);
        check(unmatched.next_competition==34 && unmatched.next_opponent==-1,
              "active-event fallback is competition 34 with the Source no-opponent portrait");
        check(unmatched.next_competition_label==desk_texts[(::bmh::layout::DeskState_DeskFromGame_TextPointer-::bmh::layout::TextPointerTable)/4+34] &&
              !unmatched.next_competition_detail.empty(),
              "active-event no-opponent desk shows the Source free-day title and parenthesized first league");
        MatchGroup cup_fallback; cup_fallback.desc.assign(0xd0,0); cup_fallback.desc[0]=6;
        cup_fallback.fixtures={13,14};
        std::vector<MatchGroup> cup_fallback_groups{cup_fallback};
        auto cup_day = desk_from_game(match_game,exe,10,&cup_fallback_groups,34);
        const size_t cup_detail_id=(::bmh::layout::DeskState_FormatError_ExecutableField3-::bmh::layout::TextPointerTable)/4+cup_fallback.desc[0];
        check(cup_day.next_competition_detail=="("+desk_texts.strings.at(cup_detail_id)+")",
              "free-day cup detail resolves its name through runtime TEXTD");
        check(!exe.cstring(::bmh::layout::DeskDrawers_Recent_TextPointer).empty(),"Source executable contains the opponent separator string");
        cup_day.items.push_back({40,100,149,33,29,15,0,0,false,false});
        cup_day.next_opponent=13; cup_day.next_opponent_name="TESTCITY";
        check(DesktopScreen(data,cup_day).render().pixels.size()==320*240,
              "Source separator and runtime cup detail render on desk item 15");
        unmatched = desk_from_game(match_game,exe,11,&prepared,33);
        check(unmatched.next_competition==33 && unmatched.next_opponent==-1,
              "no-event desk retains Source defaults 33 and no opponent");
        unmatched = desk_from_game(match_game,exe,12,&prepared,32);
        check(unmatched.next_competition==32 && unmatched.next_opponent==-1,
              "season-end desk retains Source defaults 32 and no opponent");
        // The international-phase manager uses the special kNoTeam slot and
        // the 24-player squad at 3072; projection is display-only.
        auto nat = fixture(); nat.manager_teams[0] = kNoTeam; nat.memory.set32(::bmh::layout::CompetitionSelection,2);
        nat.memory.set8(::bmh::layout::Desktops + 5 * 16 + 5,6);
        for (int i=0;i<2;++i) {
            const auto at=::bmh::layout::DesktopsUnsigned+uint32_t(i+6)*16;
            nat.memory.set16(at,uint16_t(200+i*40)); nat.memory.set8(at+2,120);
            nat.memory.set8(at+3,32); nat.memory.set8(at+4,25); nat.memory.set8(at+5,uint8_t(i?0:7));
            nat.memory.set8(at+12,uint8_t(i?22:14));
        }
        nat.club_names = {'D','E','U','T','S','C','H','L','A','N','D',0};
        nat.club_ids[kNoTeam] = 0; nat.squad_slots.assign(3096,0x0fff); nat.players.resize(24);
        nat.team_morale.resize(419,25);
        for (int row=0; row<24; ++row) {
            nat.squad_slots[3072 + row] = uint16_t(row | (row < 11 ? 0x8000 : row < 16 ? 0x4000 : 0));
            nat.players[row].w35c = 50 | (90 << 7) | (90 << 14);
            nat.tactics[size_t(row)*0x58 + 0x0c] = 4;
        }
        const auto nat_seed = nat.memory.u32(::bmh::layout::CareerSession_Save_RuntimeField);
        const auto nat_records = nat.manager_records;
        const auto nat_squads = nat.squad_slots;
        auto nd = desk_from_game(nat,exe,41);
        check(nd.national_team && nd.runtime_club==kNoTeam && nd.club_name=="DEUTSCHLAND" && nd.strength==90,
              "national team identity and squad projection");
        check(nat_records==nat.manager_records && nat_squads==nat.squad_slots && nat.memory.u32(::bmh::layout::CareerSession_Save_RuntimeField)==nat_seed,
              "national projection is read-only and drawless");
        check(std::count_if(nd.items.begin(),nd.items.end(),[](const DeskItem& item){return item.kind==14&&item.disabled;})==1 &&
              std::count_if(nd.items.begin(),nd.items.end(),[](const DeskItem& item){return item.kind==22&&item.disabled;})==1,
              "national bank/stadium entries disabled");
        check(desk_json(nd).find("\"national_team\":true")!=std::string::npos,"national mode is exposed to UI clients");
        DesktopScreen national_desk(data,nd);
        check(national_desk.key("A").kind==DeskRequestKind::open_menu,"national lineup remains available");
        national_desk.reject_action("test");
        check(national_desk.click(205,125).kind==DeskRequestKind::none,"national bank request is suppressed");
        check(national_desk.click(245,125).kind==DeskRequestKind::none,"national stadium request is suppressed");
        g.date = 1; g.memory.set32(::bmh::layout::CalendarDay,100); d = desk_from_game(g,exe,8);
        check(d.date.day == 2 && d.day_counter == 1,"typed date overrides stale memory");
        g.date = 0;
        g.tactics[0x0f] = 69; check(desk_from_game(g,exe,9).warnings[4] == false,"exhaustion 69");
        g.tactics[0x0f] = 70; check(desk_from_game(g,exe,9).warnings[4] == true,"exhaustion 70");
        g.tactics[0x0f] = 0;
        g.manager_records[0xd9] = 8; check(desk_from_game(g,exe,9).warnings[7] == false,"important 8");
        g.manager_records[0xd9] = 9; check(desk_from_game(g,exe,9).warnings[7] == true,"important 9");
        g.manager_records[0xd9] = 0;
        g.tactics[0x0c] = 3; check(desk_from_game(g,exe,9).warnings[8] == true,"contract below four months");
        g.tactics[0x0c] = 4;
        g.manager_records[0xcb] = 14; check(desk_from_game(g,exe,9).warnings[3] == false,"training sum fourteen");
        g.manager_records[0xcb] = 15; check(desk_from_game(g,exe,9).warnings[3] == true,"training sum fifteen");
        g.manager_records[0xcb] = 0;
        write16(g.stocks,0x50,2); write32(g.stocks,4,20); write16(g.stocks,0x4e,10);
        check(desk_from_game(g,exe,9).warnings[0] == false,"stock break-even");
        write16(g.stocks,0x4e,11); check(desk_from_game(g,exe,9).warnings[0] == true,"stock profit");
        g.stocks.assign(0x450,0);
        write32(g.manager_records,0x314,uint32_t(-6000000));
        check(desk_from_game(g,exe,9).warnings[9] == false,"licence six million boundary");
        write32(g.manager_records,0x314,uint32_t(-6000001));
        check(desk_from_game(g,exe,9).warnings[9] == true,"licence debt exceeds boundary");
        write32(g.manager_records,0x314,0);
        auto invalid = fixture(); invalid.memory.set8(::bmh::layout::Desktops + 5 * 16 + 5,1);
        rejected([&]{ desk_from_game(invalid,exe,0); });
        auto supported = d.options; supported.world = true; validate_desk_profile(supported);
        supported.managers = 4; validate_desk_profile(supported);
        auto wrong = d.options; wrong.managers = 5; rejected([&]{ validate_desk_profile(wrong); });
        wrong = d.options; wrong.world = false; wrong.league = false; rejected([&]{ validate_desk_profile(wrong); });

        d = desk_from_game(g,exe,10); DesktopScreen desk(data,d);
        const auto pixels = desk.render().pixels;
        check(desk.render().pixels == pixels,"repeat render is deterministic");
        check(desk.click(5,23).kind == DeskRequestKind::none,"calendar handle must not advance a turn");
        auto action = desk.click(50,125);
        check(action.kind == DeskRequestKind::open_menu && action.menu_kind == 3 && action.manager_slot == 0 && action.revision == 10,"lineup request");
        check(desk.click(20,40).kind == DeskRequestKind::none,"pending command blocks another turn");
        rejected([&]{ desk.acknowledge(d); });
        ++d.revision; desk.acknowledge(d);
        check(desk.click(20,40).kind == DeskRequestKind::advance_turn,"open calendar advances");
        check(desk.click(20,40).kind == DeskRequestKind::none,"no duplicate advance request");
        ++d.revision; desk.acknowledge(d);
        check(desk.click(165,125).kind == DeskRequestKind::advance_turn,"kickoff icon advances");
        check(desk.render().pixels == pixels,"commands cannot mutate the saved game view");
        desk.reject_action("unsupported_menu");
        check(!desk.pending() && desk.render().pixels == pixels,"rejected command preserves state");
        check(desk.key("A").menu_kind == 3,"original A hotkey opens lineup");
        desk.reject_action("not_implemented");
        check(desk.key("D").menu_kind == 18,"original D hotkey opens tactics");
        desk.reject_action("not_implemented");
        check(desk.key("I").menu_kind == 16 && desk_menu_name(16)=="training" && desk_menu_name(15)=="training_camp",
              "original I and dispatch distinguish training from training camp");

        StartupFlow flow(data); begin(flow);
        auto result = CareerInitializationResult{d,{{0,0,12}}};
        result.desktop.source = "original_save_read_only";
        rejected([&]{ flow.attach_initialized_career(result); });

        // The desk boundary accepts every UI-supported manager count only when
        // all confirmed choices match contiguous engine slots and runtime clubs.
        for(int count=1;count<=4;++count) {
            StartupFlow multi_flow(data); confirm_managers(multi_flow,count);
            const auto request=*multi_flow.pending_career();
            auto game=career_game(exe,request); auto career_desk=desk_from_game(game,exe,20);
            std::vector<InitializedManager> initialized;
            for(int slot=0;slot<count;++slot) initialized.push_back({slot,request.managers[slot].master_club,game.manager_teams[slot]});
            auto accepted=CareerInitializationResult{career_desk,initialized,{}};
            auto malformed=accepted;
            if(count>1) malformed.managers.back().slot=0;
            else malformed.managers.clear();
            rejected([&]{multi_flow.attach_initialized_career(malformed);});
            check(multi_flow.pending_career()!=nullptr,"missing/duplicate slots leave the career request untouched");
            malformed=accepted; malformed.managers[0].master_club++;
            rejected([&]{multi_flow.attach_initialized_career(malformed);});
            malformed=accepted; malformed.managers[0].runtime_club++;
            rejected([&]{multi_flow.attach_initialized_career(malformed);});
            malformed=accepted; malformed.desktop.runtime_club++;
            rejected([&]{multi_flow.attach_initialized_career(malformed);});
            if(count>1) {
                auto duplicate_club=game; duplicate_club.manager_teams[1]=duplicate_club.manager_teams[0];
                rejected([&]{desk_from_game(duplicate_club,exe,20);});
            }
            multi_flow.attach_initialized_career(accepted);
            check(multi_flow.desktop() && multi_flow.desktop()->state().manager_teams.size()==size_t(count),"1-4 manager career attaches with explicit runtime map");
            if(count>=2) {
                auto next_game=game; next_game.memory.set8(::bmh::layout::ActiveManager,1);
                auto next=desk_from_game(next_game,exe,21);
                next.league=uint8_t((request.managers[1].start_league+1)%9);
                next.managers[1].league=next.league;
                next.badge=8; next.managers[1].badge=8; // exercise a different W/*.VGA sheet
                auto menu=multi_flow.send_key("A");
                check(menu.request==FlowRequest::desk_action,"manager action is pending before a handoff");
                rejected([&]{multi_flow.acknowledge_desk(next);});
                check(multi_flow.desktop()->pending().has_value() && multi_flow.desktop()->state().manager_slot==0,"ordinary action cannot switch managers");
                multi_flow.reject_desk_action("test action rejected");
                auto action=multi_flow.click(20,40);
                check(action.request==FlowRequest::kickoff,"multi-manager turn action accepted");
                auto stale=next; stale.revision=20;
                rejected([&]{multi_flow.acknowledge_desk(stale);});
                check(multi_flow.desktop()->pending().has_value() && multi_flow.desktop()->state().manager_slot==0,"stale handoff leaves request and previous manager intact");
                auto wrong_map=next; wrong_map.runtime_club=12;
                rejected([&]{multi_flow.acknowledge_desk(wrong_map);});
                check(multi_flow.desktop()->pending().has_value(),"wrong runtime club does not consume handoff request");
                multi_flow.acknowledge_desk(next);
                check(multi_flow.desktop()->state().manager_slot==1 && multi_flow.desktop()->state().runtime_club==13,"newer validated ACK can hand off to another manager");
                check(multi_flow.render().pixels.size()==320*240,"manager handoff loads the new badge sheet before rendering");
            }
        }
        StartupFlow national_flow(data); confirm_managers(national_flow,1,true);
        auto national_request=*national_flow.pending_career();
        check(!national_request.options.league && national_request.options.world,"world-only UI setup is preserved");
        auto national_game=career_game(exe,national_request,true);
        national_game.club_names={'D','E','U','T','S','C','H','L','A','N','D',0};
        national_game.club_ids[kNoTeam]=0;
        national_game.squad_slots[3072]=0x8000;
        auto national_projection=desk_from_game(national_game,exe,30);
        auto national_manager=InitializedManager{0,national_request.managers[0].master_club,kNoTeam};
        national_flow.attach_initialized_career({national_projection,{national_manager},{}});
        check(national_flow.desktop()->state().national_team,"national career reaches the desktop with kNoTeam mapping");
        auto national_preparation=preparation_from_game(national_game,exe,30);
        check(national_preparation.lineup.players.size()==24 &&
              national_preparation.lineup.runtime_club==kNoTeam &&
              national_preparation.tactics.runtime_club==kNoTeam,
              "national career projects its special 24-player lineup and tactics");
        check(!national_preparation.training && !national_preparation.training_camp &&
              !national_preparation.bank && !national_preparation.stadium &&
              !national_preparation.toto && !national_preparation.pitch,
              "national preparation omits club-only contexts");
        check(LineupScreen(data,national_preparation.lineup).render().pixels.size()==320*240 &&
              TacticsScreen(data,national_preparation.tactics).render().pixels.size()==320*240,
              "national lineup and tactics render from the projected game state");
        check(national_preparation.player_cards.size()==1 &&
              national_preparation.player_cards[0].club_short_name=="DEUTSCHLAND",
              "national player card retains the full country label");
        StartupFlow world_flow(data); confirm_managers(world_flow,1,true);
        auto world_request=*world_flow.pending_career();
        auto world_game=career_game(exe,world_request); auto world_desk=desk_from_game(world_game,exe,40);
        world_flow.attach_initialized_career({world_desk,{{0,world_request.managers[0].master_club,12}}, {}});
        check(world_flow.click(20,40).request==FlowRequest::kickoff,"world-only manager can reach an international phase");
        world_game.manager_teams[0]=kNoTeam; world_game.memory.set32(::bmh::layout::CompetitionSelection,2);
        world_game.memory.set8(::bmh::layout::CoachActive,1);
        world_game.memory.add_block(::bmh::layout::OriginalManagerCount,1);
        world_game.memory.set8(::bmh::layout::OriginalManagerCount,1);
        std::copy_n(world_game.manager_records.begin(),0xb0c,world_game.manager_records.begin()+4*0xb0c);
        write16(world_game.manager_records,4*0xb0c+0x290,12);
        std::copy_n("DEUTSCHLAND",11,world_game.club_names.begin());
        world_game.club_names[11]=0;
        world_game.club_ids[kNoTeam]=0;
        auto world_national=desk_from_game(world_game,exe,41);
        check(world_national.managers[0].club_identity_key==world_desk.managers[0].club_identity_key,
              "national duty preserves the stable home-club identity");
        world_flow.acknowledge_desk(world_national);
        check(world_flow.desktop()->state().national_team && world_flow.desktop()->state().runtime_club==kNoTeam,
              "single-manager world transition accepts the national runtime club");
        check(world_flow.click(20,40).request==FlowRequest::kickoff,
              "national desk accepts the next turn request");
        auto other_home=world_game; other_home.manager_teams[0]=13;
        write16(other_home.manager_records,0x290,13);
        auto wrong_home=desk_from_game(other_home,exe,42);
        rejected([&]{world_flow.acknowledge_desk(wrong_home);});
        check(world_flow.desktop()->pending().has_value() && world_flow.desktop()->state().national_team,
              "national handback to another club is rejected without consuming the turn");
        world_game.manager_teams[0]=12; write16(world_game.manager_records,0x290,12);
        auto home_handback=desk_from_game(world_game,exe,42);
        world_flow.acknowledge_desk(home_handback);
        check(!world_flow.desktop()->state().national_team && world_flow.desktop()->state().runtime_club==12,
              "national duty returns to the same home club");
        check(flow.pending_career()!=nullptr,"rejected attachment preserves request");
        result.desktop.source = "engine_projection"; result.managers[0].master_club = 9;
        rejected([&]{ flow.attach_initialized_career(result); });
        result.managers[0].master_club = 0; flow.attach_initialized_career(result);
        check(flow.screen_name()=="desktop" && !flow.pending_career(),"accepted career reaches desktop");
        check(flow.state_json().find("\"engine_attached\":true") != std::string::npos,"engine attachment is explicit");
        rejected([&]{ flow.attach_initialized_career(result); });

        StartupFlow results_flow(data); confirm_managers(results_flow,1);
        const auto results_request=*results_flow.pending_career();
        auto results_game=career_game(exe,results_request);
        std::fill_n(results_game.manager_records.begin()+0xc5,4,2);
        std::fill_n(results_game.manager_records.begin()+0xc9,9,1);
        auto results_desk=desk_from_game(results_game,exe,100);
        results_flow.attach_initialized_career({results_desk,
            {{0,results_request.managers[0].master_club,results_game.manager_teams[0]}},{}});
        results_flow.attach_preparation_context(preparation_from_game(results_game,exe,100));
        check(results_flow.click(20,40).request==FlowRequest::kickoff,
              "prepared desktop emits one turn request before the result display");
        auto next_results_desk=desk_from_game(results_game,exe,101);
        results_flow.acknowledge_desk(next_results_desk);
        results_flow.attach_preparation_context(preparation_from_game(results_game,exe,101));
        MatchdayResults visible_results; visible_results.valid=true; visible_results.matchday=4;
        visible_results.date=12; visible_results.date_label="12.07.2003";
        MatchdayResultGroup visible_group; visible_group.competition="Liga";
        visible_group.matches.push_back({"UNITFC","TESTCITY",2,1,MatchResultStatus::played});
        visible_results.groups.push_back(std::move(visible_group));
        results_flow.show_matchday_results(std::move(visible_results));
        check(results_flow.screen_name()=="matchday_results" &&
              results_flow.state_json().find("\"screen\":\"matchday_results\"")!=std::string::npos,
              "results modal holds the prepared following desk");
        check(results_flow.click(20,40).request==FlowRequest::none &&
              results_flow.screen_name()=="matchday_results",
              "desk input cannot start a second turn while results remain open");
        const auto closed_results=results_flow.send_key("ESC");
        check(closed_results.request==FlowRequest::results_closed &&
              results_flow.screen_name()=="desktop" && !results_flow.desktop()->pending(),
              "Escape closes results without issuing another kickoff");

        if (argc > 2 && std::string(argv[2]).size()) {
            const auto original = read_file(argv[2]);
            auto state = load_reference_desk(argv[2],exe);
            check(state.manager_name=="BMHLAB" && state.runtime_club==12 && state.balance==6243300 && state.strength==86,"captured single-manager baseline");
            const std::array<bool,10> lamps{false,false,true,false,false,false,false,false,false,false};
            for (size_t i=0;i<10;++i) check(state.warnings[i]==lamps[i],"RAM-observed warning vector");
            StartupFlow reference(data); reference.load_reference(argv[2]);
            check(reference.state_json().find("\"engine_attached\":false")!=std::string::npos,"reference is not a new career/attached engine");
            check(read_file(argv[2])==original,"original save unchanged after projection/render");
            // Loading replaces the confirmed-career choice source with the
            // loaded runtime roster. Exercise a following turn and save ACK.
            const auto temp_dir=std::filesystem::temp_directory_path() /
                ("openbmh-u7b-load-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            std::filesystem::create_directories(temp_dir);
            const auto staged_save=temp_dir/"BASELINE.FBM";
            std::filesystem::copy_file(argv[2],staged_save);
            StartupFlow load_flow(data,temp_dir);
            check(load_flow.click(214,215).request==FlowRequest::submenu_opened,"setup opens load dialog");
            load_flow.click(40,50);
            check(load_flow.click(240,192).request==FlowRequest::file_action,"load dialog emits one file request");
            auto loaded=load_preparation_reference(staged_save,exe);
            set_preparation_revision_source(loaded,1,"engine_projection");
            loaded.desktop.menu_layout_shortcut=DeskItem{53,286,215,12,11,53,0,0,false,false};
            loaded.desktop.shortcut_bar_always=false;
            loaded.desktop.drawer_handles=true;
            DeskItem calendar; calendar.id=50; calendar.kind=58; calendar.x=10; calendar.y=80; calendar.width=32; calendar.height=32; calendar.open=true;
            DeskItem saver; saver.id=51; saver.kind=11; saver.x=60; saver.y=80; saver.width=32; saver.height=32;
            loaded.desktop.items.push_back(calendar); loaded.desktop.items.push_back(saver);
            {
                DesktopScreen drag_screen(data,loaded.desktop);
                drag_screen.pointer_down(65,85,true);
                check(drag_screen.pointer_move(83,101),"desktop drag changes only the local preview before release");
                auto drag_request=drag_screen.pointer_up(83,101,true);
                check(drag_request.kind==DeskRequestKind::change_layout &&
                      drag_request.layout_operation==DeskLayoutOperation::move_item &&
                      drag_request.layout_before!=drag_request.layout_after && drag_screen.pending().has_value(),
                      "desktop drag release emits an owner/revision-bound native layout request");
                DesktopScreen cancelled(data,loaded.desktop);
                cancelled.pointer_down(65,85,true); cancelled.pointer_move(83,101);
                check(cancelled.pointer_up(83,101,false).kind==DeskRequestKind::none && !cancelled.pending(),
                      "opposite-button release cancels the local drag without a native request");
            }
            loaded.lineup.source=loaded.tactics.source="engine_projection";
            check(loaded.desktop.manager_teams.size()==size_t(loaded.desktop.options.managers),"loaded projection contains complete manager roster");
            auto stale_loaded=loaded; set_preparation_revision_source(stale_loaded,0,"engine_projection");
            rejected([&]{load_flow.attach_loaded_game(stale_loaded.desktop,stale_loaded);});
            check(load_flow.state_json().find("\"mode\":\"load\"")!=std::string::npos,"stale load leaves file request pending");
            load_flow.attach_loaded_game(loaded.desktop,loaded);
            check(load_flow.desktop() && load_flow.state_json().find("\"engine_attached\":true")!=std::string::npos,"loaded projection attaches as authoritative manager baseline");
            auto turn=load_flow.click(15,85);
            check(turn.request==FlowRequest::kickoff,"loaded desk accepts next-turn request");
            auto next_loaded=loaded; set_preparation_revision_source(next_loaded,2,"engine_projection");
            load_flow.acknowledge_desk(next_loaded.desktop);
            auto open_save=load_flow.click(65,85);
            check(open_save.request==FlowRequest::submenu_opened,"loaded manager can open save dialog");
            load_flow.click(240,192); load_flow.click(240,192);
            check(load_flow.state_json().find("\"action\":\"save\"")!=std::string::npos,"save dialog emits save request");
            auto saved=next_loaded; set_preparation_revision_source(saved,3,"engine_projection");
            load_flow.acknowledge_saved_game(saved.desktop,saved);
            check(load_flow.desktop()->state().revision==3,"save ACK retains loaded multi-view manager baseline");
            load_flow.click(64,239);
            auto open_menu_save=load_flow.click(292,221);
            check(open_menu_save.request==FlowRequest::submenu_opened && open_menu_save.desk_action.menu_kind==53 &&
                  !load_flow.pending_file(),
                  "Source last-cell record-53 left action opens the MEN save picker before a request exists");
            load_flow.click(120,214); load_flow.text("MENU"); load_flow.send_key("ENTER");
            check(load_flow.pending_file() && load_flow.pending_file()->file_kind==SaveLoadFileKind::menu_layout &&
                  load_flow.pending_file()->origin_item_id==53 && load_flow.pending_file()->action==SaveLoadAction::save &&
                  load_flow.pending_file()->path.extension()==".MEN",
                  "submitting a filename emits a typed MEN save request with the Source suffix and record-53 origin");
            { std::ofstream(load_flow.pending_file()->path) << "menu-layout fixture"; }
            auto menu_saved=saved; set_preparation_revision_source(menu_saved,4,"engine_projection");
            load_flow.acknowledge_menu_layout(menu_saved.desktop,menu_saved);
            check(load_flow.desktop()->state().revision==4 && load_flow.desktop()->state().manager_teams==saved.desktop.manager_teams,
                  "MEN acknowledgement advances the UI revision while retaining active career ownership");
            load_flow.click(64,239);
            auto open_menu_load=load_flow.click(292,221,true);
            check(open_menu_load.request==FlowRequest::submenu_opened && !load_flow.pending_file(),
                  "Source last-cell record-53 right action opens the MEN load picker before a request exists");
            load_flow.click(60,49); load_flow.click(60,49);
            check(load_flow.pending_file() && load_flow.pending_file()->file_kind==SaveLoadFileKind::menu_layout &&
                  load_flow.pending_file()->origin_item_id==53 && load_flow.pending_file()->action==SaveLoadAction::load,
                  "selecting the saved MEN entry emits the typed load request");
            rejected([&]{load_flow.attach_loaded_game(menu_saved.desktop,menu_saved);});
            load_flow.reject_file_action("cancelled MEN fixture load");
            std::cout << "Original LABA0012 baseline verified.\n";
        }
        std::cout << "Desktop projection, calendar, command boundary and career attachment tests passed.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "desktop test: " << e.what() << '\n'; return 1; }
}
