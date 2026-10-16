// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "formats/font.hpp"
#include "formats/le.hpp"
#include "formats/textd.hpp"
#include "formats/vga.hpp"
#include "ui/sprite_font.hpp"
#include "ui/top_scorers_state.hpp"

namespace bmh::ui {

class TopScorersScreen {
public:
    TopScorersScreen(const std::filesystem::path& data, TopScorersState state);

    Image render() const;
    bool pointer_down(int x, int y, bool right = false);
    bool move(int x, int y);
    bool release(int x, int y);
    void reject_action(std::string reason);

    const TopScorersState& state() const { return state_; }
    const std::optional<TopScorersRequest>& pending() const { return pending_; }
    std::string state_json() const;

private:
    TopScorersRequest request(TopScorersAction action,
                              TopScorersMetric metric,
                              TopScorersScope scope) const;
    void draw_text(Image& out, int x, int baseline, std::string_view bytes,
                   uint8_t color) const;
    void draw_button(Image& out, int x, int y, int width,
                     std::string_view original_label, bool pressed) const;

    LeImage exe_;
    TextDb texts_;
    Image frame_, podium_, button_art_;
    Font font_;
    SpriteFont sprite_;
    TopScorersState state_;
    std::optional<TopScorersRequest> pending_;
    int pressed_button_ = -1;
    std::string rejection_;
};

} // namespace bmh::ui
