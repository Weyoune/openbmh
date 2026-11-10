// SPDX-License-Identifier: GPL-3.0-only
// state_digest: see digest.hpp.
#include "game/digest.hpp"

#include "game/save.hpp"

namespace bmh {
namespace {

// FNV-1a, 64 bit: simple, stable across platforms and compilers (no std::hash).
struct Fnv {
    uint64_t value = 0xcbf29ce484222325ull;
    void byte(uint8_t b) { value = (value ^ b) * 0x100000001b3ull; }
    // (integers are fed byte by byte, least significant first, so the result does not depend on
    //  the byte order of the machine)
    void u64(uint64_t v) {
        for (int i = 0; i < 8; i++) byte(uint8_t(v >> (i * 8)));
    }
    void i64(int64_t v) { u64(uint64_t(v)); }
};

}  // namespace

uint64_t state_digest(const GameState& g, int32_t game_seed, uint32_t rand_seed) {
    Fnv h;
    // 1. everything that is saved, in the block order of the original
    const Bytes stream = save_stream(g);
    h.u64(stream.size());
    for (uint8_t b : stream) h.byte(b);
    // 2. the run-time values a savegame does not hold (GameState members marked "not saved")
    h.i64(g.indoor);
    h.i64(g.level);
    h.i64(g.chance_rain);
    h.i64(g.chance_temperature);
    h.i64(g.last_shooter);
    h.i64(g.last_pick_quality);
    h.i64(g.live_scene);
    h.u64(g.live_matches.size());
    for (int team : g.live_matches) h.i64(team);
    for (uint16_t home : g.sportschau_homes) h.u64(home);
    for (uint8_t priority : g.sportschau_priorities) h.u64(priority);
    h.i64(g.sportschau_selection_available);
    h.i64(g.list_toggle);
    h.i64(g.lineups_frozen);
    h.i64(g.pools_open);
    h.i64(g.autosave_months);
    h.i64(g.autosave_month);
    h.u64(g.date);
    h.u64(g.season_start);
    h.i64(g.season);
    h.i64(g.development_balance);
    h.i64(g.trend_rotation);
    // 3. both random generators
    h.i64(game_seed);
    h.u64(rand_seed);
    return h.value;
}

}  // namespace bmh
