// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/matchday_results_screen.hpp"

#include <functional>
#include <algorithm>
#include <iostream>
#include <map>
#include <set>

#include "app/career_session.hpp"
#include "formats/le.hpp"
#include "ui/setup.hpp"
#include "ui/manager.hpp"
#include "game/cup.hpp"
#include "game/save.hpp"
#include "game/schedule.hpp"
#include "game/match_engine.hpp"
#include "game/turn_driver.hpp"

using namespace bmh;
using namespace bmh::ui;

namespace {
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
void rejected(const std::function<void()>& action) {
    try { action(); } catch (const FormatError&) { return; }
    throw std::runtime_error("expected rejected results input");
}
void set_void_mask(MatchGroup& group, uint32_t mask) {
    group.desc.assign(0xd0, 0);
    group.desc[0xb8] = uint8_t(mask);
    group.desc[0xb9] = uint8_t(mask >> 8);
    group.desc[0xba] = uint8_t(mask >> 16);
    group.desc[0xbb] = uint8_t(mask >> 24);
}
void set_id(MatchGroup& group, uint32_t id) {
    for (int byte = 0; byte < 4; ++byte) group.desc.at(0xcc + size_t(byte)) = uint8_t(id >> (byte * 8));
}
std::string title_oracle(const GameState& game, const LeImage& exe, int league) {
    // Independent Source legacy routine pointer selection from actual EXE suffix
    // bytes, followed by legacy routine; not the production league_name helper.
    if (league >= 5) return original_to_utf8(game.club_name(0x165 + league - 4));
    size_t slot = (::bmh::layout::Achievements_FormatError_TextPointer3 - ::bmh::layout::TextPointerTable) / 4;
    for (int previous = 0; previous < league; ++previous)
        if (exe.u8(::bmh::layout::Manager_FormatError_RuntimeField + uint32_t(previous) * 2) < 'B') ++slot;
    std::string title = game.texts->strings.at(slot);
    if (const auto suffix = exe.u8(::bmh::layout::CareerSession_InstallLeagueSizes_RuntimeField3 + uint32_t(league) * 2)) {
        std::string tail = game.texts->strings.at((::bmh::layout::Manager_FormatError_TextPointer2 - ::bmh::layout::TextPointerTable) / 4);
        tail.back() = char(suffix);
        title += tail;
    }
    return original_to_utf8(title);
}
int random_draws = 0;
void count_draw(int) { ++random_draws; }

void competition_tests(app::CareerSession& session, const LeImage& exe,
                       const MatchdayTeamNameResolver& team_names) {
    GameState game = session.state();
    TextDb edition = *game.texts;
    game.texts = &edition;
    // Distinct second-edition fixture: German glyphs, independent base names,
    // and a different literal suffix template expose any hard-coded labels.
    const std::vector<std::string> names{"Oberliga [st", "Zweite S}d", "Staffel Gr}n", "Region S}d"};
    for (size_t i = 0; i < names.size(); ++i)
        edition.strings.at((::bmh::layout::Achievements_FormatError_TextPointer3 - ::bmh::layout::TextPointerTable) / 4 + i) = names[i];
    edition.strings.at((::bmh::layout::Manager_FormatError_TextPointer2 - ::bmh::layout::TextPointerTable) / 4) = " /X";
    TurnResultSnapshot snapshot;
    snapshot.valid = true;
    snapshot.calendar_entry = 0x4006;
    // Deliberately differs from current state and includes foreign leagues:
    // group identity, rather than calendar/current round, controls each label.
    for (int league = 0; league < kLeagueCount; ++league) {
        MatchGroup source;
        set_void_mask(source, 0);
        set_id(source, uint32_t(league));
        source.fixtures = {12, 13};
        source.results = {0x21};
        snapshot.groups.push_back(source);
    }
    const auto native_before = save_stream(game);
    const auto hook_before = draw_hook;
    draw_hook = count_draw;
    random_draws = 0;
    const auto projected = matchday_results_from_snapshot(snapshot, team_names, exe, game);
    std::set<std::string> domestic;
    for (int league = 0; league < kLeagueCount; ++league) {
        const auto& group = projected.groups.at(size_t(league));
        check(group.league_id == league && !group.cup_id && !group.cup_entry_id && !group.cup,
              "all nine league IDs follow descriptor +0xcc, not snapshot calendar");
        check(group.competition == title_oracle(game, exe, league),
              "localized league title matches independent EXE/TEXTD/Native oracle");
        if (league < 5) domestic.insert(group.competition);
    }
    check(domestic.size() == 5 && projected.groups[2].competition == "Staffel Grün /A" &&
          projected.groups[3].competition == "Staffel Grün /B",
          "five simultaneous leagues stay distinct with edition suffix template and UTF-8");
    check(save_stream(game) == native_before && random_draws == 0,
          "localized league projection preserves whole Native stream and consumes no RNG");
    const auto compatibility = matchday_results_from_snapshot(snapshot, team_names, exe);
    check(compatibility.groups[3].competition.empty() && compatibility.groups[3].league_id == 3,
          "legacy projection keeps IDs and does not invent a competition name");

    snapshot.groups.clear();
    const auto frozen_name = utf8_to_original("In München");
    snapshot.tournament_name.assign(frozen_name.begin(), frozen_name.end());
    snapshot.tournament_name.push_back(0);
    const auto future_name = utf8_to_original("In Köln");
    game.tournament_name.assign(future_name.begin(), future_name.end());
    game.tournament_name.push_back(0);
    edition.strings.at((::bmh::layout::MatchdayResults_CompetitionName_TextPointer - ::bmh::layout::TextPointerTable) / 4) = " - Hallenpokal";
    // Every actual Original cup entry, including national/European/supercup,
    // replays, friendlies and all parallel stages. No guessed cup-ID mapping.
    std::set<int> cup_types;
    for (int entry = 0; entry < kCupEntries; ++entry) {
        const auto at = cup_entry_address(entry);
        const int type = int(exe.u16(at + 0x18));
        check(game.memory.u16(at + 0x18) == type, "Native cup ID matches Original table bytes");
        cup_types.insert(type);
        if (type != 0xb)
            edition.strings.at((::bmh::layout::DeskState_FormatError_ExecutableField3 - ::bmh::layout::TextPointerTable) / 4 + size_t(type + 1)) =
                "Pokal {st " + std::to_string(type);
        MatchGroup source;
        set_void_mask(source, 0);
        source.desc[0] = uint8_t(type + 1);
        set_id(source, at);
        source.fixtures = {12, 13};
        // Empty results deliberately prove cup identity is descriptor-based.
        snapshot.groups.push_back(source);
    }
    const auto cup_native_before = save_stream(game);
    const auto cups = matchday_results_from_snapshot(snapshot, team_names, exe, game);
    for (int entry = 0; entry < kCupEntries; ++entry) {
        const int id = int(exe.u16(cup_entry_address(entry) + 0x18));
        const auto& group = cups.groups.at(size_t(entry));
        const auto expected = id == 0xb ? std::string("In München - Hallenpokal") :
            original_to_utf8(edition.strings.at((::bmh::layout::DeskState_FormatError_ExecutableField3 - ::bmh::layout::TextPointerTable) / 4 + size_t(id + 1)));
        check(group.cup && !group.league_id && group.cup_id == id && group.cup_entry_id == entry &&
              group.competition == expected && group.matches[0].status == MatchResultStatus::unknown,
              "all Original cup stages retain table/type identities and effective edition title");
    }
    check(cup_types.count(0) && cup_types.count(0xb), "national cup and runtime indoor title covered");
    check(save_stream(game) == cup_native_before && random_draws == 0,
          "localized cup projection preserves whole Native stream and consumes no RNG");
    draw_hook = hook_before;

    auto broken = snapshot;
    broken.groups[0].desc[0] = uint8_t(broken.groups[0].desc[0] + 1);
    rejected([&] { matchday_results_from_snapshot(broken, team_names, exe, game); });
    broken = snapshot; set_id(broken.groups[0], kCupTable + 1);
    rejected([&] { matchday_results_from_snapshot(broken, team_names, exe, game); });
    broken = snapshot; set_id(broken.groups[0], kCupTable + kCupEntries * kCupEntrySize);
    rejected([&] { matchday_results_from_snapshot(broken, team_names, exe, game); });
    broken = snapshot; broken.groups[0].desc.resize(0xbc);
    rejected([&] { matchday_results_from_snapshot(broken, team_names, exe, game); });
    broken = snapshot; broken.groups[0].desc[0] = 0; set_id(broken.groups[0], kLeagueCount);
    rejected([&] { matchday_results_from_snapshot(broken, team_names, exe, game); });
    broken = snapshot; broken.tournament_name.clear();
    rejected([&] { matchday_results_from_snapshot(broken, team_names, exe, game); });
    game.texts = nullptr;
    rejected([&] { matchday_results_from_snapshot(snapshot, team_names, exe, game); });
    game.texts = &edition;
    game.memory.set16(kCupTable + 0x18, 0xff);
    rejected([&] { matchday_results_from_snapshot(snapshot, team_names, exe, game); });
    check(!native_before.empty(), "whole Native serialization available for read-only comparisons");
    std::cout << "Edition fixture: 9 leagues, 36 cup entries, " << cup_types.size()
              << " cup IDs; malformed identity rejection and frozen indoor name passed.\n";
}
}

