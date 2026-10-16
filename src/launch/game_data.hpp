// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Finding and checking the original game data OpenBMH runs on.
//
// The player names one of: an installed game directory, the game's EXE in it (BMH.EXE of
// the German versions, FLTD.EXE of the English "Football Limited"), or a ZIP/7z archive
// holding exactly one such game. An archive is unpacked once into the cache directory (a
// folder per archive content hash) and reused on the next start.
//
// OpenBMH has one game and match engine (the supported rule profile rules); the original files are only
// data. The check therefore asks whether the data is complete and readable, not which
// edition it is. Fan extras next to the files (pictures, manuals) are ignored.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace bmh::launch {

enum class GameFamily {
    unknown,
    german,    // BMH.EXE + TEXTD.BLK + M3D.MAN (CD 1996, CLASSIC/UNLIMITED/supported rule profile editions)
    english,   // FLTD.EXE + TEXTE.BLK + M3E.MAN (Football Limited)
    disk1994,  // M3.EXE + M3PC.* (the 1994 floppy version): recognised, not supported
};

struct GameCheck {
    GameFamily family = GameFamily::unknown;
    std::filesystem::path directory;  // the game directory (where the EXE is)
    std::filesystem::path exe;        // the EXE as found (its case may differ)
    std::string title;                // e.g. "Bundesliga Manager Hattrick 2026-27 (supported rule profile)"
    std::vector<std::string> problems;  // missing or unreadable files; empty = data complete
    // Data complete and the engine can run it. False also for complete data the engine does
    // not run yet (`unsupported` says why).
    bool playable = false;
    std::string unsupported;
};

// The file `name` in `directory`, matched without regard to case (the packages mix
// "BMH.exe", "BMH.EXE" and "20.vga"); nullopt if there is none.
std::optional<std::filesystem::path> find_file(const std::filesystem::path& directory, const std::string& name);

// Checks an installed game directory (or the directory of a named EXE). A directory without
// a game EXE but with exactly one sub directory holding one (an edition folder with BMH\
// inside) is followed.
GameCheck check_game_directory(const std::filesystem::path& path);

struct ResolvedGame {
    GameCheck check;
    std::string error;  // why no game directory could be found (empty: check is valid)
    bool unpacked = false;  // the directory comes from the archive cache
};

// Resolves what the player gave: a directory, an EXE or an archive. `cache` is where
// archives are unpacked (see default_cache_directory()).
ResolvedGame resolve_game(const std::filesystem::path& input, const std::filesystem::path& cache);

// The per-user cache for unpacked archives: %LOCALAPPDATA%\OpenBMH\games,
// ~/Library/Application Support/OpenBMH/games, $XDG_DATA_HOME/openbmh/games.
std::filesystem::path default_cache_directory();

// One line per problem, for message boxes and the console.
std::string describe(const ResolvedGame& game);

}  // namespace bmh::launch
