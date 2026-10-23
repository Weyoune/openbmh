// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/world_layout.hpp"
#include "ui/club_info.hpp"

#include <algorithm>
#include <array>

#include "formats/font.hpp"
#include "game/club_info.hpp"
#include "game/state.hpp"
#include "ui/manager.hpp"

namespace bmh::ui {
namespace {
void copy(Image& out, const Image& src, int sx, int sy, int x, int y, int w, int h,
          int transparent = -1) {
    if (sx < 0 || sy < 0 || w < 0 || h < 0 || sx + w > src.width || sy + h > src.height)
        throw FormatError("club info: Original asset rectangle outside source: " + std::to_string(sx) + "," +
                          std::to_string(sy) + " " + std::to_string(w) + "x" + std::to_string(h) + " in " +
                          std::to_string(src.width) + "x" + std::to_string(src.height));
    for (int yy = 0; yy < h; ++yy) for (int xx = 0; xx < w; ++xx) {
        const uint8_t value = src.pixels[size_t(sy + yy) * size_t(src.width) + size_t(sx + xx)];
        if (value != transparent && x + xx >= 0 && y + yy >= 0 &&
            x + xx < out.width && y + yy < out.height) out.at(x + xx, y + yy) = value;
    }
}
void rect(Image& out, int x, int y, int w, int h, uint8_t color) {
    for (int yy = std::max(0, y); yy < std::min(out.height, y + h); ++yy)
        for (int xx = std::max(0, x); xx < std::min(out.width, x + w); ++xx) out.at(xx, yy) = color;
}
std::string padded(int value, int width = 2) {
    std::string s = std::to_string(value);
    if (int(s.size()) < width) s.insert(0, size_t(width - int(s.size())), '%');
    return s;
}
int sprite_width(const LeImage& exe, std::string_view value) {
    int width = 0;
    for (unsigned char c : value) {
        if (c <= 31 || c >= 128) continue;
        const int glyph = exe.u8(::bmh::layout::ClubInfo_Raw_RuntimeField3 + c);
        width += glyph ? exe.u16(::bmh::layout::ClubInfo_Raw_RuntimeField2 + glyph * 2) - exe.u16(::bmh::layout::ClubInfo_Raw_RuntimeField + glyph * 2) : 2;
    }
    return width;
}
int source_manager_of(const GameState& game, int team) {
    const int manager = game.manager_of(team);
    if (manager >= 0) return manager;
    constexpr size_t fifth_team_offset = size_t(4) * 0xb0c + 0x290;
    if (!game.manager_teams.empty() && game.manager_teams.front() == no_team() &&
        game.manager_records.size() >= fifth_team_offset + 2) {
        const uint16_t fifth_team = uint16_t(game.manager_records[fifth_team_offset] |
            uint16_t(game.manager_records[fifth_team_offset + 1]) << 8);
        if (fifth_team == team) return 4;
    }
    return -1;
}
}

ClubInfoProjection club_info_from_game(const GameState& g, int team,
                                       const ClubInfoEntry& entry, const LeImage& exe,
                                       const TextDb&) {
    if (team < 0 || team > no_team()) throw FormatError("club info: runtime team outside Original range");
    ClubInfoProjection out;
    out.runtime_team = team;
    out.original_club_name = g.club_name(team);
    out.gradient_text = g.memory.u8(::bmh::layout::TextGradientDisabled) == 0 && exe.u8(::bmh::layout::TextGradientMode) == 2;
    out.decorated_background = g.memory.u8(::bmh::layout::PlainBackground) == 0;
    out.has_squad = team == no_team() || team < world_layout().league_teams;
    if (team != no_team() && team >= world_layout().league_teams) {
        out.has_squad = false;
        out.category = team >= 0 && size_t(team) < g.team_morale.size()
            ? uint8_t((g.team_morale[size_t(team)] >> 6) & 3) : 0;
        return out;
    }
    const SquadRange range = squad_range(team);
    out.squad_start_offset = int(range.first);
    if (entry.displayed_slots.size() != range.count || range.first + range.count > g.squad_slots.size())
        throw FormatError("club info: entry slot snapshot does not match Source squad range");
    out.category = team >= 0 && size_t(team) < g.team_morale.size()
        ? uint8_t((g.team_morale[size_t(team)] >> 6) & 3) : 0;
    const int manager = source_manager_of(g, team);
    std::vector<int> loan_records;
    if (manager >= 0 && team != no_team() &&
        g.memory.has(::bmh::layout::Career_CareerManagersEnd_RuntimeField + 0x17) && g.memory.has(::bmh::layout::ClubInfo_FormatError_RuntimeField2 + 0x0b) && g.memory.has(::bmh::layout::ClubInfo_FormatError_RuntimeField + 0x41f)) {
        for (int loan = 0; loan < 12; ++loan)
            if (g.memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField + uint32_t(loan) * 2) != 0x0fff &&
                g.memory.u8(::bmh::layout::ClubInfo_FormatError_RuntimeField2 + uint32_t(loan)) == uint8_t(manager)) loan_records.push_back(loan);
    }
    if (loan_records.size() > range.count) throw FormatError("club info: Source loan count exceeds squad capacity");
    const size_t ordinary_count = range.count - loan_records.size();
    int rendered_row = 0;
    auto make_player = [&](int display_row, int physical_row, int loan_record, uint16_t slot,
                           std::optional<std::array<uint8_t, 0x58>> board) {
        ClubInfoPlayer player;
        player.row = display_row;
        player.physical_row = physical_row;
        player.human_loan_index = loan_record;
        player.borrowed = loan_record >= 0;
        player.displayed_slot = slot;
        player.player_id = (player.displayed_slot & 0x0fff) == 0x0fff
            ? -1 : int(player.displayed_slot & 0x0fff);
        if (player.borrowed) player.player_id = g.memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField + uint32_t(loan_record) * 2);
        if (player.player_id >= 0) {
            player.rendered_row = rendered_row++;
            const size_t id = size_t(player.player_id);
            if (id >= g.players.size() || id >= g.player_cards.size() || id >= g.player_bonus.size())
                throw FormatError("club info: player outside authoritative tables");
            player.attributes = g.players[id];
            player.original_name = g.player_name(player.player_id);
            player.cards = g.player_cards[id];
            player.bonus = g.player_bonus[id];
        }
        if (board) player.manager_board = std::move(board);
        if (!player.borrowed && player.player_id >= 0) {
            for (int loan = 0; loan < 200; ++loan) {
                const uint32_t address = ::bmh::layout::Career_RemoveJoinLoans_RuntimeField + uint32_t(loan) * 4;
                if (g.memory.has(address + 3) && g.memory.u32(address) != 0 &&
                    (g.memory.u32(address) & 0x0fff) == uint32_t(player.player_id)) {
                    player.ordinary_loan = true; break;
                }
            }
        }
        if (player.player_id >= 0 && manager >= 0 && team != no_team()) {
            for (int offer = 0; offer < 4; ++offer) {
                const uint32_t address = ::bmh::layout::OfferSlots + uint32_t(offer) * 4;
                if (g.memory.has(address + 3)) {
                    const uint32_t value = g.memory.u32(address);
                    if (int(value & 31) == manager && int((value >> 10) & 0x0fff) == player.player_id)
                        player.offer_name_color = true;
                }
            }
        }
        out.players.push_back(std::move(player));
    };
    for (size_t row = 0; row < ordinary_count; ++row) {
        std::optional<std::array<uint8_t, 0x58>> board;
        if (manager >= 0) {
            const size_t offset = size_t(manager) * 0x840 + row * 0x58;
            if (offset + 0x58 <= g.tactics.size()) {
                board.emplace(); std::copy_n(g.tactics.begin() + static_cast<std::ptrdiff_t>(offset), board->size(), board->begin());
            }
        }
        make_player(int(row), int(row), -1, entry.displayed_slots[row], std::move(board));
    }
    for (size_t index = 0; index < loan_records.size(); ++index) {
        const int loan = loan_records[index];
        const size_t offset = size_t(loan) * 0x58;
        if (offset + 0x58 > 0x420 || !g.memory.has(::bmh::layout::ClubInfo_FormatError_RuntimeField + uint32_t(offset) + 0x57))
            throw FormatError("club info: borrowed-manager board truncated");
        std::array<uint8_t, 0x58> board{};
        for (size_t byte = 0; byte < board.size(); ++byte) board[byte] = g.memory.u8(::bmh::layout::ClubInfo_FormatError_RuntimeField + uint32_t(offset + byte));
        make_player(int(ordinary_count + index), -1, loan,
                    g.memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField + uint32_t(loan) * 2), std::move(board));
    }
    return out;
}

