// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "world_cup_statistics.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "formats/binary.hpp"
#include "formats/vga.hpp"
#include "game/messages.hpp"
#include "ui/setup.hpp"

namespace bmh::ui {
namespace {
constexpr uint32_t kPlacements = ::bmh::layout::Placements;
constexpr int kNationCount = 62;
constexpr int kEditionCount = 18;

void copy(Image& out, const Image& src, int sx, int sy, int x, int y, int w, int h,
          int transparent = -1) {
    if (sx < 0 || sy < 0 || sx + w > src.width || sy + h > src.height)
        throw FormatError("World Cup statistics: Source atlas bounds");
    for (int yy = 0; yy < h; ++yy) for (int xx = 0; xx < w; ++xx) {
        const uint8_t value = src.pixels[size_t(sy + yy) * size_t(src.width) + size_t(sx + xx)];
        if (value != transparent && x + xx >= 0 && y + yy >= 0 && x + xx < out.width && y + yy < out.height)
            out.at(x + xx, y + yy) = value;
    }
}

void draw_text(const Font& font, Image& out, int x, int baseline, const std::string& value, uint8_t color) {
    font.draw(out, x, baseline - font.baseline, value, color);
}

void source_hover_window(Image& out, const Image& kb56, bool flat_fill) {
    // LoadGameEndTaxAnimationAssets(NULL,...): allocate/copy original KB56 surface unless SolidPanelBackground is set.
    if (flat_fill) {
        for (int y = 0x73; y <= 0x9b; ++y)
            for (int x = 0xe2; x <= 0x109; ++x) out.at(x, y) = 0x42;
    } else {
        copy(out, kb56, 0xe2, 0x73 - 0x13, 0xe3, 0x74, 38, 38, 0xc0);
    }
    for (int x = 0xe2; x <= 0x109; ++x) {
        out.at(x, 0x73) = 0x46;
        out.at(x, 0x9b) = 0x40;
    }
    for (int y = 0x73; y <= 0x9b; ++y) {
        out.at(0xe2, y) = 0x46;
        out.at(0x109, y) = 0x40;
    }
}

void source_sort(WorldCupNation* first, int count) {
    // Games's in-place descending quicksort, including its tie swaps.
    while (count > 1) {
        const uint32_t pivot = first[count / 2].medals;
        int left = -1, right = count;
        for (;;) {
            do { ++left; } while (pivot < first[left].medals);
            do { --right; } while (first[right].medals < pivot);
            if (right <= left) break;
            std::swap(first[left], first[right]);
        }
        source_sort(first, left);
        first += left;
        count -= left;
    }
}

} // namespace

uint32_t world_cup_medal_key(const std::array<uint8_t, 72>& editions, uint8_t nation) {
    if (nation >= kNationCount) throw FormatError("World Cup statistics: nation outside 0..61");
    uint32_t key = 0;
    for (int edition = 0; edition < kEditionCount; ++edition)
        for (int place = 0; place < 4; ++place) {
            const uint8_t value = editions[size_t(edition) * 4 + size_t(place)];
            if ((value & 0x80) && (value & 0x7f) == nation)
                key += uint32_t{1} << ((3 - place) * 8);
        }
    return key;
}

void order_world_cup_nations(std::array<WorldCupNation, 62>& nations) {
    source_sort(nations.data(), int(nations.size()));
}

WorldCupStatisticsProjection world_cup_statistics_from_game(const GameState& game, const LeImage& exe,
                                                               WorldCupStatisticsOwner owner) {
    if (!game.memory.has(kPlacements + 71))
        throw FormatError("World Cup statistics: save lacks the Original 18-edition placement block");
    WorldCupStatisticsProjection out;
    out.can_select_opponent = owner == WorldCupStatisticsOwner::friendly_match_nation_picker;
    for (size_t i = 0; i < out.editions.size(); ++i) out.editions[i] = game.memory.u8(kPlacements + uint32_t(i));
    for (uint8_t value : out.editions)
        if ((value & 0x80) && (value & 0x7f) >= kNationCount)
            throw FormatError("World Cup statistics: placement names a nation outside 0..61");
    out.badge_heap = game.club_heap_78;
    for (int nation = 0; nation < kNationCount; ++nation) {
        const int team = 0x165 + nation;
        if (size_t(team) >= game.club_ids.size())
            throw FormatError("World Cup statistics: national-team identity is unavailable");
        out.country_names[size_t(nation)] = game.club_name(team);
        out.ranking[size_t(nation)] = WorldCupNation{uint8_t(nation), out.country_names[size_t(nation)],
            world_cup_medal_key(out.editions, uint8_t(nation))};
    }
    order_world_cup_nations(out.ranking);

    int x = 0x23, y = 0x22, segment = 0, used = 0;
    for (int nation = 0; nation < kNationCount; ++nation) {
        out.flag_cells[size_t(nation)] = {x, y};
        if (segment < 2) y += 0x0b;
        else x += 0x0e;
        ++used;
        if (used >= exe.u8(::bmh::layout::WorldCupStatistics_FormatError_RuntimeField + uint32_t(segment))) {
            used = 0;
            ++segment;
            if (segment >= 4 && nation + 1 < kNationCount)
                throw FormatError("World Cup statistics: EXE nation-bank segments do not cover 62 nations");
            if (segment < 4) {
                x = exe.u16(::bmh::layout::WorldCupStatistics_FormatError_RuntimeField3 + uint32_t(segment) * 2);
                y = exe.u8(::bmh::layout::WorldCupStatistics_FormatError_RuntimeField2 + uint32_t(segment));
            }
        }
    }
    return out;
}

std::optional<uint8_t> world_cup_nation_at(const WorldCupStatisticsProjection& p, int x, int y) {
    if (x >= 0x34 && x <= 0x10a && y >= 0x2e && y <= 0x50) {
        const int edition = (x - 0x34) / 0x0c, place = (y - 0x2e) / 9;
        if (edition >= 0 && edition < kEditionCount && place >= 0 && place < 4) {
            const uint8_t value = p.editions[size_t(edition) * 4 + size_t(place)];
            if (value & 0x80) return uint8_t(value & 0x7f);
            return {};
        }
    }
    for (uint8_t nation = 0; nation < kNationCount; ++nation) {
        const auto cell = p.flag_cells[nation];
        if (x >= cell.x && x <= cell.x + 10 && y >= cell.y && y <= cell.y + 7) return nation;
    }
    return {};
}

WorldCupStatisticsRequest world_cup_selection_request(const WorldCupStatisticsProjection& p,
                                                       std::optional<uint8_t> nation) {
    if (!p.can_select_opponent || !nation || *nation == 0 || *nation >= kNationCount) return {};
    return {WorldCupStatisticsRequestKind::select_opponent, 0x165 + *nation};
}

WorldCupStatisticsScreen::WorldCupStatisticsScreen(const std::filesystem::path& data,
        WorldCupStatisticsProjection projection, Image underlay, bool flat_source_window)
    : projection_(std::move(projection)), data_directory_(data), underlay_(std::move(underlay)),
      flags_(load_vga(asset_path(data / "KB", "24.VGA"))),
      window_atlas_(load_vga(asset_path(data / "KB", "56.VGA"))),
      exe_(load_le(asset_path(data, "BMH.exe"))),
      body_(load_font(asset_path(data / "F", "BMH3.FNT"))),
      flat_source_window_(flat_source_window) {
    if (underlay_.width != 320 || underlay_.height != 240 || flags_.width < 108 || flags_.height < 38)
        throw FormatError("World Cup statistics: expected a 320x240 accepted parent and KB24 atlas");
    if (window_atlas_.width < 264 || window_atlas_.height < 134)
        throw FormatError("World Cup statistics: KB56 hover-window source is too small");
}

void WorldCupStatisticsScreen::draw_flag(Image& out, uint8_t nation, int x, int y) const {
    copy(out, flags_, 0x12, 0x1e, x, y, 11, 8, 0xc0);
    const int sx = (nation % 12) * 9, sy = (nation / 12) * 6;
    copy(out, flags_, sx, sy, x + 1, y + 1, 9, 6, 0xc0);
}

void WorldCupStatisticsScreen::draw_badge(Image& out, uint8_t nation, int x, int y) const {
    const int team = 0x165 + nation;
    if (size_t(team) * 2 + 1 >= projection_.badge_heap.size()) return;
    const uint16_t selector = uint16_t(projection_.badge_heap[size_t(team) * 2] |
        uint16_t(projection_.badge_heap[size_t(team) * 2 + 1]) << 8) & 0x1ff;
    const int bank = selector >> 3;
    const Image badge = load_vga(asset_path(data_directory_ / "W", std::to_string(bank) + ".VGA"));
    copy(out, badge, (selector & 7) * 38, 0, x, y, 38, 38, 0xc0);
}

bool WorldCupStatisticsScreen::move(int x, int y) {
    const auto next = world_cup_nation_at(projection_, x, y);
    if (next == hovered_) return false;
    hovered_ = next;
    hover_frame_shown_ = true;
    return true;
}

WorldCupStatisticsRequest WorldCupStatisticsScreen::pointer_down(int x, int y, bool right) {
    if (right) return {WorldCupStatisticsRequestKind::close, -1};
    return world_cup_selection_request(projection_, world_cup_nation_at(projection_, x, y));
}

Image WorldCupStatisticsScreen::render() const {
    Image out = underlay_;
    // Original nation bank and 18×4 edition grid; the accepted parent supplies the window.
    for (uint8_t nation = 0; nation < kNationCount; ++nation) {
        const auto cell = projection_.flag_cells[nation];
        draw_flag(out, nation, cell.x, cell.y);
    }
    for (int edition = 0; edition < kEditionCount; ++edition)
        for (int place = 0; place < 4; ++place) {
            const int x = 0x34 + edition * 12, y = 0x2e + place * 9;
            copy(out, flags_, 0x12, 0x1e, x, y, 11, 8, 0xc0);
            const uint8_t value = projection_.editions[size_t(edition) * 4 + size_t(place)];
            if (value & 0x80) draw_flag(out, uint8_t(value & 0x7f), x, y);
        }
    int row = 0;
    for (const auto& entry : projection_.ranking) {
        if (!entry.medals) break;
        if (row == 10) break;
        const int baseline = 103 + row * 8;
        draw_text(body_, out, 52, baseline,
                  format_number(GameState{}, row + 1, 0x82), 0x6b);
        draw_flag(out, entry.id, 65, baseline - 6);
        draw_text(body_, out, 80, baseline, entry.name, 0x6b);
        const std::string medals =
            format_number(GameState{}, int((entry.medals >> 24) & 0xff), 0x82) + exe_.cstring(::bmh::layout::WorldCupStatistics_Render_ExecutableField) +
            format_number(GameState{}, int((entry.medals >> 16) & 0xff), 2) + exe_.cstring(::bmh::layout::WorldCupStatistics_Render_ExecutableField) +
            format_number(GameState{}, int((entry.medals >> 8) & 0xff), 2) + exe_.cstring(::bmh::layout::WorldCupStatistics_Render_ExecutableField) +
            format_number(GameState{}, int(entry.medals & 0xff), 2) + exe_.cstring(::bmh::layout::AlltimeTable_Render_ExecutableField8);
        draw_text(body_, out, 0x93, baseline, medals, 0x6b);
        ++row;
    }
    if (hover_frame_shown_) {
        copy(out, flags_, 0, 0x26, 0x70, 0x53, 0x5f, 9, 0xc0);
        source_hover_window(out, window_atlas_, flat_source_window_);
        if (hovered_) {
            const auto& name = projection_.country_names[*hovered_];
            if (!name.empty()) {
                const int width = body_.text_width(name);
                draw_text(body_, out, 112 + (96 - width) / 2, 90, name, 0x5f);
                draw_badge(out, *hovered_, 227, 116);
            }
        }
    }
    return out;
}

} // namespace bmh::ui
