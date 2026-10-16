// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/training_camp.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <sstream>

#include "formats/binary.hpp"
#include "game/desk_actions.hpp"
#include "game/messages.hpp"
#include "ui/manager.hpp"
#include "ui/setup.hpp"
#include "ui/game_end.hpp"

namespace bmh::ui {
namespace {

constexpr size_t kRecord = 0xb0c;
constexpr size_t kTeam = 0x290;
constexpr size_t kFocus = 0xc1;
constexpr size_t kStays = 0x21f;
constexpr size_t kMoney = 0x314;

void copy(Image& out, const Image& src, int sx, int sy, int x, int y, int w, int h, int transparent = -1) {
    if (sx < 0 || sy < 0 || sx + w > src.width || sy + h > src.height)
        throw FormatError("training camp: sprite outside original atlas");
    for (int yy = 0; yy < h; ++yy) for (int xx = 0; xx < w; ++xx) {
        const uint8_t color = src.pixels[size_t(sy + yy) * src.width + sx + xx];
        if (x + xx >= 0 && y + yy >= 0 && x + xx < out.width && y + yy < out.height && color != transparent)
            out.at(x + xx, y + yy) = color;
    }
}
void rect(Image& out, int x, int y, int w, int h, uint8_t color) {
    for (int yy = std::max(0, y); yy < std::min(out.height, y + h); ++yy)
        for (int xx = std::max(0, x); xx < std::min(out.width, x + w); ++xx) out.at(xx, yy) = color;
}
int32_t rd32(const std::vector<uint8_t>& v, size_t at) {
    return int32_t(uint32_t(v.at(at)) | uint32_t(v.at(at + 1)) << 8 | uint32_t(v.at(at + 2)) << 16 |
                   uint32_t(v.at(at + 3)) << 24);
}
uint16_t rd16(const std::vector<uint8_t>& v, size_t at) {
    return uint16_t(v.at(at) | uint16_t(v.at(at + 1)) << 8);
}
const char* feedback_name(TrainingCampFeedback feedback) {
    switch (feedback) {
    case TrainingCampFeedback::none: return "none";
    case TrainingCampFeedback::booked: return "booked";
    case TrainingCampFeedback::insufficient_funds: return "insufficient_funds";
    case TrainingCampFeedback::booked_this_week: return "booked_this_week";
    case TrainingCampFeedback::visit_limit: return "visit_limit";
    }
    return "none";
}
const char* request_kind_name(TrainingCampRequestKind kind) {
    switch (kind) {
    case TrainingCampRequestKind::none: return "none";
    case TrainingCampRequestKind::set_focus: return "set_focus";
    case TrainingCampRequestKind::book: return "book";
    }
    return "none";
}

}  // namespace

TrainingCampState training_camp_from_game(const GameState& g, const LeImage& exe, int manager, uint64_t revision) {
    if (g.manager_teams.size() > 4 || manager < 0 || size_t(manager) >= g.manager_teams.size())
        throw FormatError("training camp: invalid manager slot");
    TrainingCampState state;
    const size_t record = size_t(manager) * kRecord;
    const int team = g.manager_teams[size_t(manager)];
    state.revision = revision;
    state.source = "engine_projection";
    for (int resort = 0; resort < 8; ++resort) {
        state.prices[size_t(resort)] = training_camp_price(g, manager, resort);
        state.quality[size_t(resort)] = g.training_camp_quality[size_t(resort)];
    }
    state.currency_multiplier = std::bit_cast<int32_t>(g.memory.has(::bmh::layout::CurrencyMultiplier) ?
        g.memory.u32(::bmh::layout::CurrencyMultiplier) : exe.u32(::bmh::layout::CurrencyMultiplier));
    state.currency_divisor = std::bit_cast<int32_t>(g.memory.has(::bmh::layout::CurrencyDivisor) ?
        g.memory.u32(::bmh::layout::CurrencyDivisor) : exe.u32(::bmh::layout::CurrencyDivisor));
    state.currency_suffix = g.currency_suffix;
    state.gradient_text = g.memory.u8(::bmh::layout::TextGradientDisabled) == 0;
    state.manager_slot = manager;
    state.runtime_club = team;
    state.club_name = g.club_name(team);
    for (size_t i = 0; i < 26; ++i) {
        const char c = char(g.manager_records.at(record + 0xa8 + i));
        if (!c) break;
        state.manager_name.push_back(c);
    }
    state.portrait = g.manager_records.at(record + 0x221);
    state.badge = uint16_t(rd16(g.club_heap_78, size_t(team) * 2) & 0x1ff);
    state.stays = g.manager_records.at(record + kStays);
    state.balance = rd32(g.manager_records, record + kMoney);
    for (size_t i = 0; i < state.focus.size(); ++i)
        state.focus[i] = g.manager_records.at(record + kFocus + i);
    for (uint8_t value : state.focus) if (value > 15) throw FormatError("training camp: focus outside 0..15");
    const uint8_t derived = uint8_t(15 - (unsigned(state.focus[0]) + state.focus[1] + state.focus[2]) / 3);
    state.focus_normalization_required = state.focus[3] != derived;
    state.focus[3] = derived; // SetTrainingCampFocus derives slider four from the first three while drawing.
    return state;
}

std::string training_camp_price_text(const TrainingCampState& state, int resort) {
    const uint32_t product = uint32_t(state.prices.at(size_t(resort))) * uint32_t(state.currency_multiplier);
    const int32_t signed_product = std::bit_cast<int32_t>(product);
    if (state.currency_divisor == 0 ||
        (signed_product == std::numeric_limits<int32_t>::min() && state.currency_divisor == -1))
        throw FormatError("training camp: invalid currency division");
    // Source 0x186 uses six number positions; '%' is the invisible five-pixel
    // padding glyph of BMH3.FNT, including FLTD's five-digit cheapest stay.
    const std::string amount = format_number(GameState{}, signed_product / state.currency_divisor, 0x86);
    // The loaded pound sign is the '&' glyph. FormatAmount prefixes it in FLTD;
    // currencies whose loaded text is a suffix retain the German rendering.
    if (state.currency_suffix == " &") return "& " + amount;
    return amount + state.currency_suffix;
}

std::string training_camp_json(const TrainingCampState& s) {
    std::ostringstream out;
    out << "{\"screen\":\"training_camp\",\"source\":" << json_quote(s.source)
        << ",\"manager_slot\":" << s.manager_slot << ",\"runtime_club\":" << s.runtime_club
        << ",\"revision\":" << s.revision << ",\"club_name\":" << json_quote(original_to_utf8(s.club_name))
        << ",\"manager_name\":" << json_quote(original_to_utf8(s.manager_name)) << ",\"portrait\":" << int(s.portrait)
        << ",\"badge\":" << s.badge
        << ",\"focus\":[" << int(s.focus[0]) << ',' << int(s.focus[1]) << ',' << int(s.focus[2]) << ',' << int(s.focus[3])
        << "],\"stays\":" << int(s.stays) << ",\"visit_count\":" << s.visit_count()
        << ",\"focus_normalization_required\":" << (s.focus_normalization_required ? "true" : "false")
        << ",\"booked_this_week\":" << (s.booked_this_week() ? "true" : "false")
        << ",\"booking_allowed\":" << (s.booking_allowed() ? "true" : "false")
        << ",\"balance\":" << s.balance << ",\"prices\":[";
    for (size_t i = 0; i < s.prices.size(); ++i) { if (i) out << ','; out << s.prices[i]; }
    out << "],\"currency_multiplier\":" << s.currency_multiplier
        << ",\"currency_divisor\":" << s.currency_divisor
        << ",\"currency_suffix\":" << json_quote(s.currency_suffix)
        << ",\"feedback\":" << json_quote(feedback_name(s.feedback)) << '}';
    return out.str();
}

std::string training_camp_request_json(const TrainingCampRequest& r) {
    std::ostringstream out;
    out << "{\"kind\":" << json_quote(request_kind_name(r.kind)) << ",\"manager_slot\":" << r.manager_slot
        << ",\"runtime_club\":" << r.runtime_club << ",\"revision\":" << r.revision
        << ",\"resort\":" << r.resort << ",\"focus\":[" << int(r.focus[0]) << ',' << int(r.focus[1])
        << ',' << int(r.focus[2]) << ',' << int(r.focus[3]) << "]}";
    return out.str();
}

TrainingCampScreen::TrainingCampScreen(const std::filesystem::path& data, TrainingCampState state)
    : state_(std::move(state)), exe_(load_le(asset_path(data, "BMH.exe"))), texts_(load_textd(asset_path(data, "TEXTD.BLK"))),
      background_(load_vga(asset_path(data / "KB", "129.VGA"))), map_(load_vga(asset_path(data / "KB", "12.VGA"))),
      resorts_one_(load_vga(asset_path(data / "KB", "8.VGA"))), resorts_two_(load_vga(asset_path(data / "KB", "9.VGA"))),
      widgets_(load_vga(asset_path(data / "KB", "52.VGA"))), buttons_(load_vga(asset_path(data / "KB", "0.VGA"))),
      letters_(load_vga(asset_path(data / "KB", "54.VGA"))), portraits_(load_vga(asset_path(data / "KB", "26.VGA"))),
      badge_(load_vga(asset_path(data / "W", std::to_string(state_.badge >> 3) + ".VGA"))),
      small_(load_font(asset_path(data / "F", "BMH3.FNT"))), large_(load_font(asset_path(data / "F", "BMH2.FNT"))), sprite_font_(data) {
    if (state_.manager_slot < 0 || state_.manager_slot > 3 || state_.runtime_club < 0 ||
        state_.manager_name.empty() || state_.portrait > 11 || state_.badge > 0x1ff ||
        state_.source.empty() || background_.width != 320 || background_.height != 240 ||
        map_.width != 185 || map_.height != 206 || resorts_one_.width != 224 || resorts_one_.height != 136 ||
        resorts_two_.width != 224 || resorts_two_.height != 136)
        throw FormatError("training camp: unsupported state/assets");
    for (uint8_t focus : state_.focus) if (focus > 15) throw FormatError("training camp: focus outside 0..15");
    if (state_.currency_suffix.empty()) {
        state_.currency_multiplier = std::bit_cast<int32_t>(exe_.u32(::bmh::layout::CurrencyMultiplier));
        state_.currency_divisor = std::bit_cast<int32_t>(exe_.u32(::bmh::layout::CurrencyDivisor));
        state_.currency_suffix = exe_.cstring(::bmh::layout::CurrencySuffixText);
    }
}

void TrainingCampScreen::text(Image& out, int x, int baseline, std::string_view value, uint8_t color, int right) const {
    const int width = small_.text_width(value);
    if (right >= 0) x += (right - x) / 2 - width / 2;
    if (x < 0 || x + width > out.width || baseline < 5 || baseline >= out.height) return;
    Image mask(std::max(1, width), 6); small_.draw(mask, 0, 0, value, 1);
    int gradient = 0;
    while (gradient < 32 && exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2 + gradient) != color) ++gradient;
    for (int yy = 0; yy < 6; ++yy) for (int xx = 0; xx < width; ++xx)
        if (mask.at(xx, yy)) out.at(x + xx, baseline - 5 + yy) = exe_.u8(::bmh::layout::Advertising_Text_RuntimeField + gradient * 6 + yy);
}