ClubInfoScreen::ClubInfoScreen(const std::filesystem::path& data, ClubInfoProjection projection)
    : projection_(std::move(projection)), exe_(load_le(asset_path(data, "BMH.exe"))),
      font_(load_font(asset_path(data / "F", "BMH3.FNT"))), font2_(load_font(asset_path(data / "F", "BMH2.FNT"))),
      texts_(load_textd(asset_path(data, "TEXTD.BLK"))),
      background_(load_vga(asset_path(data / "KB", "129.VGA"))),
      widgets_(load_vga(asset_path(data / "KB", "0.VGA"))),
      atlas_(load_vga(asset_path(data / "KB", "2.VGA"))),
      flags_(load_vga(asset_path(data / "KB", "24.VGA"))),
      letters_(load_vga(asset_path(data / "KB", "54.VGA"))) {
    if (atlas_.width == 314) { Image padded_atlas(320, atlas_.height, 0); padded_atlas.blit(atlas_, 0, 0); atlas_ = std::move(padded_atlas); }
    if (projection_.runtime_team < 0 ||
        (projection_.has_squad && projection_.players.size() != 16u && projection_.players.size() != 24u) ||
        (!projection_.has_squad && !projection_.players.empty()))
        throw FormatError("club info: invalid projection");
    if (projection_.category > 2) throw FormatError("club info: unsupported category value");
    int expected_rendered_row = 0;
    for (size_t i = 0; i < projection_.players.size(); ++i) {
        const auto& p = projection_.players[i];
        if (p.row != int(i) || p.player_id != ((p.displayed_slot & 0xfff) == 0xfff ? -1 : p.displayed_slot & 0xfff))
            throw FormatError("club info: inconsistent projected row");
        if ((p.player_id >= 0 && p.rendered_row != expected_rendered_row++) ||
            (p.player_id < 0 && p.rendered_row != -1) ||
            (p.borrowed != (p.human_loan_index >= 0)) ||
            (p.borrowed && p.physical_row != -1) ||
            (!p.borrowed && p.physical_row != p.row))
            throw FormatError("club info: inconsistent displayed/player identity");
    }
}

