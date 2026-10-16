// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/team_statistics.hpp"

#include <algorithm>
#include <cstddef>
#include <string>

#include "game/schedule.hpp"
#include "game/season.hpp"
#include "ui/manager.hpp"
#include "ui/setup.hpp"

namespace bmh::ui {
namespace {
void copy(Image& out, const Image& src, int sx, int sy, int x, int y, int w, int h,
          int transparent = -1) {
    if (sx < 0 || sy < 0 || w < 0 || h < 0 || sx + w > src.width || sy + h > src.height)
        throw FormatError("team statistics: Source asset bounds");
    for (int yy = 0; yy < h; ++yy) for (int xx = 0; xx < w; ++xx) {
        const uint8_t value = src.pixels[size_t(sy + yy) * size_t(src.width) + size_t(sx + xx)];
        if (value != transparent && x + xx >= 0 && y + yy >= 0 && x + xx < out.width && y + yy < out.height)
            out.at(x + xx, y + yy) = value;
    }
}
void rect(Image& out, int x, int y, int w, int h, uint8_t value) {
    for (int yy = std::max(0, y); yy < std::min(out.height, y + h); ++yy)
        for (int xx = std::max(0, x); xx < std::min(out.width, x + w); ++xx) out.at(xx, yy) = value;
}
void source_frame(Image& out, const Image& widgets, const LeImage& exe,
                  int x, int y, int w, int h, uint8_t color) {
    for (int yy = 0; yy < h - 7; ++yy) for (int xx = 0; xx < w - 7; ++xx)
        out.at(x + 4 + xx, y + 4 + yy) = exe.u8(::bmh::layout::Advertising_PatternRect_RuntimeField + (yy & 15) * 16 + (xx & 15));
    copy(out, widgets, 0, 0, x, y, 4, 4); copy(out, widgets, 4, 0, x + w - 4, y, 4, 4);
    copy(out, widgets, 0, 4, x, y + h - 4, 4, 4); copy(out, widgets, 4, 4, x + w - 4, y + h - 4, 4, 4);
    for (int xx = 4; xx < w - 4; xx += 74) {
        const int amount = std::min(74, w - 4 - xx);
        copy(out, widgets, 0, 104, x + xx, y, amount, 4); copy(out, widgets, 0, 108, x + xx, y + h - 4, amount, 4);
    }
    for (int yy = 4; yy < h - 4; yy += 96) {
        const int amount = std::min(96, h - 4 - yy);
        copy(out, widgets, 0, 8, x, y + yy, 4, amount); copy(out, widgets, 4, 8, x + w - 4, y + yy, 4, amount);
    }
    int ramp = 0; while (ramp < 32 && exe.u8(::bmh::layout::Advertising_Text_RuntimeField2 + ramp) != color) ++ramp;
    const uint8_t shadow = exe.u8(::bmh::layout::Advertising_Text_RuntimeField + ramp * 6 + 5);
    rect(out, x + 2, y + h, w, 2, 0x40); rect(out, x + w, y + 2, 2, h, 0x40);
    (void)shadow;
}
std::string source_field(int value, int width) {
    std::string s = std::to_string(value);
    if (int(s.size()) < width) s.insert(0, size_t(width - int(s.size())), '%');
    return s;
}
std::string pointer_text(const TextDb& texts, uint32_t pointer_address) {
    if (pointer_address < ::bmh::layout::TextPointerTable || ((pointer_address - ::bmh::layout::TextPointerTable) & 3) != 0)
        throw FormatError("team statistics: invalid Source TEXTD pointer");
    return texts[(pointer_address - ::bmh::layout::TextPointerTable) / 4];
}
std::string source_text(const TextDb& texts, uint32_t address) {
    return pointer_text(texts, address);
}
std::string source_league_name(const GameState& game, int league, const TextDb& texts) {
    if (league < 0 || league >= kLeagueCount) throw FormatError("team statistics: Source league name outside range");
    if (league < 5 && game.texts) return bmh::league_name(game, league);
    if (league < 5) {
        constexpr std::array<char, 5> suffix{0, 0, 'A', 'B', 0};
        int index = 0;
        for (int l = 0; l < league; ++l) if (uint8_t(suffix[size_t(l + 1)]) < 0x42) ++index;
        std::string name = source_text(texts, ::bmh::layout::Achievements_FormatError_TextPointer3 + uint32_t(index) * 4);
        if (suffix[size_t(league)] != 0) {
            name += source_text(texts, ::bmh::layout::Manager_FormatError_TextPointer2);
            if (!name.empty()) name.back() = suffix[size_t(league)];
        }
        return name;
    }
    const int mapped = league - 4;
    std::string name;
    if (game.memory.has(::bmh::layout::TeamStatistics_SourceLeagueName_RuntimeField) && game.memory.u8(::bmh::layout::TeamStatistics_SourceLeagueName_RuntimeField) != 0)
        name = source_text(texts, ::bmh::layout::SportschauAwards_MatchdayIntroView_TextPointer);
    name += game.club_name((mapped & 0x7f) + 0x165);
    return name;
}
}

TeamStatisticsProjection team_statistics_from_game(const GameState& game, int team,
                                                     TeamStatisticsMode mode,
                                                     const LeImage& exe, const TextDb& texts) {
    if (team < 0 || team > 0x165) throw FormatError("team statistics: runtime team outside LeagueCalculations range");
    const int mode_number = int(mode);
    if (mode_number < 0 || mode_number > 2) throw FormatError("team statistics: invalid mode");
    if (team >= 138 && team < 168)
        throw FormatError("team statistics: simulated teams require Source draw-state ratings and are not exposed here");
    TeamStatisticsProjection out;
    out.runtime_team = team;
    out.mode = mode;
    out.source_badge_fallback = team == 0x0fff || (team >= 90 && team <= 137) || (team >= 168 && team < 0x165);
    out.badge_heap = game.club_heap_78;
    if (team != 0x165) {
        int region_team = team;
        const int league_gate = exe.u8(::bmh::layout::LeaguePairings_LeaguePairingsConstants_RuntimeField);
        for (int league = 0; league < 9; ++league) {
            region_team -= exe.u8(::bmh::layout::LeagueSizes + uint32_t(league));
            if (region_team < 0) {
                if (league >= league_gate) out.flag_index = uint8_t(league - league_gate + 1);
                break;
            }
            if (league == 8 && region_team >= int(exe.u16(::bmh::layout::CareerSession_InstallLeagueSizes_RuntimeField6)))
                out.flag_index = uint8_t((region_team - int(exe.u16(::bmh::layout::CareerSession_InstallLeagueSizes_RuntimeField6))) / 7 - league_gate + 10);
        }
    }
    out.source_has_league_panel = team < 0xa8;
    out.league = team > 0x164 ? 0 : team_league(team);
    if (size_t(team) < game.club_ids.size()) out.original_team_name = game.club_name(team);
    if (out.source_has_league_panel) {
        if (out.league < 0 || out.league >= kLeagueCount)
            throw FormatError("team statistics: Source league index unavailable");
        out.league_row = team - league_first_team(out.league);
        if (out.league_row < 0 || out.league_row >= kLeagueSizes[out.league])
            throw FormatError("team statistics: runtime team is not in a supported league table");
        out.source_flags = exe.u8(::bmh::layout::TableFlags + uint32_t(mode_number));
        out.points_mode = exe.u8(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField + uint32_t(out.league));
        out.original_league_name = source_league_name(game, out.league, texts);
        out.table = compute_league_table(game, out.league, 0, league_matchdays(out.league),
                                         out.source_flags, out.points_mode);
        out.league_rounds = out.league < int(game.league_rounds.size()) ? game.league_rounds[size_t(out.league)] : 0;
        out.club_name_pool = game.club_names;
        if (size_t(team) * 0x4a + 0x4a <= game.club_table.size()) {
            std::copy_n(game.club_table.begin() + static_cast<std::ptrdiff_t>(size_t(team) * 0x4a),
                        out.team_record.size(), out.team_record.begin());
            out.has_team_record = true;
        }
        if (size_t(team) * 0x1a + 0x1a <= game.position_history.size()) {
            std::copy_n(game.position_history.begin() + static_cast<std::ptrdiff_t>(size_t(team) * 0x1a),
                        out.position_history.size(), out.position_history.begin());
            out.source_has_position_history = true;
        }
    } else if (team == 0x165) {
        // Source 357 maps to league 0 for common labels, but the save does not
        // contain a position-history row for this national-team pseudo-club.
        out.source_has_position_history = false;
    }
    out.gradient_text = (!game.memory.has(::bmh::layout::TextGradientDisabled) || game.memory.u8(::bmh::layout::TextGradientDisabled) == 0) && exe.u8(::bmh::layout::TextGradientMode) == 2;
    out.decorated_background = !game.memory.has(::bmh::layout::PlainBackground) || game.memory.u8(::bmh::layout::PlainBackground) == 0;

    if (team < 138) {
        constexpr std::array<uint16_t, 5> average_flags{0x1f1, 0x1f2, 0x1f4, 0x1f8, 0x1ff};
        for (size_t i = 0; i < average_flags.size(); ++i)
            out.source_averages[i] = uint16_t(team_average(game, team, average_flags[i]).value);
        const SquadRange range = squad_range(team);
        std::array<uint32_t, 5> total{};
        std::array<uint16_t, 5> count{};
        const size_t end = std::min<size_t>(range.count, 11);
        for (size_t row = 0; row < end && range.first + row < game.squad_slots.size(); ++row) {
            const uint16_t slot = game.squad_slots[range.first + row];
            const int id = slot & 0x0fff;
            if (id == 0x0fff || size_t(id) >= game.players.size()) continue;
            const auto& player = game.players[size_t(id)];
            const int position = player.position();
            if (position < 0 || position >= 4) continue;
            ++count[size_t(position)]; ++count[4];
            total[size_t(position)] += uint32_t(player.strength());
            total[4] += uint32_t(player.strength());
        }
        for (size_t i = 0; i < count.size(); ++i)
            out.average_strength[i] = count[i] ? uint16_t(total[i] / count[i]) : 0;
    }
    if (game.memory.has(::bmh::layout::ActiveManager)) {
        const int manager = game.memory.u8(::bmh::layout::ActiveManager);
        const size_t start = size_t(manager) * 0x690;
        if (manager < 4 && start + out.active_manager_results.size() <= game.manager_results.size()) {
            out.active_manager_slot = manager;
            std::copy_n(game.manager_results.begin() + static_cast<std::ptrdiff_t>(start),
                        out.active_manager_results.size(), out.active_manager_results.begin());
            out.has_active_head_to_head = true;
            if (game.memory.has(::bmh::layout::ManagerCount) && game.memory.u8(::bmh::layout::ActiveManager) < game.memory.u8(::bmh::layout::ManagerCount)) {
                const int owned = manager < int(game.manager_teams.size()) ? game.manager_teams[size_t(manager)] : -1;
                out.active_manager_team = owned;
                if (owned >= 0 && size_t(owned) < game.club_ids.size())
                    out.active_manager_team_name = game.club_name(owned);
                out.source_has_head_to_head = team != owned && team < 0xa8 && owned != 0x165;
            }
        }
    }
    return out;
}

TeamStatisticsScreen::TeamStatisticsScreen(const std::filesystem::path& data,
                                           TeamStatisticsProjection projection, Image club_info_backdrop)
    : projection_(std::move(projection)), data_directory_(data), exe_(load_le(asset_path(data, "BMH.exe"))),
      font_(load_font(asset_path(data / "F", "BMH3.FNT"))), header_font_(load_font(asset_path(data / "F", "BMH2.FNT"))), sprite_font_(data),
      texts_(load_textd(asset_path(data, "TEXTD.BLK"))), backdrop_(std::move(club_info_backdrop)),
      widgets_(load_vga(asset_path(data / "KB", "0.VGA"))),
      flags_(load_vga(asset_path(data / "KB", "24.VGA"))), faces_(load_vga(asset_path(data / "KB", "27.VGA"))),
      ratings_(load_vga(asset_path(data / "KB", "30.VGA"))) {
    if (backdrop_.width != 320 || backdrop_.height != 240 || projection_.runtime_team < 0 ||
        projection_.runtime_team > 0x165 ||
        (projection_.source_has_league_panel && (projection_.league < 0 || projection_.league >= kLeagueCount ||
         projection_.table.league != projection_.league || projection_.league_row < 0 ||
         projection_.league_row >= projection_.table.size)))
        throw FormatError("team statistics: invalid owned projection/backdrop");
    if (!projection_.source_badge_fallback) {
        if (size_t(projection_.runtime_team) * 2 + 1 >= projection_.badge_heap.size())
            throw FormatError("team statistics: Source crest selector missing from owned projection");
        const uint16_t selector = uint16_t(projection_.badge_heap[size_t(projection_.runtime_team) * 2] |
            uint16_t(projection_.badge_heap[size_t(projection_.runtime_team) * 2 + 1]) << 8) & 0x1ff;
        crest_ = load_vga(asset_path(data / "W", std::to_string(selector >> 3) + ".VGA"));
    }
}

void TeamStatisticsScreen::text(Image& out, int x, int baseline, std::string_view value, uint8_t color) const {
    const int width = font_.text_width(value);
    if (x < 0 || x + width > out.width || baseline < 5 || baseline >= out.height) return;
    Image mask(std::max(1, width), 6); font_.draw(mask, 0, 0, value, 1);
    int ramp = 0; while (ramp < 32 && exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2 + ramp) != color) ++ramp;
    for (int yy = 0; yy < 6; ++yy) for (int xx = 0; xx < width; ++xx)
        if (mask.at(xx, yy)) out.at(x + xx, baseline - 5 + yy) = projection_.gradient_text
            ? exe_.u8(::bmh::layout::Advertising_Text_RuntimeField + ramp * 6 + yy) : color;
}

