// SPDX-License-Identifier: GPL-3.0-only
#include "game/state.hpp"
#include "formats/badge_policy.hpp"
#include "formats/executable_layout.hpp"
#include "ui/sportschau.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <stdexcept>

#include "formats/binary.hpp"
#include "game/messages.hpp"
#include "game/state.hpp"

namespace bmh::ui {
namespace {

constexpr int kAtlasWidth = 288;
constexpr int kAtlasHeight = 235;
constexpr int kAtlasColumns = 4;
constexpr int kAtlasRows = 5;

void horizontal(Image& out, int x1, int x2, int y, uint8_t color) {
    if (x1 > x2 || y < 0 || y >= out.height) throw FormatError("sportschau: invalid Source line");
    for (int x = std::max(0, x1); x <= std::min(out.width - 1, x2); ++x) out.at(x, y) = color;
}

void vertical(Image& out, int x, int y1, int y2, uint8_t color) {
    if (y1 > y2 || x < 0 || x >= out.width) throw FormatError("sportschau: invalid Source line");
    for (int y = std::max(0, y1); y <= std::min(out.height - 1, y2); ++y) out.at(x, y) = color;
}

void source_border(Image& out, int x1, int y1, int x2, int y2) {
    horizontal(out, x1, x2, y1, 0x46);
    vertical(out, x1, y1, y2, 0x46);
    horizontal(out, x1, x2, y2, 0x40);
    vertical(out, x2, y1, y2, 0x40);
}

std::string source_number(unsigned value) { return std::to_string(value); }

std::string longest_source_word(std::string_view value) {
    std::string longest;
    size_t start = 0;
    while (start < value.size()) {
        const size_t end = value.find(' ', start);
        const std::string word(value.substr(start, end == std::string_view::npos ? end : end - start));
        if (word.size() > longest.size()) longest = word;
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return longest;
}

std::string fit_source_name(std::string value, const Font& font, bool longest_word) {
    if (longest_word && font.text_width(value) >= 48) {
        const auto longest = longest_source_word(value);
        if (!longest.empty()) value = longest;
    }
    while (font.text_width(value) >= 48 && value.size() >= 2) value.resize(value.size() - 2);
    return value;
}

std::string event_detail(const LeImage& exe, const TextDb& texts, const SportschauLogEvent& event) {
    if (event.player_id == 0x0fff) return {};
    std::string result(event.player_name.begin(), event.player_name.end());
    if (event.source_code >= 2 || !event.has_following_pair) return result;
    const uint8_t next_type = uint8_t(event.following_code_player_word >> 12);
    const uint8_t subtype = next_type > 6 ? uint8_t(next_type - 6) : 0;
    const uint16_t second_player = event.following_code_player_word & 0x0fff;
    const bool source_appends_pair = (next_type < 3 || next_type > 6) &&
        (second_player != 0x0fff || (subtype != 0 && subtype != 3));
    if (!source_appends_pair) return result;
    result += exe.cstring(::bmh::layout::Sportschau_Result_ExecutableField);
    if (subtype == 0 || subtype == 3) {
        if (second_player == 0x0fff) throw FormatError("sportschau: missing Source companion player");
        result.append(event.following_player_name.begin(), event.following_player_name.end());
    } else {
        const size_t text_id = (::bmh::layout::Sportschau_Result_TextPointer2 - ::bmh::layout::TextPointerTable) / 4 + subtype;
        if (text_id >= texts.strings.size()) throw FormatError("sportschau: Source event text id is invalid");
        result += texts[text_id];
    }
    result += exe.cstring(::bmh::layout::Sportschau_Result_TextPointer);
    return result;
}

Image load_kb(const std::filesystem::path& data_directory, const char* file) {
    return load_vga(data_directory / "KB" / file);
}

std::optional<Palette> load_optional_embedded_palette(const std::filesystem::path& path) {
    const Bytes bytes = read_file(path);
    if (bytes.size() < 8) throw FormatError("sportschau: VGA palette source is truncated");
    const size_t rle_end = 8 + size_t(le32(bytes, 4));
    if (rle_end > bytes.size()) throw FormatError("sportschau: VGA RLE length exceeds palette source");
    const size_t trailing = bytes.size() - rle_end;
    if (trailing == 0) return std::nullopt;
    if (trailing < 768) throw FormatError("sportschau: truncated embedded VGA palette");

    // PaletteOverride reads 0x300 bytes after the RLE stream and installs them
    // through KeepSameSixBitDACExpansion/Size5. Keep the same six-bit DAC expansion
    // used for M3PC.COL; only the palette source differs for embedded VGA art.
    Palette palette;
    for (size_t i = 0; i < palette.argb.size(); ++i) {
        const auto channel = [&](size_t component) {
            return uint32_t(vga_dac_rgb8(bytes[rle_end + i * 3 + component]));
        };
        palette.argb[i] = 0xff000000u | channel(0) << 16 | channel(1) << 8 | channel(2);
    }
    return palette;
}

std::string short_bytes(const std::vector<uint8_t>& bytes) {
    return std::string(bytes.begin(), bytes.end());
}

void copy_opaque(const Image& source, int source_x, int source_y,
                 Image& destination, int destination_x, int destination_y,
                 int width, int height) {
    if (source_x < 0 || source_y < 0 || width <= 0 || height <= 0 ||
        source_x + width > source.width || source_y + height > source.height ||
        destination_x < 0 || destination_y < 0 ||
        destination_x + width > destination.width || destination_y + height > destination.height) {
        throw FormatError("sportschau: Source frame rectangle is out of bounds");
    }
    for (int y = 0; y < height; ++y) {
        const auto source_begin = source.pixels.begin() + size_t(source_y + y) * source.width + source_x;
        auto destination_begin = destination.pixels.begin() + size_t(destination_y + y) * destination.width + destination_x;
        std::copy_n(source_begin, width, destination_begin);
    }
}

void copy_zero_transparent(const Image& source, int source_x, int source_y,
                           Image& destination, int destination_x, int destination_y,
                           int width, int height, bool clipped) {
    if (source_x < 0 || source_y < 0 || width <= 0 || height <= 0 ||
        source_x + width > source.width || source_y + height > source.height)
        throw FormatError("sportschau: Source transparent-copy source is out of bounds");
    if (!clipped && (destination_x < 0 || destination_y < 0 ||
                     destination_x + width > destination.width ||
                     destination_y + height > destination.height))
        throw FormatError("sportschau: Source event icon is out of bounds");
    const int left = std::max(0, destination_x), top = std::max(0, destination_y);
    const int right = std::min(destination.width, destination_x + width);
    const int bottom = std::min(destination.height, destination_y + height);
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            const int sx = source_x + x - destination_x;
            const int sy = source_y + y - destination_y;
            const uint8_t value = source.pixels[size_t(sy) * source.width + sx];
            if (value != 0) destination.at(x, y) = value;
        }
    }
}

void restore_source_rect(const Image& source, int source_x, int source_y,
                        Image& destination, int destination_x, int destination_y,
                        int width, int height) {
    // FormatError2 returns without drawing when the destination rectangle extends
    // outside the viewport, so a late miss row is a no-op rather than an error.
    if (destination_x < 0 || destination_y < 0 ||
        destination_x + width > destination.width ||
        destination_y + height > destination.height) return;
    if (source_x < 0 || source_y < 0 || width <= 0 || height <= 0 ||
        source_x + width > source.width || source_y + height > source.height)
        throw FormatError("sportschau: saved Source restore rectangle is invalid");
    copy_opaque(source, source_x, source_y, destination,
                destination_x, destination_y, width, height);
}

} // namespace

