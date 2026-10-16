// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/minute_statistics.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

#include "game/messages.hpp"
#include "ui/manager.hpp"
#include "ui/setup.hpp"

namespace bmh::ui {
namespace {
constexpr size_t kManagerStride = 0xb0c;
constexpr size_t kSeriesOffset = 0x1ae;
constexpr size_t kSeriesBytes = 18;
constexpr size_t kSeriesCount = 4;
constexpr int kSourceRightEvent = 8;

void copy_transparent(Image& target, const Image& source, int sx, int sy,
                      int dx, int dy, int width, int height, uint8_t transparent) {
    if (sx < 0 || sy < 0 || dx < 0 || dy < 0 || width < 0 || height < 0 ||
        sx + width > source.width || sy + height > source.height ||
        dx + width > target.width || dy + height > target.height)
        throw FormatError("minute statistics: Original KB36 crop outside image");
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const uint8_t pixel = source.pixels[size_t(sy + y) * size_t(source.width) + size_t(sx + x)];
            if (pixel != transparent) target.at(dx + x, dy + y) = pixel;
        }
}

// SourceBarColor / SourceBarColor2 scan each current 18-byte series. Their
// returned extrema, not the helpers' initial sentinels, drive the color bands.
uint8_t source_bar_color(const LeImage& exe, size_t series, uint8_t value,
                         uint8_t source_max, uint8_t source_min) {
    uint8_t color_index = 0;
    if (source_max > 1) {
        if (int(value) * 12 / 10 < int(source_max) - 1) {
            if (int(value) <= (int(source_min) * 12) / 10 + 1)
                color_index = exe.u8(::bmh::layout::MinuteStatistics_SourceBarColor_RuntimeField + size_t(1 < series));
        } else {
            color_index = exe.u8(::bmh::layout::MinuteStatistics_SourceBarColor_RuntimeField + size_t(series < 2));
        }
    }
    return color_index;
}
} // namespace

MinuteStatisticsProjection minute_statistics_from_game(const GameState& game, int manager_slot) {
    if (manager_slot < 0 || manager_slot >= 4)
        throw FormatError("minute statistics: manager slot outside Original four records");
    const size_t base = size_t(manager_slot) * kManagerStride;
    if (base > game.manager_records.size() ||
        kSeriesOffset + kSeriesCount * kSeriesBytes > game.manager_records.size() - base)
        throw FormatError("minute statistics: save lacks active manager's four source series");

    MinuteStatisticsProjection result;
    result.manager_slot = manager_slot;
    for (size_t series = 0; series < kSeriesCount; ++series) {
        const size_t source = base + kSeriesOffset + series * kSeriesBytes;
        std::copy_n(game.manager_records.begin() + std::ptrdiff_t(source), kSeriesBytes,
                    result.source_series[series].begin());
    }
    return result;
}

MinuteStatisticsScreen::MinuteStatisticsScreen(const std::filesystem::path& data_directory,
                                               MinuteStatisticsProjection projection)
    : exe_(load_le(asset_path(data_directory, "BMH.exe"))),
      chart_atlas_(load_vga(asset_path(data_directory / "KB", "36.VGA"))),
      font_(load_font(asset_path(data_directory / "F", "BMH3.FNT"))),
      projection_(std::move(projection)) {
    if (projection_.manager_slot < 0 || projection_.manager_slot >= 4 ||
        chart_atlas_.width < 32 || chart_atlas_.height < 64)
        throw FormatError("minute statistics: incomplete Source projection or KB36 atlas");
    for (size_t series = 0; series < kSeriesCount; ++series) {
        const auto& values=projection_.source_series[series];
        const auto [minimum,maximum]=std::minmax_element(values.begin(),values.end());
        for (uint8_t value : values)
            if (size_t(source_bar_color(exe_,series,value,*maximum,*minimum))*16+16>size_t(chart_atlas_.height))
                throw FormatError("minute statistics: Source EXE bar color outside KB36 atlas");
    }
}

void MinuteStatisticsScreen::draw_text(Image& image, int x, int baseline,
                                       std::string_view text, uint8_t color) const {
    const int top = baseline - font_.baseline;
    if (x >= 0 && top >= 0 && top + font_.height <= image.height &&
        x + font_.text_width(text) <= image.width)
        font_.draw(image, x, top, text, color);
}

Image MinuteStatisticsScreen::render(const Image& accepted_underlay) const {
    if (accepted_underlay.width != 320 || accepted_underlay.height != 240)
        throw FormatError("minute statistics: expected 320x240 accepted parent underlay");
    Image out = accepted_underlay;

    // HandleSourceEvent2 draws 0,5,...,90 at x=14+(16*i)+(i>1?2:0), baseline 60.
    for (int index = 0; index < 19; ++index) {
        const int x = 20 + index * 16;
        const int draw_x = x - 6 + (index > 1 ? 2 : 0);
        draw_text(out, draw_x, 0x3c,
                  format_number(GameState{}, index * 5, 0x82), 0x69);
    }

    for (size_t series = 0; series < kSeriesCount; ++series) {
        const int y = 0x3f + int(series) * 16;
        copy_transparent(out, chart_atlas_, 0, int(series) * 16, 2, y, 16, 16, 0xc0);
        const auto& values=projection_.source_series[series];
        const auto [minimum,maximum]=std::minmax_element(values.begin(),values.end());
        for (size_t index = 0; index < kSeriesBytes; ++index) {
            const int x = 20 + int(index) * 16;
            const uint8_t value = values[index];
            const uint8_t bar_color = source_bar_color(exe_,series,value,*maximum,*minimum);
            copy_transparent(out, chart_atlas_, 16, int(bar_color) * 16,
                             x, y, 16, 16, 0xc0);
            draw_text(out, x + 4, y + 11,
                      format_number(GameState{}, int(value), 0x82), 99);
        }
    }
    return out;
}

bool MinuteStatisticsScreen::handle_source_event(uint8_t event_code) {
    if (closed_ || event_code != kSourceRightEvent) return false;
    closed_ = true;
    return true;
}

} // namespace bmh::ui