void TeamStatisticsScreen::sprite_text(Image& out, int x, int baseline, std::string_view value, int mode) const {
    sprite_font_.draw(out, x, baseline, value, mode);
}

void TeamStatisticsScreen::button(Image& out, int x, std::string_view label, bool selected) const {
    const int width = 50, height = 10;
    const int source_y = selected ? 44 : 74;
    if (selected) { const uint8_t base = out.at(x, 195); rect(out, x, 185, width + 1, 2, base); rect(out, x, 185, 2, height + 1, base); x += 2; }
    else { rect(out, x + 2, 195, width, 2, 0x40); rect(out, x + width, 187, 2, 9, 0x40); }
    const int draw_y = 185 + (selected ? 2 : 0);
    copy(out, widgets_, 24, source_y, x, draw_y, 31, 10);
    int at = 31, remaining = width - 31;
    while (remaining > 20) { const int n = std::min(8, remaining - 20); copy(out, widgets_, 24, source_y + 10, x + at, draw_y, n, 10); at += n; remaining -= n; }
    copy(out, widgets_, 45 - remaining, source_y + 20, x + at, draw_y, remaining, 10);
    if (label.empty()) return;
    const int text_mode = exe_.u8(::bmh::layout::Advertising_Button_RuntimeField + int(selected));
    if (text_mode == 3) {
        const int tw = font_.text_width(label);
        text(out, x + width / 2 - tw / 2, draw_y + 7, label, 0);
    } else {
        const std::string bytes(label);
        sprite_text(out, x + width / 2 - sprite_font_.text_width(bytes) / 2,
                    draw_y + 7, bytes, text_mode);
    }
}