CompetitionWinnerScreen::CompetitionWinnerScreen(const std::filesystem::path& data)
    : exe_(load_le(data / "BMH.exe")), font_(load_font(data / "F" / "BMH2.FNT")) {
    for (size_t i = 0; i < 2; ++i) {
        const std::string background = i ? "131.VGA" : "128.VGA";
        backdrop_[i] = load_kb(data, background.c_str());
        lower_[i] = load_kb(data, i ? "47.VGA" : "40.VGA");
        palettes_[i] = load_optional_embedded_palette(data / "KB" / background);
    }
    for (size_t i = 1; i < trophies_.size(); ++i) if (i != 7) {
        const std::string file = std::to_string(40 + i) + ".VGA";
        trophies_[i] = load_kb(data, file.c_str());
    }
}

const std::optional<Palette>& CompetitionWinnerScreen::palette_override(uint8_t title) const {
    if (title > 8) throw FormatError("winner screen: invalid Source title");
    return palettes_[title == 7 ? 1 : 0];
}

Image CompetitionWinnerScreen::render(const GameState& state, const CompetitionWinnerView& view) const {
    if (view.title > 8) throw FormatError("winner screen: invalid Source title");
    const size_t art = view.title == 7 ? 1 : 0;
    Image out = backdrop_[art];
    copy_opaque(lower_[art], 0, 0, out, 0, 101, 320, 139);
    if (view.title != 0 && view.title != 7)
        copy_opaque(trophies_[view.title], 0, 0, out, 64, 60, 79, 98);
    const uint32_t table = exe_.u8(::bmh::layout::SeasonEntrants_FormatError_TextPointer + view.title) ? ::bmh::layout::CupDraw_FormatError_ExecutableField6 : ::bmh::layout::SeasonEntrants_FormatError_TextPointer2;
    const std::string caption = message_text(state, ::bmh::layout::Sportschau_Render_TextPointer3) + exe_.cstring(::bmh::layout::Sportschau_Render_TextPointer) +
        message_text(state, table + uint32_t(exe_.u8(::bmh::layout::SeasonEntrants_FormatError_RuntimeField + view.title)) * 4);
    const std::string club = state.club_name(view.team);
    const uint8_t color = view.title == 7 ? 0x76 : 0x92;
    const auto label = [&](std::string_view text, int low, int high, int baseline, uint8_t ink) {
        // DrawnFirstBy centers within inclusive Source bounds, then accepts/rejects
        // the whole font2 label; it does not truncate the club name.
        const int width = font_.text_width(text);
        const int x = low + (high - low) / 2 - width / 2;
        const int top = baseline - font_.baseline;
        if (x < 0 || top < 0 || x + width > 320 || baseline > 240) return;
        font_.draw(out, x, top, text, ink);
    };
    label(caption, 0, 317, 224, 0);
    label(caption, 0, 319, 225, color);
    label(club, 2, 317, 235, 0);
    label(club, 2, 319, 236, color);
    return out;
}

