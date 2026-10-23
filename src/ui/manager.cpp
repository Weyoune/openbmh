// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/world_layout.hpp"
#include "ui/manager.hpp"

#include <algorithm>
#include <iomanip>
#include <fstream>
#include <numeric>
#include <set>
#include <sstream>
#include <utility>

namespace bmh::ui {
namespace {

void rect(Image& out, int x, int y, int w, int h, uint8_t color) {
    for (int yy = std::max(y, 0); yy < std::min(y + h, out.height); ++yy)
        for (int xx = std::max(x, 0); xx < std::min(x + w, out.width); ++xx) out.at(xx, yy) = color;
}

void copy(Image& out, const Image& src, int sx, int sy, int x, int y, int w, int h, bool transparent = false) {
    if (sx < 0 || sy < 0 || sx + w > src.width || sy + h > src.height)
        throw FormatError("manager: sprite outside original atlas");
    for (int yy = 0; yy < h; ++yy)
        for (int xx = 0; xx < w; ++xx) {
            const auto c = src.pixels[size_t(sy + yy) * src.width + sx + xx];
            if (x + xx >= 0 && y + yy >= 0 && x + xx < out.width && y + yy < out.height && (!transparent || c))
                out.at(x + xx, y + yy) = c;
        }
}

void border(Image& out, int x, int y) {
    rect(out, x - 1, y - 1, 1, 40, 0x46);
    rect(out, x - 1, y - 1, 40, 1, 0x46);
    rect(out, x - 1, y + 38, 40, 1, 0x40);
    rect(out, x + 38, y - 1, 1, 40, 0x40);
}

constexpr std::array<std::string_view, 7> german = {"Ä", "Ö", "Ü", "ä", "ö", "ü", "ß"};
constexpr std::string_view original_german = "[\\]{|}~";

}  // namespace

std::string json_quote(std::string_view value) {
    std::ostringstream s; s << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') s << '\\' << c;
        else if (c < 32) s << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c) << std::dec;
        else s << c;
    }
    s << '"'; return s.str();
}

std::string original_to_utf8(std::string_view value) {
    std::string out;
    for (char c : value) {
        auto i = original_german.find(c);
        if (i != std::string_view::npos) out += german[i];
        else if (static_cast<unsigned char>(c) < 128) out += c;
        else out += "�"; // Unsupported original byte remains visibly unknown.
    }
    return out;
}

std::string utf8_to_original(std::string_view value) {
    std::string out;
    for (size_t i = 0; i < value.size();) {
        bool found = false;
        for (size_t j = 0; j < german.size(); ++j)
            if (value.substr(i, german[j].size()) == german[j]) {
                out += original_german[j]; i += german[j].size(); found = true; break;
            }
        if (found) continue;
        const unsigned char c = value[i++];
        if (c >= 128) throw FormatError("manager name: unsupported UTF-8 character");
        out += char(c);
    }
    return out;
}

SelectionContext load_selection_context(const std::filesystem::path& path, uint64_t revision) {
    std::ifstream input(path);
    if (!input) throw FormatError("cannot read selection context: " + path.string());
    SelectionContext context; context.revision = revision; context.source = "reference_fixture";
    int id;
    while (input >> id) {
        if (context.club_order.size() >= 4096) throw FormatError("selection context too large");
        context.club_order.push_back(id);
    }
    if (!input.eof()) throw FormatError("selection context must contain decimal club IDs only");
    if (context.club_order.empty()) throw FormatError("selection context is empty");
    return context;
}