Image TeamStatisticsScreen::render() const {
    Image out = backdrop_;
    // PatternStartsAt(0,38,320,162,0x68,0x12). Source pattern starts at source (0,0).
    for (int y = 42; y <= 196; ++y) for (int x = 4; x <= 316; ++x)
        out.at(x, y) = exe_.u8(::bmh::layout::Advertising_PatternRect_RuntimeField + ((y - 42) & 15) * 16 + ((x - 4) & 15));
    copy(out, widgets_, 0, 0, 0, 38, 4, 4); copy(out, widgets_, 4, 0, 316, 38, 4, 4);
    copy(out, widgets_, 0, 4, 0, 196, 4, 4); copy(out, widgets_, 4, 4, 316, 196, 4, 4);
    for (int x = 4; x < 316; x += 74) {
        const int w = std::min(74, 316 - x);
        copy(out, widgets_, 0, 104, x, 38, w, 4); copy(out, widgets_, 0, 108, x, 196, w, 4);
    }
    for (int y = 42; y < 196; y += 96) {
        const int h = std::min(96, 197 - y);
        copy(out, widgets_, 0, 8, 0, y, 4, h); copy(out, widgets_, 4, 8, 316, y, 4, h);
    }

    const int mode = int(projection_.mode);
    int face_difference = 0;
    copy(out, flags_, (projection_.flag_index % 12) * 9, (projection_.flag_index / 12) * 6, 5, 42, 9, 6);
    const int header_y = 49 - header_font_.baseline;
    if (header_y >= 0 && header_y + header_font_.height <= out.height)
        header_font_.draw(out, 20, header_y, projection_.original_team_name, 0x5f);
    rect(out, 3, 50, 313, 1, 0x69);
    if (!projection_.source_has_league_panel) {
        const std::string first = pointer_text(texts_, ::bmh::layout::TeamStatistics_Render_TextPointer22), second = pointer_text(texts_, ::bmh::layout::TeamStatistics_Render_TextPointer23);
        text(out, (320 - font_.text_width(first)) / 2, 108, first, 0x6c);
        text(out, (320 - font_.text_width(second)) / 2, 116, second, 0x6c);
    } else {
        // Int3: category averages and KB/30 bars; the projection owns the
        // first-eleven calculations and render never asks Core for new values.
        const int overall = projection_.average_strength[4];
        for (int position = 0; position < 4; ++position) {
            int height = overall ? int(projection_.average_strength[size_t(position)]) * 100 / overall : 0;
            height = std::clamp(height, 0x5b, 0x6d) - 0x5a;
            const int x = 220 + position * 23, top = 130 - height;
            copy(out, ratings_, 0, 27, x, 130, 21, 5, 0);
            copy(out, ratings_, 0, 5, x, top, 21, height, 0);
            copy(out, ratings_, 0, 0, x, top - 3, 21, 5, 0);
        }
        const size_t row = size_t(projection_.league_row);
        const int games = projection_.table.games(int(row));
        const int wins = projection_.table.wins[row];
        const int defeats = projection_.table.defeats[row];
        const int draws = std::max(0, games - wins - defeats);
        const std::string mode_string = mode == 0 ? pointer_text(texts_, ::bmh::layout::TeamStatistics_Render_TextPointer5) :
            mode == 1 ? pointer_text(texts_, ::bmh::layout::TeamStatistics_Render_TextPointer10) : pointer_text(texts_, ::bmh::layout::TeamStatistics_Render_TextPointer7);
        const int rank = int(projection_.table.rank(int(row))) + 1;
        const std::string league_rank = (games > 0 ? source_field(rank, 1) + pointer_text(texts_, ::bmh::layout::TeamStatistics_Render_TextPointer24) : std::string()) +
            projection_.original_league_name;
        text(out, 314 - font_.text_width(league_rank), 48, league_rank, 0x6c);
        text(out, 6, 62, mode_string, 0x6b);
        text(out, 80, 62, source_field(wins, 2) + pointer_text(texts_, ::bmh::layout::TeamStatistics_Render_TextPointer6) +
             source_field(draws, 2) + pointer_text(texts_, ::bmh::layout::TeamStatistics_Render_TextPointer8) + source_field(defeats, 2) + pointer_text(texts_, ::bmh::layout::TeamStatistics_Render_TextPointer9), 0x6c);
        std::string league_line = pointer_text(texts_, ::bmh::layout::TeamStatistics_Render_TextPointer11) + pointer_text(texts_, ::bmh::layout::TeamStatistics_Render_RuntimeField3 + uint32_t(mode) * 4);
        text(out, 6, 70, league_line, 0x6b);
        const int gf10 = games ? int(projection_.table.goals_for[row]) * 10 / games : 0;
        const int ga10 = games ? int(projection_.table.goals_against[row]) * 10 / games : 0;
        text(out, 80, 70, source_field(gf10 / 10, 2) + exe_.cstring(::bmh::layout::SeriesRecords_Render_ExecutableField3) +
             source_field(gf10 % 10, 1) + exe_.cstring(::bmh::layout::DeskRecords_FillRect_ExecutableField2) + exe_.cstring(::bmh::layout::SeriesRecords_Render_ExecutableField4) +
             source_field(ga10 / 10, 2) + exe_.cstring(::bmh::layout::SeriesRecords_Render_ExecutableField3) +
             source_field(ga10 % 10, 1) + exe_.cstring(::bmh::layout::DeskRecords_FillRect_ExecutableField2), 0x6c);
        std::string average_values = exe_.cstring(::bmh::layout::CupDraw_FormatError_ExecutableField2);
        for (size_t i = 0; i < projection_.source_averages.size(); ++i) {
            average_values += source_field(projection_.source_averages[i], 2);
            if (i == 0) average_values += pointer_text(texts_, ::bmh::layout::CupDraw_FormatError_TextPointer5);
            else if (i == 1) average_values += pointer_text(texts_, ::bmh::layout::LeaguePairings_FormatError_TextPointer8);
            else if (i == 2) average_values += pointer_text(texts_, ::bmh::layout::LeaguePairings_FormatError_TextPointer9);
            else if (i == 3) average_values += exe_.cstring(::bmh::layout::CupDraw_FormatError_TextPointer2);
            else average_values += exe_.cstring(::bmh::layout::CupDraw_FormatError_TextPointer3);
        }
        text(out, 218, 142, average_values, 0x6c);

        if (projection_.has_team_record) {
        // LeagueCalculations's recent-record rows use up to three event rows from the
        // clicked club's owned 0x4a-byte record. Source suppresses repeated
        // event/side pairs through local_68 and never prints more than 3 rows.
        const int record_side = exe_.u8(::bmh::layout::TeamStatistics_Render_RuntimeField + uint32_t(mode));
        const uint32_t mode_pointer = ::bmh::layout::TeamStatistics_Render_RuntimeField3 + uint32_t(mode) * 4;
        text(out, 6, 129, pointer_text(texts_, ::bmh::layout::TeamStatistics_Render_TextPointer12) + pointer_text(texts_, mode_pointer), 0x6b);
        std::array<bool, 21> seen{};
        int printed = 0;
        for (int metric = 0; metric < 7 && printed < 3; ++metric) {
            const int kind = record_side;
            const int field = metric * 3 + kind;
            if (kind < 0 || kind >= 3 || projection_.team_record[size_t(field)] <= 1 ||
                (metric == 4 && seen[size_t(kind * 7)]) || (metric == 3 && seen[size_t(kind * 7 + 1)])) continue;
            std::string record_text = source_field(projection_.team_record[size_t(field)], 1) +
                pointer_text(texts_, ::bmh::layout::TeamStatistics_Render_TextPointer13) +
                pointer_text(texts_, ::bmh::layout::TeamStatistics_Render_TextPointer3 + uint32_t(record_side) * 4) +
                pointer_text(texts_, ::bmh::layout::TeamStatistics_Render_TextPointer4 + uint32_t(metric) * 4);
            text(out, 6, 137 + 8 * printed, record_text, 0x5f);
            seen[size_t(metric + kind * 7)] = true;
            ++printed;
        }
        if (printed == 0) text(out, 6, 145, pointer_text(texts_, ::bmh::layout::TeamStatistics_Render_TextPointer14), 0x5f);

        // SizeT5 emits mode-selected home or away extrema and includes the
        // stored opponent pool-name offset; never replace it with current IDs.
        auto extrema = [&](int metric) {
            std::string value;
            const uint8_t packed = projection_.team_record[size_t(0x15 + metric)];
            if (packed == 0) return std::pair<std::string, uint8_t>{pointer_text(texts_, ::bmh::layout::TeamStatistics_Render_TextPointer21), packed};
            if (metric == 0 || metric == 1 || metric == 4 || metric == 5) {
                const int numerator = metric < 4 ? packed >> 4 : packed & 0x0f;
                const int encoded = metric < 4 ? packed & 0x0f : packed >> 4;
                value = source_field(numerator, 0) + exe_.cstring(::bmh::layout::SeriesRecords_Render_ExecutableField4) + source_field(encoded, 0) + exe_.cstring(::bmh::layout::DeskRecords_FillRect_ExecutableField2);
            } else {
                const int number = (packed >> 4) ? packed >> 4 : packed & 0x0f;
                value = source_field(number, 0);
            }
            value += pointer_text(texts_, ::bmh::layout::TeamStatistics_Render_TextPointer25);
            const int name_offset = int(projection_.team_record[size_t(0x2a + metric * 2)]) |
                (int(projection_.team_record[size_t(0x2b + metric * 2)]) << 8);
            if (name_offset >= 0 && size_t(name_offset) < projection_.club_name_pool.size()) {
                const char* name = reinterpret_cast<const char*>(projection_.club_name_pool.data() + name_offset);
                const size_t available = projection_.club_name_pool.size() - size_t(name_offset);
                const size_t length = std::find(name, name + available, '\0') - name;
                value.append(name, length);
            }
            return std::pair<std::string, uint8_t>{std::move(value), packed};
        };
        for (int i = 0; i < 4; ++i) {
            int metric = i + (mode == 1 ? 4 : mode == 2 ? 4 : 0);
            auto selected = extrema(metric);
            if (mode == 2) {
                auto home = extrema(metric - 4);
                auto away = extrema(metric);
                const uint8_t h = home.second, a = away.second;
                if (h == 0 && a == 0) selected = std::move(home);
                else {
                    int hs = int(h >> 4) - int(h & 0x0f);
                    int as = int(a >> 4) - int(a & 0x0f);
                    if (metric == 4 || metric == 6) as = -as;
                    if (metric == 5 || metric == 7) hs = -hs;
                    selected = hs <= as ? std::pair<std::string, uint8_t>{away.first + pointer_text(texts_, ::bmh::layout::TeamStatistics_Render_TextPointer16), a}
                                        : std::pair<std::string, uint8_t>{home.first + pointer_text(texts_, ::bmh::layout::TeamStatistics_Render_TextPointer15), h};
                }
            }
            const uint32_t label_address = (mode == 2 ? ::bmh::layout::TeamStatistics_Render_TextPointer2 : ::bmh::layout::TeamStatistics_Render_TextPointer) + uint32_t(metric) * 4;
            const uint8_t row_color = exe_.u8(::bmh::layout::TeamStatistics_Render_RuntimeField2 + uint32_t(metric % 2));
            text(out, 6, 91 + 8 * i, pointer_text(texts_, label_address), row_color);
            text(out, 84, 91 + 8 * i, selected.first, row_color);
        }
        }
        if (projection_.source_has_head_to_head && projection_.has_active_head_to_head) {
            const uint32_t at = uint32_t(projection_.runtime_team) * 10;
            if (at + 10 <= projection_.active_manager_results.size()) {
                text(out, 6, 168, pointer_text(texts_, ::bmh::layout::TeamStatistics_Render_TextPointer17) + projection_.active_manager_team_name, 0x6b);
                std::string scores = pointer_text(texts_, ::bmh::layout::TeamStatistics_Render_TextPointer18);
                for (int side = 0; side < 2; ++side) {
                for (int match = 0; match < 5; ++match) {
                    const uint8_t packed = projection_.active_manager_results[at + size_t((1 - side) * 5 + match)];
                    if (packed == 0xff) continue;
                    if (match != 0) scores += exe_.cstring(::bmh::layout::TeamStatistics_Render_ExecutableField);
                    const int home = packed >> 4, away = packed & 0x0f;
                    if (mode == side || mode == 2) face_difference += (home > away) - (home < away);
                    scores += source_field(home, 1) + exe_.cstring(::bmh::layout::SeriesRecords_Render_ExecutableField4) +
                              source_field(away, 1) + exe_.cstring(::bmh::layout::DeskRecords_FillRect_ExecutableField2);
                }
                if (side == 0 && scores.size() == 3) scores += pointer_text(texts_, ::bmh::layout::TeamStatistics_Render_TextPointer19);
                if (side == 0) scores += pointer_text(texts_, ::bmh::layout::TeamStatistics_Render_TextPointer20);
            }
            if (!scores.empty() && scores.back() == ' ') scores += pointer_text(texts_, ::bmh::layout::TeamStatistics_Render_TextPointer19);
            text(out, 6, 176, scores, 0x6c);
            }
        }
    }

    // Original team crest/frame, face, and saved-history panel around SourceBadgeFallback/SourceFrame.
    for (int x = 220; x < 300; x += 49) {
        // PatternStartsAt rectangle calls at (x,53) and PaintsFourBorderEdges border.
        source_frame(out, widgets_, exe_, x, 53, 42, 42, 0x69);
    }
    source_frame(out, widgets_, exe_, 220, 145, 92, 52, 0x69);
    // Source border rectangle uses the endpoint convention of PaintsFourBorderEdges.
    for (int x = 221; x <= 260; ++x) { out.at(x, 54) = 0x46; out.at(x, 93) = 0x40; }
    for (int y = 54; y <= 93; ++y) { out.at(221, y) = 0x46; out.at(260, y) = 0x40; }
    for (int x = 270; x <= 309; ++x) { out.at(x, 54) = 0x46; out.at(x, 93) = 0x40; }
    for (int y = 54; y <= 93; ++y) { out.at(270, y) = 0x46; out.at(309, y) = 0x40; }
    face_difference = std::clamp(face_difference / 2, -2, 2);
    copy(out, faces_, (face_difference + 2) * 38, 0, 222, 55, 38, 38);
    if (projection_.source_badge_fallback) copy(out, widgets_, 8, 0, 271, 55, 38, 38);
    else if (crest_.width >= 38) {
        const uint16_t selector = uint16_t(projection_.badge_heap[size_t(projection_.runtime_team) * 2] |
            uint16_t(projection_.badge_heap[size_t(projection_.runtime_team) * 2 + 1]) << 8) & 0x1ff;
        copy(out, crest_, (selector & 7) * 38, 0, 271, 55, 38, 38);
    }
    if (projection_.source_has_league_panel || projection_.runtime_team == 0x165) {
        const int league = projection_.league;
        rect(out, 225, 150, 1, 43, 0x6a); rect(out, 225, 192, 81, 1, 0x6a);
        if (projection_.source_has_position_history && projection_.source_has_league_panel) {
        const int clubs = kLeagueSizes[league];
        for (int round = 0; round < projection_.league_rounds && round < 39; ++round) {
            const size_t word_index = size_t(round / 3) * 2;
            const uint16_t packed = uint16_t(projection_.position_history[word_index] |
                uint16_t(projection_.position_history[word_index + 1]) << 8);
            const int rank_value = (packed >> (5 * (round % 3))) & 0x1f;
            int kind = 1;
            if (rank_value < int(exe_.u8(::bmh::layout::CareerSession_InstallLeagueSizes_RuntimeField + league)) + int(exe_.u8(::bmh::layout::DeskDrawers_Curve_RuntimeField + league)) ||
                (rank_value == 0 && league == 0)) kind = 0;
            if (int(exe_.u8(::bmh::layout::LeagueSizes + league)) - int(exe_.u8(::bmh::layout::CareerSession_InstallLeagueSizes_RuntimeField2 + league)) <= rank_value) kind = 2;
            for (int series = 0; series < 2; ++series) {
                const int x = 226 + round * 2 + series;
                if (x >= 306) continue;
                const int y = 193 - (clubs - rank_value) * 2;
                const uint8_t color = exe_.u8(::bmh::layout::DeskDrawers_Curve_ExecutableField + kind * 8 + series * 4);
                for (int yy = std::min(y, 191); yy <= std::max(y, 191); ++yy)
                    if (x >= 0 && x < out.width && yy >= 0 && yy < out.height) out.at(x, yy) = color;
            }
        }
        }
    }
    constexpr std::array<int, 4> xs{1, 55, 109, 167};
    for (int i = 0; i < 4; ++i) {
        const auto id = static_cast<TeamStatisticsButton>(i);
        const bool selected = i < 3 && mode == i;
        const bool active = selected || pressed_button_ == id;
        button(out, xs[size_t(i)], texts_[size_t(i)], active);
    }
    return out;
}