void TrainingCampScreen::sprite_text(Image& out, int x, int baseline, std::string_view value, int font) const {
    for (unsigned char c : value) {
        if (c <= 31 || c >= 128) continue;
        const int glyph = exe_.u8(::bmh::layout::ClubInfo_Raw_RuntimeField3 + c);
        if (!glyph) { x += 2; continue; }
        const int sx = exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField + glyph * 2), width = exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField2 + glyph * 2) - sx;
        copy(out, letters_, sx, (font - 4) * 7, x, baseline - 6, width, 7, 0); x += width;
    }
}

void TrainingCampScreen::resort_details(Image& out, int resort) const {
    const auto name = texts_[371 + size_t(resort)];
    text(out, 198, 101, name, 0x5f, 314);
    const std::string price_line = training_camp_price_text(state_, resort);
    text(out, 268, 148, price_line, 0x5f);
    for (int quality = 0; quality < 4; ++quality) {
        const int count = state_.quality[size_t(resort)][size_t(quality)];
        for (int star = 0; star < count; ++star)
            copy(out, widgets_, 0, 17, 268 + star * 9, exe_.u8(::bmh::layout::TrainingCamp_ResortDetails_RuntimeField + quality), 9, 8, 0xc0);
    }
}

void TrainingCampScreen::focus_controls(Image& out) const {
    // BookTrainingCamp draws all nine fixed labels before any resort is hovered.
    // DrawnFirstBy's 0x40 mode draws an ordinary font-3 shadow at +2,+2.
    for (int i = 0; i < 9; ++i) {
        const int baseline = exe_.u8(::bmh::layout::TrainingCamp_FocusControls_RuntimeField2 + uint32_t(i));
        const auto& label = texts_[243 + size_t(i)];
        small_.draw(out, 200, baseline + 2 - small_.baseline, label, 0x40);
        text(out, 198, baseline, label, exe_.u8(::bmh::layout::TrainingCamp_FocusControls_TextPointer + uint32_t(i)));
    }
    // SetTrainingCampFocus paints a complete rail first, then the current focus handle.
    for (int i = 0; i < 4; ++i) {
        const int y = exe_.u8(::bmh::layout::TrainingCamp_FocusControls_RuntimeField + i);
        copy(out, widgets_, 0, 0, 255, y, 52, 9, 0xc0);
        // PathTreatsActive -> UsesForTheseSprites uses DesktopRuntimeField=0 as the transparency key.
        copy(out, widgets_, 0, 9, 255 + int(state_.focus[size_t(i)]) * 3, y, 16, 8, 0);
    }
}

