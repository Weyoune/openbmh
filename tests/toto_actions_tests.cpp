// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>

#include "game/desk_actions.hpp"

using namespace bmh;

namespace {
void check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
}

int main() {
    GameState g;
    g.manager_teams = {12};
    g.manager_records.assign(0xb0c, 0);
    g.memory.add_block(::bmh::layout::Coupon, 5 * 0x14);
    g.memory.add_block(::bmh::layout::DeskActions_SubmitPoolsCoupon_RuntimeField, 44);
    g.memory.add_block(::bmh::layout::CareerSession_DispatchToto_RuntimeField, 1);
    for (int i = 0; i < 11; ++i) {
        g.memory.set16(::bmh::layout::DeskActions_SubmitPoolsCoupon_RuntimeField + uint32_t(i) * 4, uint16_t(i * 2));
        g.memory.set16(::bmh::layout::DeskActions_SubmitPoolsCoupon_RuntimeField + uint32_t(i) * 4 + 2, uint16_t(i * 2 + 1));
    }
    g.memory.set8(::bmh::layout::CareerSession_DispatchToto_RuntimeField, 11);
    std::array<uint8_t, 11> tips{};
    for (size_t i = 0; i < tips.size(); ++i) tips[i] = uint8_t(i % 3);
    g.memory.set32(::bmh::layout::Coupon + 4, 0x12345678);
    check(submit_pools_coupon(g, 0, 50000, tips), "valid coupon submits");
    check(g.memory.u32(::bmh::layout::Coupon) == 50000, "stake stored at manager record base");
    check(g.memory.u32(::bmh::layout::Coupon + 4) == 0x12345678, "intervening record bytes preserved");
    for (size_t i = 0; i < tips.size(); ++i) check(g.memory.u8(::bmh::layout::Coupon + 8 + uint32_t(i)) == tips[i], "pick stored in +8..+12");
    const uint32_t stake_before = g.memory.u32(::bmh::layout::Coupon);
    const uint8_t pick_before = g.memory.u8(::bmh::layout::Coupon + 8);
    auto invalid = tips; invalid[4] = 3;
    check(!submit_pools_coupon(g, 0, 50000, invalid), "invalid outcome rejected");
    check(!submit_pools_coupon(g, 0, 9999, tips), "stake below original minimum rejected");
    check(!submit_pools_coupon(g, 0, 400001, tips), "stake above original maximum rejected");
    check(submit_pools_coupon(g, 0, 400000, tips), "original maximum stake accepted");
    check(g.memory.u32(::bmh::layout::Coupon) == 400000, "maximum stake stored");
    g.memory.set32(::bmh::layout::Coupon, 50000);
    check(g.memory.u32(::bmh::layout::Coupon) == stake_before && g.memory.u8(::bmh::layout::Coupon + 8) == pick_before,
          "rejected requests leave coupon unchanged");
    check(!submit_pools_coupon(g, 1, 50000, tips), "unknown manager rejected");
    check(g.memory.u32(::bmh::layout::Coupon) == stake_before, "invalid manager preserves coupon");
    g.memory.set8(::bmh::layout::CareerSession_DispatchToto_RuntimeField, 10);
    check(!submit_pools_coupon(g, 0, 50000, tips), "coupon requires all eleven matches");
    check(g.memory.u32(::bmh::layout::Coupon) == stake_before, "unavailable coupon preserves stake");
    std::puts("toto actions: stake, eleven 1/X/2 picks, original boundaries and atomic rejection OK");
    return 0;
}
