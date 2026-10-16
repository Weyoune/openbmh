// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/desk_drawers.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>

#include "formats/le.hpp"
#include "formats/vga.hpp"
#include "ui/manager.hpp"

namespace bmh::ui {
namespace {

constexpr std::array<std::array<int, 2>, 4> kSourcePairs{{
    {{3, 4}}, {{11, 12}}, {{16, 17}}, {{20, 21}}
}};

const DeskRankCurve* curve_for(const DeskState& state, int team) {
    for (const auto& curve : state.rank_curves)
        if (curve.runtime_team == team) return &curve;
    return nullptr;
}

std::string team_name(const DeskState& state, int team) {
    if (team == state.runtime_club) return state.club_name;
    if (state.next_opponent && *state.next_opponent == team) return state.next_opponent_name;
    if (const auto* curve = curve_for(state, team)) return curve->team_name;
    return {};
}

std::string result_piece(uint8_t packed, const LeImage& exe) {
    return std::to_string(packed & 0x0f) + exe.cstring(::bmh::layout::DeskDrawers_Recent_ExecutableField) +
           std::to_string(packed >> 4) + exe.cstring(::bmh::layout::DeskDrawers_Recent_ExecutableField2);
}

} // namespace

std::optional<int> source_drawer_team(const DeskState& state, int local_kind) {
    for (const auto& pair : kSourcePairs) {
        if (local_kind != pair[0] && local_kind != pair[1]) continue;
        int open_count = 0;
        // SourceDrawerTeam: local64 stays zero unless the saved option and raw away
        // byte permit ordering, and retained param6 actually has an opponent.
        // Projection already omitted hidden linked-list records; disabled but
        // visible drawers still participate in Source's count.
        if (state.paired_drawers_away_order && state.next_opponent.value_or(-1)>=0)
            for (const auto& item : state.items)
                if (item.open && (item.kind - 50 == pair[0] || item.kind - 50 == pair[1])) ++open_count;

        // SourceDrawerTeam retains local44=param6 and local48=param8 before its
        // away-side swap. The drawer cases use those retained values: when
        // both are open, uVar21 is local48; otherwise it is local44.
        const int local44 = state.next_opponent.value_or(-1);
        const int local48 = state.national_team ? 0x165 : state.runtime_club;
        const int uVar21 = (open_count == 2) ? local48 : local44;
        const bool first_case = local_kind == pair[0];
        const int side = first_case ? (uVar21 != local48 ? local48 : local44) : uVar21;
        if (side < 0) return std::nullopt;
        return side;
    }
    return std::nullopt;
}

DeskDrawerRenderer::DeskDrawerRenderer(const std::filesystem::path& data)
    : exe_(load_le(asset_path(data, "BMH.exe"))),
      icons_(load_vga(asset_path(data / "KB", "3.VGA"))),
      small_(load_font(asset_path(data / "F", "BMH3.FNT"))),
      texts_(load_textd(asset_path(data, "TEXTD.BLK"))) {}

bool DeskDrawerRenderer::handles(int native_kind) const {
    switch (native_kind - 50) {
    case 0: case 1: case 2: case 3: case 4: case 9: case 10:
    case 11: case 12: case 16: case 17: case 20: case 21:
        return true;
    default: return false; // Source 0xd / 0x16 are empty switch cases.
    }
}

void DeskDrawerRenderer::copy(Image& target, int sx, int sy, int x, int y, int w, int h) const {
    if (sx < 0 || sy < 0 || sx + w > icons_.width || sy + h > icons_.height)
        throw FormatError("desk drawer: sprite outside KB/3.VGA");
    for (int yy = 0; yy < h; ++yy) for (int xx = 0; xx < w; ++xx) {
        const uint8_t pixel = icons_.pixels[size_t(sy + yy) * size_t(icons_.width) + size_t(sx + xx)];
        if (pixel && x + xx >= 0 && y + yy >= 0 && x + xx < target.width && y + yy < target.height)
            target.at(x + xx, y + yy) = pixel;
    }
}

void DeskDrawerRenderer::text(Image& target, const DeskState& state, int x, int baseline,
                              std::string_view value, uint8_t color, int right) const {
    const int width = small_.text_width(value);
    if (right >= 0) x += (right - x) / 2 - width / 2;
    if (x < 0 || x + width > target.width || baseline < 5 || baseline >= target.height) return;
    Image mask(std::max(1, width), 6);
    small_.draw(mask, 0, 0, value, 1);
    int gradient = 0;
    while (gradient < 32 && exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2 + uint32_t(gradient)) != color) ++gradient;
    for (int yy = 0; yy < 6; ++yy) for (int xx = 0; xx < width; ++xx)
        if (mask.at(xx, yy))
            target.at(x + xx, baseline - 5 + yy) = state.gradient_text
                ? exe_.u8(::bmh::layout::Advertising_Text_RuntimeField + uint32_t(gradient * 6 + yy)) : color;
}