Image TrainingCampScreen::render() const {
    Image out = background_;
    copy(out, map_, 0, 0, 1, 19, map_.width, map_.height, 0xc0);
    if (preview_resort_ >= 0) {
        const Image& atlas = preview_resort_ < 4 ? resorts_one_ : resorts_two_;
        const int cell = preview_resort_ % 4;
        copy(out, atlas, (cell % 2) * 110, (cell / 2) * 68, 198, 25, 110, 68, 0xc0);
        // Text -> ColorBottomRightBevel keeps the photograph's two-pixel shadow.
        rect(out, 200, 93, 110, 2, 0x40);
        rect(out, 308, 27, 2, 68, 0x40);
    }
    if (selected_resort_ >= 0) resort_details(out, selected_resort_);
    focus_controls(out);
    sprite_text(out, 188, 13, texts_[1264], 5);
    sprite_text(out, 5, 235, state_.club_name, 5);
    const std::string visit_text = state_.booked_this_week() ? texts_[1263] :
        (state_.visit_count() < 3 ? std::string(texts_[1260]) + std::to_string(3 - state_.visit_count()) + texts_[1261] +
            (state_.visit_count() < 2 ? exe_.cstring(::bmh::layout::Bank_OriginalToUtf8_TextPointer) : std::string{}) + texts_[1262] : texts_[1263]);
    sprite_text(out, 104, 235, visit_text, 5);
    if (chosen_resort_ >= 0 && state_.booking_allowed()) {
        constexpr int x = 223, y = 214, width = 64, height = 10;
        rect(out, x + 2, y + height, width, 2, 0x40); rect(out, x + width, y + 2, 2, height, 0x40);
        copy(out, buttons_, 24, 74, x, y, 31, height);
        int at = 31, remaining = width - 31;
        while (remaining > 20) { const int n = std::min(8, remaining - 20); copy(out, buttons_, 24, 84, x + at, y, n, height); at += n; remaining -= n; }
        copy(out, buttons_, 45 - remaining, 94, x + at, y, remaining, height);
        const int label_width = small_.text_width(texts_[1267]);
        text(out, x + width / 2 - label_width / 2, y + 7, texts_[1267], 0);
    }
    if (!rejection_.empty() || state_.feedback != TrainingCampFeedback::none) {
        std::string message;
        if (state_.feedback == TrainingCampFeedback::booked) message = texts_[1259];
        else if (state_.feedback == TrainingCampFeedback::insufficient_funds) message = texts_[837];
        else if (state_.feedback == TrainingCampFeedback::booked_this_week) message = texts_[1265];
        else if (state_.feedback == TrainingCampFeedback::visit_limit) message = texts_[1266];
        else if (!rejection_.empty()) message = rejection_;
        TaxDialogProjection notice;
        notice.manager_name=state_.manager_name;
        notice.portrait=state_.portrait;
        notice.badge_sprite=state_.badge;
        notice.badge_fallback=source_badge_fallback(state_.runtime_club);
        notice.body=message;
        // TrainingCampEffect ->SystemIdentity acknowledges success; SDLFlushEvents3 warnings are timed.
        notice.source_manager_flags=state_.feedback==TrainingCampFeedback::booked ||
            state_.feedback==TrainingCampFeedback::none ? 0x80 : 0x40;
        draw_source_dialog(out,{buttons_,portraits_,&badge_,small_,large_,exe_,sprite_font_,texts_},
            notice,state_.gradient_text);
    }
    return out;
}

