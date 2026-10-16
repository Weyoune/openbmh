// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <filesystem>
#include <string>

#include "formats/font.hpp"
#include "formats/le.hpp"
#include "formats/textd.hpp"
#include "game/table.hpp"

namespace bmh::ui {

struct SeriesRecordEntry {
    uint8_t score_byte = 0;
    uint16_t opponent_name_offset = 0xffff;
    uint16_t saved_date = 0xffff;
    std::string opponent_name;
};

struct SeriesRecordsProjection {
    int manager_slot = -1;
    int runtime_team = -1;
    int league = -1;
    int league_row = -1;
    int rounds = 0;
    bool source_available = true;
    bool gradient_text = true;
    std::array<uint8_t, 3> source_flags{};
    std::array<uint8_t, 3> series_indices{};
    std::array<bmh::LeagueTable, 3> tables{};
    std::array<uint8_t, 21> current_streaks{};
    std::array<uint8_t, 21> manager_best_streaks{};
    std::array<SeriesRecordEntry, 8> records{};
    std::string club_name;
};

SeriesRecordsProjection series_records_from_game(const GameState&, const LeImage& exe, int manager_slot);

class SeriesRecordsScreen {
public:
    SeriesRecordsScreen(const std::filesystem::path& data_directory,
                        SeriesRecordsProjection projection, Image accepted_parent_frame);
    Image render() const;
private:
    SeriesRecordsProjection projection_;
    Image backdrop_;
    LeImage exe_;
    TextDb texts_;
    Font font_;
    void text(Image&, int x, int baseline, std::string_view value, uint8_t color) const;
};

} // namespace bmh::ui