void DeskDrawerRenderer::line(Image& target, int x1, int y1, int x2, int y2, uint8_t color) const {
    const int dx = std::abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
    const int dy = -std::abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        if (x1 >= 0 && y1 >= 0 && x1 < target.width && y1 < target.height) target.at(x1, y1) = color;
        if (x1 == x2 && y1 == y2) break;
        const int twice = 2 * error;
        if (twice >= dy) error += dy, x1 += sx;
        if (twice <= dx) error += dx, y1 += sy;
    }
}

void DeskDrawerRenderer::folder(Image& target, const DeskState& state, const DeskItem& item) const {
    const int folder_id = item.kind - 50;
    if (folder_id < 0 || folder_id >= int(state.folder_icon_slots.size())) return;
    const int left = item.x + 5, top = item.y + 4;
    int x = left, y = top;
    const int right = item.x + item.width - 4;
    const int bottom = item.y + item.height - 4;
    for (uint8_t id : state.folder_icon_slots[size_t(folder_id)]) {
        const int tile = int(id) - 1;
        if (tile < 0 || tile >= 30) continue;
        if (x + 32 > right) x = left, y += 27;
        if (y + 25 > bottom) break;
        copy(target, (tile % 8) * 32, (tile / 8) * 25, x, y, 32, 25);
        x += 34;
    }
}

void DeskDrawerRenderer::curve(Image& target, const DeskState& state, const DeskItem& item, int local_kind) const {
    const auto selected = source_drawer_team(state, local_kind);
    if (!selected) {
        text(target, state, item.x + 5, item.y + 49, texts_[1207], 0x6a, item.x + 85); // DeskDrawersRuntimeField
        return;
    }
    const auto* curve = curve_for(state, *selected);
    if (!curve || curve->league < 0 || curve->league >= 9) {
        text(target, state, item.x + 5, item.y + 49, texts_[1206], 0x6a, item.x + 85); // DeskDrawersRuntimeField2
        return;
    }
    const uint32_t league = uint32_t(curve->league);
    const int teams = exe_.u8(::bmh::layout::LeagueSizes + league);
    const int low_cut = exe_.u8(::bmh::layout::CareerSession_InstallLeagueSizes_RuntimeField + league) + exe_.u8(::bmh::layout::DeskDrawers_Curve_RuntimeField + league);
    const int high_cut = teams - exe_.u8(::bmh::layout::CareerSession_InstallLeagueSizes_RuntimeField2 + league);
    const int left = item.x + 5, top = item.y + 1, bottom = top + 42;
    line(target, left, top, left, bottom, 0x6a);
    line(target, left, bottom, left + 80, bottom, 0x6a);
    for (const DeskRankSample& sample : curve->samples) {
        if (sample.matchday < 1 || sample.matchday > 38 || sample.source_rank < 0 || sample.source_rank >= teams) continue;
        int band = 1;
        if (sample.source_rank < low_cut || (sample.source_rank == 0 && curve->league == 0)) band = 0;
        if (high_cut <= sample.source_rank) ++band;
        const int y = top + 43 - (teams - sample.source_rank) * 2;
        const uint32_t at = ::bmh::layout::DeskDrawers_Curve_ExecutableField + uint32_t(band * 8);
        if (!exe_.contains(at + 4)) continue;
        for (int stroke = 0; stroke < 2; ++stroke)
            line(target, left + 1 + 2 * (sample.matchday - 1) + stroke, y, left + 1 + 2 * (sample.matchday - 1) + stroke, top + 41,
                 exe_.u8(at + uint32_t(stroke * 4)));
    }
}

