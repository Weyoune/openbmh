// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/executable_layout.hpp"
#include "formats/le.hpp"
#include <stdexcept>
namespace bmh {
// Original data separates internal domestic slots from player-visible levels.
inline int player_league_count(const LeImage& image) {
    const auto address=layout::LeaguePairings_LeaguePairingsConstants_RuntimeField;
    if(!image.contains(address+1)||image.u8(address)!=5||image.u8(address+1)<1||image.u8(address+1)>5)
        throw std::invalid_argument("invalid player-facing league count");
    return image.u8(address+1);
}
}
