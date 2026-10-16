// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/mailbox_screen.hpp"

#include <algorithm>
#include <cstdio>
#include <set>
#include <sstream>

#include "ui/desk_state.hpp"
#include "ui/manager.hpp"
#include "ui/setup.hpp"
#include "ui/game_end.hpp"

namespace bmh::ui {
namespace {
constexpr int kX = 0x33, kY = 0x16, kWidth = 0xe1, kHeight = 0xca;

void copy(Image& out, const Image& source, int sx, int sy, int dx, int dy, int w, int h,
          bool transparent = false) {
    if (sx < 0 || sy < 0 || sx + w > source.width || sy + h > source.height)
        throw FormatError("mailbox: sprite outside original atlas");
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const auto c = source.pixels[size_t(sy + y) * source.width + sx + x];
        if ((!transparent || c) && dx + x >= 0 && dy + y >= 0 &&
            dx + x < out.width && dy + y < out.height) out.at(dx + x, dy + y) = c;
    }
}
}

MailboxScreen::MailboxScreen(const std::filesystem::path& data, MailboxState state,
                             std::string manager_name, Image desktop_background)
    : state_(std::move(state)), manager_name_(std::move(manager_name)),
      desktop_background_(std::move(desktop_background)),
      atlas_(load_vga(asset_path(data / "KB", "32.VGA"))),
      exe_(load_le(asset_path(data, "BMH.exe"))), texts_(load_textd(asset_path(data, "TEXTD.BLK"))),
      font_(load_font(asset_path(data / "F", "BMH3.FNT"))) {
    validate_state(state_);
    page_ = page_count() - 1; // MailboxStateFromGame opens the latest three-message page.
}

void MailboxScreen::validate_state(const MailboxState& state) const {
    if (state.manager_slot < 0 || state.manager_slot > 3 || state.messages.size() > 40 ||
        manager_name_.empty() || desktop_background_.width != 320 || desktop_background_.height != 240 ||
        atlas_.width != 225 || atlas_.height != 221)
        throw FormatError("mailbox: invalid owned state or original assets");
    (void)utf8_to_original(manager_name_);
    std::set<int> slots;
    for (size_t i = 0; i < state.messages.size(); ++i) {
        const auto& message = state.messages[i];
        if (message.entry_index >= 40 || !slots.insert(message.entry_index).second ||
            !(message.recipient_mask & (uint32_t(1) << state.manager_slot)) ||
            message.kept_for_manager != bool(message.kept_by_mask & (uint32_t(1) << state.manager_slot)) ||
            message.general_broadcast != bool(message.recipient_mask & 0x80000000u))
            throw FormatError("mailbox: inconsistent message identity or flags");
        (void)utf8_to_original(message.text);
        if (i) {
            const auto& before = state.messages[i - 1];
            if (before.date_counter > message.date_counter ||
                (before.date_counter == message.date_counter && before.entry_index >= message.entry_index))
                throw FormatError("mailbox: messages are not in original reader order");
        }
    }
}

size_t MailboxScreen::page_count() const { return std::max<size_t>(1, (state_.messages.size() + 2) / 3); }

void MailboxScreen::text(Image& out, int x, int baseline, std::string_view original,
                         uint8_t color, int right) const {
    std::string bytes(original);
    while (!bytes.empty() && font_.text_width(bytes) > right - x) bytes.pop_back();
    const int top = baseline - font_.baseline;
    if (x >= 0 && top >= 0 && top + font_.height <= out.height)
        font_.draw(out, x, top, bytes, color);
}

void MailboxScreen::body(Image& out, int x, int baseline, int right, int bottom,
                         std::string_view utf8, uint8_t color) const {
    // WrapSourceText selects flag2 for block alignment, then mode2 flushes the
    // unstretched final line. The same EXE-backed syllable rules serve notices.
    const auto lines=wrap_source_text(font_,exe_,utf8_to_original(utf8),right-x,true);
    for(const auto& line:lines) {
        if(baseline>=bottom)break;
        Image glyphs(std::max(1,font_.text_width(line)),font_.height,0);
        font_.draw(glyphs,0,0,line,1);
        const int top=baseline-font_.baseline;
        for(int yy=0;yy<font_.height;++yy)for(int xx=0;xx<glyphs.width;++xx)
            if(glyphs.at(xx,yy)&&x+xx>=0&&x+xx<=right&&x+xx<out.width&&
               top+yy>=0&&top+yy<bottom&&top+yy<out.height)out.at(x+xx,top+yy)=color;
        baseline+=7;
    }
}

