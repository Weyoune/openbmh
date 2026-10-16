// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/analytics.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <stdexcept>

#include "ui/setup.hpp"
#include "ui/manager.hpp"
#include "game/schedule.hpp"

namespace bmh::ui {
namespace {
constexpr size_t kManagerRecord = 0xb0c;
constexpr size_t kFlags = ::bmh::layout::Flags;
constexpr int kSeriesCount = 6;
constexpr int kSourcePointLimit = 52; // local_1d8[52] in CopyOpaqueSlice.

uint32_t read32(const std::vector<uint8_t>& bytes, size_t at) {
    if (at + 3 >= bytes.size()) throw FormatError("analytics: truncated Source manager record");
    return uint32_t(bytes[at]) | uint32_t(bytes[at + 1]) << 8 |
           uint32_t(bytes[at + 2]) << 16 | uint32_t(bytes[at + 3]) << 24;
}
uint16_t read16(const std::vector<uint8_t>& bytes, size_t at) {
    if (at + 1 >= bytes.size()) throw FormatError("analytics: truncated Source word");
    return uint16_t(bytes[at]) | uint16_t(bytes[at + 1]) << 8;
}
int32_t source_asset_mask_1e(const GameState& game, int manager) {
    const size_t rec = size_t(manager) * kManagerRecord;
    uint32_t sum = read32(game.manager_records, rec + 0x318); // HighscoreScore mask bit 1.
    for (uint32_t k = 0; k < 6; ++k) {
        const uint32_t quantity = game.memory.u16(::bmh::layout::Advertising_OriginalShareAdjustment_RuntimeField + uint32_t(manager) * 2 + k * 0x1c);
        const uint32_t value = game.memory.u32(::bmh::layout::FundBlock + k * 0x1c);
        sum += quantity * value; // bit 2, 32-bit Source arithmetic.
    }
    for (uint32_t k = 0; k < 12; ++k) {
        const size_t at = size_t(k) * 0x5c;
        const uint32_t held = read16(game.stocks, at + 0x50 + size_t(manager) * 2);
        const uint32_t price = read16(game.stocks, at + 0x4e);
        sum += held * price; // bit 3.
    }
    for (uint32_t k = 0; k < 5; ++k)
        sum -= game.memory.u32(::bmh::layout::Analytics_SourceAssetMaskE_RuntimeField + uint32_t(manager) * 0x78 + k * 0x18); // bit 4.
    return int32_t(sum);
}
int source_manager_league(const GameState& game, const LeImage& exe, int manager) {
    const int team = game.manager_teams.at(size_t(manager));
    if (team > 0x164) return 0; // TeamLeague2 special-team early return.
    int cumulative = 0;
    for (int league = 0; league < 9; ++league) {
        cumulative += exe.u8(::bmh::layout::LeagueSizes + uint32_t(league));
        if (team < cumulative) return league;
    }
    return 9; // TeamLeague2 return for the remaining club IDs.
}
void line(Image& out, int x0, int y0, int x1, int y1, uint8_t color) {
    const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        if (x0 >= 0 && x0 < out.width && y0 >= 0 && y0 < out.height) out.at(x0, y0) = color;
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 > -dy) err -= dy, x0 += sx;
        if (e2 < dx) err += dx, y0 += sy;
    }
}
void copy_opaque_slice(Image& out, const Image& src, int sx, int sy, int dx, int dy, int w, int h) {
    if (sx < 0 || sy < 0 || dx < 0 || dy < 0 || sx + w > src.width || sy + h > src.height)
        throw FormatError("analytics: Source tab-button slice bounds");
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (dx + x >= out.width || dy + y >= out.height) continue;
            const uint8_t c = src.pixels[size_t(sy + y) * size_t(src.width) + size_t(sx + x)];
            if (c != 0xc0) out.at(dx + x, dy + y) = c;
        }
}
} // namespace

