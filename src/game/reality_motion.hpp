// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <utility>

#include "formats/le.hpp"
#include "cli/json.hpp"

namespace bmh {

// RealityFieldScreen consumes positions chosen by the Reality simulation. This owned
// input is deliberately independent of GameState and both random generators.
struct RealityFigureMotion {
    bool active = false;
    int old_x = 0, old_y = 0, next_x = 0, next_y = 0;
    uint8_t phase = 0;
    bool operator==(const RealityFigureMotion&) const = default;
};
struct RealityMotionInput {
    std::array<RealityFigureMotion, 24> figures{}; // 22 players, shadow, ball
    bool ball_airborne = false;
    bool numbers = true; // Source RealityMatch_FormatError_ExecutableField == 0
    bool operator==(const RealityMotionInput&) const = default;
};
struct RealityMotionSprite {
    int figure = 0, x = 0, y = 0, cell = 0, number = 0;
    bool away_palette = false;
    bool operator==(const RealityMotionSprite&) const = default;
};
struct RealityMotionFrame {
    std::array<RealityMotionSprite, 24> sprites{};
    size_t count = 0;
    bool operator==(const RealityMotionFrame&) const = default;
};
struct RealityMotionResult {
    std::array<RealityMotionFrame, 11> frames{};
    std::array<uint8_t, 24> phases_after{};
    bool operator==(const RealityMotionResult&) const = default;
};

// RealityDirection / Int7. Coordinates use the original 43 by 25 Reality grid.
int reality_direction(int x, int y);
std::pair<int, int> reality_project(const LeImage&, int x, int y);
RealityMotionResult reality_motion(const RealityMotionInput&, const LeImage&);

struct RealityPath {
    std::array<std::pair<int, int>, 51> points{};
    size_t count = 0;
    bool operator==(const RealityPath&) const = default;
};
// RealityPath's integer line, including both endpoints. The goal helper uses the
// Source's three goal-mouth target rows (11, 12, 13).
RealityPath reality_path(int x, int y, int target_x, int target_y);
RealityPath reality_goal_path(int x, int y, int goal_x);

// Place. Each side has its own occupancy plane: opponents may share a cell,
// teammates may not. Ball placement records the previous cell without occupying
// either player plane. This is runtime scene state, not a Native save extension.
class RealityGrid {
public:
  cli::Json checkpoint() const;
  void restore_checkpoint(const cli::Json &);
  bool place(int figure, int x, int y, int side);
  void clear(int x, int y, int side); // caller clears old occupancy before a move
  void reset_occupancy();             // Source initialization preserves old figure/ball data
  bool operator==(const RealityGrid &) const = default;
  void stage_position(int figure, int x, int y); // afb0c/afb23 before occupancy is committed
  uint8_t occupant(int x, int y, int side) const;
  std::pair<int, int> position(int figure) const;
  std::pair<int, int> ball() const { return ball_; }
  std::pair<int, int> previous_ball() const { return previous_ball_; }
private:
    std::array<std::array<uint8_t, 0x478>, 2> occupied_{};
    std::array<std::pair<int, int>, 24> positions_{};
    std::pair<int, int> ball_{}, previous_ball_{};
};

// Source scene globals persist across matchdays, but are not Native FBM data.
struct RealityResidue {
    bool initialized = false, deflected = false;
    uint8_t card_kind = 0;
    int distance = 0;
    std::array<uint8_t,5> cue_actors{};
    RealityGrid grid;
    bool operator==(const RealityResidue&) const = default;
};

} // namespace bmh
