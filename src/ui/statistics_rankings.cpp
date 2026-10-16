// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/statistics_rankings.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "ui/setup.hpp"
#include "ui/manager.hpp"

namespace bmh::ui {
namespace {
constexpr uint32_t kTitles = ::bmh::layout::Titles;
constexpr uint32_t kTitleCount = 9;
constexpr uint32_t kSeasonCount = 50;
// OtherGames_Layout_ExecutableField and SeasonEntrants_FormatError_TextPointer2 are runtime TEXTD pointer tables, not initialized
// pointers in BMH.exe's static image. Their TEXTD slots are 601 and 623.
constexpr size_t kAlternateTitleTextBase = 601;
constexpr size_t kTitleTextBase = 623;
constexpr size_t kEmptyTitleText = 913;

void copy_transparent(Image& out, const Image& src, int sx, int sy, int x, int y, int w, int h) {
    if (sx < 0 || sy < 0 || x < 0 || y < 0 || w < 0 || h < 0 ||
        sx + w > src.width || sy + h > src.height || x + w > out.width || y + h > out.height)
        throw FormatError("statistics rankings: Original sprite bounds");
    for (int yy = 0; yy < h; ++yy)
        for (int xx = 0; xx < w; ++xx) {
            const uint8_t color = src.pixels[size_t(sy + yy) * size_t(src.width) + size_t(sx + xx)];
            if (color != 0xc0) out.at(x + xx, y + yy) = color;
        }
}

void fill(Image& out, int x, int y, int w, int h, uint8_t color) {
    if (x < 0 || y < 0 || w < 0 || h < 0 || x + w > out.width || y + h > out.height)
        throw FormatError("statistics rankings: Original button bounds");
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx) out.at(xx, yy) = color;
}

uint8_t original_name_color(const GameState& game, int runtime_team) {
    const size_t count = std::min<size_t>(game.manager_teams.size(), 4);
    const int active = game.memory.has(::bmh::layout::ActiveManager) ? game.memory.u8(::bmh::layout::ActiveManager) : -1;
    const int manager_count = game.memory.has(::bmh::layout::ManagerCount) ? game.memory.u8(::bmh::layout::ManagerCount) : int(count);
    const bool current = game.memory.has(::bmh::layout::DeskPosition) && game.memory.u8(::bmh::layout::DeskPosition) < manager_count;
    for (size_t slot = 0; slot < count; ++slot) {
        const size_t at = slot * 0xb0c;
        if (at + 0xa8 >= game.manager_records.size() || game.manager_records[at + 0xa8] == 0) continue;
        if (game.manager_teams[slot] == runtime_team)
            return int(slot) == active && current ? 0x7a : 0x75;
    }
    if (game.memory.has(::bmh::layout::AlltimeTable_TeamColor_RuntimeField) && game.memory.u16(::bmh::layout::AlltimeTable_TeamColor_RuntimeField) == runtime_team) return 0xf7;
    return 0x5f;
}

} // namespace

StatisticsRankingState statistics_rankings_from_game(const GameState& game, const LeImage& exe,
                                                       int manager_slot, int category, int first_season,
                                                       const Image& underlay) {
    if (category < 0 || category >= int(kTitleCount)) throw FormatError("statistics rankings: category outside 0..8");
    if (first_season < 0 || first_season >= int(kSeasonCount) || (first_season % 4) != 0)
        throw FormatError("statistics rankings: Source page must start at a four-season boundary");
    if (!game.memory.has(kTitles + kTitleCount * kSeasonCount * 2 - 1))
        throw FormatError("statistics rankings: save lacks the Original Titles title-holder block");
    if (manager_slot < 0 || manager_slot >= 4 ||
        size_t(manager_slot) * 0xb0c + 0x2aa + 1 >= game.manager_records.size())
        throw FormatError("statistics rankings: missing Source manager season-number field");
    if (underlay.width != 320 || underlay.height != 240)
        throw FormatError("statistics rankings: expected accepted 320x240 parent underlay");

    StatisticsRankingState out;
    out.category = category;
    out.first_season = first_season;
    out.manager_slot = manager_slot;
    const uint32_t manager_year_at = uint32_t(manager_slot) * 0xb0c + 0x2aa;
    out.season_number_base = int(exe.u16(::bmh::layout::Achievements_FormatError_RuntimeField2)) +
        int(game.manager_records[manager_year_at] | (uint16_t(game.manager_records[manager_year_at + 1]) << 8));
    out.current_season = std::clamp(game.season, 0, int(kSeasonCount) - 1);
    out.underlay = underlay;
    for (uint32_t index = 0; index < kTitleCount; ++index) {
        const uint8_t title_id = exe.u8(::bmh::layout::StatisticsRankings_FormatError_RuntimeField + index);
        const bool alternate = exe.u8(::bmh::layout::StatisticsRankings_FormatError_RuntimeField2 + index) != 0;
        if (title_id >= kTitleCount) throw FormatError("statistics rankings: Source title id outside nine stored lists");
        out.categories[index] = StatisticsRankingCategory{title_id, alternate};
    }
    for (uint32_t category_index = 0; category_index < kTitleCount; ++category_index) {
        for (uint32_t season = 0; season < kSeasonCount; ++season) {
            // StatisticsRankingsFromGame reads Titles by selected control index (local_14).
            // title_id only chooses the Original label pointer above.
            const uint32_t at = kTitles + category_index * 100 + season * 2;
            out.title_holders[category_index][season] = game.memory.u16(at);
            const uint16_t team = out.title_holders[category_index][season];
            if (team != 0xffff) {
                if (team >= game.club_ids.size())
                    throw FormatError("statistics rankings: Source title holder has no club identity/name");
                out.club_names[category_index][season] = game.club_name(team);
                out.club_name_colors[category_index][season] = original_name_color(game, team);
            }
        }
    }
    return out;
}

