// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "game/reality_setup.hpp"

namespace bmh {

// Source spatial decisions, still independent of the unfinished Reality loop.
bool reality_on_path(const RealityPath&, int x, int y); // RealityOffside
bool reality_offside(const RealitySetup&, int x); // RealityNearby, current attacking side
int reality_nearby(const RealitySetup&, int x, int y, int side, int radius, bool forward); // RealityPenaltyArea
int reality_penalty_area(const RealitySetup&, const LeImage&, int side, int x = -1, int y = -1); // RealityLineOccupants2
struct RealityLineOccupants { int count = 0; uint8_t selected = 0; };
// RealityLineOccupants excludes the terminal cell; sampling uses Watcom, not GameRandom.
RealityLineOccupants reality_line_occupants(const RealitySetup&, const RealityPath&, int side,
                                           bool select, WatcomRandom&);
struct RealitySpaceTarget {
    bool found = false;
    int x = 0, y = 0, score = 0, figure = -1;
};
// RealitySeekSpace: caller supplies the existing ball-to-goal path, flags and mode.
RealitySpaceTarget reality_seek_space(const RealitySetup&, const LeImage&, int figure, int side,
                                     uint8_t flags, uint8_t mode, const RealityPath& goal_path);
// RealitySupport: best unopposed occupied support cell within the Source 9x9 scan.
RealitySpaceTarget reality_support(const RealitySetup&, int x, int y, int side,
                                  int16_t minimum_score, uint8_t pressure);
// RealityReturnToBox. The Source caller must clear the figure's old occupancy first.
// Updates runtime placement/next position and consumes Game RNG only on collision.
void reality_return_to_box(RealitySetup&, int figure, int side, uint8_t steps, GameRandom&);

} // namespace bmh
