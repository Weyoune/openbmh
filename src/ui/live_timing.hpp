// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>

namespace bmh::ui {
// Saved column-nine delays are source timer units. Values below six use
// integer halving; zero genuinely disables the pause.
constexpr uint32_t live_delay_milliseconds(uint8_t saved) {
    return uint32_t(saved < 6 ? saved / 2 : saved) * 20;
}
// VGA mode 13h timing is independent of the host monitor's refresh rate.
inline constexpr unsigned kOriginalRefreshHz = 70;
}  // namespace bmh::ui