MatchdayIntro::MatchdayIntro(const std::filesystem::path& data)
    : backdrop_(load_kb(data, "136.VGA")), font_(load_font(data / "F" / "BMH2.FNT")),
      palette_override_(load_optional_embedded_palette(data / "KB" / "136.VGA")) {
    if (backdrop_.width != 320 || backdrop_.height != 240)
        throw FormatError("matchday intro: KB136 must be 320x240");
}

Image MatchdayIntro::render(const MatchdayIntroView& view) const {
    Image out = backdrop_;
    const auto label = [&](std::string_view text, int x, int baseline, uint8_t color) {
        // TextWidth3 under font2 accepts or rejects each whole label.
        const int width = font_.text_width(text);
        if (x + width > 320 || baseline < font_.baseline || baseline > 240) return;
        font_.draw(out, x, baseline - font_.baseline, text, color);
    };
    for (size_t i = 0; i < view.competition_names.size(); ++i) {
        const int baseline = 52 + int(i) * 9;
        label(view.competition_names[i], 135, baseline + 1, 0x76);
        label(view.competition_names[i], 134, baseline, 0x44);
    }
    return out;
}

SportschauEleven::SportschauEleven(const std::filesystem::path& data_directory)
    : exe_(load_le(data_directory / "BMH.exe")),
      font_(load_font(data_directory / "F" / "BMH3.FNT")),
      room_(load_kb(data_directory, "57.VGA")), figures_(load_kb(data_directory, "16.VGA")) {
    if (room_.width != 320 || room_.height != 240 || figures_.width < 132 || figures_.height < 31)
        throw FormatError("sportschau: invalid Original eleven artwork");
}

