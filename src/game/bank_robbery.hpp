// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/state.hpp"
#include <limits>
#include <optional>

namespace bmh {
// OpenBMH-owned OBMHEXT1 section BRM1: four little-endian blocked month-start keys.
// Unknown histories use UINT32_MAX; original save fields are not repurposed.
inline constexpr uint32_t kBankRobberyMonthExtension = 0x314d5242;
inline constexpr uint32_t kUnknownBankRobberyMonth = std::numeric_limits<uint32_t>::max();
inline constexpr uint32_t kBankRobberyMonthBytes = 16;

inline std::optional<uint32_t> bank_robbery_guard_month(const GameState& state, int manager) {
    if (manager < 0 || manager > 3) throw FormatError("bank robbery: invalid manager");
    if (!state.memory.has(kBankRobberyMonthExtension)) return {};
    if (!state.memory.has(kBankRobberyMonthExtension + kBankRobberyMonthBytes - 1))
        throw FormatError("bank robbery: truncated monthly guard");
    const auto key = state.memory.u32(kBankRobberyMonthExtension + uint32_t(manager) * 4);
    return key == kUnknownBankRobberyMonth ? std::nullopt : std::optional<uint32_t>{key};
}
inline void set_bank_robbery_guard_month(GameState& state, int manager, std::optional<uint32_t> key) {
    if (manager < 0 || manager > 3) throw FormatError("bank robbery: invalid manager");
    if (!state.memory.has(kBankRobberyMonthExtension)) {
        state.memory.add_block(kBankRobberyMonthExtension, kBankRobberyMonthBytes);
        for (uint32_t slot = 0; slot < 4; ++slot)
            state.memory.set32(kBankRobberyMonthExtension + slot * 4, kUnknownBankRobberyMonth);
    }
    if (!state.memory.has(kBankRobberyMonthExtension + kBankRobberyMonthBytes - 1))
        throw FormatError("bank robbery: truncated monthly guard");
    state.memory.set32(kBankRobberyMonthExtension + uint32_t(manager) * 4, key.value_or(kUnknownBankRobberyMonth));
}
// The first absolute day of the source calendar month, distinct across years.
uint32_t bank_robbery_month_key(uint32_t date);
}
