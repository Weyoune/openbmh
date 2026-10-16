// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/top_scorers.hpp"

#include <algorithm>
#include <sstream>

#include "ui/manager.hpp"

namespace bmh::ui {
namespace {
constexpr uint32_t kTextBase = ::bmh::layout::TextPointerTable;

std::string_view source_text(const TextDb& texts, uint32_t address) {
    if (address < kTextBase || ((address - kTextBase) & 3U) != 0)
        throw FormatError("top scorers: invalid Original text address");
    const size_t index = size_t((address - kTextBase) / 4);
    if (index >= texts.strings.size()) throw FormatError("top scorers: Original text address out of range");
    return texts[index];
}

void copy(Image& out, const Image& src, int sx, int sy, int x, int y, int w, int h) {
    if (sx < 0 || sy < 0 || x < 0 || y < 0 || w < 0 || h < 0 ||
        sx + w > src.width || sy + h > src.height || x + w > out.width || y + h > out.height)
        throw FormatError("top scorers: Original sprite bounds");
    for (int yy = 0; yy < h; ++yy)
        for (int xx = 0; xx < w; ++xx)
            out.at(x + xx, y + yy) = src.pixels[size_t(sy + yy) * size_t(src.width) + size_t(sx + xx)];
}

void fill(Image& out, int x, int y, int w, int h, uint8_t color) {
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx)
            out.at(xx, yy) = color;
}

std::string source_number(unsigned value, unsigned width = 0) {
    auto out = std::to_string(value);
    if (out.size() < width) out.insert(0, size_t(width - out.size()), '%');
    return out;
}

uint32_t metric_text_address(TopScorersMetric metric) {
    switch (metric) {
    case TopScorersMetric::goals: return ::bmh::layout::TopScorers_MetricTextAddress_ExecutableField;   // TEXTD 266
    case TopScorersMetric::assists: return ::bmh::layout::TopScorers_MetricTextAddress_ExecutableField2;  // TEXTD 267
    case TopScorersMetric::scorer: return ::bmh::layout::TopScorers_MetricTextAddress_ExecutableField3;   // TEXTD 268
    }
    throw FormatError("top scorers: invalid metric");
}

uint32_t scope_text_address(TopScorersScope scope) {
    switch (scope) {
    case TopScorersScope::league: return ::bmh::layout::TopScorers_ScopeTextAddress_ExecutableField;   // TEXTD 269
    case TopScorersScope::managers: return ::bmh::layout::TopScorers_ScopeTextAddress_ExecutableField2; // TEXTD 270
    }
    throw FormatError("top scorers: invalid scope");
}
} // namespace

TopScorersScreen::TopScorersScreen(const std::filesystem::path& data, TopScorersState state)
    : exe_(load_le(asset_path(data, "BMH.exe"))),
      texts_(load_textd(asset_path(data, "TEXTD.BLK"))),
      frame_(load_vga(asset_path(data / "KB", "129.VGA"))),
      podium_(load_vga(asset_path(data / "KB", "18.VGA"))),
      button_art_(load_vga(asset_path(data / "KB", "0.VGA"))),
      font_(load_font(asset_path(data / "F", "BMH3.FNT"))), sprite_(data),
      state_(std::move(state)) {
    const auto& id = state_.identity;
    const auto& projection = state_.projection;
    if (id.manager_slot < 0 || id.manager_slot > 3 || id.runtime_team < 0 ||
        id.club_identity < 0 || id.league < 0 || id.league > 8 || !id.scope ||
        projection.manager_slot != id.manager_slot || projection.league != id.league ||
        projection.metric != top_scorers_metric_to_game(state_.metric) ||
        projection.scope != top_scorers_scope_to_game(state_.scope) ||
        id.expected_settings_word != 0 || id.expected_family_word != 0 ||
        projection.rows.size() > 10 || (projection.no_results != projection.rows.empty()))
        throw FormatError("top scorers: incomplete or inconsistent projection");
    if (frame_.width < 320 || frame_.height < 240 || podium_.width < 112 || podium_.height < 70)
        throw FormatError("top scorers: Original frame asset dimensions");
    if (state_.cached_backdrop && (state_.underlay.width != 320 || state_.underlay.height != 240))
        throw FormatError("top scorers: cached Source frame is not 320 by 240");
    (void)render();
}