static void viewer_text(Image& out, std::string_view value, int center, int baseline,
                        bool centered, bool gradient, const Font& font_, const LeImage& exe_) {
    const int width = font_.text_width(value);
    const int x = centered ? center - width / 2 : center;
    // DrawnFirstBy accepts or rejects the entire shadowed label. The equal
    // endpoints select the center, rather than defining a clipping column.
    if (width <= 0 || x < 0 || x + width + 2 > out.width ||
        baseline < font_.baseline || baseline + 2 > out.height) return;
    Image mask(width, font_.height); font_.draw(mask, 0, 0, value, 1);
    int palette_row = 0;
    if (gradient) while (palette_row < 32 && exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2 + uint32_t(palette_row)) != 0x5f) ++palette_row;
    for (int pass = 0; pass < 2; ++pass) {
        const int offset = pass == 0 ? 2 : 0;
        for (int y = 0; y < font_.height; ++y) for (int dx = 0; dx < width; ++dx) {
            const int ty = baseline - font_.baseline + y + offset, tx = x + dx + offset;
            if (ty < 0 || ty >= out.height || tx < 0 || tx >= out.width || !mask.at(dx,y)) continue;
            out.at(tx,ty) = pass == 0 ? 0x40 : gradient
                ? exe_.u8(::bmh::layout::Advertising_Text_RuntimeField + uint32_t(palette_row * 6 + y)) : 0x5f;
        }
    }
}

Image SportschauEleven::render(const SportschauAwardsView& view, bool gradient) const {
    Image out = view.source_backdrop_available ? room_ : Image(320, 240);
    viewer_text(out, view.competition_title, 235, 230, false, gradient, font_, exe_);
    size_t role = 0, in_role = 0;
    for (size_t i = 0; i < 11; ++i) {
        if (in_role >= exe_.u8(::bmh::layout::Sportschau_Render_RuntimeField3 + uint32_t(role))) { ++role; in_role = 0; }
        if (role >= 4) throw FormatError("sportschau: invalid eleven role table");
        const int center = exe_.u16(::bmh::layout::Sportschau_Render_RuntimeField5 + uint32_t(i * 2));
        const int baseline = exe_.u8(::bmh::layout::Sportschau_Render_RuntimeField2 + uint32_t(role)) + (i == 5 || i == 6 ? 15 : 0);
        copy_opaque(figures_, exe_.u8(::bmh::layout::Sportschau_Render_RuntimeField4 + uint32_t(role)), 0, out,
                    center - 14, baseline - 32, 33, 31);
        viewer_text(out, view.players[i].player_name, center, baseline + 4, true, gradient, font_, exe_);
        viewer_text(out, view.players[i].club_name, center, baseline + 12, true, gradient, font_, exe_);
        ++in_role;
    }
    return out;
}

SportschauTeamInfo::SportschauTeamInfo(const std::filesystem::path& data_directory)
    : exe_(load_le(data_directory / "BMH.exe")),
      font_(load_font(data_directory / "F" / "BMH3.FNT")),
      room_(load_kb(data_directory, "57.VGA")), atlas_(load_kb(data_directory, "59.VGA")) {
    if (room_.width != 320 || room_.height != 240 || atlas_.width < 152 || atlas_.height < 56)
        throw FormatError("sportschau: invalid Original team info artwork");
}

