// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <filesystem>
#include <map>
#include <string>

#include "cli/json.hpp"
#include "formats/le.hpp"
#include "formats/m3d.hpp"
#include "formats/textd.hpp"

namespace bmh::obw {

// An archive entry, deliberately independent of GameState and CareerSession.
// Keys are relative, authored paths; strings are binary-safe (including NUL).
using Documents = std::map<std::string, std::string>;

struct Options {
    std::string start_date; // Required: legacy databases have ages, not birth dates.
    std::string title = "Imported legacy world";
    // Empty selects BMH-classic / FLTD-classic from the installation.
    std::string mod;
    // Explicit developer-only partial export; the default requires a complete installation.
    bool database_only = false;
};

Documents convert_database(const Database& database, const TextDb& texts,
                           const LeImage& executable, bool english, const Options& options);
Documents import_installation(const std::filesystem::path& source, const Options& options);

// Checks the declared import profile, references and (for Classic) complete payload.
// This is not a generic competition engine or a JSON Schema implementation.
void validate(const Documents& documents);
Documents read(const std::filesystem::path& source);

// Refuses existing destinations. Publishes a complete sibling staging path by rename.
// No files in the installation are changed, copied back or executed.
void write(const Documents& documents, const std::filesystem::path& destination, bool directory);

} // namespace bmh::obw
