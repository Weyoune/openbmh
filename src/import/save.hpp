// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/fbm.hpp"
#include "formats/savestate.hpp"
namespace bmh::obw {
// Translate the V2.20 state-block contract, including its native cup regions,
// to the existing in-memory Classic layout. No executable or FBM file is emitted.
Bytes normalize_english_save(const Savegame &, const DbCounts &counts = {});
} // namespace bmh::obw
