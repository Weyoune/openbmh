// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/indoor_standings.hpp"

#include <algorithm>
#include <sstream>

#include "game/lineup.hpp"
#include "game/messages.hpp"
#include "game/state.hpp"
#include "ui/manager.hpp"

namespace bmh::ui {
namespace {
uint8_t source_byte(const GameState& g, const LeImage& exe, uint32_t at) {
    return g.memory.has(at) ? g.memory.u8(at) : exe.u8(at);
}
uint8_t name_color(const GameState& g, const LeImage& exe, int team) {
    const int active = source_byte(g, exe, ::bmh::layout::ActiveManager);
    const int current = source_byte(g, exe, ::bmh::layout::DeskPosition);
    for (size_t slot = 0; slot < g.manager_teams.size() && slot < 4; ++slot) {
        const size_t at = slot * 0xb0c + 0xa8;
        if (at < g.manager_records.size() && g.manager_records[at] && g.manager_teams[slot] == team)
            return int(slot) == active && current < int(g.manager_teams.size()) ? 0x7a : 0x75;
    }
    const uint16_t highlighted = g.memory.has(::bmh::layout::AlltimeTable_TeamColor_RuntimeField + 1) ? g.memory.u16(::bmh::layout::AlltimeTable_TeamColor_RuntimeField) : exe.u16(::bmh::layout::AlltimeTable_TeamColor_RuntimeField);
    return highlighted == team ? 0xf7 : 0x6a;
}
}

IndoorStandingsState indoor_standings_from_game(const GameState& g, const LeImage& exe,
                                               const TextDb& texts, const DeskRequest& owner) {
    if (owner.kind != DeskRequestKind::open_menu || owner.menu_kind != 9 || owner.manager_slot < 0 ||
        owner.manager_slot > 3 || owner.manager_slot != int(g.memory.u8(::bmh::layout::ActiveManager)))
        throw FormatError("indoor standings: invalid ordinary desk owner");
    IndoorStandingsState out;
    out.owner = owner;
    out.decorated_background = source_byte(g, exe, ::bmh::layout::PlainBackground) == 0;
    out.gradient_text = source_byte(g, exe, ::bmh::layout::TextGradientDisabled) == 0 && exe.u8(::bmh::layout::TextGradientMode) == 2;
    out.indoor = g.indoor;
    out.tournaments = g.memory.u8(::bmh::layout::Cup_AdvanceCup_RuntimeField4);
    out.strength = g.memory.u8(::bmh::layout::Cup_AdvanceCup_RuntimeField3);
    // The 40-club quicksort is identical to the Core Masters qualification,
    // including its equal-key ordering. Presentation must not run that mutator.
    int16_t order[40];
    uint32_t points[40];
    for (int team = 0; team < 40; ++team) {
        order[team] = int16_t(team);
        points[team] = g.memory.u16(::bmh::layout::Cup_AdvanceCup_ExecutableField + uint32_t(team * 2));
    }
    quicksort_by_key(order, points, 40);
    // MastersQualification2 adds last season's first title holder unconditionally, then
    // deduplicates the other two. Work on owned values rather than Cup_MastersQualification_RuntimeField/CupRuntimeField.
    std::vector<uint16_t> direct;
    for (int i = 0; i < 3; ++i) {
        const auto team = g.memory.u16(::bmh::layout::Cup_AdvanceCup_RuntimeField6 + uint32_t(i * 2));
        if (i == 0 || std::find(direct.begin(), direct.end(), team) == direct.end()) direct.push_back(team);
    }
    int qualifiers = int(direct.size());
    if (out.tournaments) {
        out.footer = texts[text_id(::bmh::layout::IndoorStandings_FormatError_TextPointer2)] + format_number(g, out.tournaments, 1) + texts[text_id(::bmh::layout::IndoorStandings_FormatError_TextPointer3)];
        for (int rank = 0; rank < 16; ++rank) {
            const int team = order[rank];
            const uint8_t color = qualifiers > 7 ? 0xf7 : 0x51;
            if (std::find(direct.begin(), direct.end(), team) == direct.end()) ++qualifiers;
            out.rows.push_back({team, g.memory.u16(::bmh::layout::Cup_AdvanceCup_ExecutableField + uint32_t(team * 2)),
                                g.club_name(team), name_color(g, exe, team), color});
        }
    }
    if (out.indoor) {
        out.strength_text = texts[text_id(::bmh::layout::IndoorStandings_FormatError_TextPointer4)] + format_number(g, out.strength, 3) + texts[text_id(::bmh::layout::DeskRecords_FillRect_ExecutableField5)];
        for (int i = 0; i < 4; ++i)
            out.prize_text[size_t(i)] = format_number(g, i + 1, 0x81) + texts[text_id(::bmh::layout::IndoorStandings_FormatError_TextPointer5)] +
                format_number(g, exe.u8(::bmh::layout::IndoorStandings_FormatError_TextPointer + uint32_t(i)) * out.strength, 3) + texts[text_id(::bmh::layout::DeskRecords_FillRect_ExecutableField5)];
    }
    return out;
}

IndoorStandingsScreen::IndoorStandingsScreen(const std::filesystem::path& data, IndoorStandingsState state)
    : state_(std::move(state)), exe_(load_le(asset_path(data, "BMH.exe"))),
      texts_(load_textd(asset_path(data, "TEXTD.BLK"))),
      background_(load_vga(asset_path(data / "KB", "129.VGA"))),
      font_(load_font(asset_path(data / "F", "BMH3.FNT"))), sprite_(data) {
    if (state_.rows.size() != (state_.tournaments ? 16 : 0))
        throw FormatError("indoor standings: incomplete Source ranking");
}

void IndoorStandingsScreen::text(Image& out, int x, int baseline, std::string_view bytes, uint8_t color) const {
    const int width = font_.text_width(bytes), top = baseline - font_.baseline;
    if (x < 0 || x + width > out.width || top < 0 || top + font_.height > out.height) return;
    Image mask(std::max(1, width), font_.height);
    font_.draw(mask, 0, 0, bytes, 1);
    int index = 0;
    while (index < 32 && exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2 + uint32_t(index)) != color) ++index;
    for (int y = 0; y < font_.height; ++y) for (int xx = 0; xx < width; ++xx)
        if (mask.at(xx, y)) out.at(x + xx, top + y) = state_.gradient_text && index < 32 ?
            exe_.u8(::bmh::layout::Advertising_Text_RuntimeField + uint32_t(index * 6 + y)) : color;
}

