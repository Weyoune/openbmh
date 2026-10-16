// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "formats/le.hpp"
#include "game/match_engine.hpp"

namespace bmh::ui {

struct LiveSetPieceSprite {
    int x = 0, y = 0, figure = 0;
    uint8_t code = 0;
    bool operator==(const LiveSetPieceSprite&) const = default;
};
struct LiveSetPieceFrame {
    int camera_x = 0, camera_y = 0, phase = 0;
    std::vector<LiveSetPieceSprite> sprites;
    bool operator==(const LiveSetPieceFrame&) const = default;
};

// LiveSetPieceFrames geometry, using only the values already drawn by MatchEngine.
// Rejects missing, mismatched or surplus draws instead of inventing a scene.
std::vector<LiveSetPieceFrame> live_set_piece_frames(const LiveSetPieceEvent&, const LeImage&);

} // namespace bmh::ui
