// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>

namespace bmh::ui {
// PointerHoldSourceTicks -> SDLGetTicks samples BDA46c: PIT1193182 /65536 Hz.
// Convert absolute host samples, then subtract ticks so the phase is retained.
// These are not DecodeTaxAnimationScript2's separate 20ms delay units.
constexpr uint64_t pointer_hold_source_ticks(uint64_t milliseconds) {
    constexpr uint64_t period = 65536ull * 1000;
    return (milliseconds / period) * 1193182 + (milliseconds % period) * 1193182 / period;
}
constexpr bool pointer_source_hold_ready(uint64_t ticks) { return ticks > 0x19; }
} // namespace bmh::ui