void DeskDrawerRenderer::recent(Image& target, const DeskState& state, const DeskItem& item) const {
    text(target, state, item.x + 1, item.y + 6, texts_[1208], 0x6a, item.x + item.width - 1);
    int row = 0;
    for (const DeskRecentMatch& match : state.recent_matches) {
        const int baseline = item.y + 13 + row * 7;
        text(target, state, item.x + 1, baseline, match.home_name, 0x5f);
        text(target, state, item.x + 94, baseline, exe_.cstring(::bmh::layout::DeskDrawers_Recent_TextPointer), 0x5f);
        text(target, state, item.x + 101, baseline, match.away_name, 0x5f);
        const std::string score = std::to_string(match.source_score & 0x0f) + exe_.cstring(::bmh::layout::DeskDrawers_Recent_ExecutableField) +
                                  std::to_string(match.source_score >> 4) + exe_.cstring(::bmh::layout::DeskDrawers_Recent_ExecutableField2);
        text(target, state, item.x + 188, baseline, score, 0x5f);
        if (++row == 6) break;
    }
}

void DeskDrawerRenderer::head_to_head(Image& target, const DeskState& state, const DeskItem& item) const {
    if (!state.next_opponent || *state.next_opponent < 0 || *state.next_opponent > 0xa7) {
        text(target, state, item.x + 1, item.y + 13, texts_[1207], 0x6a, item.x + 115); // DeskDrawersRuntimeField
        return;
    }
    const auto found = std::find_if(state.opponent_histories.begin(), state.opponent_histories.end(),
        [&](const DeskOpponentHistory& row) { return row.runtime_team == *state.next_opponent; });
    const std::string opponent = team_name(state, *state.next_opponent);
    text(target, state, item.x + 1, item.y + 6,
         texts_[1209] + texts_[state.next_match_away ? 1211 : 1210], 0x6a, item.x + 115);
    text(target, state, item.x + 1, item.y + 13, opponent, 0x6a, item.x + 115);
    if (found == state.opponent_histories.end()) {
        text(target, state, item.x + 1, item.y + 20, texts_[1212], 0x6a, item.x + 115);
        return;
    }
    const size_t venue = state.next_match_away ? 5 : 0;
    std::string values;
    for (size_t result = 0; result < 5; ++result) {
        const uint8_t packed = found->source_results[venue + result];
        if (packed == 0xff) break;
        if (!values.empty()) values += exe_.cstring(::bmh::layout::DeskDrawers_HeadToHead_ExecutableField);
        values += result_piece(packed, exe_);
    }
    if (values.empty()) values = texts_[1212];
    text(target, state, item.x + 1, item.y + 20, values, 0x5f, item.x + 115);
}

void DeskDrawerRenderer::rank_strength(Image& target, const DeskState& state, const DeskItem& item,
                                       int local_kind) const {
    const auto selected = source_drawer_team(state, local_kind);
    if (!selected) return;
    const auto* curve = curve_for(state, *selected);
    std::string value;
    std::optional<int> position, strength;
    if (*selected == state.runtime_club) {
        position = state.table_position;
        strength = state.strength;
    } else if (curve) {
        position = curve->current_position;
        strength = curve->strength;
    }
    if (position) value = std::to_string(*position + 1) + texts_[1213];
    if (strength) {
        if (!value.empty()) value += texts_[1214];
        else value = texts_[1214];
        if (*strength < 10) value += '%'; // FNT3's transparent 5px field pad.
        value += std::to_string(*strength);
    }
    if (value.empty()) {
        const auto name = team_name(state, *selected);
        if (!name.empty()) text(target, state, item.x + 5, item.y + 6, name, 0x5f);
        return;
    }
    if (!value.empty()) text(target, state, item.x, item.y + 6, value, 0x5f, item.x + 94);
}

