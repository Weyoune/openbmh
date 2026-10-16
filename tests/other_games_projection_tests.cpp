// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

#include "game/cup.hpp"
#include "game/other_games.hpp"

namespace {
int rng_draws = 0;
void count_rng_draw(int) { ++rng_draws; }

void check(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

bmh::GameState fixture() {
    bmh::GameState game;
    game.memory.add_block(::bmh::layout::CupTable, bmh::kCupEntries * bmh::kCupEntrySize);
    game.memory.add_block(::bmh::layout::ActiveSelectors, 8);
    game.memory.add_block(::bmh::layout::SyntheticCupParticipantsBegin, 0x100);
    game.memory.add_block(::bmh::layout::SyntheticCupResultsBegin, 0x100);
    game.memory.add_block(::bmh::layout::SyntheticCupWorkspaceBegin, 0x100);
    game.memory.add_block(::bmh::layout::CalendarState_FormatError_RuntimeField3, 2);
    for (int i = 0; i < bmh::kCupEntries; ++i) {
        game.memory.set16(bmh::cup_entry_address(i) + 0x18, 0xffff);
        game.memory.set8(bmh::cup_entry_address(i) + 0x15, 0xff);
    }
    game.memory.set8(::bmh::layout::ActiveSelectors, 0);
    game.memory.set8(::bmh::layout::Cup_RemoveCompetition_RuntimeField, 1);
    game.memory.set8(::bmh::layout::FixtureFirstCupPresentationOption, 0x14);
    game.memory.set8(::bmh::layout::FixtureSecondCupPresentationOption, 0x14);
    game.memory.set8(::bmh::layout::CareerSession_FormatError_RuntimeField3, 0x14);
    game.memory.set8(::bmh::layout::FixtureFourthCupPresentationOption, 0x14);
    game.memory.set8(::bmh::layout::FixtureFifthCupPresentationOption, 0x14);
    game.memory.set8(::bmh::layout::Cup_RemoveCompetition_RuntimeField2, 0x14);

    auto descriptor = [&](int index, int id, int teams, int rounds, uint8_t flags,
                          uint32_t team_address, uint32_t result_address) {
        const uint32_t e = bmh::cup_entry_address(index);
        game.memory.set_pointer(e, team_address);
        game.memory.set_pointer(e + 0x0c, result_address);
        game.memory.set16(e + 0x18, uint16_t(id));
        game.memory.set8(e + 0x14, flags);
        game.memory.set8(e + 0x1a, uint8_t(teams));
        game.memory.set8(e + 0x1b, uint8_t(rounds));
        game.memory.set8(e + 0x1d, 0);
        game.memory.set8(e + 0x1e, 0);
    };
    descriptor(0, 0, 4, 2, 4, ::bmh::layout::SyntheticCupParticipantsBegin, ::bmh::layout::SyntheticCupResultsBegin);
    descriptor(1, 0, 2, 1, 4, ::bmh::layout::SyntheticCupSecondParticipantBlock, ::bmh::layout::SyntheticCupSecondResultBlock);
    game.memory.set8(bmh::cup_entry_address(0) + 0x16, 1);
    descriptor(5, 1, 2, 1, 4, ::bmh::layout::SyntheticCupFifthParticipantBlock, ::bmh::layout::SyntheticCupFifthResultBlock);
    descriptor(6, 1, 4, 3, 0, ::bmh::layout::SyntheticCupFifthParticipantBlock, ::bmh::layout::SyntheticCupFifthResultBlock);
    game.memory.set8(bmh::cup_entry_address(6) + 0x1e, 1);
    descriptor(2, 1, 4, 3, 0, ::bmh::layout::SyntheticCupThirdParticipantBlock, ::bmh::layout::SyntheticCupThirdResultBlock);
    game.memory.set_pointer(bmh::cup_entry_address(2) + 8, ::bmh::layout::CareerSession_RuntimeError_RuntimeField);
    game.memory.set8(bmh::cup_entry_address(2) + 0x1e, 2);
    descriptor(3, 1, 4, 3, 0, ::bmh::layout::SyntheticCupFifthParticipantBlock, ::bmh::layout::SyntheticCupFifthResultBlock);
    game.memory.set_pointer(bmh::cup_entry_address(3) + 8, ::bmh::layout::CareerSession_RuntimeError_RuntimeField);
    game.memory.set8(bmh::cup_entry_address(3) + 0x1e, 0x12);
    game.memory.set8(bmh::cup_entry_address(2) + 0x17, 3);
    descriptor(4, 12, 2, 1, 4, ::bmh::layout::SyntheticCupFourthParticipantBlock, ::bmh::layout::SyntheticCupFourthResultBlock);
    game.memory.set_pointer(bmh::cup_entry_address(4) + 0x10, ::bmh::layout::SyntheticCupAlternateResultBlock);
    game.memory.set16(::bmh::layout::SyntheticCupFourthParticipantBlock, 0);
    game.memory.set16(::bmh::layout::SyntheticCupFourthParticipantValue, 1);
    for (int i = 0; i < 4; ++i) {
        game.memory.set16(::bmh::layout::SyntheticCupParticipantsBegin + uint32_t(i) * 2, uint16_t(i));
        game.memory.set16(::bmh::layout::SyntheticCupThirdParticipantBlock + uint32_t(i) * 2, uint16_t(i + 4));
    }
    game.memory.set16(::bmh::layout::SyntheticCupResultsBegin, 0xcccc);
    game.memory.set16(::bmh::layout::SyntheticCupFirstResultPadding, 0xcccc);
    game.memory.set16(::bmh::layout::SyntheticCupSecondResultPadding, 0xcccc);
    game.memory.set16(::bmh::layout::SyntheticCupThirdResultBlock, 0xcccc);
    game.memory.set16(::bmh::layout::SyntheticCupThirdResultPadding, 0xcccc);

    game.club_ids.resize(419);
    game.club_names.resize(419 * 2, 0);
    for (size_t i = 0; i < game.club_ids.size(); ++i) {
        game.club_ids[i] = uint16_t(i * 2);
        game.club_names[i * 2] = uint8_t('A' + (i % 26));
    }
    game.squad_slots.resize(3096, bmh::kEmptySlot);
    game.team_morale.resize(419, 0);
    game.foreign_strength.resize(419 - 168, 0);
    return game;
}

bmh::TextDb texts() {
    bmh::TextDb db;
    db.strings.resize(1100);
    for (size_t i = 567; i < 600; ++i) db.strings[i] = "Round";
    db.strings[579] = "Unit cup A";
    db.strings[580] = "Unit cup B";
    db.strings[601] = "DFB";
    db.strings[602] = "UEFA";
    db.strings[574] = "Unit group label0";
    db.strings[577] = "Unit competition label 0 - phase";
    db.strings[574][16] = '1';
    return db;
}
}  // namespace

int main() {
    bmh::GameState game = fixture();
    const bmh::TextDb db = texts();
    bool rejected = false;
    const std::array<uint8_t, 8> selectors_before = [&] {
        std::array<uint8_t, 8> values{};
        for (uint32_t i = 0; i < values.size(); ++i) values[i] = game.memory.u8(::bmh::layout::ActiveSelectors + i);
        return values;
    }();
    const uint8_t descriptor_flags_before = game.memory.u8(bmh::cup_entry_address(0) + 0x14);
    const uint16_t result_before_projection = game.memory.u16(::bmh::layout::SyntheticCupResultsBegin);
    bmh::draw_hook = count_rng_draw;
    const uint16_t first_result_before = game.memory.u16(::bmh::layout::SyntheticCupResultsBegin);
    const auto knockout = bmh::other_games_from_game(game, db);
    check(knockout.competition == 0 && knockout.entry_index == 0 && knockout.title == "Unit cup A",
          "ordinary opening selector resolves descriptor zero and loaded TEXTD title");
    check(knockout.selectors.size() == 8 && knockout.selectors[0].active &&
          knockout.selectors[0].competition == 0 && knockout.selectors[1].active &&
          knockout.selectors[0].title == "DFB" && knockout.selectors[1].competition == 1 &&
          knockout.selectors[1].title == "UEFA" && !knockout.selectors[2].active,
          "all eight selector slots preserve active order and blank positions");
    check(knockout.knockout && knockout.rows.size() == 2 && knockout.rows[0].home_team == 0 &&
          knockout.rows[0].away_team == 1 && !knockout.rows[0].played &&
          knockout.rows[0].home_metadata.has_squad_values &&
          knockout.rows[0].home_metadata.averages[0] == 1,
          "knockout row uses Source team order and unplayed sentinel");
    bool untouched = game.memory.u16(::bmh::layout::SyntheticCupResultsBegin) == result_before_projection &&
        game.memory.u8(bmh::cup_entry_address(0) + 0x14) == descriptor_flags_before;
    for (uint32_t i = 0; i < selectors_before.size(); ++i)
        untouched = untouched && game.memory.u8(::bmh::layout::ActiveSelectors + i) == selectors_before[i];
    check(untouched && rng_draws == 0,
          "const projection preserves live Native selector/descriptor/result bytes and both RNG draw hooks");
    bmh::draw_hook = nullptr;
    const auto forced_detail = bmh::other_games_from_game(game, db, 0, 0, 0,
        bmh::OtherGamesDisplayMode::compact, 0, false);
    check(forced_detail.display_mode == bmh::OtherGamesDisplayMode::detail &&
          forced_detail.round_arrows_visible,
          "Source forces effective detail mode and round arrows when participant count is at most sixteen");
    const auto retained_small_cursor = bmh::other_games_from_game(game, db, 0, 0, 0,
        bmh::OtherGamesDisplayMode::detail, 16, false);
    const auto forced_detail_retained_cursor = bmh::other_games_from_game(game, db, 0, 0, 0,
        bmh::OtherGamesDisplayMode::compact, 16, false);
    check(retained_small_cursor.participant_offset == 16 && retained_small_cursor.rows.empty() &&
          forced_detail_retained_cursor.display_mode == bmh::OtherGamesDisplayMode::detail &&
          forced_detail_retained_cursor.participant_offset == 16 && forced_detail_retained_cursor.rows.empty(),
          "small-round redraw forces high bit without replacing a retained Source cursor or inventing rows");
    game.memory.set16(::bmh::layout::SyntheticCupParticipantsBegin, 138);
    const auto no_squad_domestic = bmh::other_games_from_game(game, db, 0, 0, 0);
    check(no_squad_domestic.rows[0].home_metadata.has_squad_values &&
          no_squad_domestic.rows[0].home_metadata.averages[0] == 1,
          "Source no-squad domestic club keeps the zero-count average value");
    game.memory.set16(::bmh::layout::SyntheticCupParticipantsBegin, 200);
    game.foreign_strength[32] = 73;
    const auto foreign = bmh::other_games_from_game(game, db, 0, 0, 0);
    check(!foreign.rows[0].home_metadata.has_squad_values &&
          foreign.rows[0].home_metadata.foreign_strength == 73,
          "unowned foreign team metadata comes from its live strength byte");
    game.memory.set16(::bmh::layout::SyntheticCupParticipantsBegin, bmh::kNoTeam);
    game.foreign_strength[189] = 42;
    const auto foreign_no_team = bmh::other_games_from_game(game, db, 0, 0, 0);
    check(!foreign_no_team.rows[0].home_metadata.has_squad_values &&
          foreign_no_team.rows[0].home_metadata.foreign_strength == 42,
          "unowned national sentinel uses the Source foreign-strength metadata branch");
    game.manager_teams = {bmh::kNoTeam};
    const auto owned_no_team = bmh::other_games_from_game(game, db, 0, 0, 0);
    check(owned_no_team.rows[0].home_metadata.has_squad_values &&
          owned_no_team.rows[0].home_metadata.averages[0] == 1,
          "owned national sentinel uses the Source special squad averages");
    game.memory.add_block(::bmh::layout::ManagerRecords, 5 * 0xb0c);
    game.manager_records.resize(5 * 0xb0c, 0);
    constexpr size_t national_backup = 4 * 0xb0c + 0x290;
    game.manager_records[national_backup] = 200;
    game.manager_records[national_backup + 1] = 0;
    game.memory.set16(::bmh::layout::CupDraw_FormatError_RuntimeField3, 199); // stale raw cache deliberately disagrees with typed slot4.
    game.memory.set16(::bmh::layout::SyntheticCupParticipantsBegin, 200);
    bool owned_foreign_rejected = false;
    try { (void)bmh::other_games_from_game(game, db, 0, 0, 0); }
    catch (const bmh::FormatError&) { owned_foreign_rejected = true; }
    check(owned_foreign_rejected,
          "live National slot4 ownership overrides a conflicting saved-memory manager cache");
    game.manager_teams.clear();
    game.memory.set16(::bmh::layout::SyntheticCupParticipantsBegin, 0);
    game.memory.set16(::bmh::layout::SyntheticCupResultsBegin, uint16_t((2 << 5) | 1));
    const uint16_t scored_result_before = game.memory.u16(::bmh::layout::SyntheticCupResultsBegin);
    const auto scored = bmh::other_games_from_game(game, db, 0, 0, 0);
    check(scored.rows[0].played && scored.rows[0].home_goals == 2 && scored.rows[0].away_goals == 1,
          "packed cup result decodes home and away score fields");
    check(game.memory.u16(::bmh::layout::SyntheticCupResultsBegin) == scored_result_before && first_result_before == 0xcccc,
          "projection leaves all source result bytes unchanged");
    const auto linked = bmh::other_games_from_game(game, db, 0, 1, 0);
    check(linked.entry_index == 1 && linked.rows.size() == 1 && linked.rows[0].home_team == 0,
          "linked page follows descriptor +0x16 next-stage pointer");
    const auto parallel = bmh::other_games_from_game(game, db, 1, 1, 0,
        bmh::OtherGamesDisplayMode::compact, 0, false);
    check(parallel.entry_index == 3 && parallel.competition == 1 &&
          parallel.linked_pages.empty(),
          "Source suppresses linked-page selectors when the first stage has no +0x16 continuation");
    game.memory.set8(bmh::cup_entry_address(2) + 0x16, 4);
    game.memory.set8(bmh::cup_entry_address(3) + 0x17, 1);
    game.memory.set8(bmh::cup_entry_address(4) + 0x16, 6);
    const auto parallel_pages = bmh::other_games_from_game(game, db, 1, 1, 0,
        bmh::OtherGamesDisplayMode::compact, 0, false);
    check(parallel_pages.linked_pages.size() == 3 &&
          parallel_pages.linked_pages[0].entry_index == 2 &&
          parallel_pages.linked_pages[1].entry_index == 3 &&
          parallel_pages.linked_pages[2].entry_index == 6,
          "Source follows +0x17 after the final sibling and exposes the reached +0x16 stage group");
    game.memory.set8(::bmh::layout::Cup_RemoveCompetition_RuntimeField, 0);
    game.memory.set8(::bmh::layout::FixtureFirstCupPresentationOption, 0x14);
    game.memory.set8(::bmh::layout::FixtureSecondCupPresentationOption, 0);
    const auto duplicate_hole = bmh::other_games_from_game(game, db, 0, 0, 0,
        bmh::OtherGamesDisplayMode::detail, 0, false);
    check(duplicate_hole.selectors[1].active && duplicate_hole.selectors[1].competition == 0 &&
          duplicate_hole.selectors[2].active == false && duplicate_hole.selectors[3].active &&
          duplicate_hole.selectors[3].competition == 0,
          "Source selector list preserves duplicate entries after a blank slot");
    game.memory.set8(::bmh::layout::ActiveSelectors, 0);
    game.memory.set8(::bmh::layout::Cup_RemoveCompetition_RuntimeField, 1);
    for (int slot = 2; slot < 8; ++slot) game.memory.set8(::bmh::layout::ActiveSelectors + uint32_t(slot), 0x14);
    game.memory.set8(bmh::cup_entry_address(3) + 0x1d, 3);
    game.memory.set8(bmh::cup_entry_address(3) + 0x16, 5);
    const auto retained_page_open = bmh::other_games_from_game(game, db, 1, 1, -1,
        bmh::OtherGamesDisplayMode::detail, 0, true);
    check(retained_page_open.entry_index == 5,
          "new-screen auto-advance applies after resolving a retained nonzero group occurrence");
    game.memory.set8(bmh::cup_entry_address(3) + 0x1d, 0);
    game.memory.set8(bmh::cup_entry_address(3) + 0x16, 0);
    game.memory.set8(bmh::cup_entry_address(0) + 0x1d, 2);
    game.memory.set8(::bmh::layout::ActiveSelectors, 1);
    game.memory.set8(::bmh::layout::Cup_RemoveCompetition_RuntimeField, 1);
    const auto absent_final_button = bmh::other_games_from_game(game, db, 0, 0, -1,
        bmh::OtherGamesDisplayMode::detail, 0, true);
    check(absent_final_button.entry_index == 1 &&
          std::none_of(absent_final_button.selectors.begin(), absent_final_button.selectors.end(),
                       [](const bmh::OtherGamesSelector& s) { return s.active && s.competition == 0; }),
          "completed DFB final remains resolvable after selector12 cleanup removed button0");
    game.memory.set8(bmh::cup_entry_address(0) + 0x1d, 0);
    game.memory.set8(::bmh::layout::ActiveSelectors, 0);
    game.memory.set8(::bmh::layout::Cup_RemoveCompetition_RuntimeField, 1);
    game.memory.set8(bmh::cup_entry_address(0) + 0x1d, 2);
    const auto auto_advanced = bmh::other_games_from_game(game, db, 0, 0, -1,
        bmh::OtherGamesDisplayMode::detail, 0, true);
    check(auto_advanced.entry_index == 1,
          "a new competition entry follows a completed +0x16 stage exactly once");
    game.memory.set8(bmh::cup_entry_address(0) + 0x1d, 0);
    const auto future = bmh::other_games_from_game(game, db, 0, 0, 1,
        bmh::OtherGamesDisplayMode::detail, 0, false);
    check(!future.fixtures_available && future.rows.empty(),
          "future knockout round suppresses rows and controls");
    const uint32_t knockout_entry = bmh::cup_entry_address(0);
    const uint32_t saved_ko_team_list = game.memory.pointer(knockout_entry);
    const uint32_t saved_ko_results = game.memory.pointer(knockout_entry + 0x0c);
    game.memory.set_pointer(knockout_entry, 0);
    game.memory.set_pointer(knockout_entry + 0x0c, 0);
    rejected = false;
    bmh::OtherGamesProjection blocked_future;
    try { blocked_future = bmh::other_games_from_game(game, db, 0, 0, 1,
        bmh::OtherGamesDisplayMode::detail, 0, false); }
    catch (const bmh::FormatError&) { rejected = true; }
    check(!rejected && !blocked_future.fixtures_available && blocked_future.rows.empty(),
          "future knockout message returns before touching absent fixture/result arrays");
    game.memory.set_pointer(knockout_entry, saved_ko_team_list);
    game.memory.set_pointer(knockout_entry + 0x0c, saved_ko_results);
    game.memory.set8(bmh::cup_entry_address(1) + 0x15, 0);
    const auto pending_stage = bmh::other_games_from_game(game, db, 0, 1, 0,
        bmh::OtherGamesDisplayMode::detail, 0, false);
    check(pending_stage.unfinished_predecessor && !pending_stage.fixtures_available,
          "unfinished predecessor suppresses the selected stage fixtures");
    game.memory.set8(bmh::cup_entry_address(1) + 0x15, 0xff);
    game.memory.set_pointer(bmh::cup_entry_address(2) + 0x10, 0xdead);
    const auto group = bmh::other_games_from_game(game, db, 1, 0, 2,
        bmh::OtherGamesDisplayMode::compact, 0, false);
    check(!group.knockout && group.rows.size() == 2 && group.rows[0].home_team == 4 &&
          group.rows[0].away_team == 7 && group.source_participant_count == 1 &&
          group.group_tables.size() == 1 && group.rows[0].source_second_leg == 0xcccc &&
          !group.rows[0].second_leg_array_present && group.rows[0].source_winner == -1,
          "group rows retain all later-round fixtures and ignore the unused second-result pointer");
    game.memory.set_pointer(bmh::cup_entry_address(2) + 0x10, 0);
    const uint32_t group_entry = bmh::cup_entry_address(2);
    const uint32_t saved_group_team_list = game.memory.pointer(group_entry);
    const uint32_t saved_group_results = game.memory.pointer(group_entry + 0x0c);
    const uint32_t saved_group_schedule = game.memory.pointer(group_entry + 8);
    game.memory.set8(bmh::cup_entry_address(0) + 0x1d, 0); // predecessor remains unfinished.
    game.memory.set8(group_entry + 0x15, 0);
    game.memory.set_pointer(group_entry, 0);
    game.memory.set_pointer(group_entry + 0x0c, 0);
    game.memory.set_pointer(group_entry + 8, 0);
    rejected = false;
    bmh::OtherGamesProjection blocked_group;
    try { blocked_group = bmh::other_games_from_game(game, db, 1, 0, 0,
        bmh::OtherGamesDisplayMode::detail, 0, false); }
    catch (const bmh::FormatError&) { rejected = true; }
    check(!rejected && blocked_group.unfinished_predecessor &&
          !blocked_group.fixtures_available && blocked_group.rows.empty(),
          "unfinished group predecessor returns Source message before team/result/schedule arrays");
    game.memory.set_pointer(group_entry, saved_group_team_list);
    game.memory.set_pointer(group_entry + 0x0c, saved_group_results);
    game.memory.set_pointer(group_entry + 8, saved_group_schedule);
    game.memory.set8(group_entry + 0x15, 0xff);
    game.memory.set8(bmh::cup_entry_address(0) + 0x1d, 2);

    // >16 participants enables Source's detail/compact toggle. Detail windows show the first
    // sixteen participant slots (eight matches); compact mode projects the whole round.
    game.memory.set8(bmh::cup_entry_address(0) + 0x1a, 32);
    game.memory.set8(bmh::cup_entry_address(0) + 0x1b, 5);
    for (int i = 0; i < 32; ++i) game.memory.set16(::bmh::layout::SyntheticCupParticipantsBegin + uint32_t(i) * 2, uint16_t(i));
    const auto detail = bmh::other_games_from_game(game, db, 0, 0, 0,
        bmh::OtherGamesDisplayMode::detail);
    const auto compact = bmh::other_games_from_game(game, db, 0, 0, 0,
        bmh::OtherGamesDisplayMode::compact);
    const auto second_window = bmh::other_games_from_game(game, db, 0, 0, 0,
        bmh::OtherGamesDisplayMode::detail, 16);
    check(detail.display_mode_toggle_available && detail.rows.size() == 8 &&
          compact.rows.size() == 16 && second_window.rows.size() == 8 &&
          second_window.rows.front().home_team == 16,
          "Source compact/detail toggle and sixteen-participant detail paging");
    const auto past_detail_window = bmh::other_games_from_game(game, db, 0, 0, 0,
        bmh::OtherGamesDisplayMode::detail, 32, false);
    const auto retained_compact_window = bmh::other_games_from_game(game, db, 0, 0, 0,
        bmh::OtherGamesDisplayMode::compact, 16, false);
    check(past_detail_window.participant_offset == 32 && past_detail_window.rows.empty() &&
          retained_compact_window.participant_offset == 0 && retained_compact_window.rows.size() == 16,
          "detail retains an exhausted Source cursor while compact draws the complete round");

    // A two-leg descriptor keeps its primary +0x0c score and the reversed +0x10 score.
    // legacy routine reads the latter when present even when its separate detail line is hidden.
    game.memory.set8(bmh::cup_entry_address(0) + 0x14, 0x15);
    game.memory.set_pointer(bmh::cup_entry_address(0) + 0x10, ::bmh::layout::SyntheticCupAlternateResultBlock);
    game.memory.set8(bmh::cup_entry_address(0) + 0x1d, 1);
    game.memory.set16(::bmh::layout::SyntheticCupResultsBegin, uint16_t((1 << 5) | 0));
    game.memory.set16(::bmh::layout::SyntheticCupAlternateResultBlock, uint16_t(1));
    const auto tie_break = bmh::other_games_from_game(game, db, 0, 0, 0,
        bmh::OtherGamesDisplayMode::compact, 0, false);
    check(tie_break.rows[0].source_result == 32 && tie_break.rows[0].source_second_leg == 1 &&
          tie_break.rows[0].second_leg_visible && tie_break.rows[0].source_winner == 0,
          "two-leg row retains both raw scores and Source away-goal winner");
    game.memory.set8(bmh::cup_entry_address(0) + 0x14, 0x05);
    game.memory.set16(::bmh::layout::SyntheticCupResultsBegin, 6);  // 0-6 balances the encoded 12-6 second word.
    game.memory.set16(::bmh::layout::SyntheticCupAlternateResultBlock, 0xcccc);
    const auto tied_result = bmh::other_games_from_game(game, db, 0, 0, 0,
        bmh::OtherGamesDisplayMode::compact, 0, false);
    check(tied_result.rows[0].source_winner == 2 &&
          tied_result.rows[0].source_result == 6 && tied_result.rows[0].source_second_leg == 0xcccc,
          "winner projection retains legacy routine's explicit tied result code and raw score words");
    game.memory.set8(bmh::cup_entry_address(0) + 0x14, 0x15);
    game.memory.set16(::bmh::layout::SyntheticCupResultsBegin, uint16_t((1 << 5) | 0));

    // Source's winner helper reads the second array even on a round where local4c hides the
    // separate second-leg text line. Without the hidden array value this fixture's winner flips.
    game.memory.set8(bmh::cup_entry_address(0) + 0x1d, 2);
    game.memory.set16(::bmh::layout::SyntheticCupResultsBegin + 32, 1);  // primary 0-1
    game.memory.set16(::bmh::layout::SyntheticCupAlternateResultBlock + 32, 2);  // reversed second score 0-2
    const auto hidden_second_leg = bmh::other_games_from_game(game, db, 0, 0, 1,
        bmh::OtherGamesDisplayMode::compact, 0, false);
    check(!hidden_second_leg.rows[0].second_leg_visible &&
          hidden_second_leg.rows[0].second_leg_array_present &&
          hidden_second_leg.rows[0].source_winner == 0,
          "hidden second-leg line still contributes to Source aggregate winner");

    game.memory.set8(::bmh::layout::ActiveSelectors, 12);
    game.memory.set8(::bmh::layout::Cup_RemoveCompetition_RuntimeField, 0x14);
    game.memory.set16(::bmh::layout::CalendarState_FormatError_RuntimeField3, 1000);
    game.memory.set16(::bmh::layout::SyntheticCupFourthResultBlock, 0xfffe);
    const auto scheduled = bmh::other_games_from_game(game, db, 12, 0, 0,
        bmh::OtherGamesDisplayMode::compact);
    check(scheduled.scheduled_day_mode && scheduled.rows[0].kind == bmh::OtherGamesRowKind::scheduled_competition &&
          scheduled.rows[0].scheduled_source_day == 998 && !scheduled.rows[0].played,
          "competition 12 interprets primary words as signed scheduled-day offsets");
    game.memory.set16(::bmh::layout::SyntheticCupFourthResultBlock, 0xffff);
    const auto locked = bmh::other_games_from_game(game, db, 12, 0, 0,
        bmh::OtherGamesDisplayMode::compact);
    check(locked.rows[0].scheduled_source_day == 999 && !locked.rows[0].played,
          "competition 12 treats 0xffff as a signed -1 day offset, not a score/lock sentinel");
    game.memory.set16(::bmh::layout::CalendarState_FormatError_RuntimeField3, 0);
    const auto wrapped_date = bmh::other_games_from_game(game, db, 12, 0, 0,
        bmh::OtherGamesDisplayMode::compact);
    check(wrapped_date.rows[0].scheduled_source_day == 0xffffffffu,
          "scheduled date preserves Source uint32 wrap for date zero plus signed -1");
    game.memory.set16(::bmh::layout::CalendarState_FormatError_RuntimeField3, 0xffff);
    game.memory.set16(::bmh::layout::SyntheticCupFourthResultBlock, 0x7fff);
    const auto wide_date = bmh::other_games_from_game(game, db, 12, 0, 0,
        bmh::OtherGamesDisplayMode::compact);
    check(wide_date.rows[0].scheduled_source_day == 98302u,
          "scheduled date retains positive signed16 offset above 16-bit date range");
    game.memory.set16(::bmh::layout::CalendarState_FormatError_RuntimeField3, 1000);
    game.memory.set16(::bmh::layout::SyntheticCupFourthResultBlock, 0xffff);
    game.manager_teams = {200};
    game.memory.set16(::bmh::layout::SyntheticCupFourthParticipantBlock, 200);
    rejected = false;
    try { (void)bmh::other_games_from_game(game, db, 12, 0, 0,
        bmh::OtherGamesDisplayMode::compact); }
    catch (const bmh::FormatError&) { rejected = true; }
    check(rejected, "manager-controlled foreign-club metadata is refused without Source W context");
    game.manager_teams.clear();
    game.memory.set16(::bmh::layout::SyntheticCupFourthParticipantBlock, 0);

    rejected = false;
    try { (void)bmh::other_games_from_game(game, db, 2, 0, 0); }
    catch (const bmh::FormatError&) { rejected = true; }
    check(rejected, "missing selected competition descriptor is rejected");
    rejected = false;
    try { (void)bmh::other_games_from_game(game, db, 0, 99, 0); }
    catch (const bmh::FormatError&) { rejected = true; }
    check(rejected, "linked page outside actual descriptor chain is rejected");
    game.memory.set8(::bmh::layout::FixtureFirstCupPresentationOption, 2);
    rejected = false;
    try { (void)bmh::other_games_from_game(game, db); }
    catch (const bmh::FormatError&) { rejected = true; }
    check(rejected, "active selector without a live descriptor is rejected");
    game.memory.set8(::bmh::layout::ActiveSelectors, 0);
    game.memory.set8(::bmh::layout::Cup_RemoveCompetition_RuntimeField, 1);
    for (int slot = 2; slot < 8; ++slot) game.memory.set8(::bmh::layout::ActiveSelectors + uint32_t(slot), 0x14);
    const auto sentinel_positive = bmh::other_games_from_game(game, db, 0, 0, 0,
        bmh::OtherGamesDisplayMode::detail, 0, false);
    check(!sentinel_positive.rows.empty(), "valid active-button frame before sentinel mutation");
    game.memory.set16(::bmh::layout::SyntheticCupParticipantsBegin, 0x0fff);
    rejected = false;
    try { (void)bmh::other_games_from_game(game, db, 0, 0, 0,
        bmh::OtherGamesDisplayMode::detail, 0, false); }
    catch (const bmh::FormatError&) { rejected = true; }
    check(rejected, "unproven empty-club row sentinel is rejected instead of inventing a name");
    game.memory.set16(::bmh::layout::SyntheticCupParticipantsBegin, 0);
    const auto sentinel_retry = bmh::other_games_from_game(game, db, 0, 0, 0,
        bmh::OtherGamesDisplayMode::detail, 0, false);
    check(!sentinel_retry.rows.empty(), "restoring only the team word returns the valid projection");
    std::cout << "other-games-projection-tests: Source selectors, linked stages, group fixtures, results, "
                 "detail paging, and invalid-state guards passed\n";
}
