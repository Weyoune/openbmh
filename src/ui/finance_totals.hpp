// SPDX-License-Identifier: GPL-3.0-only
// Finance totals page (eleven rows of club income and expenses with formatted amounts).
// finance_totals_from_game() builds the read-only FinanceTotals; the monthly values reuse the finance preview
// from advertising.cpp. FinanceTotalsScreen only renders over the supplied underlay and raises no requests.
// Implementation: finance_totals.cpp.
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
