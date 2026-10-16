// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <optional>
#include <string>

#include "formats/font.hpp"
#include "formats/le.hpp"
#include "formats/textd.hpp"
#include "formats/vga.hpp"
#include "ui/other_games_state.hpp"
#include "ui/sprite_font.hpp"

namespace bmh::ui {

class OtherGamesScreen {
public:
    OtherGamesScreen(const std::filesystem::path& data, OtherGamesState state);

    Image render() const;
    bool pointer_down(int x, int y, bool right = false);
    bool move(int x, int y);
    bool release(int x, int y);
    void reject_action(std::string reason);

    const OtherGamesState& state() const { return state_; }
    const std::optional<OtherGamesRequest>& pending() const { return pending_; }
    std::string state_json() const;

private:
    OtherGamesRequest request(OtherGamesAction action, int value = -1) const;
    void draw_text(Image& out, int x, int baseline, std::string_view bytes, uint8_t color) const;
    void draw_source_panel(Image& out, int left, int top, int right, int bottom) const;
    void draw_arrow_frame(Image& out, int x, int y, int style) const;
    void draw_sprite_text(Image& out, int x, int baseline, std::string_view bytes) const;
    void draw_button(Image& out, int x, int y, int width, std::string_view label,
                     bool pressed, bool selected = false) const;
    bool fixture_hit(int x, int y, int& team) const;
    int button_at(int x, int y) const;
    int arrow_at(int x, int y) const;
    bool in_arrow(int arrow, int x, int y) const;

    LeImage exe_;
    TextDb texts_;
    Image frame_, panel_, arrows_, widgets_, flags_;
    Font font_;
    SpriteFont sprite_;
    OtherGamesState state_;
    std::optional<OtherGamesRequest> pending_;
    int pressed_button_ = -1;
    int pressed_arrow_ = -1;
    std::string rejection_;
};

} // namespace bmh::ui