ManagerScreen::ManagerScreen(const std::filesystem::path& data, int slot, const std::vector<ManagerChoice>& confirmed, SelectionContext context)
    : confirmed_(confirmed), context_(std::move(context)), data_(data), exe_(load_le(asset_path(data, "BMH.exe"))),
      db_(load_database(asset_path(data, "M3D.MAN"),
          {exe_.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField), exe_.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3), exe_.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2), exe_.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4)})),
      texts_(load_textd(asset_path(data, "TEXTD.BLK"))),
      small_(load_font(asset_path(data / "F", "BMH3.FNT"))),
      large_(load_font(asset_path(data / "F", "BMH2.FNT"))),
      background_(load_vga(asset_path(data / "KB", "135.VGA"))),
      faces_(load_vga(asset_path(data / "KB", "26.VGA"))),
      widgets_(load_vga(asset_path(data / "KB", "0.VGA"))),
      letters_(load_vga(asset_path(data / "KB", "54.VGA"))) {
    if (slot < 0 || slot > 3 || int(confirmed.size()) != slot)
        throw FormatError("manager selection: expected preceding confirmed slots");
    for (int i = 0; i < slot; ++i) {
        const auto& c = confirmed[i];
        if (c.slot != i || c.master_club < 0 || c.master_club >= int(db_.club_names.size()) || c.portrait < 0 || c.portrait > 11)
            throw FormatError("manager selection: invalid preceding choice");
        for (int j = 0; j < i; ++j)
            if (c.master_club == confirmed[j].master_club || c.portrait == confirmed[j].portrait)
                throw FormatError("manager selection: duplicate preceding club or portrait");
    }
    if (background_.width != 320 || background_.height != 240 || exe_.u8(::bmh::layout::LeaguePairings_LeaguePairingsConstants_RuntimeField) != 5)
        throw FormatError("manager selection: unsupported original layout");
    if (context_.club_order.empty()) {
        context_.club_order.resize(db_.club_names.size());
        std::iota(context_.club_order.begin(), context_.club_order.end(), 0);
    }
    if (context_.club_order.size() < size_t(exe_.u16(::bmh::layout::CareerSession_DispatchLineup_RuntimeField)) + exe_.u16(::bmh::layout::CareerSession_InstallLeagueSizes_RuntimeField6))
        throw FormatError("manager selection: club order does not cover all selectable groups");
    std::set<int> unique;
    for (int id : context_.club_order)
        if (id < 0 || id >= int(db_.club_names.size()) || !unique.insert(id).second)
            throw FormatError("manager selection: club order must contain unique valid master IDs");
    choice_.slot = slot; choice_.roster_revision = context_.revision;
    choice_.name = texts_[(::bmh::layout::Career_NewCareerMarketsAndMoods_ExecutableField - ::bmh::layout::TextPointerTable) / 4 + slot];
    while (club_locked(master_id(choice_.selection_club))) ++choice_.selection_club;
    choice_.master_club = master_id(choice_.selection_club);
    while (portrait_locked(choice_.portrait)) ++choice_.portrait;
    for (int i = 0; i < 4; ++i) {
        text_x_[i] = exe_.u16(::bmh::layout::Manager_FormatError_RuntimeField2 + i * 2) + 3;
        baseline_[i] = exe_.u8(::bmh::layout::Manager_FormatError_RuntimeField6 + i) + 6;
        field_x_[i] = exe_.u16(::bmh::layout::Manager_FormatError_RuntimeField3 + i * 2);
        field_y_[i] = exe_.u16(::bmh::layout::Manager_FormatError_RuntimeField4 + i * 2);
        field_width_[i] = exe_.u16(::bmh::layout::Manager_FormatError_RuntimeField5 + i * 2);
    }
    int text_id = (::bmh::layout::Achievements_FormatError_TextPointer3 - ::bmh::layout::TextPointerTable) / 4, start = 0;
    for (int i = 0; i < 5; ++i) {
        first_[i] = start; last_[i] = start + exe_.u8(::bmh::layout::LeagueSizes + i) - 1;
        start = last_[i] + 1;
        leagues_[i] = texts_[text_id];
        if (auto suffix = exe_.u8(::bmh::layout::CareerSession_InstallLeagueSizes_RuntimeField3 + i * 2)) {
            auto tail = texts_[(::bmh::layout::Manager_FormatError_TextPointer2 - ::bmh::layout::TextPointerTable) / 4];
            if (tail.empty()) throw FormatError("manager: missing league suffix");
            tail.back() = char(suffix); leagues_[i] += tail;
        }
        if (exe_.u8(::bmh::layout::Manager_FormatError_RuntimeField + i * 2) < 'B') ++text_id;
        if (last_[i] - first_[i] >= 20) throw FormatError("manager: league exceeds picker grid");
    }
    first_[5] = world_layout().league_teams; last_[5] = first_[5] + 19;
    first_[6] = first_[5] + 20; last_[6] = first_[5] + exe_.u16(::bmh::layout::CareerSession_InstallLeagueSizes_RuntimeField6) - 1;
    leagues_[5] = texts_[(::bmh::layout::Manager_FormatError_TextPointer - ::bmh::layout::TextPointerTable) / 4]; leagues_[6] = leagues_[5];
    if (leagues_[6].empty()) throw FormatError("manager: missing extra-team label");
    leagues_[6].back() = 'B';
    if (last_[6] >= int(context_.club_order.size()) || last_[6] - first_[6] >= 20)
        throw FormatError("manager: unsupported extra-team table");
}