Image MailboxScreen::render() const {
    auto out = desktop_background_;
    copy(out, atlas_, 0, 0, kX, kY, kWidth, kHeight);
    // ColorBottomRightBevel's two-pixel shadow and the original three-article dividers.
    for (int y = kY + kHeight; y <= kY + kHeight + 1; ++y)
        for (int x = kX + 2; x <= kX + kWidth + 1; ++x) out.at(x, y) = 0x40;
    for (int y = kY + 2; y <= kY + kHeight + 1; ++y)
        for (int x = kX + kWidth; x <= kX + kWidth + 1; ++x) out.at(x, y) = 0x40;
    for (int y = 0x35; y <= 0xc2; ++y) out.at(0xb8, y) = 0;
    for (int x = 0x36; x <= 0xb8; ++x) out.at(x, 0x78) = 0;
    for (size_t article = 0; article < 3; ++article) {
        const auto index = page_ * 3 + article;
        if (index >= state_.messages.size()) break;
        const auto& message = state_.messages[index];
        const int x = kX + exe_.u8(::bmh::layout::MailboxScreen_Click_RuntimeField5 + uint32_t(article));
        const int baseline = kY + 5 + exe_.u8(::bmh::layout::MailboxScreen_Click_RuntimeField6 + uint32_t(article));
        const int right = kX + exe_.u8(::bmh::layout::MailboxScreen_Click_RuntimeField7 + uint32_t(article));
        const int bottom = kY + exe_.u8(::bmh::layout::MailboxScreen_Click_RuntimeField8 + uint32_t(article));
        const std::string heading = (message.general_broadcast ? texts_[(::bmh::layout::MailboxScreen_Render_TextPointer - ::bmh::layout::TextPointerTable) / 4] :
            utf8_to_original(manager_name_)) + texts_[(::bmh::layout::MailboxScreen_Render_TextPointer2 - ::bmh::layout::TextPointerTable) / 4];
        text(out, x, baseline + 11, heading, message.general_broadcast ? 0 : 0xf3, right);
        std::string date;
        if (message.date_counter == 0xffff) date = exe_.cstring(::bmh::layout::Fltd_Layout_TextPointer5);
        else {
            const auto d = desk_date(message.date_counter, exe_);
            char value[32];
            std::snprintf(value, sizeof value, "%02d.%02d.%04d", d.day, d.month, d.year);
            date = value;
        }
        const uint8_t color = message.kept_for_manager ? 0xf3 : 0;
        text(out, right - font_.text_width(date), baseline, date, color, right);
        body(out, x, baseline + 22, right, bottom, message.text, color);
    }
    // SelectsTransparentIndex: the page digit atlas at (39,202), centered at (256,45).
    int value = int(page_ + 1), digits = value >= 10 ? 2 : 1;
    int cursor = 0x100 - 4 + 5 * digits - (5 * digits) / 2;
    do {
        copy(out, atlas_, 0x27 + value % 10 * 4, 0xca, cursor, kY + 0x17, 4, 5, true);
        value /= 10; cursor -= 5;
    } while (value);
    if (action_mode_ != MailboxActionKind::none) {
        const uint32_t index = action_mode_ == MailboxActionKind::toggle_keep ? 0 : 1;
        copy(out, atlas_, exe_.u8(::bmh::layout::MailboxScreen_Render_RuntimeField3 + index), 0xca,
             kX + exe_.u8(::bmh::layout::MailboxScreen_Render_RuntimeField + index), kY + exe_.u8(::bmh::layout::MailboxScreen_Render_RuntimeField2 + index),
             exe_.u8(::bmh::layout::MailboxScreen_Render_RuntimeField4 + index), 0x13);
    }
    if (!feedback_.empty()) text(out, kX + 4, 238, utf8_to_original(feedback_), 0x5f, 318);
    return out;
}

