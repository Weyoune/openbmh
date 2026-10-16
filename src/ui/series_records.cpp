// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "series_records.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>

#include "game/messages.hpp"
#include "game/schedule.hpp"
#include "ui/desk_state.hpp"
#include "ui/manager.hpp"
#include "ui/setup.hpp"

namespace bmh::ui {
namespace {
constexpr size_t kManagerRecord = 0xb0c;
constexpr size_t kClubRecord = 0x4a;
constexpr uint32_t kSeriesSelector = ::bmh::layout::SeriesSelector;
constexpr uint32_t kTableFlags = ::bmh::layout::TableFlags;

uint16_t read16(const std::vector<uint8_t>& bytes, size_t at) {
    return uint16_t(bytes.at(at) | (uint16_t(bytes.at(at + 1)) << 8));
}

std::string pool_name(const std::vector<uint8_t>& names, uint16_t offset) {
    if (offset == 0xffff || offset >= names.size())
        throw FormatError("series records: saved opponent name offset is invalid");
    const auto first = names.begin() + offset;
    const auto last = std::find(first, names.end(), uint8_t(0));
    if (last == names.end()) throw FormatError("series records: unterminated saved opponent name");
    std::string result;
    result.reserve(size_t(last - first));
    for (auto it = first; it != last; ++it) result.push_back(char(*it));
    return result;
}

std::string number(int value, int width) {
    std::string out = std::to_string(value);
    if (int(out.size()) < width) out.insert(0, size_t(width - int(out.size())), '%');
    return out;
}
}

SeriesRecordsProjection series_records_from_game(const GameState& game, const LeImage& exe,
                                                  int manager_slot) {
    if (manager_slot < 0 || manager_slot >= 4 ||
        size_t(manager_slot + 1) * kManagerRecord > game.manager_records.size())
        throw FormatError("series records: manager record missing");
    const size_t manager_at = size_t(manager_slot) * kManagerRecord;
    const int team = int(read16(game.manager_records, manager_at + 0x290));
    if (team < 0 || team > 0x165)
        throw FormatError("series records: invalid Source manager club");

    SeriesRecordsProjection out;
    out.manager_slot = manager_slot;
    out.runtime_team = team;
    out.gradient_text = (!game.memory.has(::bmh::layout::TextGradientDisabled) || game.memory.u8(::bmh::layout::TextGradientDisabled) == 0) &&
                        exe.u8(::bmh::layout::TextGradientMode) == 2;
    if (team >= 0x5a || game.club_table.size() < 168 * kClubRecord) {
        out.source_available = false; // TeamLeague shows SeriesRecordsRuntimeField outside its 0..89 table range.
        return out;
    }
    const int league = team_league(team);
    if (league < 0 || league >= kLeagueCount || league >= int(game.league_rounds.size()))
        throw FormatError("series records: saved league round count is unavailable");

    out.league = league;
    out.league_row = team - league_first_team(league);
    out.rounds = game.league_rounds[size_t(league)];
    if (out.league_row < 0 || out.league_row >= kLeagueSizes[size_t(league)])
        throw FormatError("series records: manager club is outside the Source league");
    out.club_name = game.club_name(team);

    const size_t club_at = size_t(team) * kClubRecord;
    std::copy_n(game.club_table.begin() + std::ptrdiff_t(club_at), 21, out.current_streaks.begin());
    std::copy_n(game.manager_records.begin() + std::ptrdiff_t(manager_at + 0x5e), 21,
                out.manager_best_streaks.begin());

    for (size_t col = 0; col < 3; ++col) {
        out.source_flags[col] = exe.u8(kTableFlags + uint32_t(col));
        out.series_indices[col] = exe.u8(kSeriesSelector + uint32_t(col));
        if (out.series_indices[col] >= 3)
            throw FormatError("series records: invalid Original streak selector");
        out.tables[col] = compute_league_table(game, league, 0, out.rounds,
                                                out.source_flags[col], 0);
    }

    for (size_t record = 0; record < out.records.size(); ++record) {
        auto& target = out.records[record];
        target.score_byte = game.club_table.at(club_at + 0x15 + record);
        target.opponent_name_offset = read16(game.club_table, club_at + 0x2a + record * 2);
        target.saved_date = read16(game.club_table, club_at + 0x3a + record * 2);
        if (target.score_byte != 0)
            target.opponent_name = pool_name(game.club_names, target.opponent_name_offset);
    }
    return out;
}

SeriesRecordsScreen::SeriesRecordsScreen(const std::filesystem::path& data,
                                         SeriesRecordsProjection projection,
                                         Image accepted_parent_frame)
    : projection_(std::move(projection)), backdrop_(std::move(accepted_parent_frame)),
      exe_(load_le(asset_path(data, "BMH.exe"))),
      texts_(load_textd(asset_path(data, "TEXTD.BLK"))),
      font_(load_font(asset_path(data / "F", "BMH3.FNT"))) {
    if (projection_.manager_slot < 0 || projection_.manager_slot >= 4 ||
        projection_.runtime_team < 0 || projection_.runtime_team > 0x165 ||
        (projection_.source_available &&
         (projection_.league < 0 || projection_.league >= kLeagueCount ||
          projection_.league_row < 0 || projection_.league_row >= kLeagueSizes[size_t(projection_.league)])) ||
        backdrop_.width != 320 || backdrop_.height != 240)
        throw FormatError("series records: incomplete Original projection");
}

void SeriesRecordsScreen::text(Image& out, int x, int baseline, std::string_view value,
                               uint8_t color) const {
    const int width = font_.text_width(value);
    if (x < 0 || x + width > out.width || baseline < 5 || baseline >= out.height) return;
    Image mask(std::max(1, width), 6);
    font_.draw(mask, 0, 0, value, 1);
    int ramp = 0;
    while (ramp < 32 && exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2 + uint32_t(ramp)) != color) ++ramp;
    for (int y = 0; y < 6; ++y)
        for (int xx = 0; xx < width; ++xx)
            if (mask.at(xx, y)) out.at(x + xx, baseline - 5 + y) = projection_.gradient_text
                ? exe_.u8(::bmh::layout::Advertising_Text_RuntimeField + uint32_t(ramp * 6 + y)) : color;
}

