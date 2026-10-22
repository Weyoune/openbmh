// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "cli/json.hpp"
#include "game/state.hpp"
namespace bmh {
// Explicit, platform-independent checkpoint. No object dumps, pointers or callbacks.
cli::Json encode_native_state(const GameState &);
GameState decode_native_state(const cli::Json &);
cli::Json encode_native_bytes(const Bytes &);
Bytes decode_native_bytes(const cli::Json &, size_t expected);
} // namespace bmh