int main(int argc, char** argv) {
    try {
        if (argc < 2) throw std::runtime_error("usage: ui_matchday_results_tests <BMH data>");
        const auto exe = load_le(asset_path(argv[1], "BMH.exe"));
        app::CareerSession session(argv[1], 8128);
        SetupOptions options;
        ManagerChoice choice;
        choice.name = "SIM"; choice.master_club = 0; choice.strength = 2;
        std::string error;
        check(session.confirm_manager(options, choice, error), "fresh manager setup failed");
        CareerInitializationResult initialized;
        check(session.finish(initialized, error), "fresh career initialization failed");
        const auto initial_checkpoint = session.manager_join_checkpoint();
        TurnResultSnapshot snapshot;
        snapshot.valid = true; snapshot.date = 0; snapshot.calendar_entry = 7; snapshot.matchday = 12;

        // As after a real matchday: desc+0xb8 holds the ended-match bits of every played
        // match (finish_group_phase -> set_void); the match that did not take place is in
        // the kickoff mask (local_70). Issue #19: the projection must not read +0xb8.
        MatchGroup league; set_void_mask(league, 0x3f);
        league.kickoff_void_mask = 1u << 4;
        league.fixtures = {12,13, 14,15, 16,17, 18,19, 20,21, 22,23};
        league.results = {0x21, 0xcc, 0x34, 0x56, 0x10, 0x05};
        snapshot.groups.push_back(league);

        MatchGroup cup; set_void_mask(cup, 0); cup.desc[0] = 1;
        set_id(cup, cup_entry_address(find_cup(session.state().memory, 0)));
        cup.fixtures = {12,14, 13,15};
        cup.cup_results = {uint16_t((2 << 5) | 1), 0xcccc};
        snapshot.groups.push_back(cup);

        MatchGroup unknown; set_void_mask(unknown, 0);
        unknown.fixtures = {18,19};
        snapshot.groups.push_back(unknown);

        const std::map<int,std::string> teams{{12,"München"},{13,"Dortmund"},{14,"Bremen"},
            {15,"Hamburg"},{16,"Koeln"},{17,"Mainz"},{18,"Leipzig"},{19,"Freiburg"},
            {20,"Stuttgart"},{21,"Bochum"},{22,"Essen"},{23,"Aachen"}};
        const auto resolver = [&](int id) {
            const auto it = teams.find(id);
            return it == teams.end() ? std::string{} : it->second;
        };
        auto results = matchday_results_from_snapshot(snapshot, resolver, exe, session.state());
        check(results.valid && results.matchday == 12 && results.calendar_entry == 7 &&
              !results.date_label.empty(), "snapshot header and date copied");
        check(results.groups.size() == 3 && results.groups[0].matches.size() == 6 &&
              results.groups[0].matches[0].home == "München" &&
              results.groups[0].matches[0].home_goals == 2 &&
              results.groups[0].matches[0].away_goals == 1, "league order and packed score decoded");
        check(results.groups[0].matches[1].status == MatchResultStatus::postponed &&
              !results.groups[0].matches[1].home_goals, "postponed score sentinel remains unscored");
        check(results.groups[0].matches[4].status == MatchResultStatus::not_played,
              "kickoff-void game remains explicitly unplayed");
        check(results.groups[0].matches[2].status == MatchResultStatus::played &&
              results.groups[0].matches[2].home_goals == 3 && results.groups[0].matches[5].status == MatchResultStatus::played,
              "ended-match bits in desc+0xb8 do not hide played results (#19)");
        check(results.groups[1].cup && results.groups[1].matches[0].home_goals == 2 &&
              results.groups[1].matches[0].away_goals == 1 &&
              results.groups[1].matches[1].status == MatchResultStatus::not_played,
              "cup scores use the recorded leg only; missing leg is not invented");
        check(results.groups[2].matches[0].status == MatchResultStatus::unknown,
              "missing result storage stays unknown");
        check(results.groups[0].competition == title_oracle(session.state(), exe, 0) &&
              results.groups[0].league_id == 0 && results.groups[1].cup_id == 0 &&
              results.groups[1].competition == original_to_utf8(session.state().texts->strings.at((::bmh::layout::CupDraw_FormatError_ExecutableField6 - ::bmh::layout::TextPointerTable) / 4)),
              "stock league and national cup titles come from actual edition texts");
        competition_tests(session, exe, resolver);

        MatchdayResultsScreen screen(argv[1], results);
        check(screen.page_count() == 2 && screen.page() == 0, "all matches page across results");
        const auto pixels = screen.render().pixels;
        check(pixels == screen.render().pixels, "repeated rendering is stable");
        const auto font = load_font(asset_path(std::filesystem::path(argv[1]) / "F", "BMH3.FNT"));
        Image expected_glyphs(320, 240);
        std::fill(expected_glyphs.pixels.begin(), expected_glyphs.pixels.end(), 0x15);
        font.draw(expected_glyphs, 114, 68, "M}nchen", 0x4f);
        for (int y = 68; y < 68 + font.height; ++y)
            for (int x = 114; x < 189; ++x)
                check(pixels[size_t(y) * 320 + x] == expected_glyphs.at(x, y),
                      "UTF-8 club name uses the original umlaut glyph at the font boundary");
        check(screen.key("RIGHT") && screen.page() == 1, "keyboard reaches later result groups");
        check(screen.click(30,214) && screen.page() == 0, "mouse returns to earlier results");
        check(screen.click(200,214) && screen.page() == 1, "mouse advances through remaining results");
        const auto saved_results = screen.results().groups[0].matches[0].home_goals;
        check(screen.click(270,214) && screen.close_requested() &&
              screen.results().groups[0].matches[0].home_goals == saved_results,
              "end closes only the display and preserves its data");
        auto invalid = results; invalid.valid = false;
        rejected([&] { MatchdayResultsScreen reject(argv[1], invalid); });
        const auto unresolved = matchday_results_from_snapshot(snapshot, resolver, exe);
        rejected([&] { MatchdayResultsScreen reject(argv[1], unresolved); });

        uint64_t revision = initialized.desktop.revision;
        bool five_leagues_played = false;
        for (int turn = 0; turn < 16 && !five_leagues_played; ++turn) {
            const auto desk = session.desktop_projection(revision);
            DeskRequest request;
            request.kind = DeskRequestKind::advance_turn;
            request.manager_slot = desk.manager_slot; request.revision = revision;
            const auto advanced = session.dispatch_kickoff(request, revision, error);
            check(advanced.kind != TurnDeskKind::Failed, "fresh career failed before real result projection");
            std::set<int> domestic_ids;
            if (session.completed_matchday_results())
                for (const auto& group : session.completed_matchday_results()->groups)
                    if (group.league_id && *group.league_id < 5) domestic_ids.insert(*group.league_id);
            five_leagues_played = domestic_ids.size() == 5;
        }
        check(five_leagues_played, "fresh career produces five simultaneous domestic league results");
        const auto native_before = save_stream(session.state());
        const auto game_seed = session.game().seed();
        const auto watcom_seed = session.rand().seed();
        const auto game_calls = session.game().calls();
        const auto watcom_calls = session.rand().calls();
        const auto hook_before = draw_hook;
        draw_hook = count_draw; random_draws = 0;
        const auto& real_snapshot = session.last_turn_results();
        const auto real = matchday_results_from_snapshot(real_snapshot, [&](int team) {
            return original_to_utf8(session.state().club_name(team & 0xfff));
        }, exe, session.state());
        check(real.groups.size() == real_snapshot.groups.size(), "real snapshot groups stay in Source order");
        std::set<int> real_leagues;
        for (size_t index = 0; index < real.groups.size(); ++index) {
            const auto& projected = real.groups[index];
            const auto& app_projected = session.completed_matchday_results()->groups[index];
            check(projected.competition == app_projected.competition && projected.league_id == app_projected.league_id &&
                  projected.cup_id == app_projected.cup_id && projected.cup_entry_id == app_projected.cup_entry_id,
                  "production App uses authoritative competition labels and IDs");
            if (projected.league_id) {
                check(projected.competition == title_oracle(session.state(), exe, *projected.league_id),
                      "real played league results match independent edition-name oracle");
                real_leagues.insert(*projected.league_id);
            }
        }
        MatchdayResultsScreen real_screen(argv[1], real);
        real_screen.render(); real_screen.key("RIGHT"); real_screen.render(); real_screen.key("ESC");
        check(save_stream(session.state()) == native_before && random_draws == 0 &&
              session.game().seed() == game_seed && session.rand().seed() == watcom_seed &&
              session.game().calls() == game_calls && session.rand().calls() == watcom_calls,
              "real projection/render/navigation preserve complete Native bytes and both RNG states/counts");
        draw_hook = hook_before;

        const auto frozen_title = real_snapshot.tournament_name;
        const auto frozen_groups = real_snapshot.groups;
        DeskRequest stale;
        stale.kind = DeskRequestKind::advance_turn;
        stale.manager_slot = session.desktop_projection(revision).manager_slot;
        stale.revision = revision + 1;
        check(session.dispatch_kickoff(stale, revision, error).kind == TurnDeskKind::Failed &&
              session.last_turn_results().tournament_name == frozen_title &&
              session.last_turn_results().groups.size() == frozen_groups.size() &&
              session.last_turn_results().groups.front().desc == frozen_groups.front().desc &&
              save_stream(session.state()) == native_before &&
              session.game().calls() == game_calls && session.rand().calls() == watcom_calls,
              "failed advance preserves existing snapshot title/groups, Native state and RNG");
        std::cout << "Fresh career: " << real.groups.size() << " Source groups, " << real_leagues.size()
                  << " league IDs; production App and whole-Native/RNG invariants passed.\n";
        session.restore_manager_join_checkpoint(initial_checkpoint);
        revision = initialized.desktop.revision;
        const auto old_title_bytes = utf8_to_original("In München");
        const auto next_title_bytes = utf8_to_original("In Köln");
        Bytes before_match(old_title_bytes.begin(), old_title_bytes.end()); before_match.push_back(0);
        Bytes after_match(next_title_bytes.begin(), next_title_bytes.end()); after_match.push_back(0);
        bool renamed_during_match = false;
        session.set_live_minute([&](int minute, MatchEngine&, std::vector<MatchGroup>&, GameState& state) {
            if (minute == 90) { state.tournament_name = after_match; renamed_during_match = true; }
        });
        for (int turn = 0; turn < 16 && !renamed_during_match; ++turn) {
            session.state().tournament_name = before_match;
            const auto desk = session.desktop_projection(revision);
            DeskRequest request;
            request.kind = DeskRequestKind::advance_turn;
            request.manager_slot = desk.manager_slot; request.revision = revision;
            check(session.dispatch_kickoff(request, revision, error).kind != TurnDeskKind::Failed,
                  "real title capture turn failed");
        }
        check(renamed_during_match && session.last_turn_results().tournament_name == before_match &&
              session.state().tournament_name == after_match,
              "real TurnDriver captures owned pre-match name before finish-event/current-state changes");
        std::cout << "Real TurnDriver pre-match title capture passed.\n";
        std::cout << "Snapshot projection and paged matchday results passed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
