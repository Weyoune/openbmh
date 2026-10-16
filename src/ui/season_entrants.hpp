// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <filesystem>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "formats/le.hpp"
#include "formats/font.hpp"
#include "formats/vga.hpp"
#include "ui/sprite_font.hpp"

namespace bmh { struct GameState; struct TextDb; }

namespace bmh::ui {

struct SeasonEntrant {
    int runtime_team_id = -1;
    uint16_t badge_selector = 0;
    std::string name; // owned UTF-8
    int x = 0;
    int y = 0;
};

struct SeasonEntrantsState {
    int competition = -1; // 0 UEFA, 1 Champions League, 2 Cup Winners' Cup
    std::string heading;  // original TEXTD #1249
    std::string footer;   // original CompetitionWinnerScreen competition-name table
    bool decorated_background = true; // PlainBackground == 0
    bool gradient_text = true; // TextGradientDisabled == 0 && TextGradientMode == 2
    std::vector<SeasonEntrant> teams;
};

// Source SeasonEntrantsFromGame / CompetitionWinnerScreen(param1=1): projects only German entrants
// and uses the executable's original placement tables. Empty means source guards
// suppress these pages for the current run.
std::vector<SeasonEntrantsState> season_entrants_from_game(
    const GameState& game, const LeImage& exe, const TextDb& texts);

class SeasonEntrantsScreen {
public:
    SeasonEntrantsScreen(const std::filesystem::path& data_directory, SeasonEntrantsState state);
    Image render() const;

private:
    SeasonEntrantsState state_;
    std::filesystem::path data_directory_;
    LeImage exe_;
    Image background_;
    std::map<int, Image> logo_atlases_;
    SpriteFont sprite_font_;
    Font font_;
};

} // namespace bmh::ui
