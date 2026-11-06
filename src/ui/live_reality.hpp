// SPDX-License-Identifier: GPL-3.0-only
// Reality field renderer: only the lower pitch field of the live reality view, drawn from a motion frame
// and a backdrop. Header and reporter text are owned by the caller; this renderer cannot select a pass,
// a player or a match result. Implementation: live_reality.cpp.
#pragma once

#include <filesystem>

#include "formats/vga.hpp"
#include "game/reality_motion.hpp"

namespace bmh::ui {

// Only the lower Reality field (RealityFieldScreen). Header and reporter text are owned
// by the caller. This renderer cannot select a pass, a player or a match result.
class RealityFieldScreen {
public:
    RealityFieldScreen(const std::filesystem::path& data, uint8_t ball_style);
    Image render(const RealityMotionFrame&, const Image& backdrop) const;
private:
    Image atlas_, digits_;
};

} // namespace bmh::ui
