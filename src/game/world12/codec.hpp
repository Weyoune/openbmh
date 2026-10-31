// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/world12/world.hpp"
namespace bmh::world12 {
inline constexpr const char* kProfile="dual-classic-2";
// This database codec is independent of M3D's 12-bit roster words and 16-bit pools.
Bytes encode_world(const World&);
World decode_world(const Bytes&);
}
