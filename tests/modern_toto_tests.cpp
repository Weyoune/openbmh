// SPDX-License-Identifier: GPL-3.0-only
// The Modern Toto with up to six columns (docs/new_engine/toto.md): the rules of a coupon
// (columns, stakes, total, duplicates) and its cost, on a synthetic coupon block.
#include <cstdio>

#include "formats/executable_layout.hpp"
#include "modern/game/desk_actions.hpp"
#include "modern/game/modern_state.hpp"

using namespace bmh;
using namespace bmh::modern;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// One manager, an open coupon of eleven matches.
ModernState coupon() {
    ModernState g;
    g.manager_teams = {0};
    g.manager_records.assign(0xb0c, 0);
    g.memory.add_block(::bmh::layout::Coupon, 5 * 0x14);
    g.memory.add_block(::bmh::layout::DeskActions_SubmitPoolsCoupon_RuntimeField, 44);
    g.memory.add_block(::bmh::layout::CareerSession_DispatchToto_RuntimeField, 1);
    g.memory.set8(::bmh::layout::CareerSession_DispatchToto_RuntimeField, 11);
    return g;
}

std::array<uint8_t, 11> column(uint8_t first) {
    std::array<uint8_t, 11> c{};
    c.fill(1);
    c[0] = first;
    return c;
}
}  // namespace

int main() {
    {
        ModernState g = coupon();
        check(submit_pools_columns(g, 0, 50000, {column(1), column(0), column(2)}) == PoolsColumnsResult::ok, "three columns");
        check(g.memory.u32(::bmh::layout::Coupon) == 50000 && g.pools.at(0).extra.size() == 2, "column 1 on the coupon, 2-3 extra");
        check(submit_pools_columns(g, 0, 50000, {column(1), column(1)}) == PoolsColumnsResult::duplicate_column, "no two columns alike");
        check(submit_pools_columns(g, 0, 100000, {column(0), column(1), column(2), std::array<uint8_t, 11>{}}) == PoolsColumnsResult::ok,
              "four columns of 100,000: 400,000 in all");
        check(submit_pools_columns(g, 0, 100000, {column(0), column(1), column(2), std::array<uint8_t, 11>{}, column(2)}) ==
                  PoolsColumnsResult::total_too_high, "five columns of 100,000 exceed 400,000");
        std::vector<std::array<uint8_t, 11>> seven;
        for (int i = 0; i < 7; ++i) {
            auto c = column(1);
            c[1] = uint8_t(i % 3), c[2] = uint8_t(i / 3);
            seven.push_back(c);
        }
        check(submit_pools_columns(g, 0, 10000, seven) == PoolsColumnsResult::too_many_columns, "at most six columns");
        check(submit_pools_columns(g, 0, 90000, {seven[0], seven[1], seven[2], seven[3], seven[4]}) == PoolsColumnsResult::total_too_high,
              "five columns of 90,000 exceed 400,000");
        check(submit_pools_columns(g, 0, 5000, {column(1)}) == PoolsColumnsResult::stake_out_of_range, "stake at least 10,000");
        check(submit_pools_columns(g, 0, 0, {}) == PoolsColumnsResult::ok && g.memory.u32(::bmh::layout::Coupon) == 0 &&
              g.pools.at(0).extra.empty(), "a stake of 0 clears every column");
        g.memory.set8(::bmh::layout::CareerSession_DispatchToto_RuntimeField, 9);
        check(submit_pools_columns(g, 0, 50000, {column(1)}) == PoolsColumnsResult::invalid, "only with eleven matches");
    }
    // the cost: the stake per column, 10 % more for every column beyond the first
    check(pools_cost(100000, 1) == 100000, "one column costs the stake");
    check(pools_cost(100000, 4) == 430000, "four columns of 100,000 cost 430,000");
    check(pools_cost(60000, 6) == 390000, "six columns of 60,000 cost 390,000");
    if (failures) return 1;
    std::puts("modern toto checks passed");
    return 0;
}
