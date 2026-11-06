// SPDX-License-Identifier: GPL-3.0-only
// Synthetic fallback matchday music: a deterministic 8-bar loop generated in code. It is
// presentation filler, not decoded original music and not tied to any original sound id.
// The result is a PcmSample that Playback::start_music_loop() can play; implemented in
// music_fallback.cpp.
#pragma once

#include "container.hpp"

#include <string>

namespace openbmh::audio {

// Builds an explicitly synthetic, deterministic 8-bar matchday loop. This is
// presentation fallback material, not decoded original music or a Sound ID.
bool synthesize_matchday_music_loop(PcmSample& out, std::string& error);

} // namespace openbmh::audio