Image SportschauTeamInfo::render(const SportschauTeamInfoView& view, bool gradient) const {
    Image out = view.source_backdrop_available ? room_ : Image(320, 240);
    viewer_text(out, view.competition_title, 235, 230, false, gradient, font_, exe_);
    for (size_t i = 0; i < 4; ++i) {
        const int x = exe_.u8(::bmh::layout::Sportschau_Render_TextPointer2 + uint32_t(i)), y = exe_.u8(::bmh::layout::Sportschau_Render_RuntimeField + uint32_t(i));
        copy_opaque(atlas_, int(i) * 38, 0, out, x, y, 38, 38);
        // ColorBottomRightBevel: two inclusive, two-pixel strips below/right of the icon.
        const int right = std::min(318, x + 38) + 1, bottom = std::min(238, y + 38) + 1;
        for (int yy = y + 38; yy <= bottom; ++yy)
            for (int xx = x + 2; xx <= right; ++xx) out.at(xx, yy) = 0x40;
        for (int yy = y + 2; yy <= bottom; ++yy)
            for (int xx = x + 38; xx <= right; ++xx) out.at(xx, yy) = 0x40;
        // SelectsTransparentIndex counts signed base-ten digits and emits right-to-left at
        // 12-pixel steps. The final 0xff is a VGA write mask; color zero is
        // transparent. Keep zero as one digit and Source's +1 left bearing.
        int32_t value = view.totals[i]; int digits = 1;
        for (int32_t n = value; n >= 10; n /= 10) ++digits;
        int cursor = x + 50 - 11 + 12 * digits;
        do {
            const int sx = int(value % 10) * 11;
            // UsesForTheseSprites rejects a negative Source glyph rectangle.
            if (sx >= 0 && cursor >= 0 && cursor + 11 <= out.width)
                copy_zero_transparent(atlas_, sx, 38, out, cursor, y + 13, 11, 18, false);
            value /= 10; cursor -= 12;
        } while (value != 0);
    }
    return out;
}

SportschauIntro::SportschauIntro(const std::filesystem::path& data_directory)
    : backdrop_(load_kb(data_directory, "132.VGA")),
      atlases_{load_kb(data_directory, "60.VGA"), load_kb(data_directory, "61.VGA")},
      palette_override_(load_optional_embedded_palette(data_directory / "KB" / "132.VGA")) {
    if (backdrop_.width != 320 || backdrop_.height != 240)
        throw FormatError("sportschau: KB/132 must be 320x240");
    for (const auto& atlas : atlases_) {
        if (atlas.width != kAtlasWidth || atlas.height != kAtlasHeight ||
            kAtlasColumns * frame_width > atlas.width || kAtlasRows * frame_height != atlas.height) {
            throw FormatError("sportschau: KB/60 and KB/61 must be 288x235 Source atlases");
        }
    }
}

Image SportschauIntro::render_frame(uint32_t index) const {
    if (index >= frame_count) throw FormatError("sportschau: frame index must be in [0, 40)");

    Image frame = backdrop_;
    const uint32_t atlas_index = index / frames_per_atlas;
    const uint32_t atlas_frame = index % frames_per_atlas;
    const int source_x = int(atlas_frame % kAtlasColumns) * frame_width;
    const int source_y = int(atlas_frame / kAtlasColumns) * frame_height;
    copy_opaque(atlases_[atlas_index], source_x, source_y, frame,
                destination_x, destination_y, frame_width, frame_height);
    return frame;
}

SportschauMatchReport::SportschauMatchReport(const std::filesystem::path& data_directory,
        SportschauFixtureView fixture, SportschauReportContext context)
    : fixture_(std::move(fixture)), context_(std::move(context)),
      exe_(load_le(data_directory / "BMH.exe")),
      small_(load_font(data_directory / "F" / "BMH3.FNT")),
      heading_(load_font(data_directory / "F" / "BMH2.FNT")),
      texts_(load_textd(data_directory / "TEXTD.BLK")),
      room_(load_kb(data_directory, "57.VGA")),
      event_art_(load_kb(data_directory, "14.VGA")),
      panel_art_(load_kb(data_directory, "58.VGA")),
      fallback_badge_(load_kb(data_directory, "0.VGA")) {
    if (fixture_.home_team < 0 || fixture_.away_team < 0 ||
        fixture_.home_name.empty() || fixture_.away_name.empty() ||
        fixture_.source_footer_variant > 2 || room_.width != 320 || room_.height != 240 ||
        event_art_.width < 306 || event_art_.height < 48 || panel_art_.width < 313 ||
        panel_art_.height < 197 || fallback_badge_.width < 46 || fallback_badge_.height < 38 ||
        small_.baseline <= 0 || heading_.baseline <= 0) {
        throw FormatError("sportschau: incomplete Source report inputs/assets");
    }
    const std::array<uint16_t, 2> badge_words{fixture_.home_badge_word, fixture_.away_badge_word};
    for (size_t side = 0; side < badge_words.size(); ++side) {
        const int team = side == 0 ? fixture_.home_team : fixture_.away_team;
        if (team == 0x0fff) continue; // SourceBadgeFallback uses its generic missing-club icon.
        const bool has_source_badge = !original_badge_fallback(team);
        if (!has_source_badge) continue; // Source FormatError draws a distinct generic icon; see notes.
        if (badge_words[side] == 0xffff) throw FormatError("sportschau: missing live Source badge word");
        const unsigned atlas = (badge_words[side] & 0x01ff) >> 3;
        if (atlas >= 64) throw FormatError("sportschau: invalid Source badge atlas selector");
        badge_atlas_[side] = load_vga(data_directory / "W" / (std::to_string(atlas) + ".VGA"));
        const auto& image = *badge_atlas_[side];
        if (image.width < 38 * 8 || image.height < 38)
            throw FormatError("sportschau: truncated Source badge atlas");
    }
}