bool MailboxScreen::click(int x, int y, bool right) {
    if (close_requested_ || pending_ || x < 0 || y < 0 || x >= 320 || y >= 240) return false;
    if (right) { close_requested_ = true; return true; }
    if (action_mode_ != MailboxActionKind::none) for (uint32_t article = 0; article < 3; ++article) {
        if (x < kX + exe_.u8(::bmh::layout::MailboxScreen_Click_RuntimeField5 + article) || x > kX + exe_.u8(::bmh::layout::MailboxScreen_Click_RuntimeField7 + article) ||
            y < kY + exe_.u8(::bmh::layout::MailboxScreen_Click_RuntimeField6 + article) || y > kY + exe_.u8(::bmh::layout::MailboxScreen_Click_RuntimeField8 + article)) continue;
        const size_t index = page_ * 3 + article;
        if (index >= state_.messages.size()) return false;
        const auto& message = state_.messages[index];
        pending_ = MailboxActionRequest{action_mode_, state_.manager_slot, state_.revision,
                                       int(message.entry_index), message.raw_entry};
        feedback_.clear();
        return true;
    }
    for (int i = 0; i < 4; ++i) {
        if (x < kX + exe_.u8(::bmh::layout::MailboxScreen_Click_RuntimeField + i) || x > kX + exe_.u8(::bmh::layout::MailboxScreen_Click_RuntimeField3 + i) ||
            y < kY + exe_.u8(::bmh::layout::MailboxScreen_Click_RuntimeField2 + i) || y > kY + exe_.u8(::bmh::layout::MailboxScreen_Click_RuntimeField4 + i)) continue;
        if (i < 2) {
            page_ = i == 0 ? (page_ + page_count() - 1) % page_count() : (page_ + 1) % page_count();
            feedback_.clear();
        } else {
            const auto selected = i == 2 ? MailboxActionKind::toggle_keep : MailboxActionKind::delete_for_manager;
            action_mode_ = action_mode_ == selected ? MailboxActionKind::none : selected;
            feedback_.clear();
        }
        return true;
    }
    return false;
}

bool MailboxScreen::key(std::string_view value) {
    if (close_requested_ || pending_) return false;
    if (value == "ESC") { close_requested_ = true; return true; }
    if (value == "LEFT" || value == "RIGHT") {
        page_ = value == "LEFT" ? (page_ + page_count() - 1) % page_count() : (page_ + 1) % page_count();
        feedback_.clear(); return true;
    }
    return false;
}

void MailboxScreen::acknowledge(MailboxState state) {
    if (!pending_ || state.manager_slot != state_.manager_slot || state.revision <= state_.revision)
        throw FormatError("mailbox: stale action acknowledgement");
    validate_state(state);
    state_ = std::move(state);
    page_ = std::min(page_, page_count() - 1);
    pending_.reset(); feedback_.clear();
}

void MailboxScreen::reject(std::string reason) {
    pending_.reset(); feedback_ = std::move(reason);
}

std::string MailboxScreen::state_json() const {
    std::ostringstream out;
    out << "{\"screen\":\"mailbox\",\"manager_slot\":" << state_.manager_slot
        << ",\"read_only\":false,\"action_mode\":" << int(action_mode_)
        << ",\"action_pending\":" << (pending_ ? "true" : "false")
        << ",\"page\":" << page_ + 1 << ",\"page_count\":" << page_count()
        << ",\"message_count\":" << state_.messages.size() << ",\"close_requested\":"
        << (close_requested_ ? "true" : "false") << ",\"feedback\":" << json_quote(feedback_)
        << ",\"visible_entries\":[";
    bool comma = false;
    for (size_t i = page_ * 3; i < std::min(state_.messages.size(), page_ * 3 + 3); ++i) {
        if (comma) out << ',';
        comma = true;
        out << int(state_.messages[i].entry_index);
    }
    return out.str() + "]}";
}

} // namespace bmh::ui
