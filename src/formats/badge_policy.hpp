// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "formats/executable_layout.hpp"
#include "formats/le.hpp"

namespace bmh {

// Installed alongside the loaded edition's other runtime tables. Club selectors
// remain database/savegame data; this only reproduces the original drawing gate.
inline int original_foreign_badge_end = 138;
inline bool expanded_badges = false;

inline int original_badge_gate(const LeImage& exe) {
    if(const auto* policy=exe.runtime_policy())return int(policy->badge_end);
    constexpr auto instruction = layout::ForeignBadgeGateInstruction;
    if (exe.contains(instruction) && exe.contains(instruction + 6) &&
        exe.u8(instruction) == 0x66 && exe.u8(instruction + 1) == 0x81 &&
        exe.u8(instruction + 2) == 0xfb && exe.u8(instruction + 5) == 0x7c &&
        exe.u8(instruction + 6) == 7) {
        const int end = exe.u16(instruction + 3);
        if (end == 90 || end == 138) return end;
    }
    // Never invent individual foreign crests for an unverified drawing routine.
    return 138;
}

inline void install_original_badge_policy(const LeImage& exe) {
    original_foreign_badge_end = original_badge_gate(exe);
    expanded_badges = exe.expanded_profile();
}

inline bool original_badge_fallback(int token, const LeImage* exe = nullptr) {
    const int team = token & 0xfff;
    if (exe ? exe->expanded_profile() : expanded_badges) return team == 0xfff;
    const int end = exe ? original_badge_gate(*exe) : original_foreign_badge_end;
    return team == 0xfff || (team >= 90 && team < end) || (team >= 168 && team < 357);
}

}  // namespace bmh