void SportschauMatchReport::text(Image& out, const Font& font, int x, int baseline,
                                  std::string_view value, uint8_t color, int right, bool shadow) const {
    const int width = font.text_width(value);
    const bool gradient = context_.gradient_text && &font == &small_;
    if (right >= 0) x += (right - x) / 2 - width / 2;
    // DrawnFirstBy centers first, then accepts/rejects the full string against the
    // viewport. The right argument selects the center; it is not a clip edge.
    if (width <= 0 || x < 0 || x + width > out.width ||
        baseline < font.baseline || baseline >= out.height) return;
    Image mask(width, font.height);
    font.draw(mask, 0, 0, value, 1);
    if (shadow) {
        for (int y = 0; y < font.height; ++y) {
            const int target_y = baseline - font.baseline + y + 2;
            if (target_y < 0 || target_y >= out.height) continue;
            for (int dx = 0; dx < width; ++dx) {
                const int target_x = x + dx + 2;
                if (mask.at(dx, y) && target_x >= 0 && target_x < out.width)
                    out.at(target_x, target_y) = 0x40;
            }
        }
    }
    int palette_row = 0;
    if (gradient)
        while (palette_row < 32 && exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2 + uint32_t(palette_row)) != color) ++palette_row;
    for (int y = 0; y < font.height; ++y) {
        const int target_y = baseline - font.baseline + y;
        if (target_y < 0 || target_y >= out.height) continue;
        for (int dx = 0; dx < width; ++dx) {
            const int target_x = x + dx;
            if (mask.at(dx, y) && target_x >= 0 && target_x < out.width) {
                out.at(target_x, target_y) = gradient
                    ? exe_.u8(::bmh::layout::Advertising_Text_RuntimeField + uint32_t(palette_row) * 6 + uint32_t(y))
                    : color;
            }
        }
    }
}

void SportschauMatchReport::panel(Image& out, int x1, int y1, int x2, int y2) const {
    if (context_.solid_panel_background) {
        for (int y = y1; y <= y2; ++y)
            for (int x = x1; x <= x2; ++x) out.at(x, y) = 0x42;
    } else {
        // LoadGameEndTaxAnimationAssets's textured branch copies KB58 one pixel inset and uses
        // a source y offset of 19 rows.
        copy_opaque(panel_art_, x1, y1 - 19, out, x1 + 1, y1 + 1,
                    (x2 - x1) - 1, (y2 - y1) - 1);
    }
    source_border(out, x1, y1, x2, y2);
}

void SportschauMatchReport::draw_badge(Image& out, int x, int y, int team,
                                       uint16_t badge_word, size_t side) const {
    source_border(out, x - 1, y - 1, x + 38, y + 38);
    if (team == 0x0fff || !badge_atlas_[side]) {
        copy_opaque(fallback_badge_, 8, 0, out, x, y, 38, 38);
        return;
    }
    const int sx = int(badge_word & 7) * 38;
    copy_opaque(*badge_atlas_[side], sx, 0, out, x, y, 38, 38);
}

Image SportschauMatchReport::render_report(size_t visible_log_entries) const {
    return render_report(visible_log_entries, std::nullopt);
}