void ClubInfoScreen::text(Image& out, int x, int baseline, std::string_view value,
                          uint8_t color, int right) const {
    const int width = font_.text_width(value);
    if (right >= 0) x += (right - x) / 2 - width / 2;
    if (x < 0 || x + width > out.width || baseline < 5 || baseline >= out.height) return;
    Image mask(std::max(1, width), 6); font_.draw(mask, 0, 0, value, 1);
    int gradient = 0; while (gradient < 32 && exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2 + gradient) != color) ++gradient;
    for (int yy = 0; yy < 6; ++yy) for (int xx = 0; xx < width; ++xx)
        if (mask.at(xx, yy)) out.at(x + xx, baseline - 5 + yy) = projection_.gradient_text
            ? exe_.u8(::bmh::layout::Advertising_Text_RuntimeField + gradient * 6 + yy) : color;
}

void ClubInfoScreen::sprite_text(Image& out, int x, int baseline, std::string_view value, int mode) const {
    for (unsigned char c : value) {
        if (c <= 31 || c >= 128) continue;
        const int glyph = exe_.u8(::bmh::layout::ClubInfo_Raw_RuntimeField3 + c);
        if (!glyph) { x += 2; continue; }
        const int sx = exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField + glyph * 2);
        const int width = exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField2 + glyph * 2) - sx;
        copy(out, letters_, sx, (mode - 4) * 7, x, baseline - 6, width, 7, 0);
        x += width;
    }
}

std::vector<std::pair<int, int>> ClubInfoScreen::rows() const {
    std::vector<std::pair<int, int>> out;
    int baseline = 36;
    int loan_row = 0;
    for (size_t i = 0; i < projection_.players.size(); ++i) {
        const auto& player = projection_.players[i];
        if (player.borrowed) {
            if (player.player_id >= 0) out.emplace_back(int(i), 209 - 8 * loan_row);
            ++loan_row;
            continue;
        }
        if (i && ((player.displayed_slot & 0xc000) != (projection_.players[i - 1].displayed_slot & 0xc000))) baseline += 2;
        if (player.player_id < 0) continue;
        out.emplace_back(int(i), baseline); baseline += 7;
    }
    return out;
}

