// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/season_entrants.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

#include "formats/textd.hpp"
#include "game/state.hpp"
#include "ui/manager.hpp"

namespace bmh::ui {
namespace {
constexpr std::array<uint32_t, 3> kPoolAddress{::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField25, ::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField14, ::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField19};
constexpr std::array<uint32_t, 3> kCountAddress{::bmh::layout::Career_EuropeanClub_ExecutableField, ::bmh::layout::Career_EuropeanClub_ExecutableField2, ::bmh::layout::Career_EuropeanClub_ExecutableField3};
constexpr uint32_t kGermanTeamsText = 1249;
constexpr int kMaxEntrants = 32;
constexpr int kPlacementSlots = 8;

void fill(Image& out, int x, int y, int w, int h, uint8_t color) {
    for (int yy = std::max(0, y); yy < std::min(out.height, y + h); ++yy)
        for (int xx = std::max(0, x); xx < std::min(out.width, x + w); ++xx)
            out.at(xx, yy) = color;
}

void source_badge_frame(Image& out, int x, int y) {
    // PaintsFourBorderEdges: top, left in 0x46; bottom, right in 0x40. The latter
    // calls overwrite the corners in the same order as the original helper.
    for (int xx = x - 1; xx <= x + 38; ++xx) {
        if (xx >= 0 && xx < out.width && y - 1 >= 0) out.at(xx, y - 1) = 0x46;
    }
    for (int yy = y - 1; yy <= y + 38; ++yy) {
        if (yy >= 0 && yy < out.height && x - 1 >= 0) out.at(x - 1, yy) = 0x46;
    }
    for (int xx = x - 1; xx <= x + 38; ++xx) {
        if (xx >= 0 && xx < out.width && y + 38 < out.height) out.at(xx, y + 38) = 0x40;
    }
    for (int yy = y - 1; yy <= y + 38; ++yy) {
        if (yy >= 0 && yy < out.height && x + 38 < out.width) out.at(x + 38, yy) = 0x40;
    }
}

void copy_logo(Image& out, const Image& atlas, int sx, int x, int y) {
    if (atlas.width < sx + 38 || atlas.height < 38) throw FormatError("season entrants: logo outside atlas");
    for (int yy = 0; yy < 38; ++yy) for (int xx = 0; xx < 38; ++xx) {
        const uint8_t pixel = atlas.pixels[size_t(yy) * size_t(atlas.width) + size_t(sx + xx)];
        if (pixel && x + xx >= 0 && x + xx < out.width && y + yy >= 0 && y + yy < out.height)
            out.at(x + xx, y + yy) = pixel;
    }
}

} // namespace

std::vector<SeasonEntrantsState> season_entrants_from_game(
        const GameState& game, const LeImage& exe, const TextDb& texts) {
    std::vector<SeasonEntrantsState> out;
    const uint8_t hidden = game.memory.has(::bmh::layout::SeasonEntrants_SeasonEntrantsFromGame_TextPointer) ? game.memory.u8(::bmh::layout::SeasonEntrants_SeasonEntrantsFromGame_TextPointer) : exe.u8(::bmh::layout::SeasonEntrants_SeasonEntrantsFromGame_TextPointer);
    if (hidden != 0 || !game.memory.has(::bmh::layout::CompetitionSelection) || (game.memory.u32(::bmh::layout::CompetitionSelection) & 1) == 0)
        return out;
    if (texts.strings.size() <= kGermanTeamsText || texts.strings.size() <= 604)
        throw FormatError("season entrants: missing original TEXTD entries");

    for (int competition = 0; competition < 3; ++competition) {
        const uint32_t count_address = kCountAddress[size_t(competition)];
        const uint32_t pool_address = kPoolAddress[size_t(competition)];
        if (!game.memory.has(count_address) || !game.memory.has(pool_address))
            throw FormatError("season entrants: missing saved competition pool");
        const int count = int8_t(game.memory.u8(count_address));
        if (count < 0 || count > kMaxEntrants || !game.memory.has(pool_address + uint32_t(count * 2 - (count != 0))))
            throw FormatError("season entrants: invalid competition pool size");
        std::vector<int> domestic;
        domestic.reserve(size_t(count));
        for (int i = 0; i < count; ++i) {
            const int team = game.memory.u16(pool_address + uint32_t(i * 2));
            if (team < 0x5a) domestic.push_back(team); // CompetitionWinnerScreen only draws German clubs.
        }

        SeasonEntrantsState screen;
        screen.competition = competition;
        screen.heading = original_to_utf8(texts[kGermanTeamsText]);
        // CompetitionWinnerScreen indexes the long/short competition-name tables using
        // SeasonEntrants_FormatError_RuntimeField and SeasonEntrants_FormatError_TextPointer; the abbreviated desk labels are different texts.
        const uint32_t kind = uint32_t(competition + 2);
        const uint32_t footer_address = (exe.u8(::bmh::layout::SeasonEntrants_FormatError_TextPointer + kind) ? ::bmh::layout::CupDraw_FormatError_ExecutableField6 : ::bmh::layout::SeasonEntrants_FormatError_TextPointer2)
            + uint32_t(exe.u8(::bmh::layout::SeasonEntrants_FormatError_RuntimeField + kind)) * 4;
        screen.footer = original_to_utf8(texts[(footer_address - ::bmh::layout::TextPointerTable) / 4]);
        screen.decorated_background = !game.memory.has(::bmh::layout::PlainBackground) || game.memory.u8(::bmh::layout::PlainBackground) == 0;
        screen.gradient_text = (!game.memory.has(::bmh::layout::TextGradientDisabled) || game.memory.u8(::bmh::layout::TextGradientDisabled) == 0) && exe.u8(::bmh::layout::TextGradientMode) == 2;

        if (!domestic.empty()) {
            const size_t table_index = size_t(domestic.size());
            if (table_index >= size_t(kPlacementSlots) || !exe.contains(::bmh::layout::SeasonEntrants_FormatError_ExecutableField3 + uint32_t(table_index)))
                throw FormatError("season entrants: unsupported Source placement count");
            const int first_slot = exe.u8(::bmh::layout::SeasonEntrants_FormatError_ExecutableField3 + uint32_t(table_index));
            if (first_slot < 0 || first_slot + int(domestic.size()) > kPlacementSlots)
                throw FormatError("season entrants: Source placement exceeds its eight slots");
            for (size_t i = 0; i < domestic.size(); ++i) {
                const int slot = first_slot + int(i);
                if (slot < 0 || slot >= kPlacementSlots ||
                    !exe.contains(::bmh::layout::SeasonEntrants_FormatError_ExecutableField + uint32_t(slot)) || !exe.contains(::bmh::layout::SeasonEntrants_FormatError_ExecutableField2 + uint32_t(slot)))
                    throw FormatError("season entrants: invalid Source placement slot");
                const int team = domestic[i];
                if (team < 0 || size_t(team) >= game.club_ids.size() ||
                    size_t(team) * 2 + 1 >= game.club_heap_78.size())
                    throw FormatError("season entrants: incomplete club identity data");
                const uint16_t selector = uint16_t(game.club_heap_78[size_t(team) * 2] |
                    uint16_t(game.club_heap_78[size_t(team) * 2 + 1]) << 8);
                screen.teams.push_back({team, selector, original_to_utf8(game.club_name(team)),
                    exe.u8(::bmh::layout::SeasonEntrants_FormatError_ExecutableField + uint32_t(slot)), exe.u8(::bmh::layout::SeasonEntrants_FormatError_ExecutableField2 + uint32_t(slot))});
            }
        }
        out.push_back(std::move(screen));
    }
    return out;
}

SeasonEntrantsScreen::SeasonEntrantsScreen(const std::filesystem::path& data_directory,
                                           SeasonEntrantsState state)
    : state_(std::move(state)), data_directory_(data_directory),
      exe_(load_le(asset_path(data_directory, "BMH.exe"))),
      background_(load_vga(asset_path(data_directory / "KB", "129.VGA"))),
      sprite_font_(data_directory),
      font_(load_font(asset_path(data_directory / "F", "BMH3.FNT"))) {
    if (state_.competition < 0 || state_.competition >= 3 || background_.width != 320 || background_.height != 240)
        throw FormatError("season entrants: unsupported screen state or asset");
    for (const auto& team : state_.teams) {
        if (team.runtime_team_id < 0 || team.name.empty() || team.x < 0 || team.y < 0 ||
            team.x + 38 > 320 || team.y + 38 > 240)
            throw FormatError("season entrants: invalid projected team row");
        const int group = (team.badge_selector & 0x1ff) >> 3;
        if (!logo_atlases_.contains(group))
            logo_atlases_.emplace(group, load_vga(asset_path(data_directory_ / "W", std::to_string(group) + ".VGA")));
    }
}

Image SeasonEntrantsScreen::render() const {
    Image out = state_.decorated_background ? background_ : Image(320, 240, 0);
    if (!state_.decorated_background) {
        for (int x = 0; x < 320; ++x) {
            out.at(x, 4) = 0x44;
            out.at(x, 16) = 0x44;
            out.at(x, 228) = 0x44;
            out.at(x, 237) = 0x44;
        }
        for (int y = 4; y <= 16; ++y) { out.at(0, y) = 0x44; out.at(319, y) = 0x44; }
        for (int y = 228; y <= 237; ++y) { out.at(0, y) = 0x44; out.at(319, y) = 0x44; }
        fill(out, 1, 5, 318, 11, 0x39);
        fill(out, 1, 229, 318, 8, 0x39);
    }

    auto draw_source_name = [&](int center, int baseline, std::string_view utf8) {
        const std::string bytes = utf8_to_original(utf8);
        const int width = font_.text_width(bytes);
        const int left = center - width / 2;
        const int top = baseline - font_.baseline;
        if (left < 0 || top < 0 || left + width > out.width || top + font_.height > out.height) return;
        if (!state_.gradient_text) {
            font_.draw(out, left, top, bytes, 0x5f);
            return;
        }
        int ramp = 0;
        while (ramp < 32 && exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2Unsigned + uint32_t(ramp)) != 0x5f) ++ramp;
        if (ramp == 32) {
            font_.draw(out, left, top, bytes, 0x5f);
            return;
        }
        Image mask(std::max(1, width), font_.height);
        font_.draw(mask, 0, 0, bytes, 1);
        for (int yy = 0; yy < font_.height; ++yy)
            for (int xx = 0; xx < width; ++xx)
                if (mask.at(xx, yy)) out.at(left + xx, top + yy) = exe_.u8(::bmh::layout::Advertising_Text_RuntimeFieldUnsigned + uint32_t(ramp * 6 + std::min(yy, 5)));
    };
    // PlainWindowFrameUsedBy selects SpriteFont mode 5 and color 0 for the black
    // backdrop title/footer; team labels use the separate FNT3 path below.
    const int heading_x = 172 + (282 - 172) / 2 - sprite_font_.text_width(state_.heading) / 2;
    sprite_font_.draw(out, heading_x, 13, state_.heading, 5);
    sprite_font_.draw(out, 5, 235, state_.footer, 5);
    for (const auto& team : state_.teams) {
        const auto it = logo_atlases_.find((team.badge_selector & 0x1ff) >> 3);
        if (it == logo_atlases_.end()) throw FormatError("season entrants: missing team logo atlas");
        source_badge_frame(out, team.x, team.y);
        copy_logo(out, it->second, (team.badge_selector & 7) * 38, team.x, team.y);
        const int center = team.x + 19;
        draw_source_name(center, team.y + 46, team.name);
    }
    return out;
}

} // namespace bmh::ui