AnalyticsState analytics_from_game(const GameState& game, const LeImage& exe, int manager_slot,
                                   const Image& underlay) {
    if (manager_slot < 0 || manager_slot >= 4 ||
        size_t(manager_slot + 1) * kManagerRecord > game.manager_records.size())
        throw FormatError("analytics: missing manager record");
    if (!game.memory.has(kFlags + size_t(manager_slot) * 3))
        throw FormatError("analytics: missing Source view flags Flags");
    if (underlay.width != 320 || underlay.height != 240)
        throw FormatError("analytics: expected accepted 320x240 parent underlay");

    AnalyticsState out;
    out.manager_slot = manager_slot;
    out.source_flags = game.memory.u8(uint32_t(kFlags + size_t(manager_slot) * 3));
    out.underlay = underlay;
    const size_t rec = size_t(manager_slot) * kManagerRecord;
    for (int i = 0; i < kSeriesCount; ++i) {
        auto& s = out.series[size_t(i)];
        s.label = exe.string_at_pointer(::bmh::layout::Analytics_FormatError_ExecutableField + uint32_t(i) * 4);
        s.color = exe.u8(::bmh::layout::Analytics_FormatError_RuntimeField + uint32_t(i));
        const uint8_t field = exe.u8(::bmh::layout::Analytics_FormatError_RuntimeField2 + uint32_t(i));
        const size_t count_at = rec + 0x283 + field;
        if (count_at >= game.manager_records.size()) throw FormatError("analytics: missing Source series count");
        const size_t count = game.manager_records[count_at];
        if (count > kSourcePointLimit) throw FormatError("analytics: Source point count exceeds local_1d8[52]");

        // CopyOpaqueSlice first uses Analytics_FormatError_RuntimeField2[metric] only to select one of the four
        // count bytes at manager-record +0x283..0x286. The data source itself
        // is selected by metric index (switch at LAB1f652), not by that count class.
        switch (i) {
        case 0: // Source case 0: byte series at record +0x248.
            for (size_t n = 0; n < count; ++n) s.values.push_back(game.manager_records.at(rec + 0x248 + n));
            break;
        case 1: // Source case 1: byte series at +0x222, multiplied by 1000.
            for (size_t n = 0; n < count; ++n) s.values.push_back(int32_t(game.manager_records.at(rec + 0x222 + n)) * 1000);
            break;
        case 2: // Source case 2: byte at +0x270 times corresponding dword +0x340.
            for (size_t n = 0; n < count; ++n)
                s.values.push_back(int32_t(uint32_t(game.manager_records.at(rec + 0x270 + n)) *
                                           read32(game.manager_records, rec + 0x340 + n * 4)));
            break;
        case 3: // Source case 3: dword series at +0x4a8.
            for (size_t n = 0; n < count; ++n) s.values.push_back(int32_t(read32(game.manager_records, rec + 0x4a8 + n * 4)));
            break;
        case 4: // Source case 4: dword series at +0x578.
            for (size_t n = 0; n < count; ++n) s.values.push_back(int32_t(read32(game.manager_records, rec + 0x578 + n * 4)));
            break;
        case 5: // Source case 5: dword series at +0x3d8.
            for (size_t n = 0; n < count; ++n) s.values.push_back(int32_t(read32(game.manager_records, rec + 0x3d8 + n * 4)));
            break;
        default: throw FormatError("analytics: unsupported Source metric");
        }
        s.absolute_min = int32_t(exe.u32(::bmh::layout::Analytics_FormatError_RuntimeField3 + uint32_t(i) * 4));
        s.absolute_max = int32_t(exe.u32(::bmh::layout::Analytics_FormatError_RuntimeField4 + uint32_t(i) * 4));
        if (s.label.empty()) throw FormatError("analytics: missing Source label");
    }
    // CopyOpaqueSlice(param2=0) rewrites exactly Analytics_FormatError_RuntimeField4[0,2,4,5] before drawing.
    // Mirror these read-only axis values from the same live owner data; do not
    // mutate EXE bytes or draw/random hooks from the renderer.
    const int league = source_manager_league(game, exe, manager_slot);
    out.series[0].absolute_max = exe.u8(::bmh::layout::LeagueSizes + uint32_t(league));
    out.series[2].absolute_max = int32_t(game.manager_records.at(rec + 0x5c)) * 25000;
    const int32_t money = int32_t(read32(game.manager_records, rec + 0x314));
    out.series[4].absolute_max = (money / 1000000 + 1) * 1000000;
    const int32_t assets = source_asset_mask_1e(game, manager_slot);
    out.series[5].absolute_max = (assets / 1000000 + 1) * 1000000;
    return out;
}

AnalyticsScreen::AnalyticsScreen(const std::filesystem::path& data, AnalyticsState state)
    : state_(std::move(state)), exe_(load_le(asset_path(data, "BMH.exe"))),
      buttons_(load_vga(asset_path(data / "KB", "0.VGA"))), sprite_(data),
      texts_(load_textd(asset_path(data, "TEXTD.BLK"))) {
    if (state_.manager_slot < 0 || state_.manager_slot >= 4 || state_.underlay.width != 320 ||
        state_.underlay.height != 240 || buttons_.width < 76 || buttons_.height < 84)
        throw FormatError("analytics: incomplete Source projection/assets");
}

