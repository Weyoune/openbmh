// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/tactics.hpp"

#include <algorithm>
#include <sstream>

#include "formats/savestate.hpp"
#include "game/state.hpp"
#include "ui/manager.hpp"

namespace bmh::ui {
namespace {
Bytes block_copy(const SaveState& s, uint32_t a) {
    const auto& b = s.block(a);
    return Bytes(s.stream().begin() + b.offset, s.stream().begin() + b.offset + b.size);
}
std::string bounded_string(const Bytes& b, size_t at, size_t limit) {
    if (at >= b.size()) throw FormatError("tactics: string outside record/pool");
    auto end = b.begin() + std::min(b.size(), at + limit);
    auto nul = std::find(b.begin() + at, end, 0);
    if (nul == end) throw FormatError("tactics: unterminated record/pool string");
    return std::string(b.begin() + at, nul);
}
TacticsState project(const StaticMemory& memory, const Bytes& managers, const Bytes& board,
        const Bytes& options, const std::vector<int>& teams, const Bytes& names,
        const std::vector<uint16_t>& offsets, const LeImage& exe, uint64_t revision) {
    const int count = memory.u8(::bmh::layout::ManagerCount), slot = memory.u8(::bmh::layout::ActiveManager);
    if (count < 1 || count > 4 || slot < 0 || slot >= count || teams.size() != size_t(count))
        throw FormatError("tactics: invalid/unsupported active manager");
    const size_t manager = size_t(slot) * 0xb0c, first = size_t(slot) * 0x840;
    if (managers.size() < manager + 0xb0c || board.size() < first + 0x840 ||
            options.size() < size_t(slot + 1) * 5)
        throw FormatError("tactics: truncated manager/board/options");
    TacticsState s;
    s.manager_slot = slot; s.runtime_club = teams.at(slot); s.revision = revision;
    const bool national = s.runtime_club == kNoTeam && slot == 0 && count == 1 &&
                          ((memory.u32(::bmh::layout::CompetitionSelection) & 2) != 0 ||
                           (memory.has(::bmh::layout::CoachActive) && memory.u8(::bmh::layout::CoachActive) != 0));
    if (s.runtime_club < 0 || s.runtime_club >= int(offsets.size()) ||
        (s.runtime_club == kNoTeam && !national))
        throw FormatError("tactics: unsupported runtime club");
    s.manager_name = bounded_string(managers, manager + 0xa8, 26);
    s.club_name = bounded_string(names, offsets.at(s.runtime_club), names.size());
    s.decorated_background = (memory.has(::bmh::layout::PlainBackground) ? memory.u8(::bmh::layout::PlainBackground) : exe.u8(::bmh::layout::PlainBackground)) == 0;
    s.live_scene_option = (memory.has(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField3) ? memory.u8(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField3) : exe.u8(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField3)) != 0;
    s.assistant_attitude = options.at(size_t(slot) * 5 + 2);
    for (int i = 0; i < 8; ++i) {
        s.values[i] = managers.at(manager + 0xd2 + i);
        if (s.values[i] > exe.u8(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField + i)) throw FormatError("tactics: value outside original range");
    }
    for (int row = 0; row < 11; ++row) {
        s.depth[row] = int8_t(board.at(first + size_t(row) * 0x58 + 0x41));
        s.lane[row] = int8_t(board.at(first + size_t(row) * 0x58 + 0x42));
    }
    return s;
}
void rect(Image& out, int x, int y, int w, int h, uint8_t color) {
    for (int yy = std::max(0, y); yy < std::min(out.height, y + h); ++yy)
        for (int xx = std::max(0, x); xx < std::min(out.width, x + w); ++xx) out.at(xx, yy) = color;
}
void copy(Image& out, const Image& src, int sx, int sy, int x, int y, int w, int h, bool transparent = false) {
    if (sx < 0 || sy < 0 || sx + w > src.width || sy + h > src.height)
        throw FormatError("tactics: sprite outside original atlas");
    for (int yy = 0; yy < h; ++yy) for (int xx = 0; xx < w; ++xx) {
        const auto c = src.pixels.at(size_t(sy + yy) * src.width + sx + xx);
        if (x + xx >= 0 && y + yy >= 0 && x + xx < out.width && y + yy < out.height && (!transparent || c))
            out.at(x + xx, y + yy) = c;
    }
}
} // namespace

TacticsState tactics_from_save(const SaveState& save, const LeImage& exe, uint64_t revision) {
    StaticMemory memory(save);
    auto managers = block_copy(save, ::bmh::layout::ManagerRecords), board = block_copy(save, ::bmh::layout::TacticsBoards);
    auto options = block_copy(save, ::bmh::layout::ManagerAutomation), raw_offsets = block_copy(save, ::bmh::layout::ClubIdentities);
    std::vector<uint16_t> offsets;
    if (raw_offsets.size() % 2) throw FormatError("tactics: invalid club-name offset table");
    for (size_t i = 0; i < raw_offsets.size(); i += 2) offsets.push_back(le16(raw_offsets, i));
    std::vector<int> teams;
    const int count = memory.u8(::bmh::layout::ManagerCount);
    if (count < 1 || count > 4 || managers.size() < size_t(count) * 0xb0c)
        throw FormatError("tactics: unsupported/truncated managers");
    for (int i = 0; i < count; ++i) teams.push_back(le16(managers, size_t(i) * 0xb0c + 0x290));
    auto s = project(memory, managers, board, options, teams, save.pool(0), offsets, exe, revision);
    s.source = "original_save_read_only"; return s;
}
TacticsState tactics_from_game(const GameState& game, const LeImage& exe, uint64_t revision) {
    auto s = project(game.memory, game.manager_records, game.tactics, game.manager_options,
        game.manager_teams, game.club_names, game.club_ids, exe, revision);
    s.source = "engine_projection"; return s;
}

std::string tactics_request_json(const TacticsRequest& r) {
    std::ostringstream out;
    out << "{\"kind\":" << json_quote(r.kind == TacticsRequestKind::set_tactic ? "set_tactic" : "none")
        << ",\"manager_slot\":" << r.manager_slot << ",\"runtime_club\":" << r.runtime_club
        << ",\"revision\":" << r.revision << ",\"index\":" << r.index
        << ",\"desired_value\":" << r.desired_value << ",\"desired_assistant_attitude\":";
    if (r.desired_assistant_attitude) out << int(*r.desired_assistant_attitude); else out << "null";
    out << '}'; return out.str();
}

TacticsScreen::TacticsScreen(const std::filesystem::path& data, TacticsState state)
    : state_(std::move(state)), exe_(load_le(asset_path(data, "BMH.exe"))),
      texts_(load_textd(asset_path(data, "TEXTD.BLK"))),
      background_(load_vga(asset_path(data / "KB", "129.VGA"))),
      board_(load_vga(asset_path(data / "KB", "11.VGA"))),
      sprites_(load_vga(asset_path(data / "KB", "53.VGA"))),
      letters_(load_vga(asset_path(data / "KB", "54.VGA"))) {
    validate(state_);
    if (background_.width != 320 || background_.height != 240 || board_.width < 295 || board_.height < 206)
        throw FormatError("tactics: unsupported original assets");
}
void TacticsScreen::validate(const TacticsState& s) const {
    if (s.manager_slot < 0 || s.manager_slot > 3 || s.runtime_club < 0 ||
            s.club_name.empty()) throw FormatError("tactics: invalid manager identity");
    for (int i = 0; i < 8; ++i)
        if (s.values[i] > exe_.u8(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField + i)) throw FormatError("tactics: invalid setting range");
}
bool TacticsScreen::enabled(int index) const {
    return !state_.live_scene_option || exe_.u8(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField2 + index) != 0;
}
int TacticsScreen::text_width(std::string_view value) const {
    int width = 0;
    for (unsigned char c : value) {
        if (c <= 31 || c >= 128) continue;
        const int glyph = exe_.u8(::bmh::layout::ClubInfo_Raw_RuntimeField3 + c);
        width += glyph ? exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField2 + glyph * 2) - exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField + glyph * 2) : 2;
    }
    return width;
}
void TacticsScreen::text(Image& out, int x, int baseline, std::string_view value) const {
    for (unsigned char c : value) {
        if (c <= 31 || c >= 128) continue;
        const int glyph = exe_.u8(::bmh::layout::ClubInfo_Raw_RuntimeField3 + c);
        if (!glyph) { x += 2; continue; }
        const int sx = exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField + glyph * 2), w = exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField2 + glyph * 2) - sx;
        copy(out, letters_, sx, 7, x, baseline - 6, w, 7, true); x += w;
    }
}
void TacticsScreen::draw_setting(Image& out, int index) const {
    const int value = state_.values[index];
    int x = exe_.u8(::bmh::layout::Tactics_DrawSetting_RuntimeField2 + index * 2), y = exe_.u8(::bmh::layout::Tactics_DrawSetting_RuntimeField3 + index * 2);
    // CopiesOriginalLocalSpritePlayerPositionsIncluding copies the original's local sprite/player positions, including
    // the per-setting pair overrides for the three long-pass/shot diagrams.
    std::array<uint8_t, 87> positions{};
    for (size_t i = 0; i < positions.size(); ++i) positions[i] = exe_.u8(::bmh::layout::Tactics_DrawSetting_RuntimeField10 + uint32_t(i));
    if (index > 0 && index < 4) {
        const uint32_t table = uint32_t((index - 1) * 24 + value * 6);
        const auto a = ::bmh::layout::Tactics_DrawSetting_ExecutableFieldUnsigned + table;
        copy(out, sprites_, exe_.u8(a), exe_.u8(a + 1), x + exe_.u8(a + 4), y + exe_.u8(a + 5),
            exe_.u8(a + 2), exe_.u8(a + 3), true);
        const int first = exe_.u8(::bmh::layout::Tactics_DrawSetting_RuntimeField9 + index), count = exe_.u8(::bmh::layout::Tactics_DrawSetting_RuntimeField8 + index);
        for (int i = 0; i < count; ++i) {
            positions.at(size_t(first + i) * 3) = exe_.u8(::bmh::layout::Tactics_DrawSetting_RuntimeField11 + table + i * 2);
            positions.at(size_t(first + i) * 3 + 1) = exe_.u8(::bmh::layout::Tactics_DrawSetting_RuntimeField12 + table + i * 2);
        }
    }
    if (index == 4 || ((index == 5 || index == 6) && value == 0)) {
        int sx = exe_.u8(::bmh::layout::Tactics_DrawSetting_RuntimeField4 + index * 2), sy = exe_.u8(::bmh::layout::Tactics_DrawSetting_RuntimeField5 + index * 2);
        if (index == 4 && value != 0) sy = 26;
        copy(out, sprites_, sx, sy, x, y, exe_.u8(::bmh::layout::Tactics_DrawSetting_RuntimeField6 + index), exe_.u8(::bmh::layout::Tactics_DrawSetting_RuntimeField7 + index), true);
    }
    if (index == 7)
        for (int i = 0; i < value; ++i) copy(out, sprites_, 152, 0, 218 + i * 5, 134, 4, 15, true);
    if (index == 0) {
        std::array<int, 11> px{}, py{};
        for (int row = 1; row < 11; ++row) {
            px[row] = uint8_t(int(state_.depth[row]) * 81 / 18 + 42);
            py[row] = uint8_t(int(state_.lane[row]) * 53 / 8 + 33);
            copy(out, sprites_, 52, 52, px[row] + int8_t(exe_.u8(::bmh::layout::Tactics_DrawSetting_RuntimeField + value)), py[row], 38, 6, true);
        }
        for (int row = 1; row < 11; ++row) copy(out, sprites_, 0, 52, px[row], py[row], 6, 6, true);
    } else {
        const int first = exe_.u8(::bmh::layout::Tactics_DrawSetting_RuntimeField9 + index), count = exe_.u8(::bmh::layout::Tactics_DrawSetting_RuntimeField8 + index);
        for (int i = 0; i < count; ++i) {
            const size_t at = size_t(first + i) * 3;
            copy(out, sprites_, int(positions.at(at + 2)) * 16, 52,
                positions.at(at), positions.at(at + 1), 6, 6, true);
        }
    }
}
Image TacticsScreen::render() const {
    Image out = background_;
    if (!state_.decorated_background) {
        rect(out, 0, 0, 320, 240, 0);
        rect(out, 0, 4, 320, 13, 0x44); rect(out, 1, 5, 318, 11, 0x39);
        rect(out, 0, 228, 320, 10, 0x44); rect(out, 1, 229, 318, 8, 0x39);
    }
    const auto& title = texts_[(::bmh::layout::Tactics_Render_TextPointer - ::bmh::layout::TextPointerTable) / 4];
    text(out, 172 + (282 - 172) / 2 - text_width(title) / 2, 13, title);
    text(out, 5, 235, state_.club_name);
    copy(out, board_, 0, 0, 19, 22, 295, 206);
    for (int i = 0; i < 8; ++i) if (enabled(i)) draw_setting(out, i);
    return out;
}
TacticsRequest TacticsScreen::click(int x, int y, bool right) {
    if (pending_ || close_requested_) return {};
    if (right) { close_requested_ = true; return {}; }
    for (int i = 0; i < 8; ++i) {
        const int left = exe_.u8(::bmh::layout::Tactics_Click_RuntimeField + i), top = exe_.u8(::bmh::layout::Tactics_Click_RuntimeField3 + i);
        if (!enabled(i) || x < left || y < top || x > left + exe_.u8(::bmh::layout::Tactics_Click_RuntimeField4 + i) ||
                y > top + exe_.u8(::bmh::layout::Tactics_Click_RuntimeField5 + i)) continue;
        int value = state_.values[i] + 1;
        if (i == 7) value = std::clamp((x - (exe_.u8(::bmh::layout::Tactics_Click_RuntimeField2) + 10)) / 5, 0, int(exe_.u8(::bmh::layout::Tactics_Click_RuntimeField6)));
        if (value > exe_.u8(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField + i)) value = 0;
        TacticsRequest r;
        r.kind = TacticsRequestKind::set_tactic; r.manager_slot = state_.manager_slot;
        r.runtime_club = state_.runtime_club; r.revision = state_.revision; r.index = i; r.desired_value = value;
        if (i == 0) r.desired_assistant_attitude = 0;
        pending_ = r; last_rejection_.clear(); return r;
    }
    return {};
}
TacticsRequest TacticsScreen::key(std::string_view) {
    // RerateTeam polls mouse events only. Physical Escape is not its event8;
    // the right-button/timer close path remains owned by click(..., true).
    return {};
}
void TacticsScreen::acknowledge(TacticsState updated) {
    if (!pending_ || updated.revision <= state_.revision || updated.manager_slot != state_.manager_slot ||
            updated.runtime_club != state_.runtime_club)
        throw FormatError("tactics: acknowledgement requires matching identity/newer revision");
    validate(updated); state_ = std::move(updated); pending_.reset();
}
void TacticsScreen::reject_action(std::string reason) {
    if (!pending_ || reason.empty()) throw FormatError("tactics: rejection requires pending request/reason");
    last_rejection_ = std::move(reason); pending_.reset();
}
std::string TacticsScreen::state_json() const {
    std::ostringstream out;
    out << "{\"screen\":\"tactics\",\"source\":" << json_quote(state_.source)
        << ",\"manager_slot\":" << state_.manager_slot << ",\"runtime_club\":" << state_.runtime_club
        << ",\"revision\":" << state_.revision << ",\"club_name\":" << json_quote(original_to_utf8(state_.club_name))
        << ",\"assistant_attitude\":" << int(state_.assistant_attitude) << ",\"values\":[";
    for (size_t i = 0; i < state_.values.size(); ++i) { if (i) out << ','; out << int(state_.values[i]); }
    out << "],\"enabled\":[";
    for (int i = 0; i < 8; ++i) { if (i) out << ','; out << (enabled(i) ? "true" : "false"); }
    out << "],\"close_requested\":" << (close_requested_ ? "true" : "false") << ",\"pending_action\":";
    if (pending_) out << tactics_request_json(*pending_); else out << "null";
    out << ",\"last_rejection\":" << json_quote(last_rejection_) << '}'; return out.str();
}

} // namespace bmh::ui
