// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "formats/font.hpp"
#include "formats/le.hpp"
#include "formats/textd.hpp"
#include "formats/vga.hpp"
#include "ui/sprite_font.hpp"

namespace bmh { struct GameState; struct SeasonPromotionEvent; }

namespace bmh::ui {

struct SeasonPromotionClub {
    uint16_t club_id = 0;
    uint16_t badge_selector = 0;
    bool badge_fallback = false; // Source SourceBadgeFallback uses club-ID ranges, not selector ranges.
    std::string original_name;   // raw Original-encoded club name from ClubName2
};

struct SeasonPromotionProjection {
    int group = 0;               // 0 Bundesliga, 1 2. Bundesliga, 2 3. Liga, 3 Regionalliga
    size_t count = 0;            // only the first count entries on each side are drawn
    std::string league_footer;   // raw Original-encoded leagueName(firstLeague)
    bool gradient_text = true;
    int scanline_steps = 0;      // 0 initial image; 1..38 Source's automatic scanline exchange
    std::vector<SeasonPromotionClub> relegated;
    std::vector<SeasonPromotionClub> promoted;
};

// Draw-only port of SeasonPromotionScreen. The caller owns transition order, waits,
// presentation ticks, and whether Source's BalancePlan_BalanceFromGame_RuntimeField flag suppresses animation.
class SeasonPromotionScreen {
public:
    SeasonPromotionScreen(const std::filesystem::path& data_directory,
                          SeasonPromotionProjection projection);
    Image render() const;
    Image render(int scanline_steps) const;

private:
    SeasonPromotionProjection projection_;
    LeImage exe_;
    Image background_, widgets_;
    TextDb texts_;
    SpriteFont sprite_font_;
    Font font_;
    std::map<int, Image> badge_atlases_;
};

} // namespace bmh::ui

namespace bmh::ui {
SeasonPromotionProjection season_promotion_from_game(
    const GameState& game, const SeasonPromotionEvent& event,
    const LeImage& exe, const TextDb& texts);
} // namespace bmh::ui
