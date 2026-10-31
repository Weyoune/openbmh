// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/world12/fixtures.hpp"
#include "formats/fbm.hpp"
namespace bmh::world12 {
struct CareerSnapshot {
    World world;
    SeasonResults results;
    uint32_t date=0,season=0,watcom_seed=1;
    int32_t game_seed=0;
    std::array<uint8_t,14> league_rounds{};
    // Typed active career snapshots will extend this record as their owning
    // progression transactions are integrated. No legacy FBM stream is guessed.
    void validate()const;
    bool operator==(const CareerSnapshot&)const=default;
};
Savegame make_save(const CareerSnapshot&);
CareerSnapshot read_save(const Savegame&);
}
