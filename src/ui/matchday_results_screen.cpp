// SPDX-License-Identifier: GPL-3.0-only
#include "ui/matchday_results_screen.hpp"

#include <algorithm>
#include <sstream>
#include <string>

#include "ui/setup.hpp"
#include "ui/manager.hpp"

namespace bmh::ui {
namespace {
constexpr size_t kRowsPerPage = 5;
void fill(Image& image, uint8_t color) {
    std::fill(image.pixels.begin(), image.pixels.end(), color);
}
std::string score(const MatchdayResultMatch& match) {
    if (match.home_goals && match.away_goals)
        return std::to_string(*match.home_goals) + ":" + std::to_string(*match.away_goals);
    switch (match.status) {
    case MatchResultStatus::postponed: return "Verlegt";
    case MatchResultStatus::not_played: return "Nicht gespielt";
    case MatchResultStatus::played: return "Ergebnis offen";
    case MatchResultStatus::unknown: return "Unbekannt";
    }
    return "Unbekannt";
}
}

MatchdayResultsScreen::MatchdayResultsScreen(const std::filesystem::path& data, MatchdayResults results)
    : results_(std::move(results)), font_(load_font(asset_path(data / "F", "BMH3.FNT"))) {
    if (!results_.valid) throw FormatError("results: snapshot is not valid");
    for (size_t g = 0; g < results_.groups.size(); ++g) {
        auto& group = results_.groups[g];
        if (group.competition.empty()) throw FormatError("results: missing competition name");
        for (size_t m = 0; m < group.matches.size(); ++m) {
            const auto& match = group.matches[m];
            if (match.home.empty() || match.away.empty()) throw FormatError("results: missing team name");
            if (match.home_goals.has_value() != match.away_goals.has_value() ||
                (match.home_goals && (*match.home_goals < 0 || *match.home_goals > 31 ||
                                      *match.away_goals < 0 || *match.away_goals > 31)))
                throw FormatError("results: invalid score pair");
            rows_.push_back({g, m});
        }
    }
    page_count_ = std::max<size_t>(1, (rows_.size() + kRowsPerPage - 1) / kRowsPerPage);
}

void MatchdayResultsScreen::text(Image& image, int x, int y, std::string_view value,
                                 uint8_t color, int max_width) const {
    std::string clipped;
    try { clipped = utf8_to_original(value); }
    catch (const FormatError&) {
        // Unknown database glyphs remain visible without aborting the modal.
        for (unsigned char c : value) clipped += c < 128 ? char(c) : '?';
    }
    while (!clipped.empty() && font_.text_width(clipped) > max_width) clipped.pop_back();
    if (x >= 0 && y >= 0 && y + font_.height <= image.height)
        font_.draw(image, x, y, clipped, color);
}

Image MatchdayResultsScreen::render() const {
    Image image(320, 240);
    fill(image, 0x15);
    for (int x = 8; x < 312; ++x) { image.at(x, 8) = 0x4f; image.at(x, 231) = 0x4f; }
    for (int y = 8; y < 232; ++y) { image.at(8, y) = 0x4f; image.at(311, y) = 0x4f; }
    text(image, 18, 17, "ERGEBNISSE", 0x4f, 284);
    text(image, 18, 31, "Spieltag " + std::to_string(results_.matchday) + "   " + results_.date_label, 0x47, 284);
    text(image, 18, 48, "Wettbewerb", 0x4f, 94);
    text(image, 114, 48, "Heim", 0x4f, 75);
    text(image, 192, 48, "Stand", 0x4f, 49);
    text(image, 245, 48, "Gast", 0x4f, 60);
    const size_t begin = page_ * kRowsPerPage;
    const size_t end = std::min(rows_.size(), begin + kRowsPerPage);
    for (size_t i = begin; i < end; ++i) {
        const auto& row = rows_[i];
        const auto& group = results_.groups[row.group];
        const auto& match = group.matches[row.match];
        const int y = 68 + int(i - begin) * 28;
        text(image, 18, y, group.competition, 0x47, 94);
        text(image, 114, y, match.home, 0x4f, 75);
        text(image, 192, y, score(match), 0x4f, 49);
        text(image, 245, y, match.away, 0x4f, 60);
    }
    text(image, 18, 214, "< Zurueck", page_ ? 0x4f : 0x40, 82);
    text(image, 111, 214, std::to_string(page_ + 1) + "/" + std::to_string(page_count_), 0x47, 62);
    text(image, 190, 214, "Weiter >", page_ + 1 < page_count_ ? 0x4f : 0x40, 66);
    text(image, 258, 214, "Ende", 0x4f, 42);
    return image;
}

std::string MatchdayResultsScreen::state_json() const {
    std::ostringstream out;
    out << "{\"screen\":\"matchday_results\",\"page\":" << page_
        << ",\"page_count\":" << page_count_ << ",\"matchday\":" << results_.matchday
        << ",\"date\":" << results_.date << ",\"close_requested\":"
        << (close_requested_ ? "true" : "false") << '}';
    return out.str();
}

bool MatchdayResultsScreen::click(int x, int y) {
    if (close_requested_ || y < 202 || y > 230) return false;
    if (x >= 250) { close_requested_ = true; return true; }
    if (x >= 174 && x < 250 && page_ + 1 < page_count_) { ++page_; return true; }
    if (x < 100 && page_) { --page_; return true; }
    return false;
}

bool MatchdayResultsScreen::key(std::string_view value) {
    if (close_requested_) return false;
    if (value == "ESC" || value == "ENTER") { close_requested_ = true; return true; }
    if ((value == "RIGHT" || value == "SPACE") && page_ + 1 < page_count_) { ++page_; return true; }
    if (value == "LEFT" && page_) { --page_; return true; }
    return false;
}

} // namespace bmh::ui
