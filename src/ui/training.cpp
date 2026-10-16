// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/training.hpp"

#include <algorithm>
#include <numeric>
#include <set>
#include <sstream>

#include "formats/savestate.hpp"
#include "game/state.hpp"
#include "ui/manager.hpp"

namespace bmh::ui {
namespace {
Bytes block_copy(const SaveState& s, uint32_t address) {
    const auto& block = s.block(address);
    return {s.stream().begin() + block.offset, s.stream().begin() + block.offset + block.size};
}
std::vector<uint16_t> words(const Bytes& b) {
    if (b.size() % 2) throw FormatError("training: odd word table size");
    std::vector<uint16_t> out;
    for (size_t at = 0; at < b.size(); at += 2) out.push_back(le16(b, at));
    return out;
}
std::string string(const Bytes& pool, size_t at) {
    if (at >= pool.size()) throw FormatError("training: invalid name offset");
    const auto begin = pool.begin() + at, end = std::find(begin, pool.end(), 0);
    if (end == pool.end()) throw FormatError("training: unterminated name");
    return {begin, end};
}
template<size_t N> int sum(const std::array<uint8_t, N>& values, size_t first = 0) {
    return std::accumulate(values.begin() + first, values.end(), 0);
}
void validate(const TrainingState& s) {
    if (s.manager_slot < 0 || s.manager_slot > 3 || s.runtime_club < 0 || s.mode < 2 || s.mode > 3 ||
        (s.squad_capacity != 16 && s.squad_capacity != 24))
        throw FormatError("training: unsupported manager/mode/squad");
    for (uint8_t x : s.types) if (x < 1 || x > 3) throw FormatError("training: invalid type intensity");
    for (uint8_t x : s.areas) if (x < 1 || x > 3) throw FormatError("training: invalid area intensity");
    if (sum(s.types) > 8 || int(s.areas[0]) + s.areas[1] > 4 || sum(s.areas, 2) > 18)
        throw FormatError("training: original ball budget exceeded");
    std::set<int> rows, ids;
    for (const auto& p : s.players)
        if (p.row < 0 || p.row >= s.squad_capacity || p.player_id < 0 || p.position < 0 || p.position > 3 ||
            p.intensity > 3 || p.focus > 7 || !rows.insert(p.row).second || !ids.insert(p.player_id).second)
            throw FormatError("training: invalid/duplicate player row");
    if (s.players.size() > size_t(s.squad_capacity)) throw FormatError("training: too many players");
}
TrainingState project(const StaticMemory& memory, const Bytes& records, const Bytes& tactics, const Bytes& options,
                      const std::vector<int>& teams, const std::vector<uint16_t>& squads,
                      const std::vector<Player>& players, const std::vector<uint16_t>& club_offsets, const Bytes& clubs,
                      const std::vector<uint16_t>& name_offsets, const Bytes& names, const LeImage& exe, uint64_t revision) {
    TrainingState s; s.manager_slot = memory.u8(::bmh::layout::ActiveManager); s.mode = memory.u8(::bmh::layout::GameMode); s.revision = revision;
    const int manager_count = memory.u8(::bmh::layout::ManagerCount);
    if (manager_count < 1 || manager_count > 4 || s.manager_slot >= manager_count || teams.size() != size_t(manager_count))
        throw FormatError("training: inconsistent active manager");
    s.runtime_club = teams.at(s.manager_slot);
    const int max24 = exe.u16(::bmh::layout::TeamsWithSquads), max16 = exe.u16(::bmh::layout::CareerSession_DispatchLineup_RuntimeField);
    if (s.runtime_club < 0 || s.runtime_club >= max16) throw FormatError("training: national/cup-only squad unsupported");
    s.squad_capacity = s.runtime_club < max24 ? 24 : 16;
    const size_t first = size_t(s.runtime_club < max24 ? s.runtime_club * 24 : s.runtime_club * 16 + max24 * 8);
    const size_t manager = size_t(s.manager_slot) * 0xb0c, board = size_t(s.manager_slot) * 0x840;
    if (records.size() < manager + 0xb0c || tactics.size() < board + size_t(s.squad_capacity) * 0x58 ||
        options.size() < size_t(s.manager_slot + 1) * 5 || squads.size() < first + size_t(s.squad_capacity))
        throw FormatError("training: truncated manager/squad tables");
    std::copy_n(records.begin() + manager + 0xc5, 4, s.types.begin());
    std::copy_n(records.begin() + manager + 0xc9, 9, s.areas.begin());
    s.automation = options.at(size_t(s.manager_slot) * 5 + 4);
    s.club_name = string(clubs, club_offsets.at(s.runtime_club));
    s.decorated_background = memory.u8(::bmh::layout::PlainBackground) == 0;
    s.gradient_text = memory.u8(::bmh::layout::TextGradientDisabled) == 0 && exe.u8(::bmh::layout::TextGradientMode) == 2;
    for (int row = 0; row < s.squad_capacity; ++row) {
        const int player = squads.at(first + row) & 0x0fff;
        if (player == 0x0fff) continue;
        const auto& p = players.at(player);
        const size_t at = board + size_t(row) * 0x58;
        const uint8_t training = tactics.at(at + 0x0a);
        s.players.push_back({row, player, p.position(), tactics.at(at + 0x0f), string(names, name_offsets.at(player)),
                             uint8_t(training & 3), uint8_t(training >> 2), p.w368});
    }
    validate(s); return s;
}
void rect(Image& out, int x, int y, int w, int h, uint8_t color) {
    for (int yy = std::max(0, y); yy < std::min(out.height, y + h); ++yy)
        for (int xx = std::max(0, x); xx < std::min(out.width, x + w); ++xx) out.at(xx, yy) = color;
}
void copy(Image& out, const Image& atlas, int sx, int sy, int x, int y, int w, int h, int transparent = -1) {
    if (sx < 0 || sy < 0 || sx + w > atlas.width || sy + h > atlas.height)
        throw FormatError("training: sprite outside original atlas");
    for (int yy = 0; yy < h; ++yy) for (int xx = 0; xx < w; ++xx) {
        const auto c = atlas.pixels[size_t(sy + yy) * atlas.width + sx + xx];
        if (c != transparent && x + xx >= 0 && y + yy >= 0 && x + xx < out.width && y + yy < out.height)
            out.at(x + xx, y + yy) = c;
    }
}
const char* request_name(TrainingRequestKind kind) {
    switch (kind) {
    case TrainingRequestKind::none: return "none";
    case TrainingRequestKind::initialize: return "initialize_training";
    case TrainingRequestKind::set_type: return "set_training_type";
    case TrainingRequestKind::set_area: return "set_training_area";
    case TrainingRequestKind::set_player_intensity: return "set_player_training_intensity";
    case TrainingRequestKind::set_player_focus: return "set_player_training_focus";
    case TrainingRequestKind::set_automation: return "set_training_automation";
    }
    return "none";
}
template<size_t N> void array_json(std::ostream& out, const std::array<uint8_t, N>& values) {
    out << '['; for (size_t i = 0; i < N; ++i) { if (i) out << ','; out << int(values[i]); } out << ']';
}
} // namespace

TrainingState training_from_save(const SaveState& save, const LeImage& exe, uint64_t revision) {
    StaticMemory memory(save);
    const auto records = block_copy(save, ::bmh::layout::ManagerRecords), tactics = block_copy(save, ::bmh::layout::TacticsBoards), options = block_copy(save, ::bmh::layout::ManagerAutomation);
    const auto w1 = block_copy(save, ::bmh::layout::PlayerConditionTechniqueForm), w2 = block_copy(save, ::bmh::layout::PlayerSkillWords), w3 = block_copy(save, ::bmh::layout::PlayerIdentityWords);
    if (w1.size() != w2.size() || w1.size() != w3.size() || w1.size() % 4) throw FormatError("training: inconsistent player words");
    std::vector<Player> players;
    for (size_t at = 0; at < w1.size(); at += 4) players.push_back({{}, le32(w1, at), le32(w2, at), le32(w3, at)});
    std::vector<int> teams;
    for (int slot = 0; slot < memory.u8(::bmh::layout::ManagerCount); ++slot) teams.push_back(le16(records, size_t(slot) * 0xb0c + 0x290));
    auto state = project(memory, records, tactics, options, teams, words(block_copy(save, ::bmh::layout::SquadSlots)), players,
                         words(block_copy(save, ::bmh::layout::ClubIdentities)), save.pool(0), words(block_copy(save, ::bmh::layout::PlayerNameOffsets)), save.pool(1), exe, revision);
    state.source = "original_save_read_only"; return state;
}
TrainingState training_from_game(const GameState& g, const LeImage& exe, uint64_t revision) {
    auto state = project(g.memory, g.manager_records, g.tactics, g.manager_options, g.manager_teams, g.squad_slots,
                         g.players, g.club_ids, g.club_names, g.player_name_offsets, g.player_names, exe, revision);
    state.source = "engine_projection"; return state;
}

std::string training_json(const TrainingState& s) {
    std::ostringstream out;
    out << "{\"screen\":\"training\",\"source\":" << json_quote(s.source) << ",\"revision\":" << s.revision
        << ",\"manager_slot\":" << s.manager_slot << ",\"runtime_club\":" << s.runtime_club
        << ",\"club_name\":" << json_quote(original_to_utf8(s.club_name)) << ",\"mode\":" << s.mode
        << ",\"types\":"; array_json(out, s.types); out << ",\"areas\":"; array_json(out, s.areas);
    out << ",\"automation\":" << int(s.automation) << ",\"needs_initialization\":" << (s.needs_initialization() ? "true" : "false")
        << ",\"remaining_type_balls\":" << 8 - sum(s.types) << ",\"remaining_condition_technique_balls\":"
        << 4 - int(s.areas[0]) - int(s.areas[1]) << ",\"remaining_skill_balls\":" << 18 - sum(s.areas, 2)
        << ",\"overtraining\":" << (sum(s.areas, 2) > 14 ? "true" : "false") << ",\"players\":[";
    for (size_t i = 0; i < s.players.size(); ++i) {
        if (i) out << ',';
        const auto& p = s.players[i];
        out << "{\"row\":" << p.row << ",\"player_id\":" << p.player_id << ",\"name\":" << json_quote(original_to_utf8(p.name))
            << ",\"position\":" << p.position << ",\"used\":" << p.used << ",\"intensity\":" << int(p.intensity)
            << ",\"focus\":" << int(p.focus) << '}';
    }
    out << "],\"engine_attached\":false}"; return out.str();
}
std::string training_request_json(const TrainingRequest& r) {
    std::ostringstream out;
    out << "{\"kind\":" << json_quote(request_name(r.kind)) << ",\"manager_slot\":" << r.manager_slot
        << ",\"runtime_club\":" << r.runtime_club << ",\"revision\":" << r.revision
        << ",\"index\":" << r.index << ",\"value\":" << r.value << ",\"player_id\":" << r.player_id
        << ",\"desired_types\":"; array_json(out, r.desired_types); out << ",\"desired_areas\":"; array_json(out, r.desired_areas);
    out << ",\"desired_automation\":" << int(r.desired_automation) << '}'; return out.str();
}

TrainingScreen::TrainingScreen(const std::filesystem::path& data, TrainingState state)
    : state_(std::move(state)), exe_(load_le(asset_path(data, "BMH.exe"))),
      background_(load_vga(asset_path(data / "KB", "129.VGA"))), widgets_(load_vga(asset_path(data / "KB", "0.VGA"))),
      skill_atlas_(load_vga(asset_path(data / "KB", "2.VGA"))), training_atlas_(load_vga(asset_path(data / "KB", "10.VGA"))),
      letters_(load_vga(asset_path(data / "KB", "54.VGA"))), small_(load_font(asset_path(data / "F", "BMH3.FNT"))),
      texts_(load_textd(asset_path(data, "TEXTD.BLK"))) {
    validate(state_);
    if (background_.width != 320 || background_.height != 240) throw FormatError("training: unsupported background size");
}
void TrainingScreen::text(Image& out, int x, int baseline, std::string_view value, uint8_t color, int right, bool shadow) const {
    const int width = small_.text_width(value);
    if (right >= 0) x += (right - x) / 2 - width / 2;
    if (x < 0 || x + width + (shadow ? 2 : 0) > out.width || baseline < 5 || baseline + (shadow ? 2 : 0) >= out.height) return;
    if (shadow) small_.draw(out, x + 2, baseline - 3, value, 0x40);
    Image mask(std::max(1, width), 6); small_.draw(mask, 0, 0, value, 1);
    int gradient = 0;
    while (gradient < 32 && exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2 + gradient) != color) ++gradient;
    for (int yy = 0; yy < 6; ++yy) for (int xx = 0; xx < width; ++xx) if (mask.at(xx, yy))
        out.at(x + xx, baseline - 5 + yy) = state_.gradient_text ? exe_.u8(::bmh::layout::Advertising_Text_RuntimeField + gradient * 6 + yy) : color;
}
void TrainingScreen::sprite_text(Image& out, int x, int baseline, std::string_view value, int font) const {
    for (unsigned char c : value) {
        if (c <= 31 || c >= 128) continue;
        const int glyph = exe_.u8(::bmh::layout::ClubInfo_Raw_RuntimeField3 + c);
        if (!glyph) { x += 2; continue; }
        const int sx = exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField + glyph * 2), w = exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField2 + glyph * 2) - sx;
        copy(out, letters_, sx, (font - 4) * 7, x, baseline - 6, w, 7, 0); x += w;
    }
}
void TrainingScreen::button(Image& out, int x, int y, int width, int height, std::string_view label, bool active) const {
    if (!active) {
        rect(out, x + 2, y + height, width, 2, 0x40); rect(out, x + width, y + 2, 2, height, 0x40);
    } else {
        const uint8_t base = out.pixels.at(size_t(y + height) * out.width + x);
        rect(out, x, y, width + 1, 2, base); rect(out, x, y, 2, height + 1, base); x += 2; y += 2;
    }
    if (height < 11 && width > 31) {
        const int shift = active ? -30 : 0;
        copy(out, widgets_, 24, 74 + shift, x, y, 31, 10);
        int cursor = 31, left = width - 31;
        while (left > 20) { const int n = std::min(8, left - 20); copy(out, widgets_, 24, 84 + shift, x + cursor, y, n, 10); cursor += n; left -= n; }
        copy(out, widgets_, 45 - left, 94 + shift, x + cursor, y, left, 10);
    } else {
        // PatternStartsAt(0,0x12): original four-pixel frame and patterned interior.
        for (int yy = 4; yy < height - 3; ++yy) for (int xx = 4; xx < width - 3; ++xx)
            out.at(x + xx, y + yy) = exe_.u8(::bmh::layout::Advertising_PatternRect_RuntimeField + ((yy - 4) % 16) * 16 + (xx - 4) % 16);
        copy(out, widgets_, 0, 0, x, y, 4, 4); copy(out, widgets_, 4, 0, x + width - 4, y, 4, 4);
        copy(out, widgets_, 0, 4, x, y + height - 4, 4, 4); copy(out, widgets_, 4, 4, x + width - 4, y + height - 4, 4, 4);
        copy(out, widgets_, 0, 104, x + 4, y, width - 8, 4); copy(out, widgets_, 0, 108, x + 4, y + height - 4, width - 8, 4);
        copy(out, widgets_, 0, 8, x, y + 4, 4, height - 8); copy(out, widgets_, 4, 8, x + width - 4, y + 4, 4, height - 8);
    }
    const int font = active ? exe_.u8(::bmh::layout::Training_Button_RuntimeField) : exe_.u8(::bmh::layout::Advertising_Button_RuntimeField);
    // Original sprite-font text width, including the two-pixel space glyph.
    int text_width = 0;
    for (unsigned char c : label) {
        const int glyph = c < 128 ? exe_.u8(::bmh::layout::ClubInfo_Raw_RuntimeField3 + c) : 0;
        text_width += glyph ? exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField2 + glyph * 2) - exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField + glyph * 2) : 2;
    }
    sprite_text(out, x + width / 2 - text_width / 2, y + height / 2 + 2, label, font);
}
Image TrainingScreen::render() const {
    Image out = state_.decorated_background ? background_ : Image(320, 240);
    if (!state_.decorated_background) {
        rect(out, 0, 4, 320, 13, 0x44); rect(out, 1, 5, 318, 11, 0x39);
        rect(out, 0, 228, 320, 10, 0x44); rect(out, 1, 229, 318, 8, 0x39);
    }
    const auto& title = texts_[(::bmh::layout::Training_Render_TextPointer5 - ::bmh::layout::TextPointerTable) / 4];
    int title_width = 0;
    for (unsigned char c : title) {
        const int glyph = exe_.u8(::bmh::layout::ClubInfo_Raw_RuntimeField3 + c);
        title_width += glyph ? exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField2 + glyph * 2) - exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField + glyph * 2) : 2;
    }
    sprite_text(out, 172 + (282 - 172) / 2 - title_width / 2, 13, title, 5);
    sprite_text(out, 5, 235, state_.club_name, 5);
    for (int i = 0; i < 9; ++i) {
        const int baseline = 29 + i * 17;
        copy(out, training_atlas_, 39, 52, 7, baseline - 10, 28, 16);
        copy(out, training_atlas_, (i % 3) * 26, (i / 3) * 14 + 68, 8, baseline - 9, 26, 14);
        text(out, 39, baseline, texts_[(::bmh::layout::Training_Render_TextPointer3 - ::bmh::layout::TextPointerTable) / 4 + i], 0x5f);
        text(out, 147, baseline, exe_.cstring(::bmh::layout::Bank_OriginalToUtf8_ExecutableField), 0x5f);
    }
    copy(out, training_atlas_, 0, 0, 7, 172, 89, 52);
    for (const auto& p : state_.players) {
        const int y = 28 + p.row * 8;
        text(out, 185, y, texts_[(::bmh::layout::ClubInfo_Render_TextPointer2 - ::bmh::layout::TextPointerTable) / 4 + p.position], 0x27, 200);
        text(out, 204, y, p.name, 0x27);
        auto used = std::to_string(p.used); if (used.size() < 2) used.insert(0, 1, ' ');
        text(out, 263, y, used, 0x27);
        if (p.focus) copy(out, widgets_, exe_.u8(::bmh::layout::Training_Render_TextPointer + p.focus), exe_.u8(::bmh::layout::Training_Render_TextPointer2 + p.focus), 277, 23 + p.row * 8, 11, 7);
    }
    rect(out, 190, 222, 66, 6, 0x44);
    text(out, 190, 220, texts_[(::bmh::layout::Training_Render_TextPointer4 - ::bmh::layout::TextPointerTable) / 4], 0x51, -1, false);
    for (int i = 0; i < 7; ++i) copy(out, skill_atlas_, 256, 32, 190 + i * 10, 222, 16, 5, 0);
    // Int6: four original table-driven groups, transparent index DesktopRuntimeField=0.
    for (int group = 0; group < 4; ++group) {
        const int count = group == 3 ? int(state_.players.size()) : exe_.u8(::bmh::layout::Training_Click_RuntimeField7 + group);
        const int x = exe_.u16(::bmh::layout::Training_Click_RuntimeField + group * 2), y = exe_.u8(::bmh::layout::Training_Click_RuntimeField3 + group);
        const int dx = exe_.u8(::bmh::layout::Training_Click_RuntimeField8 + group), dy = exe_.u8(::bmh::layout::Training_Click_RuntimeField9 + group);
        const int ball_dx = exe_.u8(::bmh::layout::Training_Click_RuntimeField5 + group), ball_dy = exe_.u8(::bmh::layout::Training_Click_RuntimeField6 + group);
        for (int index = 0; index < count; ++index) {
            int intensity;
            if (group == 0) intensity = state_.types[index];
            else if (group == 1) intensity = state_.areas[index];
            else if (group == 2) intensity = state_.areas[index + 2];
            else {
                auto it = std::find_if(state_.players.begin(), state_.players.end(), [&](const auto& p) { return p.row == index; });
                if (it == state_.players.end()) continue;
                intensity = it->intensity;
            }
            for (int ball = 0; ball < intensity; ++ball)
                copy(out, training_atlas_, group == 2 && sum(state_.areas, 2) > 14 ? 16 : 0,
                     exe_.u8(::bmh::layout::Training_Render_RuntimeField + group), x + index * dx + ball * ball_dx, y + index * dy + ball * ball_dy, 8, 8, 0);
        }
    }
    const bool automatic = (state_.automation & 1) != 0;
    button(out, 113, 196, 68, 10, texts_[(::bmh::layout::Training_Render_TextPointer6 - ::bmh::layout::TextPointerTable) / 4], automatic);
    if (automatic) for (int group = 0; group < 4; ++group)
        button(out, exe_.u16(::bmh::layout::Training_Click_RuntimeField2 + group * 2), exe_.u8(::bmh::layout::Training_Click_RuntimeField4 + group), 25, 10, exe_.cstring(::bmh::layout::Fltd_Layout_ExecutableField5),
               (state_.automation & (2 << group)) != 0);
    return out;
}

