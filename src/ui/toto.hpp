// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "formats/font.hpp"
#include "formats/vga.hpp"
#include "ui/desk_state.hpp"
#include "ui/league_table_range_dialog.hpp"
#include "ui/sprite_font.hpp"

namespace bmh::ui {

struct TotoMatchState {
    int home = -1, away = -1;
    std::string home_name, away_name;
    uint8_t pick = 0;
    std::array<int, 5> home_ratings{}, away_ratings{};
    bool ratings_available = false;
};

struct TotoState {
    int manager_slot = -1, runtime_club = -1;
    uint64_t revision = 0;
    std::string source, manager_name, club_name;
    int32_t balance = 0;
    uint32_t stake = 0;
    std::vector<TotoMatchState> matches;
    bool available = false;
    std::string feedback;
    std::array<uint8_t, 11> previous_tips{};
    uint8_t previous_correct = 0;
    uint32_t previous_winnings = 0;
    int32_t currency_multiplier = 1, currency_divisor = 1;
    bool gradient = true;
};

enum class TotoRequestKind { none, submit };
struct TotoRequest {
    TotoRequestKind kind = TotoRequestKind::none;
    int manager_slot = -1, runtime_club = -1;
    uint64_t revision = 0;
    uint32_t stake = 0;
    std::array<uint8_t, 11> tips{};
};

TotoState toto_from_game(const GameState&, uint64_t revision);
std::string toto_state_json(const TotoState&);
std::string toto_request_json(const TotoRequest&);

class TotoScreen {
public:
    TotoScreen(const std::filesystem::path&, TotoState);
    Image render() const;
    TotoRequest click(int x, int y, bool right = false);
    TotoRequest key(std::string_view);
    bool text(std::string_view);
    bool close_requested() const { return close_requested_; }
    const TotoState& state() const { return state_; }
    const std::optional<TotoRequest>& pending() const { return pending_; }
    void acknowledge(TotoState);
    void reject_action(std::string);
    std::string state_json() const;
    bool calculator_open() const { return calculator_.has_value(); }
    bool calculator_input_ready() const { return calculator_ && calculator_->source_input_ready(); }
    bool move(int x, int y);
    bool pointer_down(int x, int y, bool right = false);
    bool pointer_up(int x, int y, bool right = false);
    bool after_present(uint64_t source_units);
    bool tick_source_units(uint64_t source_units);
    bool process_timer_expired();
private:
    TotoState state_;
    std::filesystem::path data_;
    Image background_, widgets_;
    LeImage exe_;
    TextDb texts_;
    Font font_;
    SpriteFont sprite_font_;
    std::array<uint8_t, 11> tips_{};
    uint32_t stake_ = 0;
    std::string feedback_;
    std::optional<TotoRequest> pending_;
    std::optional<LeagueTableRangeDialog> calculator_;
    int calculator_x_ = 103, calculator_y_ = 55;
    uint64_t calculator_clock_ = 0;
    bool calculator_clock_valid_ = false, calculator_clock_reset_ = false;
    bool close_requested_ = false, close_after_ack_ = false;
    bool calculator_right_release_pending_ = false;
    void text_at(Image&, int, int baseline, std::string_view, uint8_t, bool shadow = false) const;
    void open_calculator();
    TotoRequest submit();
};

}  // namespace bmh::ui
