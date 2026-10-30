// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/save_load.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <system_error>

#include "ui/manager.hpp"
#include "ui/setup.hpp"
#include "formats/fbm.hpp"

namespace bmh::ui {
namespace {
constexpr uint32_t kTextBase = ::bmh::layout::TextPointerTable;
constexpr size_t kFilenameBytes = 251; // Host filename limit, including the four-byte .FBM suffix.
std::string path_to_utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return std::string(value.begin(), value.end());
}
std::filesystem::path path_from_utf8(std::string_view value) {
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}
bool valid_filename(std::string_view value) {
    return !value.empty() && value.size() <= kFilenameBytes && value != "." && value != ".." &&
        std::all_of(value.begin(), value.end(), [](unsigned char c) {
            return c >= 32 && c != 127 && c != '/' && c != '\\';
        });
}
std::string_view text_at(const TextDb& texts, uint32_t address) {
    if (address < kTextBase || ((address - kTextBase) & 3u) != 0)
        throw FormatError("save/load: invalid Original text address");
    return texts[(address - kTextBase) / 4];
}
void rect(Image& image, int x, int y, int w, int h, uint8_t color) {
    for (int yy = std::max(0, y); yy < std::min(image.height, y + h); ++yy)
        for (int xx = std::max(0, x); xx < std::min(image.width, x + w); ++xx) image.at(xx, yy) = color;
}
bool has_file_kind_extension(const std::filesystem::path& path, SaveLoadFileKind kind) {
    std::string ext = path_to_utf8(path.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return ext == (kind == SaveLoadFileKind::menu_layout ? ".men" :
                   kind == SaveLoadFileKind::settings ? ".ein" : ".fbm");
}
std::string_view file_kind_extension(SaveLoadFileKind kind) {
    return kind == SaveLoadFileKind::menu_layout ? ".MEN" :
           kind == SaveLoadFileKind::settings ? ".EIN" : ".FBM";
}
void copy(Image& out, const Image& src, int sx, int sy, int x, int y, int w, int h) {
    if (sx < 0 || sy < 0 || sx + w > src.width || sy + h > src.height)
        throw FormatError("save/load: Original widget outside atlas");
    for (int yy = 0; yy < h; ++yy) for (int xx = 0; xx < w; ++xx)
        if (x + xx >= 0 && x + xx < out.width && y + yy >= 0 && y + yy < out.height)
            out.at(x + xx, y + yy) = src.pixels[size_t(sy + yy) * size_t(src.width) + size_t(sx + xx)];
}
void pattern(Image& out, const LeImage& exe, int x, int y, int w, int h) {
    for (int yy = 0; yy < h; ++yy) for (int xx = 0; xx < w; ++xx)
        if (x + xx >= 0 && x + xx < out.width && y + yy >= 0 && y + yy < out.height)
            out.at(x + xx, y + yy) = exe.u8(::bmh::layout::Advertising_PatternRect_RuntimeField + uint32_t((yy & 15) * 16 + (xx & 15)));
}
std::string original_bytes(std::string_view value) {
    try { return utf8_to_original(value); }
    catch (const FormatError&) {
        std::string safe;
        for (unsigned char ch : value) safe.push_back(ch < 128 ? char(ch) : '?');
        return safe;
    }
}
}

SaveLoadScreen::SaveLoadScreen(const std::filesystem::path& data, std::filesystem::path directory, SaveLoadMode mode,
                               int manager_slot, uint64_t revision, Image backdrop, bool gradient_text,
                               SaveLoadFileKind file_kind, int origin_item_id)
    : directory_(std::filesystem::absolute(std::move(directory)).lexically_normal()), mode_(mode),
      file_kind_(file_kind), manager_slot_(manager_slot), origin_item_id_(origin_item_id),
      revision_(revision), backdrop_(std::move(backdrop)),
      font_(load_font(asset_path(data / "F", "BMH2.FNT"))),
      exe_(load_le(asset_path(data, "BMH.exe"))),
      widgets_(load_vga(asset_path(data / "KB", "0.VGA"))),
      texts_(load_textd(asset_path(data, "TEXTD.BLK"))), sprite_font_(data),
      footer_font_(load_font(asset_path(data / "F", "BMH3.FNT"))), gradient_text_(gradient_text) {
    if (backdrop_.width != 320 || backdrop_.height != 240 || manager_slot_ < -1 || manager_slot_ > 3)
        throw FormatError("save/load: unsupported state");
    if (widgets_.width != 77 || widgets_.height != 112 || font_.height != 8 || footer_font_.height != 6)
        throw FormatError("save/load: unsupported Original font or widget atlas");
    std::error_code ec;
    if (std::filesystem::is_directory(directory_, ec)) {
        for (std::filesystem::directory_iterator it(directory_, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code file_error;
            const auto status = it->symlink_status(file_error);
            if (!file_error && std::filesystem::is_regular_file(status) &&
                has_file_kind_extension(it->path(), file_kind_))
                entries_.push_back({it->path()});
        }
        std::sort(entries_.begin(), entries_.end(), [](const auto& a, const auto& b) {
            std::string left = path_to_utf8(a.path.filename()), right = path_to_utf8(b.path.filename());
            std::transform(left.begin(), left.end(), left.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            std::transform(right.begin(), right.end(), right.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return left < right;
        });
    } else feedback_ = "VERZEICHNIS NICHT VERFUEGBAR";
    if (mode_ == SaveLoadMode::save && !entries_.empty()) filename_ = path_to_utf8(entries_.front().path.stem());
    // OpenBMH deviates from the original: focuses the host save filename on entry and selects an existing name for replacement.
    filename_focus_ = mode_ == SaveLoadMode::save;
    filename_selected_ = filename_focus_ && !filename_.empty();
}

Image SaveLoadScreen::render() const {
    Image out = backdrop_;
    frame(out, 47, 15, 222, 209); // PatternStartsAt(...,0x2f,0x0f,0xde,0xd1,...,0x12)
    const std::string title = original_to_utf8(text_at(texts_, mode_ == SaveLoadMode::load ? ::bmh::layout::SaveLoad_Render_TextPointer5 : ::bmh::layout::SaveLoad_Render_TextPointer6));
    text(out, 49 + 217 / 2 - font_.text_width(original_bytes(title)) / 2, 34, title, 0x5f, 217, font_);
    rect(out, 51, 37, 214, 1, 0x69);     // Abs3(0x33,0x25,0x108,0x25,0x69)
    pattern(out, exe_, 53, 41, 147, 151); // PatternRect(0x35,0x29,199,0xbf,0)

    // OpenBMH deviates from the original: omits DOS drive and delete buttons in the directory-confined save/load view.
    // InfoPress also has a DOS delete control. Deletion is outside this
    // directory-confined slice, so preserve its panel background but omit it.

    // Original directory and filename controls (button rows 8/9).
    const int field_x = exe_.u8(::bmh::layout::SaveLoad_Render_RuntimeField + 8), directory_y = exe_.u8(::bmh::layout::SaveLoad_Render_RuntimeField2 + 8);
    const int field_width = exe_.u8(::bmh::layout::SaveLoad_Render_RuntimeField3 + 8), filename_y = exe_.u8(::bmh::layout::SaveLoad_Render_RuntimeField2 + 9);
    button(out, field_x, directory_y, field_width);
    button(out, field_x, filename_y, field_width);

    const auto directory_label = original_to_utf8(text_at(texts_, ::bmh::layout::SaveLoad_Render_TextPointer3)) + path_to_utf8(directory_);
    text(out, 55, 203, directory_label, 0x6c, 140, footer_font_, true);
    text(out, 55, 216, original_to_utf8(text_at(texts_, ::bmh::layout::SaveLoad_Render_TextPointer4)), 0x6c, 50, footer_font_, true);
    const std::string displayed_filename = mode_ == SaveLoadMode::load && selected_ >= 0 &&
        size_t(selected_) < entries_.size() ? path_to_utf8(entries_[size_t(selected_)].path.stem()) : filename_;
    text(out, 106, 216, displayed_filename, filename_selected_ ? 0x27 : 0x6c, 140, footer_font_, true);

    if (entries_.size() > 15) {
        button(out, 206, 183, 56, original_to_utf8(exe_.cstring(::bmh::layout::SaveLoad_Render_ExecutableField)));
    }

    for (int row = 0; row < 15; ++row) {
        const int at = scroll_ + row;
        if (at >= int(entries_.size())) break;
        const int baseline = 49 + row * 10;
        const std::string label = path_to_utf8(entries_[size_t(at)].path.stem());
        text(out, 55, baseline, label, at == selected_ ? 0x27 : 0x5f, 140, font_);
    }
    if (!feedback_.empty()) text(out, 52, 233, feedback_, 0x5f, 210, footer_font_, true);
    if (info_) {
        // Centered: PatternStartsAt(71,71,192,57,12,3), including ColorBottomRightBevel's
        // two-pixel shadow, then the literal native-header rows.
        rect(out, 73, 128, 192, 2, 0x40);
        rect(out, 263, 73, 2, 57, 0x40);
        frame(out, 71, 71, 192, 57);
        const auto centered = [&](int baseline, const std::string& value, uint8_t color) {
            auto bytes = original_bytes(value);
            // Host paths can exceed the Original DOS buffer. Keep the Original
            // centering inside 72..261 while bounding modern labels to the panel.
            while (!bytes.empty() && footer_font_.text_width(bytes) > 189) bytes.pop_back();
            text(out, 72 + 189 / 2 - footer_font_.text_width(bytes) / 2, baseline,
                 original_to_utf8(bytes), color, -1, footer_font_, true);
        };
        const auto heading = original_to_utf8(text_at(texts_, ::bmh::layout::SaveLoad_Render_TextPointer2)) +
            original_to_utf8(exe_.cstring(::bmh::layout::SaveLoad_Render_TextPointer)) + info_->path;
        // Centered2→TextWidth3 resets the selected list font to FNT3 before
        // Centered; its DrawnFirstBy calls retain that font and gradient.
        centered(81, heading, 0x6a);
        rect(out, 77, 83, 178, 1, 9);
        centered(90, original_to_utf8(text_at(texts_, ::bmh::layout::SaveLoad_Render_TextPointer7)) + info_->date, 0x6c);
        for (size_t i = 0; i < info_->rows.size(); ++i) {
            const auto& row = info_->rows[i];
            const int y = 99 + int(i) * 7;
            text(out, 76, y, row.manager, 0x6b, -1, footer_font_, true);
            text(out, 133, y, row.club, 0x6b, -1, footer_font_, true);
            text(out, 212, y, row.league, 0x6b, -1, footer_font_, true);
        }
    }
    return out;
}

bool SaveLoadScreen::info_press(int x, int y) {
    if (file_kind_ != SaveLoadFileKind::game || pending_ || info_ || x <= 52 || x >= 200 || y <= 40 ||
        y >= 192 || !select_row(y)) return false;
    try {
        const auto saved = load_savegame(entries_.at(size_t(selected_)).path);
        const auto& h = saved.plain_header;
        if (!saved.checksum_ok || saved.version != "V2.24" || h.size() < 172 || h[0] < 1 || h[0] > 4)
            throw FormatError("save/load: invalid native information header");
        const auto field = [&](size_t at, size_t count) {
            const auto end = h.begin() + at + count;
            const auto nul = std::find(h.begin() + at, end, 0);
            return original_to_utf8(std::string(h.begin() + at, nul));
        };
        FileInfo view;
        // Centered writes the terminator four bytes before the concatenated
        // path's end, removing the .FBM extension from the displayed title.
        // The configured host save directory acts as the DOS current directory;
        // show its relative filename here and its host path in the browser field.
        auto display_path = entries_[size_t(selected_)].path;
        display_path.replace_extension();
        view.path = path_to_utf8(display_path.filename());
        auto date = std::string(h.begin() + 1, h.begin() + 12);
        date.resize(date.find('\0') == std::string::npos ? date.size() : date.find('\0'));
        std::replace(date.begin(), date.end(), '\\', '_');
        view.date = original_to_utf8(date);
        for (size_t i = 0; i < h[0]; ++i) {
            const size_t at = 12 + i * 40;
            InfoRow row{field(at + 2, 19), field(at + 21, 17), {}};
            if (h[at + 1] == 0) {
                if (h[at] >= 9) throw FormatError("save/load: invalid native manager league");
                const auto league = exe_.u8(::bmh::layout::LeagueCalendarBits + h[at]);
                if (league != 30) {
                    row.league = original_to_utf8(text_at(texts_, ::bmh::layout::Achievements_FormatError_TextPointer3 + uint32_t(league) * 4));
                    auto raw = original_bytes(row.league);
                    while (!raw.empty() && footer_font_.text_width(raw) >= 52) raw.pop_back();
                    row.league = original_to_utf8(raw);
                }
            }
            view.rows.push_back(std::move(row));
        }
        info_ = std::move(view);
    } catch (const std::exception&) { feedback_ = "SPIELSTAND-INFORMATION NICHT LESBAR"; }
    return true;
}

void SaveLoadScreen::frame(Image& out, int x, int y, int width, int height) const {
    pattern(out, exe_, x + 4, y + 4, width - 7, height - 7);
    copy(out, widgets_, 0, 0, x, y, 4, 4);
    copy(out, widgets_, 4, 0, x + width - 4, y, 4, 4);
    copy(out, widgets_, 0, 4, x, y + height - 4, 4, 4);
    copy(out, widgets_, 4, 4, x + width - 4, y + height - 4, 4, 4);
    for (int at = 4; at < width - 4; at += 74) {
        const int count = std::min(74, width - 4 - at);
        copy(out, widgets_, 0, 104, x + at, y, count, 4);
        copy(out, widgets_, 0, 108, x + at, y + height - 4, count, 4);
    }
    for (int at = 4; at < height - 4; at += 96) {
        const int count = std::min(96, height - 4 - at);
        copy(out, widgets_, 0, 8, x, y + at, 4, count);
        copy(out, widgets_, 4, 8, x + width - 4, y + at, 4, count);
    }
}

void SaveLoadScreen::button(Image& out, int x, int y, int width, std::string_view label) const {
    // FormatError3 normal button bevel from KB/0.VGA, with Source horizontal tiling.
    const int sy = 74;
    copy(out, widgets_, 24, sy, x, y, 31, 10);
    int used = 31;
    while (width - used > 20) {
        const int count = std::min(8, width - used - 20);
        copy(out, widgets_, 24, sy + 10, x + used, y, count, 10);
        used += count;
    }
    copy(out, widgets_, 45 - (width - used), sy + 20, x + used, y, width - used, 10);
    if (!label.empty())
        sprite_font_.draw(out, x + width / 2 - sprite_font_.text_width(label) / 2, y + 7, label,
                          exe_.u8(::bmh::layout::Advertising_Button_RuntimeField));
}

void SaveLoadScreen::text(Image& out, int x, int baseline, std::string_view value,
                          uint8_t color, int max_width, const Font& font, bool gradient) const {
    std::string bytes = original_bytes(value);
    const int available = max_width < 0 ? out.width - x : max_width;
    while (!bytes.empty() && font.text_width(bytes) > available) bytes.pop_back();
    const int width = font.text_width(bytes);
    if (width <= 0 || x < 0 || x + width > out.width || baseline - font.baseline < 0 || baseline >= out.height) return;
    if (!gradient || !gradient_text_ || exe_.u8(::bmh::layout::TextGradientMode) != 2) {
        font.draw(out, x, baseline - font.baseline, bytes, color);
        return;
    }
    int ramp = 0;
    while (ramp < 32 && exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2Unsigned + uint32_t(ramp)) != color) ++ramp;
    if (ramp == 32) {
        font.draw(out, x, baseline - font.baseline, bytes, color);
        return;
    }
    Image mask(std::max(1, width), font.height);
    font.draw(mask, 0, 0, bytes, 1);
    const int top = baseline - font.baseline;
    for (int yy = 0; yy < font.height; ++yy)
        for (int xx = 0; xx < width; ++xx)
            if (mask.at(xx, yy)) out.at(x + xx, top + yy) = exe_.u8(::bmh::layout::Advertising_Text_RuntimeFieldUnsigned + uint32_t(ramp * 6 + std::min(yy, 5)));
}

bool SaveLoadScreen::select_row(int y) {
    if (y <= 40 || y >= 191) return false;
    const int row = scroll_ + (y - 40) / 10;
    if (row < 0 || row >= int(entries_.size())) return false;
    selected_ = row;
    if (mode_ == SaveLoadMode::save) filename_ = path_to_utf8(entries_[size_t(row)].path.stem());
    filename_focus_ = mode_ == SaveLoadMode::save;
    filename_selected_ = filename_focus_;
    overwrite_armed_ = false; feedback_.clear();
    return true;
}

void SaveLoadScreen::request_action() {
    if (pending_) return;
    if (mode_ == SaveLoadMode::load) {
        if (selected_ < 0 || selected_ >= int(entries_.size())) { feedback_ = "SPIELSTAND AUSWAEHLEN"; return; }
        pending_ = SaveLoadRequest{SaveLoadAction::load, manager_slot_, revision_,
                                   entries_[size_t(selected_)].path, false, file_kind_, origin_item_id_};
        return;
    }
    if (!valid_filename(filename_)) {
        feedback_ = "UNGUELTIGER DATEINAME"; return;
    }
    auto path = directory_ / path_from_utf8(filename_ + std::string(file_kind_extension(file_kind_)));
    // Selecting an existing mixed-case extension must overwrite that same file.
    if (selected_ >= 0 && size_t(selected_) < entries_.size() &&
        path_to_utf8(entries_[size_t(selected_)].path.stem()) == filename_)
        path = entries_[size_t(selected_)].path;
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec) { feedback_ = "DATEI NICHT PRUEFBAR"; return; }
    if (exists && !overwrite_armed_) { overwrite_armed_ = true; feedback_ = "EXISTIERT: ENTER ZUM ERSETZEN"; return; }
    pending_ = SaveLoadRequest{SaveLoadAction::save, manager_slot_, revision_, path, exists,
                               file_kind_, origin_item_id_};
}

bool SaveLoadScreen::click(int x, int y, bool right) {
    if (info_) { info_.reset(); return true; }
    if (pending_) return false;
    // OpenBMH deviates from the original: permits cancelling host file dialogs with the right mouse button.
    if (right) { close_requested_ = true; return true; }
    if (x >= 206 && y >= 60 && y <= 169)
        return false; // Source drive/delete controls have no host mapping in this slice.
    if (entries_.size() > 15 && x >= 206 && x <= 262 && y >= 183 && y <= 193) {
        const int pages = (int(entries_.size()) + 14) / 15;
        scroll_ = ((scroll_ / 15 + 1) % pages) * 15;
        selected_ = std::min(scroll_, int(entries_.size()) - 1);
        last_clicked_row_ = -1;
        return true;
    }
    if (x >= 53 && x <= 199 && select_row(y)) {
        const int row = scroll_ + (y - 40) / 10;
        if (mode_ == SaveLoadMode::load && row == last_clicked_row_) { request_action(); return true; }
        last_clicked_row_ = row;
        return true;
    }
    if (mode_ == SaveLoadMode::save && x >= 52 && x < 262 && y >= 209 && y < 219) {
        filename_focus_ = true; filename_selected_ = true; feedback_.clear(); return true;
    }
    if (x >= 52 && x < 262 && y >= 196 && y < 206) return false;
    return false;
}

bool SaveLoadScreen::key(std::string_view value) {
    if (info_) return false; // Centered waits for mouse release, not keyboard.
    if (pending_) return false;
    if (value == "ESC") { close_requested_ = true; return true; }
    if (mode_ == SaveLoadMode::load && !entries_.empty()) {
        if (value == "UP") { if (selected_ > 0) --selected_; if (selected_ < scroll_) scroll_ = selected_; return true; }
        if (value == "DOWN") { if (selected_ + 1 < int(entries_.size())) ++selected_; if (selected_ >= scroll_ + 15) scroll_ = selected_ - 14; return true; }
        if (value == "PAGEUP") { selected_ = std::max(0, selected_ - 15); scroll_ = std::max(0, scroll_ - 15); return true; }
        if (value == "PAGEDOWN") {
            selected_ = std::min(int(entries_.size()) - 1, selected_ + 15);
            scroll_ = std::max(0, std::min(selected_ - 14, int(entries_.size()) - 15));
            return true;
        }
    }
    if (mode_ == SaveLoadMode::save && filename_focus_) {
        if (value == "SELECT_ALL") { filename_selected_ = true; return true; }
        if (value == "BACKSPACE") {
            if (filename_selected_) filename_.clear();
            else if (!filename_.empty()) {
                size_t last = filename_.size() - 1;
                while (last > 0 && (uint8_t(filename_[last]) & 0xc0) == 0x80) --last;
                filename_.erase(last);
            }
            filename_selected_ = false; overwrite_armed_ = false; return true;
        }
        if (value == "ENTER") { request_action(); return true; }
    }
    if (value == "ENTER") { request_action(); return true; }
    return false;
}

bool SaveLoadScreen::text(std::string_view value) {
    if (info_ || mode_ != SaveLoadMode::save || !filename_focus_ || pending_ || value.empty()) return false;
    const std::string next = (filename_selected_ ? std::string{} : filename_) + std::string(value);
    if (!valid_filename(next)) return false;
    filename_ = next; filename_selected_ = false;
    overwrite_armed_ = false; feedback_.clear(); return true;
}

void SaveLoadScreen::acknowledge() {
    if (!pending_) throw FormatError("save/load: acknowledgement requires a pending action");
    pending_.reset(); close_requested_ = true;
}
void SaveLoadScreen::reject(std::string reason) {
    if (!pending_ || reason.empty()) throw FormatError("save/load: rejection requires a pending action and reason");
    pending_.reset(); feedback_ = std::move(reason);
}

std::string save_load_request_json(const SaveLoadRequest& r) {
    std::string out = "{\"action\":" + json_quote(r.action == SaveLoadAction::load ? "load" : "save") +
        ",\"manager_slot\":" + std::to_string(r.manager_slot) + ",\"revision\":" + std::to_string(r.revision) +
        ",\"path\":" + json_quote(path_to_utf8(r.path)) + ",\"overwrite\":" + (r.overwrite ? "true" : "false") + '}';
    if (r.file_kind == SaveLoadFileKind::menu_layout || r.file_kind == SaveLoadFileKind::settings) {
        out.pop_back();
        out += r.file_kind == SaveLoadFileKind::settings ? ",\"file_kind\":\"settings\",\"origin_item_id\":" :
            ",\"file_kind\":\"menu_layout\",\"origin_item_id\":";
        out +=
            std::to_string(r.origin_item_id) + '}';
    }
    return out;
}

std::string SaveLoadScreen::state_json() const {
    std::ostringstream out;
    out << "{\"screen\":\"save_load\",\"mode\":" << json_quote(mode_ == SaveLoadMode::load ? "load" : "save")
        << ",\"directory\":" << json_quote(path_to_utf8(directory_)) << ",\"manager_slot\":" << manager_slot_
        << ",\"revision\":" << revision_ << ",\"selected\":" << selected_ << ",\"scroll\":" << scroll_
        << ",\"filename\":" << json_quote(filename_) << ",\"entries\":[";
    for (size_t i = 0; i < entries_.size(); ++i) { if (i) out << ','; out << json_quote(path_to_utf8(entries_[i].path.filename())); }
    out << "],\"info_open\":" << (info_ ? "true" : "false")
        << ",\"pending_action\":" << (pending_ ? save_load_request_json(*pending_) : "null")
        << ",\"feedback\":" << (feedback_.empty() ? "null" : json_quote(feedback_));
    if (file_kind_ == SaveLoadFileKind::menu_layout || file_kind_ == SaveLoadFileKind::settings)
        out << ",\"file_kind\":" << json_quote(file_kind_ == SaveLoadFileKind::settings ? "settings" : "menu_layout")
            << ",\"origin_item_id\":" << origin_item_id_;
    out << '}';
    return out.str();
}

}  // namespace bmh::ui