TrainingCampRequest TrainingCampScreen::request(TrainingCampRequestKind kind, int resort,
                                                const std::array<uint8_t, 4>& focus) {
    return {kind, state_.manager_slot, state_.runtime_club, state_.revision, resort, focus};
}

TrainingCampRequest TrainingCampScreen::initialize_focus() {
    if (!state_.focus_normalization_required || pending_ || state_.source != "engine_projection") return {};
    pending_ = request(TrainingCampRequestKind::set_focus, -1, state_.focus);
    return *pending_;
}

TrainingCampRequest TrainingCampScreen::click(int x, int y, bool right) {
    if (pending_) return {};
    if (right && timed_notice_open()) {
        rejection_.clear(); state_.feedback=TrainingCampFeedback::none; return {};
    }
    if (right) { close_requested_ = true; return {}; }
    if (x < 0 || y < 0 || x >= 320 || y >= 240) return {};
    if (!rejection_.empty() || state_.feedback != TrainingCampFeedback::none) {
            if(state_.feedback==TrainingCampFeedback::booked)close_requested_=true;
        rejection_.clear(); state_.feedback = TrainingCampFeedback::none;
        return {};
    }
    if (chosen_resort_ >= 0 && x >= 223 && x < 287 && y >= 214 && y < 224) {
        if (state_.booked_this_week()) { state_.feedback = TrainingCampFeedback::booked_this_week; return {}; }
        if (state_.visit_count() >= 3) { state_.feedback = TrainingCampFeedback::visit_limit; return {}; }
        pending_ = request(TrainingCampRequestKind::book, chosen_resort_, state_.focus);
        rejection_.clear(); return *pending_;
    }
    for (int resort = 0; resort < 8; ++resort) {
        const int hx = exe_.u8(::bmh::layout::TrainingCamp_Click_RuntimeField + uint32_t(resort)), hy = exe_.u8(::bmh::layout::TrainingCamp_Click_RuntimeField2 + uint32_t(resort));
        if (x >= hx && x <= hx + 0x13 && y >= hy && y <= hy + 0x11) {
            selected_resort_ = resort;
            preview_resort_ = resort;
            if (state_.booked_this_week()) { state_.feedback = TrainingCampFeedback::booked_this_week; return {}; }
            if (state_.visit_count() >= 3) { state_.feedback = TrainingCampFeedback::visit_limit; return {}; }
            chosen_resort_ = resort;
            return {};
        }
    }
    // BookTrainingCamp accepts focus input independently of local_1c (chosen resort).
    // It clamps x=241..306 into 255..300 and divides the y range into 13px rows.
    if (x > 240 && x < 307 && y > 160 && y < 226) {
        const int i = (y - 161) / 13;
        // Only the first three slots are independent; SetTrainingCampFocus derives the fourth.
        if (i < 3) {
            auto desired = state_.focus;
            desired[size_t(i)] = uint8_t(std::clamp(x, 255, 300) - 255) / 3;
            desired[3] = uint8_t(15 - (unsigned(desired[0]) + desired[1] + desired[2]) / 3);
            if (desired == state_.focus) return {};
            pending_ = request(TrainingCampRequestKind::set_focus, -1, desired);
            rejection_.clear(); return *pending_;
        }
    }
    return {};
}