int ManagerScreen::master_id(int selection_id) const {
    return context_.club_order.at(size_t(selection_id));
}

bool ManagerScreen::club_locked(int id) const {
    return std::any_of(confirmed_.begin(), confirmed_.end(), [id](const auto& c) { return c.master_club == id; });
}
bool ManagerScreen::portrait_locked(int id) const {
    return std::any_of(confirmed_.begin(), confirmed_.end(), [id](const auto& c) { return c.portrait == id; });
}
std::string ManagerScreen::screen_name() const {
    switch (view_) {
        case ManagerView::club_picker: return "club_picker";
        case ManagerView::confirmation: return "manager_confirm";
        case ManagerView::accepted: return "manager_accepted";
        case ManagerView::name_edit: return "manager_name_edit";
        default: return "manager_selection";
    }
}
int ManagerScreen::hovered_club(int x, int y) const {
    for (int i = 0; i < 20; ++i) {
        int cx = 60 + (i % 5) * 40, cy = 15 + (i / 5) * 40;
        if (x >= cx && x < cx + 38 && y >= cy && y < cy + 38 && first_[group_] + i <= last_[group_])
            return first_[group_] + i;
    }
    return -1;
}
bool ManagerScreen::move(int x, int y) {
    int next = view_ == ManagerView::club_picker ? hovered_club(x, y) : -1;
    if (next == hover_) return false;
    hover_ = next; return true;
}
bool ManagerScreen::click(int x, int y, bool right) {
    if (x < 0 || x >= 320 || y < 0 || y >= 240 || view_ == ManagerView::accepted || view_ == ManagerView::name_edit) return false;
    if (view_ == ManagerView::confirmation) {
        if (y >= 166 && y <= 176) {
            if (x >= 62 && x <= 137) { view_ = ManagerView::accepted; return true; }
            if (x >= 175 && x <= 250) { view_ = ManagerView::selection; return true; }
        }
        return false;
    }
    if (view_ == ManagerView::club_picker) {
        if (right) { view_ = ManagerView::selection; hover_ = -1; return true; }
        for (int i = 0; i < 7; ++i) {
            int bx = i < 4 ? 64 : 161, by = 175 + (i < 4 ? i : i - 4) * 11;
            if (x >= bx && x <= bx + 94 && y >= by && y <= by + 10) {
                bool changed = group_ != i; group_ = i; hover_ = hovered_club(x, y); return changed;
            }
        }
        int id = hovered_club(x, y); bool moved = move(x, y);
        if (id >= 0 && !club_locked(master_id(id)) && id != choice_.selection_club) {
            choice_.selection_club = id; choice_.master_club = master_id(id); return true;
        }
        return moved;
    }
    if (right) { view_ = ManagerView::confirmation; return true; }
    bool open_clubs = x > 185 && x < 225 && y > 76 && y < 116;
    for (int i = 0; i < 12; ++i) {
        int px = (i < 6 ? 26 : 27) + (i % 6) * 45, py = i < 6 ? 151 : 197;
        if (x >= px && x < px + 38 && y >= py && y < py + 38) {
            if (portrait_locked(i) || choice_.portrait == i) return false;
            choice_.portrait = i; return true;
        }
    }
    for (int i = 0; i < 4; ++i) {
        if (x < field_x_[i] || x > field_x_[i] + field_width_[i] || y < baseline_[i] - 26 || y > baseline_[i] + 3) continue;
        if (i == 0) { choice_.name.clear(); view_ = ManagerView::name_edit; return true; }
        if (i == 1) { choice_.start_league = (choice_.start_league + 1) % 5; return true; }
        if (i == 2) open_clubs = true;
        if (i == 3) { choice_.strength = (choice_.strength + 1) % 3; return true; }
    }
    if (open_clubs) {
        // Original uses <= end when finding the initial group, including its boundary quirk.
        group_ = 0;
        while (group_ < 6 && last_[group_] <= choice_.selection_club) ++group_;
        view_ = ManagerView::club_picker; hover_ = hovered_club(x, y); return true;
    }
    return false;
}
bool ManagerScreen::text(std::string_view value) {
    if (view_ != ManagerView::name_edit) return false;
    const auto encoded = utf8_to_original(value); // Validate the entire UTF-8 input before mutation.
    auto before = choice_.name;
    for (unsigned char c : encoded) {
        bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (!letter && std::string_view(" ,:.'\\[{|}]~").find(char(c)) == std::string_view::npos) continue;
        const auto next = choice_.name + char(c);
        if (choice_.name.size() < 25 && small_.text_width(next) < 65) choice_.name = next;
    }
    return before != choice_.name;
}
bool ManagerScreen::key(std::string_view name) {
    if (view_ != ManagerView::name_edit) return false;
    if (name == "ENTER") { view_ = ManagerView::selection; return true; }
    if (name == "BACKSPACE" && !choice_.name.empty()) { choice_.name.pop_back(); return true; }
    return false;
}

