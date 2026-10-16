// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "container.hpp"

#include <cstdint>
#include <string>

namespace openbmh::audio {

// Presentation cues used when no verified original PCM effect is available.
// These are intentionally semantic labels, never fabricated original Sound IDs.
enum class SemanticCue : std::uint8_t {
    kickoff_whistle = 1,
    goal = 2,
    full_time_whistle = 3,
};

// Generates deterministic mono PCM16 cues at 22050 Hz. The synthesizer has no
// game state, random source, or renderer dependency.
bool synthesize_semantic_cue(SemanticCue cue, PcmSample& out, std::string& error);

} // namespace openbmh::audio
