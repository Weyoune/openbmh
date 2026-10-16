// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include "formats/font.hpp"
#include "formats/le.hpp"
#include "formats/textd.hpp"
#include "formats/vga.hpp"
#include "ui/desktop.hpp"
#include "ui/sprite_font.hpp"

namespace bmh { struct GameState; }
namespace bmh::ui {

struct IndoorStandingRow {
    int team = -1;
    uint16_t points = 0;
    std::string name; // Original font bytes.
    uint8_t name_color = 0x6a, rank_color = 0x51;
};
struct IndoorStandingsState {
    DeskRequest owner{};
    bool decorated_background = true, gradient_text = true, indoor = false;
    uint8_t tournaments = 0, strength = 0;
    std::vector<IndoorStandingRow> rows;
    std::string footer, strength_text;
    std::array<std::string, 4> prize_text{};
};

// IndoorStandingsFromGame(1): no draw/qualification writes, only a copied ranking and title holders.
IndoorStandingsState indoor_standings_from_game(const GameState&, const LeImage&, const TextDb&,
                                               const DeskRequest&);
class IndoorStandingsScreen {
public:
    IndoorStandingsScreen(const std::filesystem::path&, IndoorStandingsState);
    Image render() const;
    const IndoorStandingsState& state() const { return state_; }
    std::string state_json() const;
private:
    IndoorStandingsState state_;
    LeImage exe_;
    TextDb texts_;
    Image background_;
    Font font_;
    SpriteFont sprite_;
    void text(Image&, int, int, std::string_view, uint8_t) const;
};
} // namespace bmh::ui