void ManagerScreen::portrait(Image& out, int x, int y, int id) const {
    border(out, x, y); copy(out, faces_, (id % 6) * 38, (id / 6) * 38, x, y, 38, 38);
}
void ManagerScreen::badge(Image& out, int x, int y, int id) const {
    border(out, x, y);
    if (id > 89 && id < 138) { copy(out, widgets_, 8, 0, x, y, 38, 38); return; }
    int sprite = le16(db_.raw_club_badge_selectors, size_t(id) * 2) & 0x1ff;
    auto atlas = load_vga(asset_path(data_ / "W", std::to_string(sprite >> 3) + ".VGA"));
    copy(out, atlas, (sprite & 7) * 38, 0, x, y, 38, 38);
}
void ManagerScreen::pattern(Image& out, int x, int y, int w, int h) const {
    for (int yy = 0; yy < h; ++yy)
        for (int xx = 0; xx < w; ++xx) out.at(x + xx, y + yy) = exe_.u8(::bmh::layout::Advertising_PatternRect_RuntimeField + (yy % 16) * 16 + xx % 16);
}
void ManagerScreen::small_text(Image& out, int x, int baseline, std::string_view value) const {
    // Mask / TextWidth2: monochrome font mask, six-row palette gradient.
    Image mask(std::max(1, small_.text_width(value)), 6);
    small_.draw(mask, 0, 0, value, 1);
    int gradient = 0;
    while (gradient < 32 && exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2 + gradient) != 0x5f) ++gradient;
    for (int y = 0; y < 6; ++y)
        for (int xx = 0; xx < mask.width; ++xx)
            if (mask.at(xx, y) && x + xx >= 0 && x + xx < out.width && baseline - 5 + y >= 0 && baseline - 5 + y < out.height)
                out.at(x + xx, baseline - 5 + y) = exe_.u8(::bmh::layout::Advertising_Text_RuntimeField + gradient * 6 + y);
}
void ManagerScreen::frame(Image& out, int x, int y, int w, int h, bool shadow) const {
    if (shadow) {
        rect(out, x + 2, y + h, w, 2, 0x40);
        rect(out, x + w, y + 2, 2, h, 0x40);
    }
    pattern(out, x + 4, y + 4, w - 7, h - 7);
    copy(out, widgets_, 0, 0, x, y, 4, 4); copy(out, widgets_, 4, 0, x + w - 4, y, 4, 4);
    copy(out, widgets_, 0, 4, x, y + h - 4, 4, 4); copy(out, widgets_, 4, 4, x + w - 4, y + h - 4, 4, 4);
    for (int k = 4; k < w - 4; k += 74) {
        int n = std::min(74, w - 4 - k);
        copy(out, widgets_, 0, 104, x + k, y, n, 4); copy(out, widgets_, 0, 108, x + k, y + h - 4, n, 4);
    }
    for (int k = 4; k < h - 4; k += 96) {
        int n = std::min(96, h - 4 - k);
        copy(out, widgets_, 0, 8, x, y + k, 4, n); copy(out, widgets_, 4, 8, x + w - 4, y + k, 4, n);
    }
}
int ManagerScreen::sprite_text_width(std::string_view value) const {
    int width = 0;
    for (unsigned char c : value) {
        if (c <= 31 || c >= 128) continue;
        int glyph = exe_.u8(::bmh::layout::ClubInfo_Raw_RuntimeField3 + c);
        width += glyph ? exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField2 + glyph * 2) - exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField + glyph * 2) : 2;
    }
    return width;
}
void ManagerScreen::sprite_text(Image& out, int x, int y, std::string_view value, int font) const {
    for (unsigned char c : value) {
        if (c <= 31 || c >= 128) continue;
        int glyph = exe_.u8(::bmh::layout::ClubInfo_Raw_RuntimeField3 + c);
        if (!glyph) { x += 2; continue; }
        int sx = exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField + glyph * 2), w = exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField2 + glyph * 2) - sx;
        copy(out, letters_, sx, (font - 4) * 7, x, y - 6, w, 7, true); x += w;
    }
}
void ManagerScreen::button(Image& out, int x, int y, int w, std::string_view label, bool selected) const {
    if (selected) {
        const auto c = out.at(x, y + 10);
        rect(out, x, y, w + 1, 2, c); rect(out, x, y, 2, 11, c); x += 2; y += 2;
    } else {
        rect(out, x + 2, y + 10, w, 2, 0x40); rect(out, x + w, y + 2, 2, 10, 0x40);
    }
    int sy = selected ? 44 : 74;
    copy(out, widgets_, 24, sy, x, y, 31, 10);
    int k = 31;
    for (; w - k > 20;) {
        int n = std::min(8, w - k - 20);
        copy(out, widgets_, 24, sy + 10, x + k, y, n, 10); k += n;
    }
    copy(out, widgets_, 45 - (w - k), sy + 20, x + k, y, w - k, 10);
    int font = exe_.u8(::bmh::layout::Advertising_Button_RuntimeField + int(selected));
    sprite_text(out, x + w / 2 - sprite_text_width(label) / 2, y + 7, label, font);
}