Image AnalyticsScreen::render() const {
    Image out = state_.underlay;
    // CopyOpaqueSlice draws its four configured graph-frame edges from Analytics_Render_RuntimeField3/Analytics_Render_RuntimeField2.
    for (int edge = 0; edge < 4; ++edge)
        line(out, exe_.u16(::bmh::layout::Analytics_Render_RuntimeField3 + uint32_t(edge) * 4 + 2), exe_.u8(::bmh::layout::Analytics_Render_RuntimeField2 + uint32_t(edge) * 2 + 1),
             exe_.u16(::bmh::layout::Analytics_Render_RuntimeField3 + uint32_t(edge) * 4), exe_.u8(::bmh::layout::Analytics_Render_RuntimeField2 + uint32_t(edge) * 2),
             exe_.u8(::bmh::layout::Analytics_Render_RuntimeField + uint32_t(edge)));
    // DrawsLegendSampleBeforeDbChecksEnabled adds each enabled metric's source-colored legend sample.
    for (int i = 0; i < kSeriesCount; ++i) {
        const auto& s = state_.series[size_t(i)];
        const int tab_x = 27 + (i % 2) * 120, tab_y = 142 + (i / 2) * 11;
        // DrawsLegendSampleBeforeDbChecksEnabled draws the legend sample before CopyOpaqueSlice checks the enabled
        // flag, so it remains visible even when that metric's curve is off.
        for (int y = tab_y + 3; y <= tab_y + 6; ++y)
            for (int x = tab_x - 7; x <= tab_x - 3; ++x)
                if (x >= 0 && x < out.width && y >= 0 && y < out.height) out.at(x,y)=s.color;
        line(out, tab_x - 8, tab_y + 2, tab_x - 2, tab_y + 2, 0x68);
        line(out, tab_x - 8, tab_y + 2, tab_x - 8, tab_y + 7, 0x68);
        line(out, tab_x - 2, tab_y + 3, tab_x - 2, tab_y + 7, 0x62);
        line(out, tab_x - 8, tab_y + 7, tab_x - 2, tab_y + 7, 0x62);
        if ((state_.source_flags & (1u << i)) == 0) continue;
        const size_t n = s.values.size();
        if (n == 0) continue;
        const auto [min_it, max_it] = std::minmax_element(s.values.begin(), s.values.end());
        const int64_t low = (state_.source_flags & 0x40) ? s.absolute_min : *min_it;
        const int64_t high = (state_.source_flags & 0x40) ? s.absolute_max : *max_it;
        const int64_t range = std::llabs(high - low);
        // PlotsEachPointThrough uses max(point_count, 0x22) for case 0 and
        // max(point_count, 0x11) for the other Source series cases. Its x
        // coordinate is index*0x12a/that_denominator + 0x0d. Keep the
        // historical spacing even when a series has only a few observations.
        const int source_denominator = int(std::max<size_t>(n,
            exe_.u8(::bmh::layout::Analytics_FormatError_RuntimeField2 + uint32_t(i)) == 0 ? 0x22 : 0x11));
        int px0 = 0, py0 = 0;
        for (size_t j = 0; j < n; ++j) {
            int height = 41;
            if (!(state_.source_flags & 0x40) && high == 0) {
                // CopyOpaqueSlice only subtracts min from max when max is nonzero;
                // otherwise it writes normalized value 50 for every sample.
                height = int((50 * 83 + 50) / 100);
            } else if (range != 0) {
                // ClampInteger applies its lower bound, then upper bound (even
                // when an Original table contains the -1 sentinel maximum).
                int64_t v = s.values[j];
                if (v < low) v = low;
                if (high < v) v = high;
                const int64_t distance = std::llabs(v - low);
                if (state_.source_flags & 0x40)
                    height = int((distance * 83 + range / 2) / range);
                else {
                    const int64_t scaled = distance * 100 / range;
                    height = int((scaled * 83 + 50) / 100);
                }
            }
            const int x = int(j * 0x12a / source_denominator) + 0x0d;
            const int y = 53 + (i == 0 ? height : 83 - height);
            if (j != 0) line(out, px0, py0, x, y, s.color);
            // PlotsEachPointThrough plots each point through Fill3(x-1,y-1,x+1,y+1),
            // a filled inclusive 3x3 marker, not a single pixel.
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    if (x + dx >= 0 && x + dx < out.width && y + dy >= 0 && y + dy < out.height)
                        out.at(x + dx, y + dy) = s.color;
            px0 = x; py0 = y;
        }
    }
    // Original six labels and button hit regions: two columns by three rows,
    // as CopyOpaqueSlice advances x by 0x78 then wraps after x>200.
    for (int i = 0; i < kSeriesCount; ++i) {
        const int x = 27 + (i % 2) * 120, y = 142 + (i / 2) * 11;
        const int source_y = (state_.source_flags & (1u << i)) ? 44 : 74;
        copy_opaque_slice(out, buttons_, 24, source_y, x, y, 31, 10);
        int written = 31;
        while (96 - written > 20) {
            const int take = std::min(8, 96 - written - 20);
            copy_opaque_slice(out, buttons_, 24, source_y + 10, x + written, y, take, 10);
            written += take;
        }
        const int tail = 96 - written;
        copy_opaque_slice(out, buttons_, 45 - tail, source_y + 20, x + written, y, tail, 10);
        const auto label = original_to_utf8(exe_.string_at_pointer(::bmh::layout::Analytics_FormatError_ExecutableField + uint32_t(i) * 4));
        const int mode = exe_.u8(::bmh::layout::Advertising_Button_RuntimeField + uint32_t((state_.source_flags & (1u << i)) != 0));
        sprite_.draw(out, x + (96 - sprite_.text_width(label)) / 2, y + 7, label, mode);
    }
    for (int i = 0; i < 2; ++i) {
        // Source CopyOpaqueSlice passes x=0x109, y=0x92+4+i*0x0b,
        // width=0x1e, height=9 for param2=0.
        const int x = 0x109, y = 0x92 + 4 + i * 0x0b;
        const int source_y = (state_.source_flags & (1u << (i + 6))) ? 44 : 74;
        copy_opaque_slice(out, buttons_, 24, source_y, x, y, 30, 9);
        const auto label = original_to_utf8(texts_.strings.at(54 + size_t(i)));
        const int mode = exe_.u8(::bmh::layout::Advertising_Button_RuntimeField + uint32_t((state_.source_flags & (1u << (i + 6))) != 0));
        sprite_.draw(out, x + (30 - sprite_.text_width(label)) / 2, y + 7, label, mode);
    }
    return out;
}