bool TrainingCampScreen::expire_timed_notice() {
    if(!timed_notice_open())return false;
    state_.feedback=TrainingCampFeedback::none;rejection_.clear();return true;
}

TrainingCampRequest TrainingCampScreen::key(std::string_view key) {
    if (pending_) return {};
    if (key == "ESC" || key == "ESCAPE" || key == "BACKSPACE") {
        close_requested_ = true; return {};
    }
    if (key == "ENTER" || key == "SPACE") {
        if (!rejection_.empty() || state_.feedback != TrainingCampFeedback::none) {
            if(state_.feedback==TrainingCampFeedback::booked)close_requested_=true;
            rejection_.clear(); state_.feedback = TrainingCampFeedback::none; return {};
        }
        if (chosen_resort_ < 0) return {};
        if (state_.booked_this_week()) { state_.feedback = TrainingCampFeedback::booked_this_week; return {}; }
        if (state_.visit_count() >= 3) { state_.feedback = TrainingCampFeedback::visit_limit; return {}; }
        pending_ = request(TrainingCampRequestKind::book, chosen_resort_, state_.focus);
        rejection_.clear(); return *pending_;
    }
    return {};
}

bool TrainingCampScreen::move(int x, int y) {
    if(state_.feedback!=TrainingCampFeedback::none||!rejection_.empty())return false;
    if (pending_ || chosen_resort_ >= 0) return false;
    int resort_under_pointer = -1;
    for (int resort = 0; resort < 8; ++resort) {
        const int hx = exe_.u8(::bmh::layout::TrainingCamp_Click_RuntimeField + uint32_t(resort)), hy = exe_.u8(::bmh::layout::TrainingCamp_Click_RuntimeField2 + uint32_t(resort));
        if (x >= hx && x <= hx + 0x13 && y >= hy && y <= hy + 0x11) {
            resort_under_pointer = resort;
            break;
        }
    }
    // BookTrainingCamp -> Text(-1) clears temporary details but leaves the last photo.
    if (selected_resort_ == resort_under_pointer) return false;
    selected_resort_ = resort_under_pointer;
    if (resort_under_pointer >= 0) preview_resort_ = resort_under_pointer;
    return true;
}

