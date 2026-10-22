// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Both random generators of the original (docs/RNG.md). Must stay bit-exact.
//
// Both classes count their draws; the match engine tests compare these counts with
// recordings of the original, minute by minute.
#include <cstdint>

namespace bmh {

// Debugging aid: called before every draw of a generator (0 the game generator, 1 Watcom).
inline void (*draw_hook)(int generator) = nullptr;

// Watcom C runtime rand()/srand(); random(lo, hi) = WatcomRandom.
// Seeded from the clock at program start, so its sequence is not reproducible from a
// savegame; recordings capture its state (RandomRuntimeField) directly.
class WatcomRandom {
public:
    explicit WatcomRandom(uint32_t seed = 1) : seed_(seed) {}
    void srand(uint32_t seed) { seed_ = seed; }
    uint32_t seed() const { return seed_; }
    uint64_t calls() const { return calls_; }
    void restore(uint32_t seed, uint64_t calls) {
        seed_ = seed;
        calls_ = calls;
    }
    int rand() {
        calls_++;
        if (draw_hook) draw_hook(1);
        seed_ = seed_ * 0x41C64E6Du + 0x3039u;
        return int(seed_ >> 16 & 0x7FFF);
    }
    int range(int lo, int hi) {
        int n = hi - lo + 1;
        if (n < 1) return lo;
        int32_t prod = int32_t(uint32_t(rand()) * uint32_t(n));
        // arithmetic shift with rounding toward zero, as compiled by Watcom
        return (prod < 0 ? -((-prod) >> 15) : prod >> 15) + lo;
    }

private:
    uint32_t seed_;
    uint64_t calls_ = 0;
};

// Game's own deterministic generator, GameRandom; seed CareerSession_Save_RuntimeField lives in the savegame.
// All arithmetic is signed 32 bit with wrap-around (shl/imul/idiv in the original).
class GameRandom {
public:
    explicit GameRandom(int32_t seed = 0) : seed_(seed) {}
    int32_t seed() const { return seed_; }
    void set_seed(int32_t seed) { seed_ = seed; }
    uint64_t calls() const { return calls_; }
    void restore(int32_t seed, uint64_t calls) {
        seed_ = seed;
        calls_ = calls;
    }
    int32_t range(int32_t lo, int32_t hi) {
        calls_++;
        if (draw_hook) draw_hook(0);
        seed_ = int32_t(uint32_t(seed_) * 16u + 0xA7u) % 4000000;
        int32_t prod = int32_t(uint32_t(seed_) * uint32_t(hi - lo + 1));
        return prod / 4000000 + lo;
    }

private:
    int32_t seed_;
    uint64_t calls_ = 0;
};

}  // namespace bmh
