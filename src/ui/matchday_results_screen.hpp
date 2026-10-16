// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <filesystem>
#include <vector>

#include "formats/font.hpp"
#include "ui/matchday_results.hpp"

namespace bmh::ui {

class MatchdayResultsScreen {
public:
    MatchdayResultsScreen(const std::filesystem::path& data, MatchdayResults results);
    const MatchdayResults& results() const { return results_; }
    size_t page() const { return page_; }
    size_t page_count() const { return page_count_; }
    bool close_requested() const { return close_requested_; }
    Image render() const;
    std::string state_json() const;
    bool click(int x, int y);
    bool key(std::string_view key);

private:
    struct Row { size_t group; size_t match; };
    MatchdayResults results_;
    std::vector<Row> rows_;
    Font font_;
    size_t page_ = 0;
    size_t page_count_ = 1;
    bool close_requested_ = false;
    void text(Image&, int x, int y, std::string_view, uint8_t color, int max_width = 300) const;
};

} // namespace bmh::ui
