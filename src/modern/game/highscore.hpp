// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/highscore.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

#include "modern/game/modern_state.hpp"

namespace bmh::modern {

// HighscoreOperation / GameEndAlternateScoreScreen. Names retain Original ISO646-DE bytes.



// Assets in HighscoreScore use global ActiveManager even when scoring another manager. At the
// season boundary DailyInterest's preceding manager loop leaves ActiveManager=manager_count.
// Pass that context explicitly without modifying the caller's continuation state.
HighscoreScore highscore_score(const ModernState& g, int manager,
                              HighscoreContext context = HighscoreContext::ActiveManager);
HighscoreRow highscore_candidate(const ModernState& g, int manager,
                                HighscoreContext context = HighscoreContext::ActiveManager);
void insert_highscore(HighscoreTable& rows, const HighscoreRow& candidate);
HighscoreProjection make_highscore_projection(const ModernState& g, HighscoreTable rows = {},
    HighscoreContext context = HighscoreContext::ActiveManager);

HighscoreBytes encode_highscore_table(const HighscoreTable& rows);
HighscoreTable decode_highscore_table(const HighscoreBytes& bytes);
// The caller supplies the configured installation base and Original TEXTD889 prefix.
std::filesystem::path highscore_file(const std::filesystem::path &base, uint16_t signature,
                                     const std::string &prefix = "high/h.", bool native = false);
HighscoreTable load_highscore_table(const std::filesystem::path& file);
bool save_highscore_table(const std::filesystem::path& file, const HighscoreTable& rows);

} // namespace bmh::modern
