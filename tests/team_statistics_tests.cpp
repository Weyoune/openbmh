#include "runtime_fixture_tables.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/team_statistics.hpp"

#include <iostream>
#include <stdexcept>

#include "formats/fbm.hpp"
#include "formats/savestate.hpp"
#include "game/state.hpp"
#include "game/table.hpp"
#include "formats/font.hpp"
#include "formats/vga.hpp"
#include "ui/setup.hpp"

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
uint8_t pixel(const bmh::Image& image, int x, int y) {
    return image.pixels.at(size_t(y) * size_t(image.width) + size_t(x));
}
}

int main(int argc, char** argv) try {
    if (argc != 3) throw std::runtime_error("expected Original data directory and native fixture");
    const std::filesystem::path data(argv[1]), fixture(argv[2]);
    if (!std::filesystem::exists(data / "BMH.exe") || !std::filesystem::exists(fixture)) {
        std::cout << "SKIP: Original BMH data or team-statistics fixture is unavailable\n";
        return 0;
    }
    const auto exe = bmh::test::load_executable(bmh::ui::asset_path(data, "BMH.exe"));
    const auto texts = bmh::load_textd(bmh::ui::asset_path(data, "TEXTD.BLK"));
    const auto fbm = bmh::load_savegame(fixture);
    check(fbm.checksum_ok, "native fixture checksum");
    auto game = bmh::GameState::from_save(bmh::SaveState(fbm.stream));
    game.texts = &texts;
    const auto original_stream = fbm.stream;

    auto projection = bmh::ui::team_statistics_from_game(game, 0, bmh::ui::TeamStatisticsMode::combined, exe, texts);
    check(projection.runtime_team == 0 && projection.league == 0 && projection.source_has_league_panel,
          "projection follows the selected runtime team and Source league");
    check(projection.original_team_name == "1.FC UNION BERLIN" && projection.table.games(projection.league_row) == 34 &&
          projection.table.rank(projection.league_row) == 9,
          "native fixture retains its actual first-league team identity and Source table row");
    for (int team = 0; team < 5; ++team) {
        const auto first_round = bmh::ui::team_statistics_from_game(game, team,
            bmh::ui::TeamStatisticsMode::combined, exe, texts);
        check(first_round.flag_index == 0, "legacy routine maps first five domestic teams to country zero");
    }
    const auto second_tier = bmh::ui::team_statistics_from_game(
        game, 90, bmh::ui::TeamStatisticsMode::combined, exe, texts);
    check(second_tier.league == 5 && second_tier.league_row == 0 &&
          second_tier.original_league_name.find(game.club_name(358)) != std::string::npos,
          "foreign-league Source name uses its mapped club label without indexing domestic suffixes");
    const auto team_name = projection.original_team_name;
    const auto record = projection.team_record;
    bmh::Image club_frame(320, 240, 0);
    bmh::ui::TeamStatisticsScreen screen(data, projection, club_frame);
    const auto base = screen.render();
    check(base.width == 320 && base.height == 240, "Source statistics renderer returns 320x240");
    const auto flag_atlas = bmh::load_vga(bmh::ui::asset_path(data / "KB", "24.VGA"));
    check(pixel(base, 5, 42) == pixel(flag_atlas, 0, 0), "country flag uses the Source 9-pixel atlas stride and actual asset pixel");
    check(pixel(base, 225, 150) == 0x6a && pixel(base, 225, 192) == 0x6a,
          "position-history panel keeps the Source axis geometry");
    auto zero_games = projection;
    zero_games.table.rank_word[size_t(zero_games.league_row)] &= uint16_t(~0x3f);
    check(zero_games.table.games(zero_games.league_row) == 0 && !zero_games.original_league_name.empty(),
          "zero-games row preserves its Source league label while omitting the played-games bits");
    bmh::ui::TeamStatisticsScreen zero_screen(data, zero_games, club_frame);
    const auto zero_pixels = zero_screen.render();
    const auto body_font = bmh::load_font(bmh::ui::asset_path(data / "F", "BMH3.FNT"));
    const int league_width = body_font.text_width(zero_games.original_league_name);
    bmh::Image league_mask(std::max(1, league_width), 6);
    body_font.draw(league_mask, 0, 0, zero_games.original_league_name, 1);
    bool found_league_glyph = false;
    for (int y = 0; y < 6; ++y) for (int x = 0; x < league_width; ++x)
        if (pixel(league_mask, x, y) != 0 && pixel(zero_pixels, 314 - league_width + x, 43 + y) != 0) found_league_glyph = true;
    check(found_league_glyph, "zero-games render still draws the right-aligned league name");
    auto h2h = projection;
    h2h.source_has_head_to_head = true;
    h2h.has_active_head_to_head = true;
    h2h.active_manager_results[size_t(h2h.runtime_team) * 10 + 5] = 0x32;
    h2h.active_manager_results[size_t(h2h.runtime_team) * 10 + 6] = 0x32;
    bmh::ui::TeamStatisticsScreen h2h_screen(data, h2h, club_frame);
    const auto h2h_pixels = h2h_screen.render();
    const auto face_atlas = bmh::load_vga(bmh::ui::asset_path(data / "KB", "27.VGA"));
    check(pixel(h2h_pixels, 222, 55) == pixel(face_atlas, 3 * 38, 0),
          "head-to-head face derives its direction from both packed high/low score nibbles");
    check(screen.projection().original_team_name == team_name && screen.projection().team_record == record,
          "renderer owns runtime club text and statistic record bytes");
    if (!game.club_ids.empty() && game.club_ids[0] < game.club_names.size()) {
        const size_t name_at = game.club_ids[0];
        const uint8_t original = game.club_names[name_at];
        game.club_names[name_at] ^= 0x20;
        check(screen.projection().original_team_name == team_name && screen.render().pixels == base.pixels,
              "owned projection and rendered screen survive source name-pool changes");
        game.club_names[name_at] = original;
    }
    check(screen.button_at(51, 194) == bmh::ui::TeamStatisticsButton::home,
          "Source home button includes right and bottom hit edges");
    check(screen.button_at(51, 195) == bmh::ui::TeamStatisticsButton::home && !screen.button_at(51, 196),
          "Source stored ten-pixel button height has an inclusive y=195 edge");
    check(screen.button_at(52, 194) == std::nullopt && screen.button_at(167, 194) == bmh::ui::TeamStatisticsButton::close,
          "Source button gaps and close button identity");
    screen.press(10, 190);
    check(screen.pressed_button() == bmh::ui::TeamStatisticsButton::home && screen.render().pixels != base.pixels,
          "pressed mode button renders its active Original widget");
    screen.move(70, 190);
    check(!screen.pressed_button() && screen.release(10, 190).kind == bmh::ui::TeamStatisticsRequestKind::none,
          "leaving a mode button cancels dispatch");
    screen.press(10, 190);
    check(screen.release(60, 190).kind == bmh::ui::TeamStatisticsRequestKind::none,
          "release on another button cannot dispatch the original press");
    screen.press(10, 190);
    const auto home = screen.release(10, 190);
    check(home.kind == bmh::ui::TeamStatisticsRequestKind::change_mode &&
          home.mode == bmh::ui::TeamStatisticsMode::home && home.runtime_team == 0,
          "valid Source release carries a typed mode request for the clicked team");
    screen.press(170, 190);
    check(screen.release(170, 190).kind == bmh::ui::TeamStatisticsRequestKind::close,
          "fourth Source button exits the local modal");
    check(screen.right_button_down().kind == bmh::ui::TeamStatisticsRequestKind::close,
          "right-button DOWN exits the local modal");
    check(game.club_names == bmh::GameState::from_save(bmh::SaveState(original_stream)).club_names,
          "rendering does not mutate the loaded game name pool");
    std::cout << "Team statistics source-only data and button tests passed (no Original modal PNG available)\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
}