std::optional<TeamStatisticsButton> TeamStatisticsScreen::button_at(int x, int y) const {
    if (y < 185 || y > 195) return std::nullopt;
    constexpr std::array<int, 4> xs{1, 55, 109, 167};
    for (int i = 0; i < 4; ++i)
        if (x >= xs[size_t(i)] && x <= xs[size_t(i)] + 50) return static_cast<TeamStatisticsButton>(i);
    return std::nullopt;
}
void TeamStatisticsScreen::press(int x, int y) { pressed_button_ = button_at(x, y); }
bool TeamStatisticsScreen::move(int x, int y) {
    if (pressed_button_ && button_at(x, y) != pressed_button_) { pressed_button_.reset(); return true; }
    return false;
}
TeamStatisticsRequest TeamStatisticsScreen::release(int x, int y) {
    TeamStatisticsRequest out; out.runtime_team = projection_.runtime_team; out.mode = projection_.mode;
    const auto pressed = pressed_button_, released = button_at(x, y); pressed_button_.reset();
    if (!pressed || pressed != released) return out;
    if (*pressed == TeamStatisticsButton::close) out.kind = TeamStatisticsRequestKind::close;
    else { out.kind = TeamStatisticsRequestKind::change_mode; out.mode = TeamStatisticsMode(*pressed); }
    return out;
}
TeamStatisticsRequest TeamStatisticsScreen::right_button_down() const {
    TeamStatisticsRequest out; out.kind = TeamStatisticsRequestKind::close;
    out.runtime_team = projection_.runtime_team; out.mode = projection_.mode; return out;
}

} // namespace bmh::ui
