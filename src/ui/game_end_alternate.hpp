// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "formats/font.hpp"
#include "formats/le.hpp"
#include "formats/vga.hpp"

namespace bmh::ui {

// Owned original assets; this screen has no GameState or random-generator API.
struct GameEndAlternateScoreAssets {
    LeImage exe;
    Font font;
    Image calculator;
    std::array<std::string, 3> texts; // original-encoded TEXTD 890/891/892
};
GameEndAlternateScoreAssets load_game_end_alternate_score_assets(const std::filesystem::path& data);

enum class AlternateScorePhase { Calculator, Points, AwaitAck, Closed, Cancelled };

// GameEndAlternateScoreScreen signature0: BalanceFromGame, followed by one presented pixel per point.
// The caller owns presentation, Source waits and the career lifecycle.
class GameEndAlternateScoreScreen {
public:
    static constexpr unsigned maximum = 3536;
    static constexpr unsigned initial = 200;
    static constexpr std::array<unsigned, 8> presets{100,200,300,400,500,600,0,maximum};

    GameEndAlternateScoreScreen(GameEndAlternateScoreAssets assets, Image backdrop,
                                bool gradient_text = true);
    AlternateScorePhase phase() const { return phase_; }
    unsigned value() const { return value_; }
    unsigned points_drawn() const { return points_drawn_; }
    std::optional<unsigned> accepted_value() const;
    int window_x() const { return window_x_; }
    int window_y() const { return window_y_; }
    // Source inclusive bounds: keys width16/height14, presets width53/height14.
    std::optional<unsigned> button_at(int x, int y) const;
    bool click(int x, int y);
    bool key(std::string_view key);
    bool cancel(); // Source mouse cancellation event8; physical keyboard ESC is ignored
    bool text(std::string_view text);
    bool mouse_down(int x, int y);
    bool mouse_move(int x, int y);
    bool mouse_up();
    // Present render() once after every successful advance_point(), then call
    // finish_points() for the separate footer/ACK presentation, including n=0.
    bool advance_point();
    bool finish_points();
    bool acknowledge();
    // Optional pressed-key art is Render(param5=1); no timing or RNG inside.
    Image render(std::optional<unsigned> pressed_button = {}) const;

private:
    bool activate(unsigned button);
    void draw_text(Image&, int x, int baseline, std::string_view bytes,
                   uint8_t color, bool gradient) const;
    GameEndAlternateScoreAssets assets_;
    Image backdrop_;
    bool gradient_text_;
    AlternateScorePhase phase_ = AlternateScorePhase::Calculator;
    unsigned value_ = initial, points_drawn_ = 0;
    unsigned digit_count_ = 3; // BalanceFromGame local18; leading zero entry is limited to10
    int window_x_ = 100, window_y_ = 100;
    bool dragging_ = false;
    int drag_x_ = 0, drag_y_ = 0;
};

} // namespace bmh::ui