void DeskDrawerRenderer::opponent_name(Image& target, const DeskState& state, const DeskItem& item,
                                       int local_kind) const {
    const auto selected = source_drawer_team(state, local_kind);
    if (!selected) return;
    const std::string name = team_name(state, *selected);
    if (!name.empty()) text(target, state, item.x, item.y + 6, name, 0x5f, item.x + 94);
}

void DeskDrawerRenderer::badge(Image& target, const DeskState& state, const DeskItem& item,
                               int local_kind, const Image& own_badge_atlas,
                               const std::optional<Image>& rival_badge_atlas,
                               const Image& fallback_badge_atlas) const {
    const int x = item.x + 1, y = item.y + 1;
    line(target, x - 1, y - 1, x + 38, y - 1, 0x46);
    line(target, x - 1, y - 1, x - 1, y + 38, 0x46);
    line(target, x - 1, y + 38, x + 38, y + 38, 0x40);
    line(target, x + 38, y - 1, x + 38, y + 38, 0x40);

    const auto selected = source_drawer_team(state, local_kind);
    const Image* atlas = nullptr;
    int crest = -1;
    if (selected && *selected == state.runtime_club) {
        atlas = &own_badge_atlas;
        crest = state.badge & 0x1ff;
    } else if (selected && state.next_opponent && *selected == *state.next_opponent &&
               !state.next_opponent_badge_fallback && state.next_opponent_badge && rival_badge_atlas) {
        atlas = &*rival_badge_atlas;
        crest = *state.next_opponent_badge & 0x1ff;
    }

    if (atlas && crest >= 0 && atlas->width >= 304 && atlas->height >= 38) {
        const int sx = (crest & 7) * 38;
        for (int yy = 0; yy < 38; ++yy) for (int xx = 0; xx < 38; ++xx) {
            const uint8_t pixel = atlas->pixels[size_t(yy) * size_t(atlas->width) + size_t(sx + xx)];
            if (x + xx >= 0 && y + yy >= 0 && x + xx < target.width && y + yy < target.height)
                target.at(x + xx, y + yy) = pixel;
        }
        return;
    }

    // SourceBadgeFallback falls through FormatError(..., 0x0e) when the
    // selected Source team has no crest; DesktopScreen already owns KB/0.VGA.
    if (fallback_badge_atlas.width >= 46 && fallback_badge_atlas.height >= 38) {
        for (int yy = 0; yy < 38; ++yy) for (int xx = 0; xx < 38; ++xx) {
            const uint8_t pixel = fallback_badge_atlas.pixels[size_t(yy) * size_t(fallback_badge_atlas.width) + size_t(8 + xx)];
            if (x + xx >= 0 && y + yy >= 0 && x + xx < target.width && y + yy < target.height)
                target.at(x + xx, y + yy) = pixel;
        }
    }
}

void DeskDrawerRenderer::draw(Image& target, const DeskState& state, const DeskItem& item,
                              const Image& own_badge_atlas,
                              const std::optional<Image>& rival_badge_atlas,
                              const Image& fallback_badge_atlas) const {
    if (!handles(item.kind)) return;
    DeskItem content = item;
    if (item.open) content.x += 4, content.y += 4;
    const int local_kind = content.kind - 50;
    if (local_kind >= 0 && local_kind <= 2) return folder(target, state, content);
    switch (local_kind) {
    case 3: case 4: curve(target, state, content, local_kind); break;
    case 9: recent(target, state, content); break;
    case 10: head_to_head(target, state, content); break;
    case 11: case 12: rank_strength(target, state, content, local_kind); break;
    case 16: case 17: badge(target, state, content, local_kind, own_badge_atlas,
                            rival_badge_atlas, fallback_badge_atlas); break;
    case 20: case 21: opponent_name(target, state, content, local_kind); break;
    default: break;
    }
}

} // namespace bmh::ui
