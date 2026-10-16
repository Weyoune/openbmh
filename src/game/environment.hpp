// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// OpenBMH deviates from the original: enables the supported youth academy and represents staff levels in monthly increments of five thousand.
// The club environment ("Umfeld", desk menu 24, H2 p. 93; supported rule profile manual p. 107, 111):
// the screen Image with the supported rule profile patch (SizeT7 / EnvironmentOperation /
// YouthPrice / YouthOpen). Six monthly running costs per manager live at
// Spending + manager * 0x24 (u32 each); the month change books them
// (month.cpp monthly_expenses) and the staff values steer training, injuries and healing.
//
//   item 0, 2  the original's two other youth age groups: the patch always sets 0
//   item 1     the youth academy (supported rule profile): on or off. On it is the league's season price
//              (TeamLeague3: 1.5 M, 1 M, 580 000, 280 000), paid in full at the next month
//              change while record +0x880 (youth expenses this season) is below it. It can
//              be switched on in July and August only ("Zu spät für diese Saison...") and
//              not switched off once paid ("Der Vertrag läuft bis zum Saisonende.").
//   item 3..5  co-trainer, doctor, physio: level 0..6 times 5 000 a month (EnvironmentRuntimeField)
//
// A click on a tile sets the level from the mouse position (supported rule profile: one click per value).

#include <array>
#include <cstdint>
#include <string>

#include "game/state.hpp"

namespace bmh {

constexpr int kEnvironmentItems = 6;
enum EnvironmentItem : int { kYouthAcademy = 1, kCoTrainer = 3, kDoctor = 4, kPhysio = 5 };

struct EnvironmentState {
    std::array<int32_t, kEnvironmentItems> monthly{};  // Spending values
    std::array<int, kEnvironmentItems> level{};         // the stars the screen draws (0..6)
    int32_t youth_price = 0;    // the season price of the academy for the club's league
    bool youth_paid = false;    // record +0x880 reached the price: cannot be switched off
    bool youth_open = false;    // July/August: the academy can still be switched on
};

EnvironmentState environment_state(const GameState& g, int manager);

enum class EnvironmentResult : uint8_t { changed, unchanged, too_late, contract_runs, invalid };

// A click on `item` at `level` (0..6; for the academy any level > 0 means "on" and a click on
// an active academy switches it off). `message` gets the original's message box text, if any.
EnvironmentResult set_environment(GameState& g, int manager, int item, int level, std::string* message = nullptr);

}  // namespace bmh