void TrainingCampScreen::acknowledge(TrainingCampState state) {
    if (!pending_ || state.revision <= state_.revision || state.manager_slot != state_.manager_slot ||
        state.runtime_club != state_.runtime_club || state.source != "engine_projection")
        throw FormatError("training camp acknowledgement: stale or mismatched projection");
    for (uint8_t focus : state.focus) if (focus > 15) throw FormatError("training camp acknowledgement: focus outside 0..15");
    const auto answered = pending_->kind;
    if (answered == TrainingCampRequestKind::set_focus && (state.focus != pending_->focus || state.focus_normalization_required))
        throw FormatError("training camp acknowledgement: focus does not match request");
    if (answered == TrainingCampRequestKind::book) {
        if (state.visit_count() != state_.visit_count() + 1 || !state.booked_this_week())
            throw FormatError("training camp acknowledgement: booking effect is missing");
        state.feedback = TrainingCampFeedback::booked;
    }
    state_ = std::move(state); pending_.reset(); rejection_.clear();
}

void TrainingCampScreen::reject_action(std::string reason) {
    if (!pending_) throw FormatError("training camp rejection: no pending request");
    // The controller's failure path does not mutate state or advance its revision.
    if (reason == "insufficient_funds") state_.feedback = TrainingCampFeedback::insufficient_funds;
    else if (reason == "booked_this_week") state_.feedback = TrainingCampFeedback::booked_this_week;
    else if (reason == "visit_limit") state_.feedback = TrainingCampFeedback::visit_limit;
    rejection_ = std::move(reason);
    pending_.reset();
}

std::string TrainingCampScreen::state_json() const {
    auto json = training_camp_json(state_);
    json.pop_back();
    json += ",\"selected_resort\":" + std::to_string(selected_resort_) +
            ",\"preview_resort\":" + std::to_string(preview_resort_) +
            ",\"chosen_resort\":" + std::to_string(chosen_resort_) +
            ",\"pending\":" + (pending_ ? training_camp_request_json(*pending_) : "null") +
            ",\"rejection\":" + json_quote(original_to_utf8(rejection_)) + "}";
    return json;
}

}  // namespace bmh::ui
