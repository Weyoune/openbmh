// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "launch/mods.hpp"
namespace bmh::launch {
int run_launcher(const std::filesystem::path &executable, const SaveMigrator &);
} // namespace bmh::launch