void TopScorersScreen::draw_text(Image& out, int x, int baseline, std::string_view bytes,
                                uint8_t color) const {
    const int width = font_.text_width(bytes);
    const int top = baseline - font_.baseline;
    if (x < 0 || top < 0 || baseline >= out.height || x + width + 2 > out.width ||
        top + font_.height + 2 > out.height) return;
    Image mask(std::max(1, width), font_.height);
    font_.draw(mask, 0, 0, bytes, 1);
    const bool gradient = state_.gradient_text;
    int index = 0;
    while (index < 32 && exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2 + uint32_t(index)) != color) ++index;
    // DrawnFirstBy flag 0x40 draws the Original two-pixel dark offset first.
    for (int yy = 0; yy < font_.height; ++yy)
        for (int xx = 0; xx < width; ++xx)
            if (mask.at(xx, yy))
                out.at(x + xx + 2, baseline - font_.baseline + yy + 2) = 0x40;
    for (int yy = 0; yy < font_.height; ++yy)
        for (int xx = 0; xx < width; ++xx)
            if (mask.at(xx, yy))
                out.at(x + xx, baseline - font_.baseline + yy) = gradient && index < 32
                    ? exe_.u8(::bmh::layout::Advertising_Text_RuntimeField + uint32_t(index * 6 + yy)) : color;
}

void TopScorersScreen::draw_button(Image& out, int x, int y, int width,
                                   std::string_view original_label, bool pressed) const {
    const int dx = x + (pressed ? 2 : 0), dy = y + (pressed ? 2 : 0);
    if (pressed) {
        const auto background = out.at(x, y + 10);
        fill(out, x, y, width + 1, 2, background);
        fill(out, x, y, 2, 11, background);
    } else {
        fill(out, x + 2, y + 10, width, 2, 0x40);
        fill(out, x + width, y + 2, 2, 10, 0x40);
    }
    const int source_y = pressed ? 44 : 74;
    copy(out, button_art_, 24, source_y, dx, dy, 31, 10);
    int used = 31;
    while (width - used > 20) {
        const int count = std::min(8, width - used - 20);
        copy(out, button_art_, 24, source_y + 10, dx + used, dy, count, 10);
        used += count;
    }
    const int tail = width - used;
    copy(out, button_art_, 45 - tail, source_y + 20, dx + used, dy, tail, 10);
    const auto label = original_to_utf8(std::string(original_label));
    sprite_.draw(out, dx + width / 2 - sprite_.text_width(label) / 2, dy + 7, label,
                 exe_.u8(::bmh::layout::Advertising_Button_RuntimeField + uint32_t(pressed)));
}

Image TopScorersScreen::render() const {
    Image out = state_.cached_backdrop ? state_.underlay :
        state_.decorated_background ? frame_ : Image(320, 240);
    if (!state_.cached_backdrop && !state_.decorated_background) {
        fill(out, 0, 4, 320, 13, 0x44);
        fill(out, 1, 5, 318, 11, 0x39);
        fill(out, 0, 228, 320, 10, 0x44);
        fill(out, 1, 229, 318, 8, 0x39);
    }
    const auto title_raw = source_text(texts_, metric_text_address(state_.metric));
    const auto title = original_to_utf8(std::string(title_raw));
    const int title_width = sprite_.text_width(title);
    sprite_.draw(out, 172 + (110 - title_width) / 2, 13, title);
    const auto scope_raw = source_text(texts_, scope_text_address(state_.scope));
    sprite_.draw(out, 5, 235, original_to_utf8(std::string(scope_raw)));

    // Copy3 directly copies the static KB18 podium at logical (100,25);
    // its path does not call LoadGameEndTaxAnimationAssets and ignores the SolidPanelBackground panel gate.
    // ColorBottomRightBevel adds only the Original right/bottom shadow.
    copy(out, podium_, 0, 0, 100, 25, 112, 70);
    fill(out, 102, 95, 112, 2, 0x40);
    fill(out, 212, 27, 2, 70, 0x40);

    if (state_.projection.no_results) {
        // Source requests TEXTD 867 here; it is intentionally empty in this edition.
        const auto empty = source_text(texts_, ::bmh::layout::TopScorers_Render_TextPointer);
        const int x = 35 + (250 - font_.text_width(empty)) / 2;
        draw_text(out, x, 120, empty, 0x5f);
    } else {
        for (size_t n = 0; n < state_.projection.rows.size(); ++n) {
            const auto& row = state_.projection.rows[n];
            const int baseline = 108 + int(n) * 8;
            const auto& name = row.player_name;
            const auto& club = row.team_name;
            draw_text(out, 66, baseline,
                      source_number(unsigned(row.rank), 2) + ". " + name,
                      n == 0 ? 0x5f : 0x6a);
            draw_text(out, 134, baseline, "(" + club + ")", n == 0 ? 0x5f : 0x6a);

            std::string metric;
            if (state_.metric == TopScorersMetric::goals)
                metric = source_number(row.goals, 2);
            else if (state_.metric == TopScorersMetric::assists)
                metric = source_number(row.assists, 2);
            else
                metric = source_number(row.score, 2) + " (" + source_number(row.goals, 2) + "/" +
                         source_number(row.assists, 2) + ")";
            draw_text(out, 235, baseline, metric, n == 0 ? 0x5f : 0x6a);
        }
    }

    draw_button(out, 40, 215, 100, title_raw, pressed_button_ == 0);
    draw_button(out, 180, 215, 100, scope_raw, pressed_button_ == 1);
    return out;
}