Image SeriesRecordsScreen::render() const {
    Image out = backdrop_;
    const std::string column_heading = texts_[909];
    // TeamLeague sends the complete TEXTD 909 string to the Original text
    // routine. The '%' bytes are part of the Original string, not separators.
    text(out, 0x83, 0x28 + 5, column_heading, 0x68);
    if (!projection_.source_available) {
        const std::string message = texts_[911];
        text(out, (320 - font_.text_width(message)) / 2, 100, message, 0x6a);
        return out;
    }
    int y = 0x25;
    for (size_t row = 0; row < 19; ++row) {
        std::string label = texts_[766 + row];
        if (!label.empty() && label.front() == '!') {
            y += 3;
            label.erase(label.begin());
            text(out, 7, y + 5, label, 0x5f);
            const int underline_y = y + 1;
            const int underline_end = std::min(319, 7 + font_.text_width(label) + 3);
            for (int x = 7; x <= underline_end; ++x) out.at(x, underline_y) = 0x5f;
            // Source's ! rows have their own 3+12 spacing and no data columns.
            y += 12;
            continue;
        } else {
            text(out, 12, y + 5, label, 0x68);
        }

        if (row >= 1 && row <= 7) {
            const size_t metric = row - 1;
            for (size_t col = 0; col < 3; ++col) {
                const size_t at = metric * 3 + projection_.series_indices[col];
                const std::string value = number(projection_.current_streaks[at], 2) + "(" +
                    number(projection_.manager_best_streaks[at], 2) + ")";
                text(out, 128 + int(col) * 40, y + 5, value, 0x6c);
            }
        } else if (row == 8 || row == 9) {
            for (size_t col = 0; col < 3; ++col) {
                const auto& table = projection_.tables[col];
                const size_t team = size_t(projection_.league_row);
                const int games = table.games(int(team));
                const int total = row == 8 ? table.goals_for[team] : table.goals_against[team];
                const int tenths = games > 0 ? total * 10 / games : 0;
                const std::string value = number(tenths / 10, 2) + exe_.cstring(::bmh::layout::SeriesRecords_Render_ExecutableField3) +
                    number(tenths % 10, 1) + exe_.cstring(::bmh::layout::DeskRecords_FillRect_ExecutableField2);
                text(out, 128 + int(col) * 40, y + 5, value, 0x6c);
            }
        } else if (row >= 11 && row <= 18) {
            const size_t index = row - 11;
            const auto& record = projection_.records[index];
            std::string value;
            if (record.score_byte == 0) {
                value = texts_[482];
            } else {
                const int high = record.score_byte >> 4, low = record.score_byte & 0x0f;
                if (index == 0 || index == 1 || index == 4 || index == 5) {
                    const int first = index < 4 ? high : low;
                    const int second = index < 4 ? low : high;
                    value = number(first, 0) + exe_.cstring(::bmh::layout::SeriesRecords_Render_ExecutableField4) + number(second, 0) + exe_.cstring(::bmh::layout::DeskRecords_FillRect_ExecutableField2);
                }
                else
                    value = number(high ? high : low, 0);
                value += texts_[871];
                value += record.opponent_name;
            }
            text(out, 124, y + 5, value, 0x6c);
            if (!value.empty() && value.front() > '/' && value.front() < ':' && record.saved_date != 0xffff) {
                const DeskDate date = desk_date(record.saved_date, exe_);
                std::string date_text = texts_[910] + format_number(GameState{}, date.day, 2) +
                    exe_.cstring(::bmh::layout::OtherGamesState_ResultText_ExecutableField) + format_number(GameState{}, date.month, 2) +
                    exe_.cstring(::bmh::layout::OtherGamesState_ResultText_ExecutableField) + format_number(GameState{}, date.year, 0x44) +
                    exe_.cstring(::bmh::layout::SeriesRecords_Render_ExecutableField2) + exe_.cstring(::bmh::layout::SeriesRecords_Render_ExecutableField);
                for (char& c : date_text) if (c == '%') c = '0';
                text(out, 0xfa, y + 5, date_text, 0x5f);
            }
        }
        y += 7 + ((row != 0 && row % 7 == 0) ? 2 : 0);
    }
    return out;
}

} // namespace bmh::ui