Image ClubInfoScreen::render() const {
    Image out = projection_.decorated_background ? background_ : Image(320, 240);
    if (!projection_.decorated_background) {
        rect(out, 0, 4, 320, 13, 0x44); rect(out, 1, 5, 318, 11, 0x39);
        rect(out, 0, 228, 320, 10, 0x44); rect(out, 1, 229, 318, 8, 0x39);
    }
    sprite_text(out, 172 + (282 - 172) / 2 - sprite_width(exe_, texts_[(::bmh::layout::ClubInfo_Render_TextPointer6 - ::bmh::layout::TextPointerTable) / 4]) / 2,
                13, texts_[(::bmh::layout::ClubInfo_Render_TextPointer6 - ::bmh::layout::TextPointerTable) / 4]);
    sprite_text(out, 5, 235, projection_.original_club_name);
    if (projection_.has_squad)
        text(out, 0, 26, texts_[((projection_.view == ClubInfoView::data_a ? ::bmh::layout::ClubInfo_Render_TextPointer3 : ::bmh::layout::ClubInfo_Render_TextPointer4) - ::bmh::layout::TextPointerTable) / 4], 0x51);
    if (!projection_.has_squad) {
        const auto& message = texts_[(::bmh::layout::ClubInfo_Render_TextPointer7 - ::bmh::layout::TextPointerTable) / 4];
        const int width = font2_.text_width(message);
        font2_.draw(out, (320 - width) / 2, 120 - font2_.baseline, message, 0xfb);
    } else {
        for (const auto& [row, baseline] : rows()) {
            const auto& p = projection_.players[size_t(row)]; const auto& a = p.attributes;
            if (p.displayed_slot & 0xc000) {
                const int jersey = (p.cards & 31) + 1;
                const uint8_t color = p.displayed_slot & 0x1000 ? 0xf3 : p.borrowed ? 0x75 : p.displayed_slot & 0x8000 ? 0x6c : 0x68;
                text(out, 4, baseline, std::to_string(jersey % 10), color);
                if (jersey >= 10) text(out, 0, baseline, exe_.cstring(::bmh::layout::ClubInfo_Render_TextPointer), color);
            }
            uint8_t name_color = uint8_t(p.rendered_row % 2 ? 0x67 : 0x6b);
            if (p.borrowed) name_color = 0x65;
            if (p.offer_name_color) name_color = 0x75;
            if (hovered_row_ == p.rendered_row) name_color = 0x3e;
            text(out, 13, baseline, p.original_name, name_color);
            if (projection_.view == ClubInfoView::data_a) {
                const size_t pos = size_t(a.position());
                text(out, 66, baseline, texts_[(::bmh::layout::ClubInfo_Render_TextPointer2 - ::bmh::layout::TextPointerTable) / 4 + pos], 0x6c, 82);
                for (int field = 0; field < 2; ++field) {
                    int value = (a.w35c >> (field == 0 ? 24 : 21)) & 7; if (value > 4) --value;
                    copy(out, atlas_, 208 + value * 16, field == 0 ? 17 : 22, field == 0 ? 88 : 103, baseline - 5, 16, 5, 0);
                }
                std::string attrs = padded(a.condition()) + exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField) + padded(a.technique()) + exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField) +
                    padded(a.form()) + exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField2) + padded(a.strength()) + exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField3) + exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField4);
                text(out, 117, baseline, attrs, 0x6c);
                if (p.manager_board) {
                    const auto& board = *p.manager_board;
                    copy(out, atlas_, 304, int(board[0xe] >> 6) * 3, 179, baseline - 4, 16, 3, 0);
                    text(out, 232, baseline, padded(board[0xf]), board[0xf] < 61 ? 0x66 : board[0xf] < 71 ? 0x27 : 0xfb);
                }
                const int injury = (a.w364 >> 16) & 31;
                const int marker = a.w35c >> 27;
                if (p.borrowed) {
                    copy(out, atlas_, 197, 25, 196, baseline - 5, 10, 5);
                } else if (marker == 0 && injury == 0) {
                    const int status = p.displayed_slot & 0x8000 ? 0 : p.displayed_slot & 0x4000 ? 1 : 2;
                    copy(out, atlas_, 272, status * 5, 191, baseline - 5, 32, 5, 0);
                } else {
                    const int status = marker == 0 ? 5 : marker == 31 ? 8 : injury == 0 ? 4 : 3;
                    copy(out, atlas_, 197, (status - 3) * 6 + 1, 196, baseline - 5, 10, 5);
                }
                if (p.ordinary_loan) copy(out, atlas_, 304, 9, 9, baseline - 4, 16, 4, 0);
                const int nationality = a.nationality() + ((p.bonus & 0x80) >> 2);
                copy(out, flags_, (nationality % 12) * 9, (nationality / 12) * 6, 217, baseline - 6, 9, 6);
                text(out, 247, baseline, padded(a.w364 >> 26) + exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField4) +
                     padded((a.w364 >> 2) & 63) + exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField4) +
                     padded((a.w368 >> 18) & 63) + exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField4) +
                     padded((a.w368 >> 14) & 15) + exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField4) +
                     padded((a.w368 >> 24) & 7) + exe_.cstring(::bmh::layout::ClubInfo_Raw_ExecutableField), 0x6c);
            } else {
                text(out, 65, baseline, padded(a.age()) + texts_[(::bmh::layout::ClubInfo_Render_TextPointer5 - ::bmh::layout::TextPointerTable) / 4], 0x6c);
                for (int skill = 0; skill < 7; ++skill)
                    copy(out, atlas_, 256 + a.skill(Player::Skill(skill)) * 16, 32, 87 + skill * 10, baseline - 5, 16, 5, 0);
                std::string attrs = padded(p.bonus & 127, 3) + exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField5);
                if (p.manager_board) {
                    const auto& b = *p.manager_board;
                    attrs += padded(b[7], 3) + exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField5) + padded(b[9], 3) + exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField5);
                    Bytes raw(b.begin(), b.end()); attrs += padded(le16(raw, 0x48), 3) + exe_.cstring(::bmh::layout::ClubInfo_Raw_ExecutableField2) +
                        padded(le16(raw, 0x4a), 3) + exe_.cstring(::bmh::layout::ClubInfo_Raw_ExecutableField2) + padded(le16(raw, 0x4c), 3) + exe_.cstring(::bmh::layout::ClubInfo_Raw_ExecutableField);
                }
                text(out, 165, baseline, attrs, 0x6c);
            }
        }
    }
    auto button = [&](int x, std::string_view label) {
        // FormatError3's requested 9px height is promoted to 10px; use the same
        // Original widget slices as the existing menu renderer.
        const int width = 51, height = 10;
        const bool active = (x == 1 && pressed_button_ == (projection_.has_squad ? ClubInfoButton::view : ClubInfoButton::category)) ||
                            (x == 60 && pressed_button_ == ClubInfoButton::statistics) ||
                            (x == 120 && pressed_button_ == ClubInfoButton::category);
        if (active) { const uint8_t base = out.at(x, 225); rect(out, x, 215, width + 1, 2, base); rect(out, x, 215, 2, height + 1, base); x += 2; }
        else { rect(out, x + 2, 225, width, 2, 0x40); rect(out, x + width, 217, 2, height, 0x40); }
        const int sy = active ? 44 : 74;
        copy(out, widgets_, 24, sy, x, 215 + (active ? 2 : 0), 31, 10);
        int at = 31, remain = width - 31;
        while (remain > 20) { const int n = std::min(8, remain - 20); copy(out, widgets_, 24, sy + 10, x + at, 215 + (active ? 2 : 0), n, 10); at += n; remain -= n; }
        copy(out, widgets_, 45 - remain, sy + 20, x + at, 215 + (active ? 2 : 0), remain, 10);
        if (!label.empty()) {
            const int mode = exe_.u8(::bmh::layout::Advertising_Button_RuntimeField + int(active));
            const int label_baseline = active ? 224 : 222;
            if (mode == 3) text(out, x, label_baseline, label, 0, x + width);
            else {
                int label_width = 0;
                for (unsigned char c : label) {
                    const int glyph = exe_.u8(::bmh::layout::ClubInfo_Raw_RuntimeField3 + c);
                    label_width += glyph ? exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField2 + glyph * 2) - exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField + glyph * 2) : 2;
                }
                sprite_text(out, x + width / 2 - label_width / 2, label_baseline, label, mode);
            }
        }
    };
    button(1, projection_.has_squad ? texts_[((projection_.view == ClubInfoView::data_a ? ::bmh::layout::ClubInfo_Raw_TextPointer2 : ::bmh::layout::ClubInfo_Raw_TextPointer) - ::bmh::layout::TextPointerTable) / 4] : std::string_view{});
    if (projection_.has_squad) button(60, texts_[(::bmh::layout::ClubInfo_Raw_TextPointer3 - ::bmh::layout::TextPointerTable) / 4]);
    if (projection_.has_squad) button(120, {});
    const int icon = std::clamp(int(projection_.category), 0, 2);
    const bool category_pressed = pressed_button_ == ClubInfoButton::category;
    const int category_x = projection_.has_squad ? 140 : 21;
    copy(out, widgets_, 45 + 10 * icon, 96, category_x + (category_pressed ? 2 : 0), 216 + (category_pressed ? 2 : 0), 10, 8);
    return out;
}

