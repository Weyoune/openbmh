// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/league_table.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>

#include "app/career_session.hpp"
#include "game/messages.hpp"
#include "game/schedule.hpp"
#include "game/table.hpp"
#include "ui/desktop.hpp"
#include "ui/manager.hpp"
#include "ui/setup.hpp"

using namespace bmh;
using namespace bmh::ui;

namespace {
int counted_draws = 0;
void count_draw(int) { ++counted_draws; }
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
void rejected(const std::function<void()>& action) {
    try { action(); } catch (const FormatError&) { return; }
    throw std::runtime_error("expected rejected league table input");
}

std::string original_league_title(const GameState& game, const LeImage& exe, int league) {
    check(game.texts != nullptr, "independent title oracle needs Native TEXTD strings");
    if (league < 5) {
        // legacy routine chooses the shared domestic TEXTD row from the EXE's
        // following suffix bytes. legacy routine appends this league's suffix
        // through the literal TEXTD suffix template.
        size_t text = size_t((::bmh::layout::Achievements_FormatError_TextPointer3 - ::bmh::layout::TextPointerTable) / 4);
        for (int previous = 0; previous < league; ++previous)
            if (exe.u8(::bmh::layout::Manager_FormatError_RuntimeField + uint32_t(previous) * 2) < 'B') ++text;
        std::string name = (*game.texts)[text];
        const uint8_t suffix = exe.u8(::bmh::layout::CareerSession_InstallLeagueSizes_RuntimeField3 + uint32_t(league) * 2);
        if (suffix != 0) {
            std::string tail = (*game.texts)[size_t((::bmh::layout::Manager_FormatError_TextPointer2 - ::bmh::layout::TextPointerTable) / 4)];
            check(!tail.empty(), "Original TEXTD league suffix template is present");
            tail.back() = char(suffix);
            name += tail;
        }
        return original_to_utf8(name);
    }
    // legacy routine maps foreign league n to Native nation/team 0x165+n-4.
    return original_to_utf8(game.club_name(0x165 + league - 4));
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: ui_league_table_tests <BMH data>");
        const std::filesystem::path data(argv[1]);
        app::CareerSession session(data, 8128);
        SetupOptions options;
        ManagerChoice choice;
        choice.name = "SIM";
        choice.master_club = 0;
        choice.strength = 2;
        std::string error;
        check(session.confirm_manager(options, choice, error), "engine manager setup failed");
        CareerInitializationResult initialized;
        check(session.finish(initialized, error), "engine career initialization failed");
        // Keep the declared 60-event table fixture independent of F6 desk filtering.
        session.state().memory.set8(::bmh::layout::MatchdaySetup_MatchdayNeedsManagerDesks_RuntimeField,1);
        uint64_t revision = initialized.desktop.revision;
        for (int turn = 0; turn < 60; ++turn) {
            const auto desk = session.desktop_projection(revision);
            DeskRequest request;
            request.kind = DeskRequestKind::advance_turn;
            request.manager_slot = desk.manager_slot;
            request.revision = revision;
            const auto advanced = session.dispatch_kickoff(request, revision, error);
            check(advanced.kind != TurnDeskKind::Failed, "engine turn failed before table projection");
        }

        const int manager = session.desktop_projection(revision).manager_slot;
        const auto exe = load_le(asset_path(data, "BMH.exe"));
        const auto& game = session.state();
        const auto old_draw_hook = draw_hook;
        counted_draws = 0;
        draw_hook = count_draw;
        const auto tables = league_tables_from_game(game, manager);
        int played_games = 0;
        for (int league = 0; league < kLeagueCount; ++league) {
            const auto& actual = tables[size_t(league)];
            const auto reference = compute_league_table(game, league, 0, league_matchdays(league), 7, 1);
            check(actual.manager_slot == manager && actual.league_id == league &&
                  actual.rows.size() == size_t(kLeagueSizes[league]) && actual.points_mode == 1,
                  "all leagues project an owned state with the engine's configured points rule");
            check(!actual.league_name.empty() && actual.league_name == original_league_title(game, exe, league),
                  "league heading follows the independent EXE-suffix/TEXTD/Nation-ID oracle");
            std::array<int, 20> order{};
            for (int club = 0; club < reference.size; ++club) order[size_t(club)] = club;
            std::sort(order.begin(), order.begin() + reference.size,
                [&](int a, int b) { return reference.rank(a) < reference.rank(b); });
            for (int row_index = 0; row_index < reference.size; ++row_index) {
                const int club = order[size_t(row_index)];
                const int runtime = league_first_team(league) + club;
                const auto& row = actual.rows[size_t(row_index)];
                check(row.rank == reference.rank(club) + 1 && row.runtime_team_id == runtime &&
                      row.club_identity_key == game.club_ids[size_t(runtime)] &&
                      row.name == original_to_utf8(game.club_name(runtime)) &&
                      row.games == reference.games(club) && row.wins == reference.wins[size_t(club)] &&
                      row.draws == reference.games(club) - reference.wins[size_t(club)] - reference.defeats[size_t(club)] &&
                      row.losses == reference.defeats[size_t(club)] && row.goals_for == reference.goals_for[size_t(club)] &&
                      row.goals_against == reference.goals_against[size_t(club)] &&
                      row.points == reference.points[size_t(club)],
                      "owned row preserves actual engine rank, identity, name and aggregate data");
                played_games += row.games;
            }
        }
        // Every domestic title was compared above with an independent reader of
        // the externally supplied edition's text and suffix tables.

        GameState localized = game;
        constexpr int localized_nation = 0x166; // foreign league 5: 0x165 + 5 - 4
        const std::string localized_bytes = utf8_to_original("Österreich");
        check(localized.club_names.size() <= 0xffffu, "Native name pool leaves a representable offset");
        const uint16_t localized_offset = uint16_t(localized.club_names.size());
        localized.club_names.insert(localized.club_names.end(), localized_bytes.begin(), localized_bytes.end());
        localized.club_names.push_back(0);
        localized.club_ids.at(size_t(localized_nation)) = localized_offset;
        const auto localized_table = league_table_from_game(localized, manager, 5);
        check(localized_table.league_name == "Österreich" &&
              localized_table.league_name == original_league_title(localized, exe, 5),
              "foreign title follows a localized Native nation name, not a TEXTD league slot");
        check(played_games > 0, "60 deterministic engine turns yield played league-table rows");
        check(counted_draws == 0, "table projection consumes no random values");

        auto display_tables = tables;
        display_tables[0].rows[0].name = "München";
        LeagueTableScreen screen(data, display_tables, 0);
        check(screen.selected_league() == 0 && !screen.close_requested(), "screen starts on its explicit league");
        const auto rendered = screen.render();
        check(rendered.width == 320 && rendered.height == 240 && rendered.pixels == screen.render().pixels,
              "native-sized table render is stable");
        const auto font = load_font(asset_path(data / "F", "BMH3.FNT"));
        Image expected = load_vga(asset_path(data / "KB", "129.VGA"));
        const auto background = load_vga(asset_path(data / "KB", "56.VGA"));
        const auto mode5 = SpriteFont(data);
        const int title_width = mode5.text_width(display_tables[0].league_name);
        check(SpriteFont::centered_title_x(9) == 223,
              "mode-5 title uses source integer-division order for odd widths");
        mode5.draw(expected, SpriteFont::centered_title_x(title_width),
                   13, display_tables[0].league_name);
        const int crop_height = int(display_tables[0].rows.size()) * 7 + 9;
        for (int y = 0; y < crop_height; ++y)
            for (int x = 0; x < 318; ++x)
                expected.pixels[size_t(y + 21) * 320 + x + 1] = background.pixels[size_t(y + 1) * background.width + x];
        font.draw(expected, 30, 0x23 - font.baseline, "M}nchen", display_tables[0].rows[0].name_color);
        const auto& first_row = display_tables[0].rows[0];
        uint8_t expected_name_color = 0x5f;
        bool matched_manager = false;
        const int active_manager = game.memory.has(::bmh::layout::ActiveManager) ? game.memory.u8(::bmh::layout::ActiveManager) : -1;
        const int manager_count = game.memory.has(::bmh::layout::ManagerCount) ? game.memory.u8(::bmh::layout::ManagerCount) : int(game.manager_teams.size());
        const bool current_human = game.memory.has(::bmh::layout::DeskPosition) && game.memory.u8(::bmh::layout::DeskPosition) < manager_count;
        for (size_t slot = 0; slot < game.manager_teams.size() && slot < 4; ++slot) {
            const size_t record = slot * 0xb0c;
            if (record + 0x291 >= game.manager_records.size() || game.manager_records.at(record + 0xa8) == 0) continue;
            const int team = game.manager_teams.at(slot);
            if (team == first_row.runtime_team_id) {
                expected_name_color = int(slot) == active_manager && current_human ? 0x7a : 0x75;
                matched_manager = true;
                break;
            }
        }
        if (!matched_manager && game.memory.has(::bmh::layout::AlltimeTable_TeamColor_RuntimeField) && game.memory.u16(::bmh::layout::AlltimeTable_TeamColor_RuntimeField) == first_row.runtime_team_id)
            expected_name_color = 0xf7;
        check(first_row.name_color == expected_name_color,
              "name emphasis follows the manager/world color-source rules");
        const int baseline = 0x23;
        const std::string wuv = format_number(game, first_row.games, 0x82) + exe.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer4) +
            format_number(game, first_row.wins, 2) + exe.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer5) +
            format_number(game, first_row.draws, 2) + exe.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer5) +
            format_number(game, first_row.losses, 2) + exe.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer4);
        const std::string points = exe.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer6) + exe.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer7) +
            format_number(game, first_row.points, 2) + exe.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer8) + exe.cstring(::bmh::layout::LeagueTable_Glyph_TextPointer);
        const int difference = first_row.goals_for - first_row.goals_against;
        const std::string goals = format_number(game, first_row.goals_for, 0x83) + exe.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer9) +
            format_number(game, first_row.goals_against, 3) + exe.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer10) +
            exe.cstring(difference < 0 ? ::bmh::layout::Bank_OriginalToUtf8_ExecutableField5 : difference > 0 ? ::bmh::layout::Bank_OriginalToUtf8_ExecutableField6 : ::bmh::layout::Bank_OriginalToUtf8_ExecutableField7) +
            format_number(game, std::abs(difference), 3) + exe.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer11);
        font.draw(expected, 0x72, baseline - font.baseline, wuv, 0x6a);
        font.draw(expected, 0xce, baseline - font.baseline, points, 0x6a);
        font.draw(expected, 0xff, baseline - font.baseline, goals, 0x6a);
        for (int y = baseline - font.baseline; y < baseline - font.baseline + font.height; ++y)
            for (int x = 30; x < 108; ++x)
                check(rendered.pixels[size_t(y) * 320 + x] == expected.at(x, y),
                      "source-cropped VGA backdrop, source row baseline and UTF-8 font boundary are preserved");
        for (const auto [left, right] : {std::pair{0x72,0xce}, std::pair{0xce,0xff}, std::pair{0xff,319}})
            for (int y = baseline - font.baseline; y < baseline - font.baseline + font.height; ++y)
                for (int x = left; x < right; ++x)
                    check(rendered.pixels[size_t(y) * 320 + x] == expected.at(x, y),
                          "original numeric widths, separators and column coordinates are preserved");
        check(rendered.pixels.front() == expected.pixels.front() &&
              rendered.pixels.back() == expected.pixels.back(),
              "KB/129 desktop backdrop remains outside the exact legacy routine panel crop");
        const int bottom = int(screen.state().rows.size()) * 7 + 30;
        check(rendered.pixels[20 * 320] == 0x46 && rendered.pixels[20 * 320 + 319] == 0x40 &&
              rendered.pixels[bottom * 320 + 100] == 0x40,
              "original panel border surrounds the same crop in both display modes");
        auto flat_tables = tables;
        for (auto& t : flat_tables) { t.original_table_panel = false; t.decorated_background = false; }
        LeagueTableScreen flat_screen(data, flat_tables, 0);
        const auto flat = flat_screen.render();
        check(flat.pixels[28 * 320 + 200] == 0x42 && flat.pixels[20 * 320] == 0x46 &&
              flat.pixels[20 * 320 + 319] == 0x40 && flat.pixels[5 * 320 + 1] == 0x39 &&
              flat.pixels[238 * 320 + 100] == 0,
              "saved undecorated/flat options preserve Original panel fill, borders and outer frame");
        for (int y = 13 - 6; y < 13 + 1; ++y)
            for (int x = 172; x < 282; ++x)
                check(rendered.pixels[size_t(y) * 320 + x] == expected.at(x, y),
                      "league name is rendered at the source title position over KB/129");

        const auto initial_json = league_table_json(screen.state());
        check(initial_json.find("\"name\":\"München\"") != std::string::npos,
              "owned JSON retains UTF-8 names");
        check(!screen.click(50, 180) && screen.selected_league() == 0,
              "no unsupported mouse hitbox is exposed");
        check(screen.key("LEFT") && screen.selected_league() == kLeagueCount - 1 &&
              screen.key("RIGHT") && screen.selected_league() == 0,
              "local left/right paging changes only the selected owned league projection");
        check(league_table_json(screen.state()) == initial_json && counted_draws == 0,
              "rendering and ignored input remain presentation-only and drawless");
        check(screen.key("ESC") && screen.close_requested() && !screen.key("RIGHT"),
              "escape closes the screen without dispatching an engine action");

        LeagueTableScreen picker(data,tables,0,LeagueTableScreen::OpponentMode{});
        bool selected_third=false;
        for (int y=176;y<=216&&!selected_third;++y) {
            for (int x=205;x<=260;++x) {
                picker.pointer_down(x,y);
                if (picker.selected_league()!=3) continue;
                std::cout << "Opponent selector league3: " << x << ',' << y << '\n';
                selected_third=true;break;
            }
        }
        check(selected_third && !picker.pending_family_action() && !picker.close_requested(),
              "Source opponent league map selects an owned league without a family transaction");
        bool toggled=false;
        for (int y=176;y<=216&&!toggled;++y) {
            for (int x=262;x<=317;++x) {
                if (!picker.pointer_down(x,y)) continue;
                check(picker.selected_league()==5 && !picker.pending_family_action(),
                      "Source opponent country selector opens the foreign base league");
                check(picker.pointer_down(x,y) && picker.selected_league()==0,
                      "returning from foreign group restores the opponent picker's manager default league");
                std::cout << "Opponent country selector: " << x << ',' << y << '\n';
                toggled=true;break;
            }
        }
        check(toggled && picker.state()==tables[0] && counted_draws==0,
              "map and country paging preserve the immutable projections and consume no random draws");
        check(picker.pointer_down(162,207) && !picker.close_requested() && picker.click(162,207) &&
              picker.close_requested() && !picker.pending_family_action(),
              "mode2 footer closes only after matching button release, without a family request");

        StrengthsIdentity family_identity;
        family_identity.manager = manager;
        family_identity.runtime_club = game.manager_teams.at(size_t(manager));
        family_identity.club_identity = int(game.club_ids.at(size_t(family_identity.runtime_club)));
        family_identity.league = 0;
        family_identity.day_counter = game.date;
        family_identity.league_group = 0;
        family_identity.manager_default_league = game.manager_records.at(size_t(manager) * 0xb0c + 0x210);
        family_identity.revision = 17;
        family_identity.scope = 9;
        family_identity.sequence = 0;
        family_identity.expected_family_word = tables[0].display_word;
        LeagueTableScreen family_screen(data, tables, 0,
            LeagueTableScreen::FamilyMode{family_identity});
        const auto family_image = family_screen.render();
        check(family_image.width == 320 && family_image.height == 240,
              "family mode renders the Original 12-button footer and league selectors at screen size");
        const auto family_texts = load_textd(asset_path(data, "TEXTD.BLK"));
        const auto family_title = original_to_utf8(family_texts[(::bmh::layout::LeagueTable_OriginalToUtf8_TextPointer - ::bmh::layout::TextPointerTable) / 4]) + tables[0].league_name;
        const int raw_family_round = game.league_rounds.at(0);
        const int family_round = (raw_family_round & 0x7f) + 1;
        const auto family_subtitle = original_to_utf8(
            message_text(game, raw_family_round & 0x80 ? ::bmh::layout::LeaguePairings_FormatError_TextPointer4 : ::bmh::layout::CareerSession_FormatError_TextPointer2) +
            format_number(game, family_round, 1) + exe.cstring(::bmh::layout::CareerSession_FormatError_TextPointer) + message_text(game, ::bmh::layout::CareerSession_FormatError_TextPointer4));
        Image expected_family_headings = load_vga(asset_path(data / "KB", "129.VGA"));
        mode5.draw(expected_family_headings, SpriteFont::centered_title_x(mode5.text_width(family_title)),
                   13, family_title);
        mode5.draw(expected_family_headings, 5, 235, family_subtitle);
        for (int y = 7; y < 20; ++y)
            for (int x = 0; x < 320; ++x)
                check(family_image.pixels[size_t(y) * 320 + x] == expected_family_headings.pixels[size_t(y) * 320 + x],
                      "family title uses legacy routine's German TextD prefix and league name");
        for (int y = 229; y < 240; ++y)
            for (int x = 0; x < 320; ++x)
                check(family_image.pixels[size_t(y) * 320 + x] == expected_family_headings.pixels[size_t(y) * 320 + x],
                      "family round subtitle uses Source TextD, high-bit selector and EXE number separator");
        auto flagged_round_tables = tables;
        flagged_round_tables[0].rounds_played |= 0x80;
        LeagueTableScreen flagged_round_screen(data, flagged_round_tables, 0,
            LeagueTableScreen::FamilyMode{family_identity});
        const auto flagged_round_image = flagged_round_screen.render();
        Image expected_flagged_subtitle = load_vga(asset_path(data / "KB", "129.VGA"));
        const auto flagged_subtitle = original_to_utf8(
            message_text(game, ::bmh::layout::LeaguePairings_FormatError_TextPointer4) + format_number(game, family_round, 1) +
            exe.cstring(::bmh::layout::CareerSession_FormatError_TextPointer) + message_text(game, ::bmh::layout::CareerSession_FormatError_TextPointer4));
        mode5.draw(expected_flagged_subtitle, 5, 235, flagged_subtitle);
        for (int y = 229; y < 240; ++y)
            for (int x = 0; x < 320; ++x)
                check(flagged_round_image.pixels[size_t(y) * 320 + x] == expected_flagged_subtitle.pixels[size_t(y) * 320 + x],
                      "saved round high-bit selects legacy routine's alternate round fragment");
        auto footer_word_set = family_identity;
        footer_word_set.expected_family_word |= 0x80;
        auto footer_word_clear = family_identity;
        footer_word_clear.expected_family_word &= uint16_t(~0x80);
        LeagueTableScreen footer_word_set_screen(data, tables, 0,
            LeagueTableScreen::FamilyMode{footer_word_set});
        LeagueTableScreen footer_word_clear_screen(data, tables, 0,
            LeagueTableScreen::FamilyMode{footer_word_clear});
        const auto footer_set_image = footer_word_set_screen.render();
        const auto footer_clear_image = footer_word_clear_screen.render();
        for (int y = 215; y <= 225; ++y)
            for (int x = 66; x <= 129; ++x)
                check(footer_set_image.pixels[size_t(y) * 320 + x] == footer_clear_image.pixels[size_t(y) * 320 + x],
                      "legacy routine masks display bit 7 before drawing the Norm. Modus footer control");
        check(!family_screen.click(162, 207) && !family_screen.pending_family_action(),
              "family semantic action cannot be activated without a physical button-down");
        check(family_screen.pointer_down(162, 207) && !family_screen.pending_family_action(),
              "footer pointer-down provides pressed state without dispatching its family action");
        check(family_screen.render().pixels != family_image.pixels && family_screen.move(230, 220) &&
              !family_screen.click(162, 207) && !family_screen.pending_family_action(),
              "held footer feedback is visible and moving off the original hit cancels its release");
        check(family_screen.pointer_down(162, 207), "footer can be pressed again after a canceled gesture");
        check(family_screen.click(162, 207) && family_screen.pending_family_action() &&
              family_screen.pending_family_action()->kind == LeagueFamilyRequestKind::navigate &&
              family_screen.pending_family_action()->from_child == 1 &&
              family_screen.pending_family_action()->target_child == 2,
              "Original table footer index 10 routes to pairings with a typed family request");
        family_screen.reject_family_action("test acknowledgement");
        check(family_screen.pointer_down(162, 220) && family_screen.click(162, 220) && family_screen.pending_family_action() &&
              family_screen.pending_family_action()->target_child == 3,
              "Original table footer index 11 routes to strengths with a typed family request");
        family_screen.reject_family_action("test acknowledgement");
        check(family_screen.pointer_down(10, 180) && family_screen.click(10, 180) && family_screen.pending_family_action() &&
              family_screen.pending_family_action()->kind == LeagueFamilyRequestKind::control &&
              family_screen.pending_family_action()->control_index == 0,
              "non-navigation footer actions are emitted as explicit typed controls");
        family_screen.reject_family_action("test acknowledgement");
        bool found_selector = false;
        for (int y = 176; y <= 216 && !found_selector; ++y) {
            for (int x = 205; x <= 317; ++x) {
                if (!family_screen.pointer_down(x, y)) continue;
                check(family_screen.selected_league() == 0,
                      "league/group selector action does not mutate the child before owner acknowledgement");
                const auto& action = family_screen.pending_family_action();
                if (action && action->kind == LeagueFamilyRequestKind::select_league) {
                    check(action->league_id >= 0 && action->league_id < kLeagueCount &&
                          family_screen.selected_league() == 0,
                          "Original league selector emits a bounded semantic selection without local mutation");
                    found_selector = true;
                    break;
                }
            }
        }
        check(found_selector, "original KB/1 league map or group selector exposes a semantic action");
        family_screen.reject_family_action("test acknowledgement");
        bool found_group_toggle = false;
        for (int y = 176; y <= 216 && !found_group_toggle; ++y) {
            for (int x = 262; x <= 317; ++x) {
                if (!family_screen.pointer_down(x, y)) continue;
                const auto& action = family_screen.pending_family_action();
                if (action && action->kind == LeagueFamilyRequestKind::toggle_league_group) {
                    check(action->league_id == int(exe.u8(::bmh::layout::CareerSession_FormatError_RuntimeField4)) && family_screen.selected_league() == 0,
                          "domestic-to-secondary toggle selects Source foreign base without local mutation");
                    found_group_toggle = true;
                    break;
                }
            }
        }
        check(found_group_toggle, "Source group selector dispatches on physical DOWN");
        family_screen.reject_family_action("test acknowledgement");
        auto foreign_identity = family_identity;
        foreign_identity.league = exe.u8(::bmh::layout::CareerSession_FormatError_RuntimeField4);
        foreign_identity.league_group = 1;
        LeagueTableScreen foreign_family_screen(data, tables, foreign_identity.league,
            LeagueTableScreen::FamilyMode{foreign_identity});
        bool found_return_toggle = false;
        for (int y = 176; y <= 216 && !found_return_toggle; ++y) {
            for (int x = 262; x <= 317; ++x) {
                if (!foreign_family_screen.pointer_down(x, y)) continue;
                const auto& action = foreign_family_screen.pending_family_action();
                if (action && action->kind == LeagueFamilyRequestKind::toggle_league_group) {
                    check(action->league_id == family_identity.manager_default_league &&
                          foreign_family_screen.selected_league() == foreign_identity.league,
                          "secondary-to-domestic toggle returns to the owner manager's saved league");
                    found_return_toggle = true;
                    break;
                }
            }
        }
        check(found_return_toggle, "Source group selector uses manager-default return league");
        check(family_screen.pointer_down(310, 153) && family_screen.pending_family_action() &&
              family_screen.pending_family_action()->kind == LeagueFamilyRequestKind::team_info &&
              family_screen.pending_family_action()->runtime_team == tables[0].rows.back().runtime_team_id &&
              family_screen.pending_family_action()->expected_club_identity == tables[0].rows.back().club_identity_key,
              "last valid 18-team row y=153 resolves the typed TeamInfo runtime and stable identities");
        family_screen.reject_family_action("test acknowledgement");
        check(!family_screen.pointer_down(310, 154) && !family_screen.click(310, 154) && !family_screen.pending_family_action(),
              "strict Original row lower bound rejects y=154 for an 18-row table");
        check(family_screen.pointer_down(310, 29) && family_screen.pending_family_action() &&
              family_screen.pending_family_action()->runtime_team == tables[0].rows.front().runtime_team_id,
              "first row band begins at y=29 without inventing an x-only name hitbox");
        family_screen.reject_family_action("test acknowledgement");
        check(family_screen.pointer_down(297, 132, true) && family_screen.pending_family_action() &&
              family_screen.pending_family_action()->kind == LeagueFamilyRequestKind::close &&
              family_screen.pending_family_action()->target_child == 0,
              "right-button close requests the family owner, not a local table teardown");
        family_screen.reject_family_action("test acknowledgement");
        check(!family_screen.key("ESC") && !family_screen.pending_family_action(),
              "family mode does not invent an unverified Source Escape mapping");

        rejected([&] { (void)league_table_from_game(game, -1, 0); });
        rejected([&] { (void)league_table_from_game(game, manager, kLeagueCount); });
        auto incomplete = tables;
        incomplete[3].rows.pop_back();
        rejected([&] { LeagueTableScreen invalid(data, incomplete, 0); });
        rejected([&] { LeagueTableScreen invalid(data, tables, kLeagueCount); });
        draw_hook = old_draw_hook;
        std::cout << "Owned league table projection and read-only screen passed.\n";
        return 0;
    } catch (const std::exception& e) {
        draw_hook = nullptr;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
