// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "ui/lineup.hpp"

namespace bmh::ui {

struct PlayerStatisticsContext {
    uint8_t months = 0;       // Manager record +0x286; at most twelve board samples
    uint8_t display_flags = 0; // Flags[manager*3+2], four curves and ABS/REL
};

struct PlayerCardLoan {
    int owner = -1, borrower = -1, months = 0;
    std::array<int, 2> badges{};
    std::array<std::string, 2> full_names, short_names;
};

// Owned projection of a domestic player in the active manager's ordinary squad.
// A lender's separate twelve-slot snapshot uses the generic42dbd caller.
struct PlayerCardState {
    LineupState lineup;
    int row = -1, badge = -1;
    std::string nationality_name, club_short_name;
    bool portrait_available = true;
    int money_numerator = 1, money_denominator = 1;
    std::string unsupported_record; // explicit loan/negotiation gate, no invented card
    std::optional<PlayerCardLoan> loan;
    std::optional<PlayerStatisticsContext> statistics;
};
PlayerCardState player_card_from_save(const SaveState&, const LeImage&, int row, uint64_t revision = 0);
PlayerCardState player_card_from_game(const GameState&, const LeImage&, int row, uint64_t revision);
// AddToSquad + PlayerCardMarketValue. Below 50,000 the original draws random numbers;
// that branch deliberately returns unknown instead of changing engine RNG.
std::optional<uint32_t> player_card_market_value(const Player&, const LeImage&);
// Original graph coordinates for one monthly series; shared by the renderer and
// offline reference inspection. Curves 0..3: value, strength, form, total.
std::vector<std::pair<int,int>> player_statistics_points(const PlayerCardState&, const LeImage&, int curve, bool absolute);

class PlayerCardScreen {
public:
    PlayerCardScreen(const std::filesystem::path& data, PlayerCardState state, std::optional<Image> parent_frame = std::nullopt);
    Image render() const;
    bool click(int x, int y, bool right = false);
    bool key(std::string_view key);
    bool close_requested() const { return close_; }
    bool in_statistics() const { return statistics_open_; }
    const PlayerCardState& state() const { return state_; }
    std::string state_json() const;
private:
    PlayerCardState state_;
    LeImage exe_;
    Font small_, heading_;
    TextDb texts_;
    Image widgets_, atlas_, flags_, portrait_, letters_, badge_, parent_frame_;
    std::array<Image, 2> loan_badges_;
    bool close_ = false;
    bool statistics_open_ = false;
    std::optional<Image> statistics_frame_; // previous UI frame for original button redraw edges
    std::optional<Image> restored_card_frame_; // card loop does not reconstruct its outer frame
    uint8_t statistics_flags_ = 0;
    std::string unsupported_navigation_;
    void text(Image&, int x, int baseline, std::string_view, uint8_t color, int right = -1, bool heading = false) const;
    void frame(Image&, int x, int y, int width, int height) const;
    void button(Image&, int x, int y, int width, std::string_view, bool active = false) const;
    Image render_card() const;
    Image render_statistics() const;
    void draw_portrait(Image&, const LineupPlayer&) const;
    const std::string& label(uint32_t address) const;
};

} // namespace bmh::ui
