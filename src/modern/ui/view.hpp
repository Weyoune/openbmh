// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "launch/mods.hpp"

namespace bmh::modern_ui {
struct Config {
  std::filesystem::path world, gui_assets, screenshot, commands, state;
  std::string scene = "main", theme;
  int width = 1280, height = 720;
};
// Returns before Classic initialization. The demonstration never starts a
// match.
int run(const std::filesystem::path &executable, const launch::SaveMigrator &,
        const Config &);
bool is_modern_world(const std::filesystem::path &);
} // namespace bmh::modern_ui
