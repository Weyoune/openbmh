// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

#include "formats/font.hpp"
#include "formats/le.hpp"
#include "formats/textd.hpp"
#include "formats/vga.hpp"
#include "game/state.hpp"

namespace bmh::ui {

// FinanceTotalsFromGame rows 0..10. Row 4 is the source's group separator and has no value.
struct FinanceTotals {
    int manager_slot = -1;
    std::array<int32_t, 11> rows{};
    std::array<std::string, 11> amounts{}; // FormatNumber Source formatting with the game's currency settings.
};

// Read-only projection for the Original page. Monthly values reuse the public wrapper for
// advertising.cpp's exact FinanceTotalsFromGame3(0)/OriginalFinancePreview(0) implementation.
FinanceTotals finance_totals_from_game(const GameState&, int manager_slot);

class FinanceTotalsScreen {
public:
    FinanceTotalsScreen(const std::filesystem::path& data, FinanceTotals, Image underlay);
    Image render() const;
private:
    FinanceTotals state_;
    Image underlay_;
    TextDb texts_;
    Font font_;
};

} // namespace bmh::ui
