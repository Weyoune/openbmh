// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "content/package.hpp"
#include "formats/native_state.hpp"
#include "game/turn_driver.hpp"
namespace bmh::content {
struct SavedCareer {
    GameState state;
    WatcomRandom random;
    GameRandom game;
    cli::Json driver, session;
    Bytes header;
};
void save_career(const std::filesystem::path &, const Classic &, const GameState &, const WatcomRandom &,
                 const GameRandom &, const TurnDriver &, bool overwrite, const cli::Json &session = {});
SavedCareer load_career(const std::filesystem::path &, const Classic &);
Bytes save_information(const std::filesystem::path &);
cli::Json driver_checkpoint(const TurnDriver &);
void restore_driver(TurnDriver &, const cli::Json &);
// Native settings/layout files retain the validated Classic bytes with a typed envelope.
Bytes load_preferences(const std::filesystem::path &, const char *kind, size_t size);
void save_preferences(const std::filesystem::path &, const char *kind, const Bytes &, bool overwrite);
} // namespace bmh::content