bool AnalyticsScreen::toggle_metric(int index) {
    if (index < 0 || index >= kSeriesCount || pending_) return false;
    pending_ = AnalyticsRequest{state_.owner, AnalyticsRequestKind::toggle_metric,
        state_.manager_slot, index, state_.source_flags,
        uint8_t(state_.source_flags ^ uint8_t(1u << index))};
    return true;
}
bool AnalyticsScreen::click(int x, int y) {
    for (int i = 0; i < kSeriesCount; ++i) {
        const int bx = 27 + (i % 2) * 120, by = 142 + (i / 2) * 11;
        if (x >= bx && x <= bx + 0x60 && y >= by && y <= by + 9)
            return toggle_metric(i);
    }
    // CopyOpaqueSlice passes scale controls at x=0x109, y=0x92+4 and
    // y=0x92+4+0x0b, each 30x9. Either original click flips both flags.
    if (x >= 0x109 && x <= 0x109 + 0x1e &&
        ((y >= 0x92 + 4 && y <= 0x92 + 4 + 9) ||
         (y >= 0x92 + 4 + 0x0b && y <= 0x92 + 4 + 0x0b + 9)))
        return toggle_absolute_scale();
    return false;
}
bool AnalyticsScreen::toggle_absolute_scale() {
    if (pending_) return false;
    pending_ = AnalyticsRequest{state_.owner, AnalyticsRequestKind::toggle_absolute_scale,
        state_.manager_slot, -1, state_.source_flags, uint8_t(state_.source_flags ^ 0xc0)};
    return true;
}
bool AnalyticsScreen::pointer_down(int x, int y, bool right) {
    if (pending_) return false;
    if (right) {
        pending_ = AnalyticsRequest{state_.owner, AnalyticsRequestKind::close,
            state_.manager_slot, -1, state_.source_flags, state_.source_flags};
        return true;
    }
    return click(x, y);
}
void AnalyticsScreen::acknowledge(uint8_t accepted_flags, DeskRequest accepted_owner) {
    if (!pending_ || accepted_owner != state_.owner || accepted_owner.kind != DeskRequestKind::open_menu ||
        accepted_owner.menu_kind != 23 || accepted_owner.manager_slot != state_.manager_slot ||
        (accepted_flags != pending_->expected_flags && accepted_flags != pending_->target_flags))
        throw FormatError("analytics: mismatched accepted Source preference");
    state_.source_flags = accepted_flags;
    state_.owner = accepted_owner;
    pending_.reset();
}

} // namespace bmh::ui
