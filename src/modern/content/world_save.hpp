// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Saves of Modern careers (docs/new_engine, M6; data format F8: save_profile modern-career-v1).
//
// A Classic save embeds its sealed world. A Modern world is large (artwork included), so a
// Modern save names its world (id, data revision) and holds the career only: the game state,
// the Modern additions (names and identities by runtime index), both random generators, the
// turn driver and the session context. Loading needs the same world.

#include <filesystem>

#include "cli/json.hpp"
#include "game/random.hpp"
#include "game/turn_driver.hpp"
#include "modern/content/world.hpp"
#include "modern/game/modern_state.hpp"

namespace bmh::modern {

struct SavedWorldCareer {
    ModernState state;
    WatcomRandom random{0};
    GameRandom game{0};
    cli::Json driver, session;
    Bytes header;
};

void save_world_career(const std::filesystem::path& path, const World& world, const ModernState& state,
                       const WatcomRandom& random, const GameRandom& game, const TurnDriver& driver, bool overwrite,
                       const cli::Json& session = {});

// Throws FormatError when the save is damaged, not a Modern save or belongs to another world.
SavedWorldCareer load_world_career(const std::filesystem::path& path, const World& world);

}  // namespace bmh::modern