TopScorersRequest TopScorersScreen::request(TopScorersAction action,
                                            TopScorersMetric metric,
                                            TopScorersScope scope) const {
    return TopScorersRequest{state_.identity, action, metric, scope};
}

bool TopScorersScreen::pointer_down(int x, int y, bool right) {
    if (pending_) return false;
    if (right) {
        pending_ = request(TopScorersAction::close, state_.metric, state_.scope);
        rejection_.clear();
        return true;
    }
    pressed_button_ = -1;
    if (x >= 40 && x <= 140 && y >= 215 && y <= 225) pressed_button_ = 0;
    else if (x >= 180 && x <= 280 && y >= 215 && y <= 225) pressed_button_ = 1;
    return pressed_button_ >= 0;
}

bool TopScorersScreen::move(int x, int y) {
    if (pressed_button_ < 0) return false;
    const bool inside = pressed_button_ == 0 ? x >= 40 && x <= 140 && y >= 215 && y <= 225
                                             : x >= 180 && x <= 280 && y >= 215 && y <= 225;
    if (inside) return false;
    pressed_button_ = -1;
    return true;
}

bool TopScorersScreen::release(int x, int y) {
    if (pending_) return false;
    const int pressed = pressed_button_;
    pressed_button_ = -1;
    if (pressed < 0) return false;
    const bool inside = pressed == 0 ? x >= 40 && x <= 140 && y >= 215 && y <= 225
                                     : x >= 180 && x <= 280 && y >= 215 && y <= 225;
    if (!inside) return true;
    auto metric = state_.metric;
    auto scope = state_.scope;
    if (pressed == 0) {
        metric = metric == TopScorersMetric::goals ? TopScorersMetric::assists :
                 metric == TopScorersMetric::assists ? TopScorersMetric::scorer : TopScorersMetric::goals;
        pending_ = request(TopScorersAction::set_metric, metric, scope);
    } else {
        scope = scope == TopScorersScope::league ? TopScorersScope::managers : TopScorersScope::league;
        pending_ = request(TopScorersAction::toggle_scope, metric, scope);
    }
    rejection_.clear();
    return true;
}

void TopScorersScreen::reject_action(std::string reason) {
    if (!pending_ || reason.empty()) throw FormatError("top scorers: rejection requires a pending action and reason");
    pending_.reset();
    pressed_button_ = -1;
    rejection_ = std::move(reason);
}

std::string TopScorersScreen::state_json() const {
    std::ostringstream out;
    out << "{\"screen\":\"top_scorers\",\"manager\":" << state_.identity.manager_slot
        << ",\"runtime_team\":" << state_.identity.runtime_team
        << ",\"league\":" << state_.identity.league
        << ",\"revision\":" << state_.identity.revision
        << ",\"scope_id\":" << state_.identity.scope
        << ",\"sequence\":" << state_.identity.sequence
        << ",\"metric\":" << int(state_.metric)
        << ",\"scope\":" << int(state_.scope)
        << ",\"pressed_button\":" << pressed_button_
        << ",\"pending\":" << (pending_ ? "true" : "false")
        << ",\"last_rejection\":" << json_quote(rejection_) << ",\"rows\":[";
    for (size_t n = 0; n < state_.projection.rows.size(); ++n) {
        if (n) out << ',';
        const auto& row = state_.projection.rows[n];
        out << "{\"rank\":" << row.rank << ",\"player\":" << row.player
            << ",\"team\":" << row.team << ",\"goals\":" << row.goals
            << ",\"assists\":" << row.assists << ",\"score\":" << row.score << '}';
    }
    out << "]}";
    return out.str();
}

} // namespace bmh::ui
