// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "container.hpp"

#include <string>

namespace openbmh::audio {

// Builds an explicitly synthetic, deterministic 8-bar matchday loop. This is
// presentation fallback material, not decoded original music or a Sound ID.
bool synthesize_matchday_music_loop(PcmSample& out, std::string& error);

} // namespace openbmh::audio