Image IndoorStandingsScreen::render() const {
    Image out = state_.decorated_background ? background_ : Image(320, 240);
    if (!state_.decorated_background) {
        for (int y = 4; y <= 16; ++y) for (int x = 0; x < 320; ++x) out.at(x,y) = 0x44;
        for (int y = 5; y <= 15; ++y) for (int x = 1; x < 319; ++x) out.at(x,y) = 0x39;
        for (int y = 228; y <= 237; ++y) for (int x = 0; x < 320; ++x) out.at(x,y) = 0x44;
        for (int y = 229; y <= 236; ++y) for (int x = 1; x < 319; ++x) out.at(x,y) = 0x39;
    }
    const auto title = original_to_utf8(texts_[text_id(::bmh::layout::IndoorStandings_Render_TextPointer2)]);
    sprite_.draw(out, SpriteFont::centered_title_x(sprite_.text_width(title)), 13, title);
    sprite_.draw(out, 5, 235, original_to_utf8(state_.footer));
    for (size_t i = 0; i < state_.rows.size(); ++i) {
        const auto& row = state_.rows[i];
        const int y = 32 + int(i) * 8;
        text(out, 60, y, format_number(GameState{}, int(i) + 1, 0x82) + exe_.cstring(::bmh::layout::IndoorStandings_Render_TextPointer), row.rank_color);
        text(out, 79, y, row.name, row.name_color);
        text(out, 189, y, format_number(GameState{}, row.points, 3), 0x6a);
    }
    if (state_.indoor) {
        text(out, 10, 170, state_.strength_text, 0x5f);
        for (int i = 0; i < 4; ++i) text(out, 30, 180 + i * 7, state_.prize_text[size_t(i)], 0x5f);
    }
    return out;
}

std::string IndoorStandingsScreen::state_json() const {
    std::ostringstream out;
    out << "{\"screen\":\"indoor_standings\",\"tournaments\":" << int(state_.tournaments)
        << ",\"indoor\":" << (state_.indoor ? "true" : "false") << ",\"rows\":[";
    for (size_t i = 0; i < state_.rows.size(); ++i) {
        if (i) out << ',';
        out << "{\"team\":" << state_.rows[i].team << ",\"points\":" << state_.rows[i].points << '}';
    }
    out << "]}";
    return out.str();
}
} // namespace bmh::ui
