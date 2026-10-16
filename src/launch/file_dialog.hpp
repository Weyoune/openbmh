// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The system's "open file" dialog for choosing the game when OpenBMH starts without one:
// a ZIP/7z archive or the game's EXE. Windows, macOS and Linux (GTK or the desktop portal)
// through nativefiledialog-extended.

#include <filesystem>
#include <optional>
#include <string>

namespace bmh::launch {

// nullopt when the player cancels; throws std::runtime_error when no dialog can be shown
// (e.g. no desktop session on Linux).
std::optional<std::filesystem::path> choose_game_file(const std::string& title);

}  // namespace bmh::launch
