// SPDX-License-Identifier: GPL-3.0-only
// Debug mode (command line -debug): a process-wide, mutex-protected log file plus numbered
// automatic savegames, both kept in one folder. State is a function-local static; every
// function is a no-op until init() has succeeded, so callers may log unconditionally.
// init() also redirects stderr into the same folder. Header-only.
#pragma once

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>

namespace bmh::app::debug {

// The debug mode (command line -debug): a log with timestamps next to everything the game prints
// to stderr, and a savegame after every played match day, both in one sub folder of the save
// directory. Off by default; every function is a no-op until init() has run.
struct State {
    bool enabled = false;
    std::filesystem::path directory;
    unsigned saves = 0;
    std::mutex mutex;
};
inline State& state() { static State s; return s; }

inline bool enabled() { return state().enabled; }
inline const std::filesystem::path& directory() { return state().directory; }

inline std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm parts{};
#ifdef _WIN32
    localtime_s(&parts, &t);
#else
    localtime_r(&t, &parts);
#endif
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    char text[40];
    std::snprintf(text, sizeof text, "%04d-%02d-%02d %02d:%02d:%02d.%03d", parts.tm_year + 1900, parts.tm_mon + 1,
                  parts.tm_mday, parts.tm_hour, parts.tm_min, parts.tm_sec, int(ms));
    return text;
}

// One line, flushed at once so that a crash or a hang loses nothing.
inline void log(const std::string& line) {
    auto& s = state();
    if (!s.enabled) return;
    std::lock_guard<std::mutex> lock(s.mutex);
    std::ofstream out(s.directory / "openbmh-debug.log", std::ios::app);
    if (out) out << timestamp() << "  " << line << '\n';
}

// Creates the folder and redirects stderr into the same folder, so the messages the game
// prints on its own (for example the live overview errors) are kept as well.
inline bool init(const std::filesystem::path& directory) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec || !std::filesystem::is_directory(directory)) return false;
    auto& s = state();
    s.directory = directory;
    s.enabled = true;
    const auto errors = (directory / "openbmh-stderr.log").string();
    if (std::FILE* redirected = std::freopen(errors.c_str(), "a", stderr)) std::setvbuf(redirected, nullptr, _IONBF, 0);
    log("debug mode on, folder " + directory.string());
    return true;
}

// The file name of the next automatic savegame: DBG00001.FBM, DBG00002.FBM, ... (8.3 like the original).
inline std::filesystem::path next_save_path() {
    auto& s = state();
    char name[16];
    std::snprintf(name, sizeof name, "DBG%05u.FBM", ++s.saves);
    return s.directory / name;
}

}  // namespace bmh::app::debug