bool ClubInfoScreen::move(int x, int y) {
    bool changed = false;
    if (pressed_button_ && button_at(x, y) != pressed_button_) { pressed_button_.reset(); changed = true; }
    std::optional<int> next;
    if (projection_.has_squad && x >= 0 && x < 61) for (const auto& [row, baseline] : rows())
        if (y >= baseline - 7 && y < baseline + 2) { next = row; break; }
    std::optional<int> next_rendered;
    if (next) next_rendered = projection_.players[size_t(*next)].rendered_row;
    if (next_rendered != hovered_row_) { hovered_row_ = next_rendered; changed = true; }
    return changed;
}

ClubInfoRequest ClubInfoScreen::pointer_down(int x, int y, bool right) const {
    ClubInfoRequest out; out.runtime_team = projection_.runtime_team;
    out.squad_start_offset = projection_.squad_start_offset;
    if (right) { out.kind = ClubInfoRequestKind::close; return out; }
    if (!projection_.has_squad || x < 0 || x >= 61) return out;
    for (const auto& [row, baseline] : rows()) if (y >= baseline - 7 && y < baseline + 2) {
        const auto& player = projection_.players[size_t(row)];
        out.kind = ClubInfoRequestKind::open_player_detail; out.rendered_row = player.rendered_row;
        out.player_id = player.player_id; out.physical_row = player.physical_row;
        out.human_loan_index = player.human_loan_index; return out;
    }
    return out;
}

