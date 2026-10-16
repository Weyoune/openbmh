// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

#include "game/state.hpp"

namespace bmh {

// HighscoreOperation / GameEndAlternateScoreScreen. Names retain Original ISO646-DE bytes.
struct HighscoreRow {
    int32_t score = 0;
    std::string club_name;
    std::string manager_name;
    std::array<uint16_t, 3> titles{};
    // Preserve unused/trailing bytes of an existing Source row when saving it again.
    std::array<uint8_t, 72> original_bytes{};
};
using HighscoreTable = std::array<HighscoreRow, 5>;
using HighscoreBytes = std::array<uint8_t, 360>;

struct HighscoreProjection {
    uint16_t signature = 0; // CareerSession_FormatError_RuntimeField7: any nonzero signature selects its own ranked file.
    bool ranked = false;   // signature0 selects Source's alternate visualization.
    HighscoreTable rows{};
};

struct HighscoreScore {
    int32_t assets = 0;
    int32_t patience = 0;
    int32_t facilities = 0;
    int32_t team = 0;
    int32_t history = 0;
    int32_t titles = 0;
    int32_t total = 0;
};

enum class HighscoreContext { ActiveManager, SeasonBoundary };
// Assets in HighscoreScore use global ActiveManager even when scoring another manager. At the
// season boundary DailyInterest's preceding manager loop leaves ActiveManager=manager_count.
// Pass that context explicitly without modifying the caller's continuation state.
HighscoreScore highscore_score(const GameState& g, int manager,
                              HighscoreContext context = HighscoreContext::ActiveManager);
HighscoreRow highscore_candidate(const GameState& g, int manager,
                                HighscoreContext context = HighscoreContext::ActiveManager);
void insert_highscore(HighscoreTable& rows, const HighscoreRow& candidate);
HighscoreProjection make_highscore_projection(const GameState& g, HighscoreTable rows = {},
    HighscoreContext context = HighscoreContext::ActiveManager);

HighscoreBytes encode_highscore_table(const HighscoreTable& rows);
HighscoreTable decode_highscore_table(const HighscoreBytes& bytes);
// The caller supplies the configured installation base and Original TEXTD889 prefix.
std::filesystem::path highscore_file(const std::filesystem::path& base,
                                    uint16_t signature, const std::string& prefix = "high/h.");
HighscoreTable load_highscore_table(const std::filesystem::path& file);
bool save_highscore_table(const std::filesystem::path& file, const HighscoreTable& rows);

} // namespace bmh