StatisticsRankingsScreen::StatisticsRankingsScreen(const std::filesystem::path& data,
                                                   StatisticsRankingState state)
    : state_(std::move(state)), exe_(load_le(asset_path(data, "BMH.exe"))),
      font_(load_font(asset_path(data / "F", "BMH3.FNT"))), sprite_(data),
      button_art_(load_vga(asset_path(data / "KB", "0.VGA"))),
      texts_(load_textd(asset_path(data, "TEXTD.BLK"))) {
    if (state_.category < 0 || state_.category >= int(kTitleCount) || state_.current_season < 0 ||
        state_.current_season >= int(kSeasonCount) || state_.first_season < 0 ||
        state_.first_season >= int(kSeasonCount) || state_.first_season % 4 != 0 ||
        state_.underlay.width != 320 || state_.underlay.height != 240 ||
        button_art_.width < 76 || button_art_.height < 84)
        throw FormatError("statistics rankings: incomplete Source projection");
    for (size_t i = 0; i < state_.categories.size(); ++i) {
        const auto& category = state_.categories[i];
        if (category.title_id >= kTitleCount)
            throw FormatError("statistics rankings: incomplete Source category map");
        const size_t base = category.alternate_label_table ? kAlternateTitleTextBase : kTitleTextBase;
        category_labels_[i] = texts_[base + category.title_id];
        if (category_labels_[i].empty())
            throw FormatError("statistics rankings: missing Source TEXTD category label");
    }
    empty_label_ = texts_[kEmptyTitleText];
}

void StatisticsRankingsScreen::draw_category_button(Image& out, int index, bool pressed) const {
    const int col = index / 3, row = index % 3;
    const int x = col * 0x5b + 0x17, y = row * 0x0c + 0x9a;
    const int dx = x + (pressed ? 2 : 0), dy = y + (pressed ? 2 : 0);
    // Source FormatError3 uses the KB/0 button slices at x=0x18, y=0x2c/0x4a
    // (pressed) or 0x4a/0x5e (released), with 8-pixel center repeats.
    const int source_y = pressed ? 44 : 74;
    if (pressed) {
        const uint8_t background = out.at(x, y + 10);
        fill(out, x, y, 0x59 + 1, 2, background);
        fill(out, x, y, 2, 10, background);
    } else {
        fill(out, x + 2, y + 10, 0x59, 2, 0x40);
        fill(out, x + 0x59, y + 2, 2, 9, 0x40);
    }
    copy_transparent(out, button_art_, 24, source_y, dx, dy, 31, 10);
    int used = 31;
    while (0x59 - used > 20) {
        const int amount = std::min(8, 0x59 - used - 20);
        copy_transparent(out, button_art_, 24, source_y + 10, dx + used, dy, amount, 10);
        used += amount;
    }
    const int tail = 0x59 - used;
    copy_transparent(out, button_art_, 45 - tail, source_y + 20, dx + used, dy, tail, 10);
    const auto& label = category_labels_[size_t(index)];
    const int mode = exe_.u8(::bmh::layout::Advertising_Button_RuntimeField + uint32_t(pressed));
    const int measured = sprite_.text_width(original_to_utf8(label));
    sprite_.draw(out, dx + (0x59 - measured) / 2, dy + 7,
                 original_to_utf8(label), mode);
}

