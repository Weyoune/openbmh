// SPDX-License-Identifier: GPL-3.0-only
#include "ui/game_end_tax_animation.hpp"
#include <iostream>
#include <stdexcept>

using namespace bmh;
using namespace bmh::ui;

int main() try {
    // Authored rectangle records and art; no installed game assets are used.
    Bytes script;
    for (uint8_t i = 0; i < 4; ++i) {
        for (uint8_t value : {uint8_t(0), i, uint8_t(1), uint8_t(100+i), uint8_t(90),
                              uint8_t(2), uint8_t(2), uint8_t(i == 1 ? 103 : 5)})
            script.push_back(value);
    }
    script.push_back(255);
    const auto steps = decode_tax_animation_script(script);
    if (steps.size() != 4 || steps[0].copies[0].x != 100 || steps[1].wait_ticks != 3 ||
        steps[1].copy_after_present || !steps[0].copy_after_present)
        throw std::runtime_error("Generated script decode mismatch");
    bool rejected = false;
    try { decode_tax_animation_script(Bytes{0, 1}); } catch (const FormatError&) { rejected = true; }
    if (!rejected) throw std::runtime_error("Truncated script was accepted");
    GameEndTaxAnimationAssets assets{Image(248,69,21), Image(318,207,13), steps};
    GameEndTaxAnimationScreen screen(assets, 1, Image(320,240,7), true);
    if (!screen.advance_step() || screen.render().at(100,90) != 21 ||
        screen.render().at(0,0) != 7 || screen.wait_milliseconds() != 100 || !screen.finished())
        throw std::runtime_error("Generated art copy/presentation mismatch");
    std::cout << "Generated animation decode, rejection and framebuffer checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
