// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/executable_layout.hpp"

namespace bmh::layout {
// Football Limited V2.20 data-segment locations of the OPL voice slots and
// the 96-word key-on frequency table. Kept separate from simulation tables.
inline constexpr auto EnglishAudioOperatorSlots = EnDataStart + 0x6a63;
inline constexpr auto EnglishAudioNoteFrequencies = EnDataStart + 0xb310;
} // namespace bmh::layout