TrainingRequest TrainingScreen::request(TrainingRequestKind kind, int index, int value, int player) {
    TrainingRequest r;
    r.kind = kind; r.manager_slot = state_.manager_slot; r.runtime_club = state_.runtime_club;
    r.index = index; r.value = value; r.player_id = player; r.revision = state_.revision;
    r.desired_types = state_.types; r.desired_areas = state_.areas; r.desired_automation = state_.automation;
    return r;
}
TrainingRequest TrainingScreen::initialize() {
    if (pending_ || !state_.needs_initialization()) return {};
    auto r = request(TrainingRequestKind::initialize); r.desired_automation = 0x9f;
    pending_ = r; last_rejection_.clear(); return r;
}
TrainingRequest TrainingScreen::click(int x, int y, bool right) {
    if (pending_) return {};
    if (right) { close_requested_ = true; return {}; }
    if (x < 0 || y < 0 || x >= 320 || y >= 240) return {};
    if (state_.needs_initialization()) throw FormatError("training: engine initialization required before edits");
    if (x >= 113 && x <= 181 && y >= 196 && y <= 206) {
        auto r = request(TrainingRequestKind::set_automation, 0, (state_.automation & 1) ? 0 : 1);
        r.desired_automation ^= 1; pending_ = r; last_rejection_.clear(); return r;
    }
    if (state_.automation & 1) for (int group = 0; group < 4; ++group) {
        const int bx = exe_.u16(::bmh::layout::Training_Click_RuntimeField2 + group * 2), by = exe_.u8(::bmh::layout::Training_Click_RuntimeField4 + group);
        if (x >= bx && x <= bx + 25 && y >= by && y <= by + 10) {
            auto r = request(TrainingRequestKind::set_automation, group + 1, (state_.automation & (2 << group)) ? 0 : 1);
            r.desired_automation ^= uint8_t(2 << group); pending_ = r; last_rejection_.clear(); return r;
        }
    }
    for (int group = 0; group < 4; ++group) {
        const int count = group == 3 ? int(state_.players.size()) : exe_.u8(::bmh::layout::Training_Click_RuntimeField7 + group);
        const int bx = exe_.u16(::bmh::layout::Training_Click_RuntimeField + group * 2), by = exe_.u8(::bmh::layout::Training_Click_RuntimeField3 + group);
        const int dx = exe_.u8(::bmh::layout::Training_Click_RuntimeField8 + group), dy = exe_.u8(::bmh::layout::Training_Click_RuntimeField9 + group);
        const int ball_dx = exe_.u8(::bmh::layout::Training_Click_RuntimeField5 + group), ball_dy = exe_.u8(::bmh::layout::Training_Click_RuntimeField6 + group);
        if (x < bx - (group == 3 ? 14 : 0) || y < by || x >= bx + count * dx + 3 * ball_dx ||
            y >= by + count * dy + 3 * ball_dy) continue;
        const int index = dx ? (x - bx) / dx : (y - by) / dy;
        const int value = (ball_dx ? (x - bx) / ball_dx : (y - by) / ball_dy) + 1;
        if (index < 0 || index >= count) continue;
        TrainingRequest r;
        if (group < 3) {
            if (value < 1 || value > 3) continue;
            if (group == 0) {
                if (state_.types[index] == value || sum(state_.types) - state_.types[index] + value > 8) return {};
                r = request(TrainingRequestKind::set_type, index, value); r.desired_types[index] = uint8_t(value);
            } else {
                const int area = index + (group == 2 ? 2 : 0);
                const int budget = group == 1 ? int(state_.areas[0]) + state_.areas[1] : sum(state_.areas, 2);
                if (state_.areas[area] == value || budget - state_.areas[area] + value > (group == 1 ? 4 : 18)) return {};
                r = request(TrainingRequestKind::set_area, area, value); r.desired_areas[area] = uint8_t(value);
            }
            r.desired_automation &= uint8_t(~(2 << (group == 0 ? 0 : 1)));
        } else {
            auto it = std::find_if(state_.players.begin(), state_.players.end(), [&](const auto& p) { return p.row == index; });
            if (it == state_.players.end()) return {};
            if (x < 291) {
                r = request(TrainingRequestKind::set_player_focus, index, (it->focus + 1) % 8, it->player_id);
                // Original skill-icon changes preserve the automatic-focus flag.
            } else {
                if (value < 1 || value > 3 || value == it->intensity) return {};
                r = request(TrainingRequestKind::set_player_intensity, index, value, it->player_id);
                r.desired_automation &= uint8_t(~8);
            }
        }
        pending_ = r; last_rejection_.clear(); return r;
    }
    return {};
}
TrainingRequest TrainingScreen::key(std::string_view key) {
    if (!pending_ && (key == "ESC" || key == "ESCAPE" || key == "BACKSPACE")) close_requested_ = true;
    return {};
}
void TrainingScreen::acknowledge(TrainingState updated) {
    if (!pending_ || updated.revision <= state_.revision || updated.manager_slot != state_.manager_slot ||
        updated.runtime_club != state_.runtime_club || updated.squad_capacity != state_.squad_capacity)
        throw FormatError("training: expected newer authoritative state for same manager/club");
    validate(updated); state_ = std::move(updated); pending_.reset(); last_rejection_.clear();
}
void TrainingScreen::reject_action(std::string reason) {
    if (!pending_ || reason.empty()) throw FormatError("training: rejection requires pending action and reason");
    last_rejection_ = std::move(reason); pending_.reset();
}
std::string TrainingScreen::state_json() const {
    auto out = training_json(state_); out.pop_back();
    return out + ",\"pending_action\":" + (pending_ ? training_request_json(*pending_) : "null") +
           ",\"last_rejection\":" + (last_rejection_.empty() ? "null" : json_quote(last_rejection_)) +
           ",\"close_requested\":" + (close_requested_ ? "true" : "false") + "}";
}

} // namespace bmh::ui