Image SportschauMatchReport::render_report(const SportschauReportStep& step) const {
    return render_report(step.visible_log_entries, step.progress_minute, step.chance_wait);
}

Image SportschauMatchReport::render_report(size_t visible_log_entries,
                                          std::optional<uint8_t> progress, bool transient_chance) const {
    if (visible_log_entries > fixture_.log_events.size())
        throw FormatError("sportschau: visible log prefix exceeds Source projection");
    Image out = room_;
    panel(out, 6, 32, 313, 56);
    panel(out, 6, 59, 157, 179);
    panel(out, 162, 59, 313, 179);
    const int footer_delta = int(int8_t(exe_.u8(::bmh::layout::Sportschau_FormatError_RuntimeField2 + fixture_.source_footer_variant)));
    const int footer_width = 245 + footer_delta;
    copy_opaque(event_art_, 0, 0, out, 7, 184, footer_width, 41);

    draw_badge(out, 62, 6, fixture_.home_team, fixture_.home_badge_word, 0);
    draw_badge(out, 218, 6, fixture_.away_team, fixture_.away_badge_word, 1);
    const std::string home_name(fixture_.home_name.begin(), fixture_.home_name.end());
    const std::string away_name(fixture_.away_name.begin(), fixture_.away_name.end());
    text(out, heading_, 0, 54, home_name, 0x5f, 156);
    text(out, heading_, 156, 54, away_name, 0x5f, 312);

    const bool home_domestic = fixture_.home_team < runtime_dimensions.record_teams;
    const bool away_domestic = fixture_.away_team < runtime_dimensions.record_teams;
    // Some Original club abbreviation bytes are unavailable because the
    // source-side temporary buffer was never initialized. Keep the known
    // header, badge, score and report visible, and omit only that abbreviation.
    std::string home_short = home_domestic
        ? (fixture_.home_short_name_available ? short_bytes(fixture_.home_short_name) : std::string{})
        : home_name;
    std::string away_short = away_domestic
        ? (fixture_.away_short_name_available ? short_bytes(fixture_.away_short_name) : std::string{})
        : away_name;
    if (fixture_.home_name_requires_longest_word && small_.text_width(home_short) >= 48)
        home_short = longest_source_word(home_short);
    if (fixture_.away_name_requires_longest_word && small_.text_width(away_short) >= 48)
        away_short = longest_source_word(away_short);
    if (fixture_.home_name_requires_font_fit) home_short = fit_source_name(std::move(home_short), small_, false);
    if (fixture_.away_name_requires_font_fit) away_short = fit_source_name(std::move(away_short), small_, false);
    text(out, small_, 10, 195, home_short, 0x6c, 59);
    text(out, small_, 10, 220, away_short, 0x6c, 59);

    const Image report_base = out; // MatchdayIntro's saved Source rectangle layer.

    uint8_t home_goals = 0, away_goals = 0;
    // local_28 starts at minute 1. Source emits each progress step with the
    // old minute value, before advancing toward the next event/end minute.
    uint8_t progress_minute = 0;
    uint16_t source_minute_cursor = 1;
    bool has_progress_step = false;
    int baseline = 66;
    text(out, heading_, 160 - heading_.text_width("0:0") / 2, 42, "0:0", 0x5f, -1, true);
    for (size_t i = 0; i < visible_log_entries; ++i) {
        const auto& event = fixture_.log_events[i];
        if (event.group != fixture_.group || event.fixture != fixture_.fixture ||
            event.source_code != uint8_t(event.source_code_player_word >> 12) ||
            event.player_id != (event.source_code_player_word & 0x0fff) ||
            event.source_value != uint8_t(event.source_team_value_word & 0x7f) ||
            event.consumes_following_pair != (event.source_code < 2))
            throw FormatError("sportschau: inconsistent raw Source event words");
        const int team = int(event.source_team_value_word >> 7);
        if (team != event.team_id || (team != fixture_.home_team && team != fixture_.away_team))
            throw FormatError("sportschau: event team is outside its fixture");
        const int side = team == fixture_.home_team ? 0 : 1;
        const int x = side == 0 ? 8 : 164;
        const uint8_t code = event.source_code;
        const bool companion_visible = code < 2 && i + 1 < visible_log_entries &&
            fixture_.log_events[i + 1].source_word_index == event.source_word_index + 2;
        const bool transient = transient_chance && code < 2 &&
            i + (companion_visible ? 2 : 1) == visible_log_entries;
        while (source_minute_cursor < event.source_value) {
            progress_minute = uint8_t(source_minute_cursor++);
            has_progress_step = true;
        }
        if (code == 0 && !transient) {
            restore_source_rect(report_base, 135, 33, out, 135, 33, 50, 12);
        }
        if (code < 2) {
            if (code == 0) {
                if (side == 0) ++home_goals; else ++away_goals;
            }
            const std::string score = source_number(home_goals + (transient && code == 1 && side == 0)) +
                ":" + source_number(away_goals + (transient && code == 1 && side == 1));
            const int score_width = heading_.text_width(score);
            if (!transient) text(out, heading_, 160 - score_width / 2, 42, score, 0x5f, -1, true);
            if ((code == 0 || transient) && baseline < 179)
                text(out, small_, x, baseline, score, transient ? 0xf7 : 0x5f);
        } else if (baseline < 179) {
            if (!exe_.contains(::bmh::layout::Sportschau_FormatError_RuntimeField + code))
                throw FormatError("sportschau: Source event icon code is out of range");
            const int icon_x = 250 + exe_.u8(::bmh::layout::Sportschau_FormatError_RuntimeField + code);
            copy_zero_transparent(event_art_, icon_x, 41, out, x + 2, baseline - 5, 5, 5, false);
        }
        if (baseline < 179) {
            const std::string detail = event_detail(exe_, texts_, event);
            if (!detail.empty() && event.player_id != 0x0fff)
                text(out, small_, x + 15, baseline, detail, transient ? 0x68 : 0x6b);
            if (event.source_value != 0) {
                std::string minute = source_number(event.source_value);
                if (minute.size() < 3) minute.insert(0, 3 - minute.size(), '%');
                minute += texts_[(::bmh::layout::Sportschau_FormatError_TextPointer - ::bmh::layout::TextPointerTable) / 4];
                text(out, small_, x + 119, baseline, minute, transient ? 0x66 : 0x69);
            }
        }
        if (code != 2 && !transient) {
            const uint32_t offset_index = uint32_t((code > 1 ? 1 : 0) + side * 2);
            const int timeline_y = 100 + int(int8_t(exe_.u8(::bmh::layout::Sportschau_FormatError_RuntimeField3 + offset_index)));
            const int icon_x = 250 + exe_.u8(::bmh::layout::Sportschau_FormatError_RuntimeField + code);
            copy_zero_transparent(event_art_, icon_x, 41, out,
                                  int(source_minute_cursor) * 2 + 65, timeline_y, 5, 5, true);
        }
        // Source draws a miss row transiently, then restores the saved report
        // rectangle after its scene branch. Its final static view has no row.
        if (code == 1 && !transient)
            restore_source_rect(report_base, x, baseline - 6, out, x, baseline - 6, 140, 7);
        if (code != 1) baseline += 7;
        if (code < 2) {
            const uint16_t companion_index = uint16_t(event.source_word_index + 2);
            if (i + 1 < visible_log_entries &&
                fixture_.log_events[i + 1].source_word_index == companion_index)
                ++i;
        }
    }

    // Empty logs are valid: the score remains 0:0 and progress uses the
    // Source descriptor's played-minute byte. Never substitute final score.
    if (visible_log_entries == fixture_.log_events.size()) {
        while (source_minute_cursor <= fixture_.source_played_minute) {
            progress_minute = uint8_t(source_minute_cursor++);
            has_progress_step = true;
        }
    }
    if (progress) {
        progress_minute = *progress;
        has_progress_step = progress_minute != 0;
    }
    const int progress_width = has_progress_step ? 2 * int(progress_minute) + 4 : 0;
    if (has_progress_step && progress_width <= out.width - 62)
        copy_opaque(event_art_, 0, 41, out, 62, 201, progress_width, 7);
    return out;
}

} // namespace bmh::ui