Image StatisticsRankingsScreen::render() const {
    Image out = state_.underlay;
    // The parent supplies its accepted frame; StatisticsRankingsFromGame owns these nine
    // category controls and the historical title-holder rows.
    for (int index = 0; index < 9; ++index)
        draw_category_button(out, index, index == state_.category || index == pressed_category_);
    // StatisticsRankingsFromGame2(0x18,0x24,0x10e,0x74) is the opaque Source list-pane helper.
    // The parent owns its pixels; this overlay writes only the rows it contains.
    const int end = std::min(state_.current_season, state_.first_season + 15);
    for (int season = state_.first_season; season <= end; ++season) {
        const int visible = season - state_.first_season;
        const int y = 0x31 + visible * 6;
        const uint16_t team = state_.title_holders[size_t(state_.category)][size_t(season)];
        if (season < state_.current_season || team != 0xffff) {
            const int source_season_number = state_.season_number_base + season + 1;
            std::string n = std::to_string(source_season_number);
            while (n.size() < 4) n.insert(n.begin(), '%');
            n += "%%"; // FormatNumber(...,0xc4,&StatisticsRankingsRuntimeField): width-four Source number and suffix.
            const uint8_t year_color = team == 0xffff &&
                (state_.category == 5 || state_.category == 6) ? 0x67 : 0x6b;
            font_.draw(out, 100, y - font_.baseline, n, year_color);
            if (team != 0xffff) {
                font_.draw(out, 130, y - font_.baseline,
                           state_.club_names[size_t(state_.category)][size_t(season)],
                           state_.club_name_colors[size_t(state_.category)][size_t(season)]);
            } else {
                font_.draw(out, 130, y - font_.baseline, empty_label_, 0x67);
            }
        }
    }
    return out;
}

bool StatisticsRankingsScreen::pointer_down(int x, int y, bool right) {
    if (pending_) return false;
    if (right) {
        pending_ = StatisticsRankingsRequest{StatisticsRankingsRequestKind::close, state_.category};
        return true;
    }
    pressed_category_ = -1;
    for (int index = 0; index < 9; ++index) {
        const int col = index / 3, row = index % 3;
        const int left = col * 0x5b + 0x17, top = row * 0x0c + 0x9a;
        if (x >= left && x <= left + 0x59 && y >= top && y <= top + 9) {
            pressed_category_ = index;
            return true;
        }
    }
    // StatisticsRankingsFromGame handles the Original scroll event after the category-button
    // hit test: upper half moves four seasons toward the present, lower half back.
    if (x >= 0x18 && x <= 0x18 + 0x10e && y >= 0x24 && y <= 0x24 + 0x74) {
        scroll(y < 0x5e ? -1 : 1);
        return true;
    }
    return false;
}

bool StatisticsRankingsScreen::move(int x, int y) {
    if (pressed_category_ < 0) return false;
    const int col = pressed_category_ / 3, row = pressed_category_ % 3;
    const int left = col * 0x5b + 0x17, top = row * 0x0c + 0x9a;
    if (x >= left && x <= left + 0x59 && y >= top && y <= top + 9) return false;
    pressed_category_ = -1;
    return true;
}

bool StatisticsRankingsScreen::release(int x, int y) {
    if (pending_) return false;
    const int index = pressed_category_;
    pressed_category_ = -1;
    if (index < 0) return false;
    const int col = index / 3, row = index % 3;
    const int left = col * 0x5b + 0x17, top = row * 0x0c + 0x9a;
    if (x < left || x > left + 0x59 || y < top || y > top + 9) return true;
    if (index == state_.category) {
        // StatisticsRankingsFromGame leaves the inner loop active for the already-selected
        // button; its matching mouse event is then interpreted by the scroll
        // branch using that button's y coordinate (below 0x5e => false).
        scroll(1);
        return true;
    }
    pending_ = StatisticsRankingsRequest{StatisticsRankingsRequestKind::select_category, index};
    return true;
}

void StatisticsRankingsScreen::reject_action() {
    if (!pending_) throw FormatError("statistics rankings: no pending action to reject");
    pending_.reset();
    pressed_category_ = -1;
}

void StatisticsRankingsScreen::acknowledge_category(StatisticsRankingState accepted) {
    if (!pending_ || pending_->kind != StatisticsRankingsRequestKind::select_category ||
        accepted.category != pending_->category || accepted.first_season != 0 ||
        accepted.underlay.pixels != state_.underlay.pixels)
        throw FormatError("statistics rankings: mismatched category acknowledgement");
    state_ = std::move(accepted);
    pending_.reset();
    pressed_category_ = -1;
}

void StatisticsRankingsScreen::scroll(int delta) {
    if (pending_ || delta == 0) return;
    int next = state_.first_season + (delta < 0 ? -4 : 4);
    const int max_first = state_.current_season >= 12
        ? ((state_.current_season - 12) / 4) * 4 : 0;
    state_.first_season = std::clamp(next, 0, max_first);
}

} // namespace bmh::ui
