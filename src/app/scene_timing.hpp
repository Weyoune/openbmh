// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
namespace bmh::app {
// Presentation only: 25% more time per scene frame, without gameplay/RNG changes.
constexpr int scene_frame_delay(int milliseconds) { return std::max(16,(milliseconds*5+3)/4); }
}
