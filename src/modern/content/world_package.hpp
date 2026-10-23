// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Opening a Modern world package (docs/new_engine, M3, data format F1).
//
// A Modern world is an OBW world built on an imported Classic installation: its Classic
// payload (x-classic, x-legacy, assets) provides the original executable data, database,
// texts and artwork; its world tables (clubs, leagues, players, ...) describe the Modern
// world. The Modern engine opens only such worlds; sealed Classic packages are refused.

#include <filesystem>
#include <memory>

#include "formats/content.hpp"
#include "modern/content/world.hpp"

namespace bmh::modern {

struct WorldPackage {
    std::shared_ptr<const content::Classic> classic;  // mounted for the original data files
    std::shared_ptr<const World> world;
};

// Reads and checks a Modern world package. Throws FormatError when the package is not a
// Modern world (no runtime:modern-v1 / editor:world-v1 requirement, a sealed Classic
// package, or no Classic payload).
WorldPackage open_world_package(const std::filesystem::path& file);

// The world of the running Modern game (one game per process), set by the session.
const World* current_world();
void set_current_world(std::shared_ptr<const World> world);

}  // namespace bmh::modern