std::optional<ClubInfoButton> ClubInfoScreen::button_at(int x, int y) const {
    if (y < 215 || y > 225) return std::nullopt;
    if (!projection_.has_squad && x >= 1 && x <= 52) return ClubInfoButton::category;
    if (projection_.has_squad && x >= 1 && x <= 52) return ClubInfoButton::view;
    if (projection_.has_squad && x >= 60 && x <= 111) return ClubInfoButton::statistics;
    if (projection_.has_squad && x >= 120 && x <= 171) return ClubInfoButton::category;
    return std::nullopt;
}

void ClubInfoScreen::press(int x, int y) { pressed_button_ = button_at(x, y); }

ClubInfoRequest ClubInfoScreen::release(int x, int y) {
    ClubInfoRequest out; out.runtime_team = projection_.runtime_team;
    out.squad_start_offset = projection_.squad_start_offset;
    const auto pressed = pressed_button_;
    const auto released = button_at(x, y);
    pressed_button_.reset();
    if (!pressed || pressed != released) return out;
    if (*pressed == ClubInfoButton::view) out.kind = ClubInfoRequestKind::toggle_view;
    else if (*pressed == ClubInfoButton::statistics) out.kind = ClubInfoRequestKind::open_statistics;
    else { out.kind = ClubInfoRequestKind::change_category; out.requested_category = uint8_t((projection_.category + 1) % 3); }
    return out;
}

} // namespace bmh::ui