Image ManagerScreen::render() const {
    Image out = background_;
    for (int i = 0; i < 12; ++i) {
        int x = (i < 6 ? 26 : 27) + (i % 6) * 45, y = i < 6 ? 151 : 197;
        portrait(out, x, y, i);
        if (portrait_locked(i)) copy(out, faces_, 228, 13, x + 5, y + 4, 28, 30, true);
    }
    portrait(out, 103, 77, choice_.portrait); badge(out, 186, 77, choice_.master_club);
    const std::array<std::string, 4> values = {choice_.name, leagues_[choice_.start_league],
        db_.club_names.at(choice_.master_club), texts_[(::bmh::layout::Manager_Render_TextPointer - ::bmh::layout::TextPointerTable) / 4 + choice_.strength]};
    for (int i = 0; i < 4; ++i) {
        copy(out, background_, field_x_[i], field_y_[i], field_x_[i], field_y_[i], field_width_[i], 11);
        // TextWidth3 refuses strings extending outside the framebuffer.
        if (text_x_[i] + small_.text_width(values[i]) <= 320) {
            if (i == 0 && view_ == ManagerView::name_edit)
                small_.draw(out, text_x_[i], baseline_[i] - small_.baseline, values[i], 0x5f);
            else small_text(out, text_x_[i], baseline_[i], values[i]);
        }
    }
    if (view_ == ManagerView::name_edit)
        rect(out, text_x_[0] + small_.text_width(choice_.name) + 2, baseline_[0] - 4, 3, 5, 0x5f);
    if (view_ == ManagerView::club_picker) {
        frame(out, 55, 10, 209, 224);
        for (int i = 0; i < 20; ++i) {
            int x = 60 + (i % 5) * 40, y = 15 + (i / 5) * 40, id = first_[group_] + i;
            if (id > last_[group_]) rect(out, x, y, 38, 38, 100);
            else {
                badge(out, x, y, master_id(id));
                if (club_locked(master_id(id))) copy(out, faces_, 228, 13, x + 5, y + 4, 28, 30, true);
                if (choice_.selection_club == id) copy(out, faces_, 228, 0, x + 24, y + 23, 11, 13, true);
            }
        }
        for (int i = 0; i < 7; ++i)
            button(out, i < 4 ? 64 : 161, 175 + (i < 4 ? i : i - 4) * 11, 94, leagues_[i], group_ == i);
        // Hover clear uses a new local pattern origin, even when no club is hovered.
        pattern(out, 81, 222, 151, 8);
        if (hover_ >= 0) {
            const auto& name = db_.club_names.at(master_id(hover_));
            small_text(out, 159 - small_.text_width(name) / 2, 228, name);
        }
    } else if (view_ == ManagerView::confirmation || view_ == ManagerView::accepted) {
        frame(out, 48, 84, 217, 99);
        large_.draw(out, 105 + (210 - 105) / 2 - large_.text_width(choice_.name) / 2,
                    125 - large_.baseline, choice_.name, 0x5f);
        copy(out, widgets_, 55, 66, 99, 90, 22, 21); copy(out, widgets_, 55, 66, 192, 90, 22, 21);
        rect(out, 53, 131, 207, 1, 105); rect(out, 53, 163, 207, 1, 105);
        frame(out, 52, 87, 42, 42, false); frame(out, 219, 87, 42, 42, false);
        badge(out, 54, 89, choice_.master_club); portrait(out, 221, 89, choice_.portrait);
        const auto& prompt = texts_[(::bmh::layout::Manager_SmallText_TextPointer - ::bmh::layout::TextPointerTable) / 4];
        small_text(out, 73 + 170 / 2 - small_.text_width(prompt) / 2, 140, prompt);
        button(out, 62, 166, 75, texts_[(::bmh::layout::GameEnd_Copy_TextPointer - ::bmh::layout::TextPointerTable) / 4], false);
        button(out, 175, 166, 75, texts_[(::bmh::layout::GameEnd_Copy_TextPointer2 - ::bmh::layout::TextPointerTable) / 4], false);
    }
    return out;
}

std::string manager_json(const ManagerChoice& c) {
    std::ostringstream out;
    out << "{\"slot\":" << c.slot << ",\"name\":" << json_quote(original_to_utf8(c.name))
        << ",\"master_club\":" << c.master_club << ",\"start_league\":" << c.start_league
        << ",\"strength\":" << c.strength << ",\"portrait\":" << c.portrait
        << ",\"selection_club\":" << c.selection_club << ",\"roster_revision\":" << c.roster_revision << '}';
    return out.str();
}

}  // namespace bmh::ui
