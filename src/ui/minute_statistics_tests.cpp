// SPDX-License-Identifier: GPL-3.0-only
#include "ui/minute_statistics.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() {
    using namespace bmh;
    using namespace bmh::ui;

    GameState game;
    game.manager_records.assign(4 * 0xb0c, 0);
    constexpr int manager = 2;
    constexpr size_t base = size_t(manager) * 0xb0c + 0x1ae;
    for (size_t series = 0; series < 4; ++series)
        for (size_t sample = 0; sample < 18; ++sample)
            game.manager_records[base + series * 18 + sample] = uint8_t(series * 47 + sample * 3);

    const auto projection = minute_statistics_from_game(game, manager);
    require(projection.manager_slot == manager, "manager slot remains bound");
    for (size_t series = 0; series < 4; ++series)
        for (size_t sample = 0; sample < 18; ++sample)
            require(projection.source_series[series][sample] == uint8_t(series * 47 + sample * 3),
                    "all 72 saved Source bytes copied without derivation");
    require(projection.source_series[1][0] == 47 && projection.source_series[1][17] == 98,
            "unexplained second series is preserved verbatim");

    bool rejected = false;
    try { (void)minute_statistics_from_game(game, 4); }
    catch (const FormatError&) { rejected = true; }
    require(rejected, "invalid Original manager index is rejected");

    game.manager_records.resize(base + 4 * 18 - 1);
    rejected = false;
    try { (void)minute_statistics_from_game(game, manager); }
    catch (const FormatError&) { rejected = true; }
    require(rejected, "truncated manager projection is rejected");

    std::cout << "Minute statistics projection: 72 Source bytes and bounds checks passed\n";
    return 0;
}
