// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/stadium.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <sstream>
#include <utility>

#include "formats/vga.hpp"
#include "game/messages.hpp"
#include "game/balance_plan.hpp"
#include "game/state.hpp"
#include "ui/manager.hpp"
#include "ui/setup.hpp"

namespace bmh::ui {
namespace {
void fill(Image& im, int x, int y, int w, int h, uint8_t color) {
    for (int yy = std::max(0, y); yy < std::min(im.height, y + h); ++yy)
        for (int xx = std::max(0, x); xx < std::min(im.width, x + w); ++xx)
            im.at(xx, yy) = color;
}
void outline(Image& im, int x, int y, int w, int h, uint8_t color) {
    if (w <= 0 || h <= 0) return;
    const int right = x + w - 1;
    const int bottom = y + h - 1;
    for (int xx = std::max(0, x); xx <= std::min(im.width - 1, right); ++xx) {
        if (y >= 0 && y < im.height) im.at(xx, y) = color;
        if (bottom >= 0 && bottom < im.height) im.at(xx, bottom) = color;
    }
    for (int yy = std::max(0, y); yy <= std::min(im.height - 1, bottom); ++yy) {
        if (x >= 0 && x < im.width) im.at(x, yy) = color;
        if (right >= 0 && right < im.width) im.at(right, yy) = color;
    }
}
void source_border(Image& im, int left, int top, int right, int bottom) {
    // PaintsFourBorderEdges draws these inclusive edges in this order. The bottom edge
    // overwrites the lower-left pixel from the first edge; the right edge
    // overwrites both right corners.
    for (int y = top; y <= bottom; ++y) im.at(left, y) = 0x46;
    for (int x = left; x <= right; ++x) im.at(x, top) = 0x46;
    for (int x = left; x <= right; ++x) im.at(x, bottom) = 0x40;
    for (int y = top; y <= bottom; ++y) im.at(right, y) = 0x40;
}
void copy_crop(Image& dst, const Image& src, int sx, int sy, int x, int y, int w, int h,
               bool transparent = false) {
    for (int yy = 0; yy < h; ++yy) for (int xx = 0; xx < w; ++xx) {
        const int dx = x + xx, dy = y + yy, px = sx + xx, py = sy + yy;
        if (dx < 0 || dy < 0 || dx >= dst.width || dy >= dst.height ||
            px < 0 || py < 0 || px >= src.width || py >= src.height) continue;
        const auto color = src.pixels[size_t(py) * size_t(src.width) + size_t(px)];
        if (!transparent || color != 0) dst.at(dx, dy) = color;
    }
}
void source_shadow(Image& out, int x, int y, int w, int h) {
    // ColorBottomRightBevel draws its two inclusive rectangles after clipping to the screen.
    fill(out, x + 2, y + h, w, 2, 0x40);
    fill(out, x + w, y + 2, 2, h, 0x40);
}
void source_frame(Image& out, const Image& atlas, const LeImage& exe,
                  int x, int y, int w, int h, bool shadow = true) {
    if (shadow) { fill(out, x + 2, y + h, w, 2, 0x40); fill(out, x + w, y + 2, 2, h, 0x40); }
    for (int yy = 4; yy < h - 3; ++yy) for (int xx = 4; xx < w - 3; ++xx)
        out.at(x + xx, y + yy) = exe.u8(::bmh::layout::Advertising_PatternRect_RuntimeField + size_t((yy - 4) & 15) * 16 + size_t((xx - 4) & 15));
    copy_crop(out, atlas, 0, 0, x, y, 4, 4);
    copy_crop(out, atlas, 4, 0, x + w - 4, y, 4, 4);
    copy_crop(out, atlas, 0, 4, x, y + h - 4, 4, 4);
    copy_crop(out, atlas, 4, 4, x + w - 4, y + h - 4, 4, 4);
    for (int at = 4; at < w - 4; at += 74) {
        const int n = std::min(74, w - 4 - at);
        copy_crop(out, atlas, 0, 104, x + at, y, n, 4);
        copy_crop(out, atlas, 0, 108, x + at, y + h - 4, n, 4);
    }
    for (int at = 4; at < h - 4; at += 96) {
        const int n = std::min(96, h - 4 - at);
        copy_crop(out, atlas, 0, 8, x, y + at, 4, n);
        copy_crop(out, atlas, 4, 8, x + w - 4, y + at, 4, n);
    }
}
void draw_source_font(Image& out, const Font& font, const LeImage& exe, int x, int baseline,
                      std::string_view text, uint8_t color, bool gradient) {
    const int y = baseline - font.baseline;
    if (y < 0 || y + font.height > out.height) return;
    if (!gradient || exe.u8(::bmh::layout::TextGradientMode) != 2 || color == 0x40) {
        font.draw(out, x, y, text, color);
        return;
    }
    unsigned slot = 0;
    while (slot < 32 && exe.u8(::bmh::layout::Advertising_Text_RuntimeField2 + slot) != color) ++slot;
    if (slot == 32) { font.draw(out, x, y, text, color); return; }
    Image mask(std::max(1, font.text_width(text)), font.height, 0);
    font.draw(mask, 0, 0, text, 1);
    for (int row = 0; row < mask.height; ++row)
        for (int column = 0; column < mask.width; ++column)
            if (mask.at(column, row) && x + column >= 0 && x + column < out.width)
                out.at(x + column, y + row) = exe.u8(::bmh::layout::Advertising_Text_RuntimeField + slot * 6 + size_t(std::min(row, 5)));
}
int32_t record_i32(const std::vector<uint8_t>& bytes, size_t at) {
    return int32_t(uint32_t(bytes.at(at)) | uint32_t(bytes.at(at + 1)) << 8 |
                   uint32_t(bytes.at(at + 2)) << 16 | uint32_t(bytes.at(at + 3)) << 24);
}
uint16_t record_u16(const std::vector<uint8_t>& bytes, size_t at) {
    return uint16_t(bytes.at(at) | uint16_t(bytes.at(at + 1)) << 8);
}
StadiumInvestmentConstants investment_constants(const LeImage& exe) {
    StadiumInvestmentConstants constants;
    for (unsigned choice = 0; choice < 8; ++choice) {
        constants.price[choice] = exe.u32(::bmh::layout::CareerSession_DispatchStadium_RuntimeField + choice * 4);
        constants.weeks[choice] = exe.u8(::bmh::layout::CareerSession_DispatchStadium_RuntimeField2 + (14 + choice) * 5);
    }
    return constants;
}
bool periphery_available(const StadiumState& state, int object) {
    return object >= 0 && object < 14 && state.days[size_t(object)] == 0 &&
        (object == 0 ? state.level[0] < 0xab : state.level[size_t(object)] == 0);
}
std::string investment_name(const StadiumInvestmentRow& row) {
    return row.original_label;
}
std::string textd_string(const TextDb& textd, uint32_t pointer_variable) {
    const size_t id = text_id(pointer_variable);
    return id < textd.strings.size() ? textd[id] : std::string{};
}
std::string game_textd_string(const GameState& game, uint32_t pointer_variable) {
    if (!game.texts) return {};
    return textd_string(*game.texts, pointer_variable);
}
std::string expand_original_club_label(std::string_view name, std::string_view separator) {
    std::string expanded;
    for (char byte : name) {
        if (byte == ':') expanded += separator;
        else expanded.push_back(byte);
    }
    return expanded;
}
std::string original_duration_text(const GameState& game, const TextDb& textd,
                                   const LeImage& exe, int value, uint8_t format,
                                   uint32_t unit_address) {
    std::string result = format_number(game, value, format) + textd_string(textd, unit_address);
    if (value != 1) result += exe.cstring(::bmh::layout::Stadium_FormatError_ExecutableField);
    result += exe.cstring(::bmh::layout::CareerSession_FormatNumber_ExecutableField);
    return result;
}
std::string block_quote_json(const StadiumBlockExpansionQuote& q) {
    std::ostringstream out;
    out << "{\"manager\":" << q.manager << ",\"manager_club\":" << q.manager_club
        << ",\"slot\":" << unsigned(q.slot) << ",\"current_build_weeks\":" << unsigned(q.current_build_weeks)
        << ",\"current_raw_flags\":" << unsigned(q.current_raw_flags)
        << ",\"current_capacity_thousands\":" << unsigned(q.current_capacity_thousands)
        << ",\"current_future_capacity_thousands\":" << unsigned(q.current_future_capacity_thousands)
        << ",\"total_target_places\":" << q.total_target_places
        << ",\"desired_features\":" << unsigned(q.desired_features)
        << ",\"company_price_percent\":" << q.company_price_percent
        << ",\"price_per_1000\":" << q.price_per_1000 << ",\"max_places\":" << q.max_places
        << ",\"new_places\":" << q.new_places << ",\"new_places_cost\":" << q.new_places_cost
        << ",\"seats_cost\":" << q.seats_cost << ",\"roof_cost\":" << q.roof_cost
        << ",\"undiscounted_cost\":" << q.undiscounted_cost << ",\"base_price\":" << q.base_price
        << ",\"base_weeks\":" << unsigned(q.base_weeks) << '}';
    return out.str();
}
bool block_request_kind(StadiumRequestKind kind) {
    switch (kind) {
    case StadiumRequestKind::expansion_select:
    case StadiumRequestKind::expansion_set_total:
    case StadiumRequestKind::expansion_toggle_feature:
    case StadiumRequestKind::expansion_cancel:
    case StadiumRequestKind::expansion_confirm:
    case StadiumRequestKind::expansion_answer_tip: return true;
    default: return false;
    }
}
bool same_block_quote_owner(const StadiumBlockExpansionQuote& a,
                            const StadiumBlockExpansionQuote& b) {
    return a.manager == b.manager && a.manager_club == b.manager_club && a.slot == b.slot &&
        a.current_build_weeks == b.current_build_weeks && a.current_raw_flags == b.current_raw_flags &&
        a.current_capacity_thousands == b.current_capacity_thousands &&
        a.current_future_capacity_thousands == b.current_future_capacity_thousands &&
        a.company_price_percent == b.company_price_percent && a.price_per_1000 == b.price_per_1000 &&
        a.max_places == b.max_places && a.base_weeks == b.base_weeks;
}
bool canonical_block_quote(const StadiumBlockExpansionQuote& q, uint32_t total,
                           uint8_t features) {
    static constexpr std::array<uint32_t, 8> prices{
        300000, 375000, 300000, 350000, 300000, 375000, 300000, 350000};
    static constexpr std::array<uint32_t, 8> maxima{
        8000, 18000, 8000, 14000, 8000, 18000, 8000, 14000};
    static constexpr std::array<uint8_t, 8> weeks{30, 37, 30, 25, 30, 37, 30, 25};
    const size_t slot = static_cast<uint8_t>(q.slot);
    if (slot >= prices.size() || q.manager < 0 || q.manager_club < 0 ||
        q.current_build_weeks != 0 || (q.current_raw_flags & ~uint8_t(3)) != 0 ||
        (features & ~uint8_t(3)) != 0 ||
        (features & uint8_t(q.current_raw_flags & 3)) != uint8_t(q.current_raw_flags & 3) ||
        q.current_capacity_thousands * 1000u > maxima[slot] ||
        total < uint32_t(q.current_capacity_thousands) * 1000u || total > maxima[slot] ||
        total / 1000u > 255 || q.company_price_percent < 0 || q.company_price_percent > 427 ||
        q.price_per_1000 != prices[slot] || q.max_places != maxima[slot] || q.base_weeks != weeks[slot])
        return false;
    const uint32_t current = uint32_t(q.current_capacity_thousands) * 1000u;
    const uint32_t added = total - current;
    const auto component = [&](uint32_t places, uint32_t multiplier, uint32_t divisor,
                               uint32_t& out) {
        const uint64_t value = uint64_t(q.price_per_1000) * (places / 1000u) * multiplier;
        if (value > uint64_t(std::numeric_limits<int32_t>::max())) return false;
        out = uint32_t(value / divisor);
        return true;
    };
    uint32_t new_cost = 0, seats = 0, roof = 0;
    if (!component(added, 1, 1, new_cost) ||
        ((features & 1) && !component((q.current_raw_flags & 1) ? added : total, 45, 100, seats)) ||
        ((features & 2) && !component((q.current_raw_flags & 2) ? added : total, 30, 100, roof)))
        return false;
    const uint64_t sum = uint64_t(new_cost) + seats + roof;
    if (sum > uint64_t(std::numeric_limits<int32_t>::max()) ||
        sum * uint32_t(q.company_price_percent) > uint64_t(std::numeric_limits<int32_t>::max()))
        return false;
    return q.total_target_places == total && q.desired_features == features && q.new_places == added &&
        q.new_places_cost == new_cost && q.seats_cost == seats && q.roof_cost == roof &&
        q.undiscounted_cost == sum &&
        q.base_price == uint32_t(sum * uint32_t(q.company_price_percent) / 100u);
}
bool valid_editor_projection(const StadiumBlockExpansionEditor& editor) {
    return editor.total_target_places == editor.quote.total_target_places &&
        editor.desired_features == editor.quote.desired_features &&
        editor.header_text_variant >= 3 && editor.header_text_variant <= 10 &&
        canonical_block_quote(editor.quote, editor.total_target_places, editor.desired_features);
}
bool same_block_overviews(const std::array<StadiumBlockOverview, 8>& a,
                          const std::array<StadiumBlockOverview, 8>& b) {
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].construction_weeks != b[i].construction_weeks || a[i].flags != b[i].flags ||
            a[i].capacity_level != b[i].capacity_level || a[i].capacity != b[i].capacity)
            return false;
    return true;
}
bool same_other_block_overviews(const std::array<StadiumBlockOverview, 8>& a,
                                const std::array<StadiumBlockOverview, 8>& b,
                                size_t changed_slot) {
    for (size_t i = 0; i < a.size(); ++i) {
        if (i == changed_slot) continue;
        if (a[i].construction_weeks != b[i].construction_weeks || a[i].flags != b[i].flags ||
            a[i].capacity_level != b[i].capacity_level || a[i].capacity != b[i].capacity)
            return false;
    }
    return true;
}
bool same_source_stadium_style(const StadiumState& a, const StadiumState& b) {
    return a.plain_background == b.plain_background && a.compact_dialog_frame == b.compact_dialog_frame &&
        a.solid_panel_background == b.solid_panel_background && a.gradient_text == b.gradient_text;
}
bool same_block_compensation_context(const StadiumState& a, const StadiumState& b,
                                    bool allow_ticket_cap = false) {
    return same_source_stadium_style(a, b) &&
        a.source_level == b.source_level && a.live_league_round == b.live_league_round &&
        a.ticket_refund_rate == b.ticket_refund_rate &&
        (allow_ticket_cap || a.season_tickets == b.season_tickets);
}
uint8_t source_usable_capacity_thousands(const std::array<StadiumBlockOverview, 8>& blocks,
                                         size_t newly_busy_slot) {
    uint8_t usable = 0;
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (i == newly_busy_slot || blocks[i].construction_weeks != 0) continue;
        usable = uint8_t(unsigned(usable) + unsigned(blocks[i].capacity_level));
    }
    return usable;
}
int32_t source_wrap_i32(int64_t value) {
    return std::bit_cast<int32_t>(uint32_t(uint64_t(value)));
}
int32_t source_immediate_compensation(uint16_t tickets, uint8_t usable_thousands,
                                      uint8_t live_round, int32_t rate) {
    const int32_t capacity = int32_t(usable_thousands) * 1000;
    const int32_t missing = std::max(0, int32_t(tickets) - capacity);
    if (missing == 0) return 0;
    const int32_t weeks = std::min(15, 17 - int32_t(live_round));
    return source_wrap_i32(int64_t(missing) * int64_t(weeks) * int64_t(rate));
}
uint16_t source_ticket_cap(uint16_t tickets, uint8_t usable_thousands) {
    return uint16_t(std::min<uint32_t>(tickets, uint32_t(usable_thousands) * 1000u));
}
}

StadiumState stadium_from_game(const GameState& game, const LeImage& exe, uint64_t revision,
                               int construction_percent) {
    const int manager = int(game.memory.u8(::bmh::layout::ActiveManager));
    const size_t record = size_t(manager) * 0xb0c;
    if (size_t(manager) >= game.manager_teams.size() || record + 0x5d > game.manager_records.size())
        throw FormatError("stadium: invalid active manager or truncated record");
    StadiumState state;
    state.manager_slot = manager;
    state.runtime_club = game.manager_teams.at(size_t(manager));
    state.revision = revision;
    state.balance = record_i32(game.manager_records, record + 0x314);
    state.balance_text = format_number(game, state.balance, 0x181);
    state.construction_percent = construction_percent;
    if (state.runtime_club >= 0 && size_t(state.runtime_club) < game.club_ids.size()) {
        state.original_stadium_title = game.club_name(state.runtime_club);
        std::string footer_name;
        const size_t heap_entry = size_t(state.runtime_club) * 2;
        if ((!game.memory.has(::bmh::layout::LiveMatchday_FormatError_RuntimeField15) || game.memory.u8(::bmh::layout::LiveMatchday_FormatError_RuntimeField15) == 0) &&
            state.runtime_club < exe.u16(::bmh::layout::CareerSession_DataDirectory_RuntimeField2) && heap_entry + 1 < game.club_heap_bc.size()) {
            const size_t stadium_name = record_u16(game.club_heap_bc, heap_entry);
            if (stadium_name < game.stadium_names.size() && game.stadium_names[stadium_name] != 0) {
                for (size_t pos = stadium_name; pos < game.stadium_names.size() &&
                     game.stadium_names[pos] != 0; ++pos)
                    footer_name.push_back(char(game.stadium_names[pos]));
                footer_name = expand_original_club_label(footer_name,
                    game_textd_string(game, ::bmh::layout::LiveMatchday_FormatError_TextPointer8));
            }
        }
        if (game.memory.has(::bmh::layout::LiveMatchday_FormatError_RuntimeField15) && game.memory.u8(::bmh::layout::LiveMatchday_FormatError_RuntimeField15) != 0)
            footer_name = game_textd_string(game, ::bmh::layout::LiveMatchday_FormatError_TextPointer7);
        else if (footer_name.empty())
            footer_name = game_textd_string(game, ::bmh::layout::LiveMatchday_FormatError_TextPointer9) + state.original_stadium_title;
        state.original_stadium_footer = footer_name + exe.cstring(::bmh::layout::Stadium_FormatError_ExecutableField2) +
            format_number(game, int32_t(uint32_t(game.manager_records.at(record + 0x5c)) * 1000), 1) +
            game_textd_string(game, ::bmh::layout::Stadium_FormatError_ExecutableField3);
    }
    state.plain_background = game.memory.has(::bmh::layout::PlainBackground) && game.memory.u8(::bmh::layout::PlainBackground) != 0;
    state.compact_dialog_frame = game.memory.has(::bmh::layout::CompactDialogFrame) && game.memory.u8(::bmh::layout::CompactDialogFrame) != 0;
    state.decorated_stadium_header = !state.plain_background;
    state.periphery_animation_tick = game.memory.has(::bmh::layout::CareerSession_Invalid_RuntimeField2) ? game.memory.u32(::bmh::layout::CareerSession_Invalid_RuntimeField2) : 0;
    state.periphery_balance_text = format_number(game, state.balance, 0x187) + exe.cstring(::bmh::layout::CareerSession_FormatNumber_ExecutableField);
    for (size_t i = 0; i < 14; ++i) {
        state.periphery_price_text[i] = format_number(game, int32_t(exe.u32(::bmh::layout::StadiumPriceTable + i * 4)), 0x187) +
            exe.cstring(::bmh::layout::CareerSession_FormatNumber_ExecutableField);
        const int days = exe.u8(::bmh::layout::CareerSession_DispatchStadium_RuntimeField2 + i * 5);
        state.periphery_duration_text[i] = format_number(game, days, 0x81) + game_textd_string(game, ::bmh::layout::Stadium_FormatError_RuntimeField);
        if (days != 1) state.periphery_duration_text[i] += exe.cstring(::bmh::layout::Stadium_FormatError_ExecutableField);
        state.periphery_duration_text[i] += exe.cstring(::bmh::layout::CareerSession_FormatNumber_ExecutableField);
    }
    for (size_t i = 0; i < state.level.size(); ++i) {
        state.level[i] = game.manager_records.at(record + i);
        state.days[i] = game.manager_records.at(record + 0x1e + i);
    }
    for (unsigned i = 0; i < 8; ++i) {
        auto& block = state.blocks[i];
        block.construction_weeks = game.manager_records.at(record + 0x34 + i);
        block.flags = game.manager_records.at(record + 0x44 + i);
        block.capacity_level = game.manager_records.at(record + 0x4c + i);
        block.capacity = game.manager_records.at(record + 0x54 + i);
    }
    const auto constants = investment_constants(exe);
    for (unsigned i = 0; i < 8; ++i) {
        auto& row = state.investments[i];
        row.investment = static_cast<StadiumInvestment>(i);
        row.current_status = game.manager_records.at(record + 0x3c + i);
        row.current_level = row.current_status & 7;
        row.construction_weeks = game.manager_records.at(record + 0x2c + i);
        row.options = stadium_investment_options_source(game, manager, row.investment,
                                                        construction_percent, constants);
        row.available = !row.options.empty();
        row.option_price_text.clear();
        for (size_t option = 0; option < row.options.size(); ++option) {
            const uint32_t source_price = constants.price[i] * uint32_t(option + 1);
            row.option_price_text.push_back(format_number(game, int32_t(source_price), 0x187) +
                                              exe.cstring(::bmh::layout::CareerSession_FormatNumber_ExecutableField));
        }
    }
    return state;
}

StadiumDataProjection stadium_data_from_game(const GameState& game, const LeImage& exe,
                                              int manager, uint8_t flags) {
    if (manager < 0 || manager > 4) throw FormatError("stadium data: invalid manager");
    StadiumDataProjection out;
    out.flags = flags;
    const size_t at = size_t(manager) * 0xb0c;
    const auto& r = game.manager_records;
    out.ticket_price = r.at(at + 0x26e);
    for (int curve = 0; curve < 6; ++curve) {
        const int selector = exe.u8(::bmh::layout::Analytics_FormatError_RuntimeField2 + 6 + curve);
        if (selector < 0 || selector > 3) throw FormatError("stadium data: invalid Source history selector");
        const int count = r.at(at + 0x283 + size_t(selector));
        const int limit = selector == 0 ? 19 : selector == 2 ? 52 : 52;
        if (count > limit) throw FormatError("stadium data: Source history exceeds its record bounds");
        for (int sample = 0; sample < count; ++sample) {
            const size_t n = size_t(sample);
            int32_t value = 0;
            switch (curve) {
            case 0: value = record_i32(r, at + 0x340 + n * 4); break;
            case 1: value = record_i32(r, at + 0x38c + n * 4); break;
            case 2: value = r.at(at + 0x270 + n); break;
            case 3: value = std::bit_cast<int32_t>(uint32_t(record_i32(r, at + 0x340 + n * 4)) * r.at(at + 0x270 + n)); break;
            case 4: value = int32_t(r.at(at + 0x17a + n)) * 100; break;
            case 5: value = record_i32(r, at + 0x648 + n * 4); break;
            }
            out.series[size_t(curve)].push_back(value);
        }
        out.minimum[size_t(curve)] = std::bit_cast<int32_t>(exe.u32(::bmh::layout::Analytics_FormatError_RuntimeField3 + 24 + curve * 4));
        out.maximum[size_t(curve)] = std::bit_cast<int32_t>(exe.u32(::bmh::layout::Analytics_FormatError_RuntimeField4 + 24 + curve * 4));
    }
    out.maximum[0] = int32_t(r.at(at + 0x5c)) * 1000;
    out.maximum[1] = int32_t(r.at(at + 0x5c)) * 20000;
    const auto date_text = [&](uint32_t counter) {
        const auto date = desk_date(counter, exe);
        auto text = format_number(game, date.day, 2) + exe.cstring(::bmh::layout::OtherGamesState_ResultText_ExecutableField) +
                    format_number(game, date.month, 2) + exe.cstring(::bmh::layout::OtherGamesState_ResultText_ExecutableField) +
                    format_number(game, date.year % 100, 0x42);
        std::replace(text.begin(), text.end(), '%', '0');
        return text;
    };
    for (int kind = 0; kind < 3; ++kind) for (int row = 0; row < 7; ++row) {
        auto text = row == 1 || row == 3 ? std::string{} : message_text(game, ::bmh::layout::Stadium_StadiumDataFromGame_TextPointer);
        if (row < 4) {
            const size_t base = at + 0x718 + size_t(row >= 2) * 24 + size_t(kind) * 8;
            const int32_t value = record_i32(r, base);
            if (value != 0) {
                if (row == 0 || row == 2)
                    text = format_number(game, value, 0x87) + exe.cstring(::bmh::layout::Stadium_StadiumDataFromGame_ExecutableField) +
                           date_text(uint32_t(record_i32(r, base + 4))) + exe.cstring(::bmh::layout::SeriesRecords_Render_ExecutableField);
                else {
                    const size_t offset = record_u16(r, at + 0x8c0 + size_t(row == 3) * 6 + size_t(kind) * 2);
                    if (offset >= game.club_names.size()) throw FormatError("stadium data: opponent name offset outside Source pool");
                    for (size_t i = offset; i < game.club_names.size() && game.club_names[i]; ++i)
                        text += char(game.club_names[i]);
                }
            }
        } else {
            const int count = int8_t(r.at(at + 0x28c + size_t(kind)));
            int32_t value = 0;
            if (count != 0) {
                if (row == 4) value = count;
                else if (row == 5) value = record_i32(r, at + 0x748 + size_t(kind) * 4) / count;
                else value = record_i32(r, at + 0x754 + size_t(kind) * 4);
            }
            if (value != 0) text = format_number(game, value, 0x88);
        }
        out.table[size_t(row)][size_t(kind)] = std::move(text);
    }
    out.required_attendance_text = format_number(game, stadium_required_attendance(game, manager), 0x81);
    return out;
}

std::vector<std::pair<int, int>> stadium_data_points(const StadiumDataProjection& data,
                                                   const LeImage& exe, int curve) {
    if (curve < 0 || curve >= 6) throw FormatError("stadium data: invalid curve");
    auto values = data.series[size_t(curve)];
    if (values.empty()) return {};
    int32_t low = data.minimum[size_t(curve)], high = data.maximum[size_t(curve)];
    if (!(data.flags & 0x40)) {
        const auto [minimum, maximum] = std::minmax_element(values.begin(), values.end());
        const int32_t minimum_value = *minimum;
        const uint32_t range = *maximum == 0 ? 0 : uint32_t(*maximum) - uint32_t(minimum_value);
        for (auto& value : values) value = range ?
            std::bit_cast<int32_t>((uint32_t(value) - uint32_t(minimum_value)) * 100) / int32_t(range) : 50;
        low = 0; high = 100;
    }
    if (low > high) throw FormatError("stadium data: reversed Source graph scale");
    const int selector = exe.u8(::bmh::layout::Analytics_FormatError_RuntimeField2 + 6 + curve);
    const int horizon = std::max(int(values.size()), selector == 0 ? 34 : 17);
    std::vector<std::pair<int, int>> points;
    for (size_t sample = 0; sample < values.size(); ++sample) {
        const int32_t value = std::clamp(values[sample], low, high);
        // Amount2 uses x87 truncate toward zero, after an 83-pixel quotient.
        // With equal limits Source 0/0 converts to x87 integer-indefinite,
        // whose low short is zero before the y=101-height step.
        const int height = low == high ? 0 : int(double(int64_t(value) - low) * 83.0 / double(int64_t(high) - low));
        points.emplace_back(13 + int(sample) * 298 / horizon, 101 - height);
    }
    return points;
}

void stadium_apply_price_table(StadiumState& state, const LeImage& exe, int construction_percent) {
    (void)exe;
    // Options must be reprojected from the live GameState by the App owner; this
    // presentation-only helper intentionally cannot reconstruct/guess them.
    state.construction_percent = construction_percent;
}

std::string stadium_state_json(const StadiumState& s) {
    std::ostringstream out;
    out << "{\"manager_slot\":" << s.manager_slot << ",\"runtime_club\":" << s.runtime_club
        << ",\"stable_club_identity\":" << (s.stable_club_identity ? std::to_string(*s.stable_club_identity) : "null")
        << ",\"revision\":" << s.revision << ",\"scope\":" << s.scope << ",\"sequence\":" << s.sequence
        << ",\"source\":" << json_quote(s.source) << ",\"club_name\":" << json_quote(original_to_utf8(s.club_name))
        << ",\"balance\":" << s.balance << ",\"balance_text\":" << json_quote(original_to_utf8(s.balance_text))
        << ",\"original_stadium_title\":" << json_quote(original_to_utf8(s.original_stadium_title))
        << ",\"original_stadium_footer\":" << json_quote(original_to_utf8(s.original_stadium_footer))
        << ",\"decorated_stadium_header\":" << (s.decorated_stadium_header ? "true" : "false")
        << ",\"plain_background\":" << (s.plain_background ? "true" : "false")
        << ",\"compact_dialog_frame\":" << (s.compact_dialog_frame ? "true" : "false")
        << ",\"solid_panel_background\":" << (s.solid_panel_background ? "true" : "false")
        << ",\"construction_percent\":" << s.construction_percent
        << ",\"data_open\":" << (s.data ? "true" : "false")
        << ",\"data_flags\":" << (s.data ? std::to_string(s.data->flags) : "null")
        << ",\"data_ticket_price\":" << (s.data ? std::to_string(s.data->ticket_price) : "null")
        << ",\"data_required_attendance\":" << (s.data ? json_quote(s.data->required_attendance_text) : "null")
        << ",\"opened\":" << (s.opened ? "true" : "false")
        << ",\"investments_enabled\":" << (s.investments_enabled ? "true" : "false")
        << ",\"investments\":[";
    for (size_t i = 0; i < s.investments.size(); ++i) {
        if (i) out << ',';
        const auto& row = s.investments[i];
        out << "{\"choice\":" << i << ",\"label\":" << json_quote(original_to_utf8(row.original_label))
            << ",\"status\":" << unsigned(row.current_status) << ",\"level\":" << unsigned(row.current_level) << ",\"construction_weeks\":" << unsigned(row.construction_weeks)
            << ",\"available\":" << (row.available ? "true" : "false") << ",\"options\":[";
        for (size_t j = 0; j < row.options.size(); ++j) {
            if (j) out << ',';
            const auto& option = row.options[j];
            out << "{\"current_status\":" << unsigned(option.current_status)
                << ",\"current_level\":" << unsigned(option.current_level)
                << ",\"increment\":" << unsigned(option.increment)
                << ",\"target_level\":" << unsigned(option.target_level)
                << ",\"base_price\":" << option.base_price
                << ",\"base_weeks\":" << unsigned(option.base_weeks) << '}';
        }
        out << "]}";
    }
    out << "],\"selected_investment\":";
    if (s.selected_investment) out << unsigned(*s.selected_investment); else out << "null";
    out << ",\"selected_option\":" << (s.selected_option ? std::to_string(*s.selected_option) : "null")
        << ",\"investment_text_variant\":" << s.investment_text_variant
        << ",\"construction_tip_price\":" << s.construction_tip_price
        << ",\"construction_tip_dialog\":" << (s.construction_tip_dialog ? "true" : "false")
        << ",\"quoted_object\":" << (s.quoted_object ? std::to_string(*s.quoted_object) : "null")
        << ",\"quoted_price\":" << s.quoted_price << ",\"blocks\":[";
    for (size_t i = 0; i < s.blocks.size(); ++i) {
        if (i) out << ',';
        const auto& b = s.blocks[i];
        out << "{\"construction_weeks\":" << unsigned(b.construction_weeks)
            << ",\"flags\":" << unsigned(b.flags) << ",\"capacity_level\":" << unsigned(b.capacity_level)
            << ",\"capacity\":" << unsigned(b.capacity) << '}';
    }
    out << "],\"block_text_variant\":" << s.block_text_variant
        << ",\"block_expansion\":";
    if (!s.block_expansion) out << "null";
    else {
        const auto& editor = *s.block_expansion;
        out << "{\"quote\":" << block_quote_json(editor.quote)
            << ",\"total_target_places\":" << editor.total_target_places
            << ",\"desired_features\":" << unsigned(editor.desired_features)
            << ",\"header_text_variant\":" << editor.header_text_variant
            << ",\"offer\":";
        if (editor.offer) out << "{\"percent\":" << editor.offer->percent
            << ",\"base_price\":" << editor.offer->base_price
            << ",\"offered_price\":" << editor.offer->offered_price
            << ",\"price\":" << editor.offer->price
            << ",\"base_days\":" << editor.offer->base_days
            << ",\"construction_days\":" << editor.offer->construction_days
            << ",\"decision\":" << unsigned(editor.offer->decision) << '}';
        else out << "null";
        out << ",\"current_places_text\":" << json_quote(original_to_utf8(editor.current_places_text))
            << ",\"new_places_text\":" << json_quote(original_to_utf8(editor.new_places_text))
            << ",\"target_places_text\":" << json_quote(original_to_utf8(editor.target_places_text))
            << ",\"per_thousand_cost_text\":" << json_quote(original_to_utf8(editor.per_thousand_cost_text))
            << ",\"new_places_cost_text\":" << json_quote(original_to_utf8(editor.new_places_cost_text))
            << ",\"seats_cost_text\":" << json_quote(original_to_utf8(editor.seats_cost_text))
            << ",\"roof_cost_text\":" << json_quote(original_to_utf8(editor.roof_cost_text))
            << ",\"total_cost_text\":" << json_quote(original_to_utf8(editor.total_cost_text))
            << ",\"discount_text\":" << json_quote(original_to_utf8(editor.discount_text)) << '}';
    }
    out << ",\"action_feedback\":" << (s.action_feedback.empty() ? "null" : json_quote(s.action_feedback)) << '}';
    return out.str();
}

std::string stadium_request_json(const StadiumRequest& r) {
    const char* kind = "none";
    switch (r.kind) {
    case StadiumRequestKind::open: kind = "open"; break;
    case StadiumRequestKind::quote_periphery: kind = "quote_periphery"; break;
    case StadiumRequestKind::decline_periphery: kind = "decline_periphery"; break;
    case StadiumRequestKind::build_periphery: kind = "build_periphery"; break;
    case StadiumRequestKind::select_block: kind = "select_block"; break;
    case StadiumRequestKind::prepare_block_offer: kind = "prepare_block_offer"; break;
    case StadiumRequestKind::decline_block_offer: kind = "decline_block_offer"; break;
    case StadiumRequestKind::build_stand_block: kind = "build_stand_block"; break;
    case StadiumRequestKind::select_investment: kind = "select_investment"; break;
    case StadiumRequestKind::select_investment_option: kind = "select_investment_option"; break;
    case StadiumRequestKind::cancel_investment: kind = "cancel_investment"; break;
    case StadiumRequestKind::confirm_investment: kind = "confirm_investment"; break;
    case StadiumRequestKind::answer_construction_tip: kind = "answer_construction_tip"; break;
    case StadiumRequestKind::close: kind = "close"; break;
    case StadiumRequestKind::expansion_select: kind = "expansion_select"; break;
    case StadiumRequestKind::expansion_set_total: kind = "expansion_set_total"; break;
    case StadiumRequestKind::expansion_toggle_feature: kind = "expansion_toggle_feature"; break;
    case StadiumRequestKind::expansion_cancel: kind = "expansion_cancel"; break;
    case StadiumRequestKind::expansion_confirm: kind = "expansion_confirm"; break;
    case StadiumRequestKind::expansion_answer_tip: kind = "expansion_answer_tip"; break;
    case StadiumRequestKind::open_data: kind = "open_data"; break;
    case StadiumRequestKind::toggle_data_curve: kind = "toggle_data_curve"; break;
    case StadiumRequestKind::toggle_data_scale: kind = "toggle_data_scale"; break;
    case StadiumRequestKind::set_data_ticket_price: kind = "set_data_ticket_price"; break;
    case StadiumRequestKind::close_data: kind = "close_data"; break;
    case StadiumRequestKind::none: break;
    }
    std::ostringstream out;
    out << "{\"kind\":" << json_quote(kind) << ",\"manager_slot\":" << r.manager_slot
        << ",\"runtime_club\":" << r.runtime_club << ",\"stable_club_identity\":"
        << (r.stable_club_identity ? std::to_string(*r.stable_club_identity) : "null")
        << ",\"revision\":" << r.revision << ",\"scope\":" << r.scope << ",\"sequence\":" << r.sequence
        << ",\"object\":" << r.object << ",\"price\":" << r.price << ",\"option\":" << r.option
        << ",\"accept_tip\":" << (r.accept_tip ? "true" : "false")
        << ",\"block_slot\":" << r.block_slot << ",\"block_total_places\":" << r.block_total_places
        << ",\"block_features\":" << unsigned(r.block_features) << ",\"expected_block_quote\":";
    if (r.expected_block_quote) out << block_quote_json(*r.expected_block_quote); else out << "null";
    out << '}';
    return out.str();
}

StadiumScreen::StadiumScreen(const std::filesystem::path& data, StadiumState state)
    : state_(std::move(state)), data_(data), exe_(load_le(asset_path(data, "BMH.exe"))),
      texts_(load_textd(asset_path(data, "TEXTD.BLK"))),
      background_(load_vga(asset_path(data / "KB", "35.VGA"))),
      header_background_(load_vga(asset_path(data / "KB", "129.VGA"))),
      interior_(load_vga(asset_path(data / "KB", "38.VGA"))),
      facilities_(load_vga(asset_path(data / "KB", "39.VGA"))),
      panel_(load_vga(asset_path(data / "KB", "56.VGA"))),
      periphery_overlay_(load_vga(asset_path(data / "KB", "70.VGA"))),
      widgets_(load_vga(asset_path(data / "KB", "0.VGA"))),
      calculator_art_(load_vga(asset_path(data / "KB", "6.VGA"))),
      font_(load_font(asset_path(data / "F", "BMH3.FNT"))),
      heading_font_(load_font(asset_path(data / "F", "BMH2.FNT"))), sprite_font_(data) {
    if (background_.width <= 0 || header_background_.width <= 0 || interior_.width <= 0 || facilities_.width <= 0 ||
        panel_.width <= 0 || calculator_art_.width < 113 || calculator_art_.height < 150 || state_.manager_slot < 0)
        throw FormatError("stadium: unsupported scene or invalid manager");
    for (size_t i = 0; i < state_.investments.size(); ++i) {
        if (state_.investments[i].original_label.empty())
            state_.investments[i].original_label = texts_[734 + i];
    }
    rebuild_tip_dialog();
}

void StadiumScreen::text_at(Image& out, int x, int baseline, std::string_view text, uint8_t color,
                            bool gradient) const {
    draw_source_font(out, font_, exe_, x, baseline, text, color,
                     gradient && state_.gradient_text);
}

void StadiumScreen::original_button(Image& out, int x, int y, int w, int h,
                                    std::string_view label, bool selected) const {
    // FormatError3's requested 9px height becomes 10px for these wide controls.
    const int visible_h = w > 31 && h == 9 ? 10 : h;
    if (visible_h != 10 && !(w <= 31 && visible_h == 9)) throw FormatError("stadium: unsupported Original button height");
    if (selected) {
        const auto under = out.at(x, y + visible_h);
        fill(out, x, y, w <= 31 ? w + 1 : w, 2, under); fill(out, x, y, 2, visible_h + 1, under); x += 2; y += 2;
    } else { fill(out, x + 2, y + visible_h, w, 2, 0x40); fill(out, x + w, y + 2, 2, visible_h, 0x40); }
    if (w <= 31) {
        source_frame(out, widgets_, exe_, x, y, w, visible_h, false);
        sprite_font_.draw(out, x + w / 2 - sprite_font_.text_width(original_to_utf8(label)) / 2,
                          y + visible_h / 2 + 2, original_to_utf8(label), exe_.u8(::bmh::layout::Advertising_Button_RuntimeField + int(selected)));
        return;
    }
    const int sy = selected ? 44 : 74;
    copy_crop(out, widgets_, 24, sy, x, y, 31, 10);
    int used = 31;
    while (w - used > 20) {
        const int n = std::min(8, w - used - 20);
        copy_crop(out, widgets_, 24, sy + 10, x + used, y, n, 10); used += n;
    }
    copy_crop(out, widgets_, 45 - (w - used), sy + 20, x + used, y, w - used, 10);
    sprite_font_.draw(out, x + w / 2 - sprite_font_.text_width(original_to_utf8(label)) / 2,
                      y + 7, original_to_utf8(label), exe_.u8(::bmh::layout::Advertising_Button_RuntimeField + int(selected)));
}

StadiumRequest StadiumScreen::request(StadiumRequestKind kind, int object, int option,
                                     int32_t price, bool accept_tip) const {
    StadiumRequest r;
    r.kind = kind; r.manager_slot = state_.manager_slot; r.runtime_club = state_.runtime_club;
    r.stable_club_identity = state_.stable_club_identity; r.revision = state_.revision;
    r.scope = state_.scope; r.sequence = state_.sequence; r.object = object;
    r.option = option; r.price = price; r.accept_tip = accept_tip;
    return r;
}

StadiumRequest StadiumScreen::block_request(StadiumRequestKind kind, uint32_t total,
                                             uint8_t features) const {
    if (!block_request_kind(kind)) throw FormatError("stadium: invalid block expansion request kind");
    auto r = request(kind);
    if (kind == StadiumRequestKind::expansion_select) {
        if (pressed_ != Pressed::block_row || pressed_index_ < 0 || pressed_index_ >= 8)
            throw FormatError("stadium: block selection has no Source row owner");
        r.block_slot = pressed_index_;
        return r; // Source entry carries only the slot; App supplies the immutable quote.
    }
    if (!state_.block_expansion) throw FormatError("stadium: block request has no editor owner");
    const auto& editor = *state_.block_expansion;
    r.block_slot = int(editor.quote.slot);
    r.block_total_places = total ? total : editor.total_target_places;
    r.block_features = features ? features : editor.desired_features;
    if (kind == StadiumRequestKind::expansion_set_total) r.block_total_places = total;
    if (kind == StadiumRequestKind::expansion_toggle_feature) r.block_features = features;
    r.expected_block_quote = editor.quote;
    return r;
}

StadiumRequest StadiumScreen::initialize() {
    if (pending_) throw FormatError("stadium: open already pending");
    auto r = request(StadiumRequestKind::open);
    pending_ = r;
    return r;
}

void StadiumScreen::rebuild_tip_dialog() {
    tip_dialog_.reset();
    periphery_dialog_.reset();
    if (state_.periphery_quote_dialog) {
        GameEndRenderOptions options{state_.plain_background, state_.gradient_text, true};
        periphery_dialog_.emplace(data_, *state_.periphery_quote_dialog, options, render_scene());
    }
    if (state_.construction_tip_dialog) {
        GameEndRenderOptions options;
        options.gradient_text = state_.gradient_text;
        options.preserve_restored_backdrop = true;
        if (state_.block_expansion) options.plain_background = state_.plain_background;
        tip_dialog_.emplace(data_, *state_.construction_tip_dialog,
            options, state_.block_expansion
                ? render_block_expansion(render_scene()) : render_investment_dialog(render_scene()));
    }
}

Image StadiumScreen::render_block_expansion(Image out) const {
    if (!state_.block_expansion) return out;
    const auto& editor = *state_.block_expansion;
    const unsigned slot = unsigned(editor.quote.slot);
    if (slot >= state_.blocks.size()) throw FormatError("stadium: invalid block editor slot");
    source_frame(out, widgets_, exe_, 1, 22, 214, 128, false);
    const std::string title = texts_[742 + slot] + texts_[1089];
    const int title_width = heading_font_.text_width(title);
    draw_source_font(out, heading_font_, exe_, 1 + (215 - title_width) / 2,
                     33, title, 0x5f, false); // Front explicitly selects Font2.

    text_at(out, 5, 46, texts_[text_id(::bmh::layout::Stadium_RenderBlockExpansion_TextPointer2)], 0x6c, true);
    const std::string balance = state_.balance_text.empty() ? std::to_string(state_.balance) : state_.balance_text;
    text_at(out, 148 - font_.text_width(balance), 46, balance, 0x6c, true);
    const std::string weeks = original_duration_text(GameState{}, texts_, exe_, editor.quote.base_weeks, 0x82, ::bmh::layout::Stadium_FormatError_RuntimeField);
    text_at(out, 5, 54, texts_[text_id(::bmh::layout::Stadium_RenderBlockExpansion_TextPointer3)], 0x6c, true);
    text_at(out, 148 - font_.text_width(weeks), 54, weeks, 0x6c, true);
    text_at(out, 5, 70, texts_[text_id(::bmh::layout::Stadium_RenderBlockExpansion_TextPointer4)], 0x6c, true);
    if (editor.header_text_variant >= 0) {
        const std::string variant = format_number(GameState{}, editor.header_text_variant, 0x82) +
            texts_[text_id(::bmh::layout::Stadium_RenderBlockExpansion_TextPointer5)];
        text_at(out, 148 - font_.text_width(variant), 70, variant, 0x6c, true);
    }

    fill(out, 5, 74, 100, 25, 0x63);
    fill(out, 105, 76, 2, 25, 0x40);
    fill(out, 7, 99, 100, 2, 0x40);
    std::string block_label = texts_[text_id(::bmh::layout::Stadium_Out_TextPointer)];
    if (block_label.size() > 6) block_label[6] = char('A' + slot);
    text_at(out, 7, 81, block_label, 0x6b, true);
    const std::string start = texts_[text_id(::bmh::layout::Stadium_RenderBlockExpansion_TextPointer6)] + editor.current_places_text +
        exe_.cstring(::bmh::layout::Stadium_RenderBlockExpansion_TextPointer);
    const std::string target = texts_[text_id(::bmh::layout::Stadium_RenderBlockExpansion_TextPointer7)] + editor.target_places_text +
        texts_[text_id(::bmh::layout::Stadium_FormatError_ExecutableField3)];
    text_at(out, 7, 89, start, 0x6b, true);
    text_at(out, 7, 97, target, 0x6c, true);
    text_at(out, 5, 112, texts_[text_id(::bmh::layout::Stadium_RenderBlockExpansion_TextPointer10)], 0x6a, true);
    const std::string per_thousand_cost = editor.per_thousand_cost_text + exe_.cstring(::bmh::layout::CareerSession_FormatNumber_ExecutableField);
    text_at(out, 148 - font_.text_width(per_thousand_cost), 112,
            per_thousand_cost, 0x6c, true);
    if (editor.quote.new_places != 0) {
        text_at(out, 5, 120, texts_[text_id(::bmh::layout::Stadium_RenderBlockExpansion_TextPointer8)], 0x6a, true);
        text_at(out, 148 - font_.text_width(editor.new_places_cost_text), 120,
                editor.new_places_cost_text, 0x6c, true);
    }

    for (unsigned feature = 0; feature < 2; ++feature) {
        const uint8_t bit = uint8_t(1u << feature);
        const int icon_y = feature == 0 ? 73 : 89;
        const int source_y = feature == 0 ? 1 : 12;
        copy_crop(out, interior_, 180, source_y, 120, icon_y, 10, 10, true);
        const bool enabled = (editor.desired_features & bit) != 0;
        const std::string status = texts_[enabled ? 1114 : 1115];
        text_at(out, 140, feature == 0 ? 79 : 94, status, enabled ? 0x5f : 0x6a, true);
        const std::string& cost = feature == 0 ? editor.seats_cost_text : editor.roof_cost_text;
        text_at(out, 140, feature == 0 ? 86 : 101, cost, 0x6b, true);
    }
    text_at(out, 5, 130, texts_[text_id(::bmh::layout::Stadium_RenderBlockExpansion_TextPointer9)], 0x6a, true);
    text_at(out, 148 - font_.text_width(editor.total_cost_text), 130, editor.total_cost_text, 0x6c, true);
    text_at(out, 152, 130, editor.discount_text, 0x6c, true);
    original_button(out, 7, 136, 70, 9, texts_[156], pressed_ == Pressed::block_build);
    original_button(out, 107, 136, 70, 9, texts_[157], pressed_ == Pressed::block_cancel);
    return out;
}

Image StadiumScreen::render_block_calculator(Image out) const {
    if (!block_calculator_open_ || block_calculator_cancel_wait_ || !state_.block_expansion) return out;
    const int x0 = block_calculator_x_, y0 = block_calculator_y_;
    copy_crop(out, calculator_art_, 0, 0, x0, y0, 113, 136);
    fill(out, x0 + 113, y0 + 2, 2, 136, 0x40);
    fill(out, x0 + 2, y0 + 136, 113, 2, 0x40);
    const auto& editor = *state_.block_expansion;
    const std::string title = texts_[1118];
    const int width = font_.text_width(title);
    text_at(out, x0 + 8 + (105 - 8) / 2 - width / 2, y0 + 14, title, 0x62, true);
    constexpr std::string_view keys = "0123456789DRCJN";
    for (int i = 0; i < 15; ++i) {
        const int x = x0 + exe_.u16(::bmh::layout::Advertising_Click_RuntimeField + size_t(i) * 2);
        const int y = y0 + exe_.u8(::bmh::layout::Advertising_Click_RuntimeField2 + size_t(i));
        const bool pressed = block_calculator_pressed_key_ == i;
        copy_crop(out, calculator_art_, pressed ? 15 : 0, 136, x + (pressed ? 1 : 0),
                  y + (pressed ? 1 : 0), 15 - int(pressed), 14 - int(pressed));
        text_at(out, x + 5 + int(pressed), y + 9 + int(pressed), keys.substr(size_t(i), 1), 0x62, false);
    }
    const uint32_t current = uint32_t(editor.quote.current_capacity_thousands) * 1000;
    std::array<uint32_t, 8> presets{};
    for (uint32_t i = 0; i < 6; ++i) presets[i] = current + i * 1000;
    presets[6] = current; presets[7] = editor.quote.max_places;
    for (size_t i = 0; i < presets.size(); ++i) {
        if (presets[i] > editor.quote.max_places) continue;
        const int x = x0 + exe_.u16(::bmh::layout::Advertising_Click_RuntimeField + (i + 15) * 2);
        const int y = y0 + exe_.u8(::bmh::layout::Advertising_Click_RuntimeField2 + i + 15);
        const std::string value = format_number(GameState{}, int32_t(presets[i]), 7);
        text_at(out, x + 47 - font_.text_width(value), y + 12, value, 0x62, true);
    }
    const std::string value = format_number(GameState{}, int32_t(block_calculator_value_),
                                           block_calculator_edited_ ? 8 : 7);
    text_at(out, x0 + 106 - font_.text_width(value), y0 + 110, value, 0x00, true);
    return out;
}

void StadiumScreen::render_panorama(Image& out) const {
    // ConstructionPricePercent uses KB70 as the opaque panorama underlay; KB35 is an atlas,
    // not a full-screen background. CopyCrop skips the Original zero key.
    copy_crop(out, periphery_overlay_, 0, 0, 18, 25, 279, 183);
    fill(out, 297, 32, 6, 183, 0x40);
    fill(out, 25, 208, 272, 7, 0x40);
    const auto sprite = [&](unsigned slot) {
        const int x = exe_.u16(::bmh::layout::Stadium_PeripheryObjectContains_ExecutableField + slot * 8), y = exe_.u16(::bmh::layout::Stadium_RenderPanorama_RuntimeField + slot * 8);
        const int w = exe_.u16(::bmh::layout::Stadium_RenderPanorama_RuntimeField2 + slot * 8) - x + 1;
        const int h = exe_.u16(::bmh::layout::Stadium_RenderPanorama_RuntimeField3 + slot * 8) - y + 1;
        copy_crop(out, background_, exe_.u16(::bmh::layout::Stadium_RenderPanorama_RuntimeField4 + slot * 4),
                  exe_.u16(::bmh::layout::Stadium_RenderPanorama_RuntimeField5 + slot * 4), x, y, w, h, true);
    };
    unsigned capacity = 0;
    for (const auto& block : state_.blocks) capacity += block.capacity_level;
    if (capacity > 9) sprite(14 + std::min(3u, (capacity - 10) / 10));
    for (unsigned order = 0; order < 14; ++order) {
        const unsigned object = exe_.u8(::bmh::layout::Stadium_RenderPanorama_RuntimeField6 + order);
        const unsigned level = state_.level[object];
        const bool busy = state_.days[object] != 0;
        if ((object < 6 && busy) || level != 0) {
            unsigned slot = object;
            if (object == 0) {
                slot = 18 + unsigned(busy && level != 0);
                if (level != 0) slot += (level - 1) / 85;
            }
            sprite(slot);
        }
        if (!busy) continue;
        const size_t first = exe_.u8(::bmh::layout::Stadium_RenderPanorama_RuntimeField7 + object);
        const uint16_t packed = exe_.u16(::bmh::layout::Stadium_RenderPanorama_ExecutableField + first * 6);
        const unsigned count = packed >> 8;
        for (unsigned part = 0; part < count; ++part) {
            const size_t at = ::bmh::layout::Stadium_RenderPanorama_ExecutableField + (first + part) * 6;
            const unsigned tile = exe_.u16(at) & 0xff;
            const size_t bounds = ::bmh::layout::Stadium_RenderPanorama_RuntimeField8 + tile * 4;
            const int sx = 200 + exe_.u8(bounds), sy = exe_.u8(bounds + 1);
            const int w = exe_.u8(bounds + 2) - exe_.u8(bounds) + 1;
            const int h = exe_.u8(bounds + 3) - sy + 1;
            const bool flip = tile == 8 && ((state_.periphery_animation_tick >> (object == 2)) & 1);
            const int x = flip ? (object == 2 ? 28 : 104) : exe_.u16(at + 2);
            const int y = flip ? 127 : exe_.u16(at + 4);
            if (!flip) copy_crop(out, background_, sx, sy, x, y, w, h, true);
            else for (int yy = 0; yy < h; ++yy) for (int xx = 0; xx < w; ++xx) {
                const auto color = background_.pixels[size_t(sy + yy) * background_.width + sx + w - 1 - xx];
                if (color != 0) out.at(x + xx, y + yy) = color;
            }
        }
    }
    if (periphery_hover_ < 0) return;
    const size_t object = size_t(periphery_hover_);
    copy_crop(out, periphery_overlay_, 8, 7, 26, 32, 180, 27);
    const bool busy = state_.days[object] != 0;
    const std::string title = texts_[text_id(::bmh::layout::Stadium_RenderPanorama_TextPointer + unsigned(object) * 4)] +
        texts_[text_id(busy ? ::bmh::layout::Stadium_RenderPanorama_TextPointer2 : ::bmh::layout::Stadium_RenderPanorama_TextPointer6)];
    if (busy) {
        text_at(out, 26, 37, title, 0x27);
        text_at(out, 26, 45, texts_[text_id(::bmh::layout::Stadium_RenderPanorama_TextPointer3)], 0x6c);
        const std::string remaining = texts_[text_id(::bmh::layout::Stadium_RenderPanorama_TextPointer4)] +
            format_number(GameState{}, state_.days[object], 1) + texts_[text_id(::bmh::layout::Stadium_RenderPanorama_TextPointer5)];
        text_at(out, 155 - font_.text_width(remaining), 45, remaining, 0x5f);
    } else {
        int baseline = 37;
        for (const auto& line : wrap_source_text(data_, title, 129)) {
            text_at(out, 26, baseline, line, 0x27); baseline += font_.height;
        }
        for (unsigned line = 0; line < 3; ++line) {
            const std::array<uint32_t, 3> captions{::bmh::layout::Stadium_RenderPanorama_ExecutableField2, ::bmh::layout::Stadium_RenderBlockExpansion_TextPointer3, ::bmh::layout::Stadium_RenderBlockExpansion_TextPointer2};
            const std::string& value = line == 0 ? state_.periphery_price_text[object] :
                line == 1 ? state_.periphery_duration_text[object] : state_.periphery_balance_text;
            text_at(out, 26, 45 + int(line) * 7, texts_[text_id(captions[line])], 0x6c);
            text_at(out, 155 - font_.text_width(value), 45 + int(line) * 7, value, 0x5f);
        }
    }
}

bool StadiumScreen::periphery_object_contains(int object, int x, int y) const {
    if (object < 0 || object >= 14) return false;
    const size_t at = ::bmh::layout::Stadium_PeripheryObjectContains_ExecutableField + size_t(object) * 8;
    return x >= exe_.u16(at) && y >= exe_.u16(at + 2) &&
        x <= exe_.u16(at + 4) && y <= exe_.u16(at + 6);
}

int StadiumScreen::periphery_object_at(int x, int y) const {
    if (!panorama_ || !state_.investments_enabled) return -1;
    // Source hit order deliberately differs from the back-to-front paint order.
    for (unsigned order = 0; order < 14; ++order) {
        const int object = exe_.u8(::bmh::layout::Stadium_PeripheryObjectAt_RuntimeField + order);
        if ((state_.days[size_t(object)] != 0 || (future_information_ && periphery_available(state_, object))) &&
            (object < 11 || state_.level[size_t(object - 1)] != 0) &&
            periphery_object_contains(object, x, y)) return object;
    }
    return -1;
}

Image StadiumScreen::render_scene() const {
    Image out(320, 240);
    if (state_.decorated_stadium_header) out = header_background_;
    else { fill(out, 0, 4, 320, 13, 0x44); fill(out, 1, 5, 318, 11, 0x39);
           fill(out, 0, 228, 320, 10, 0x44); fill(out, 1, 229, 318, 8, 0x39); }
    const std::string title = state_.original_stadium_title.empty() ? state_.club_name :
        state_.original_stadium_title;
    sprite_font_.draw(out, 172 + (282 - 172) / 2 -
        sprite_font_.text_width(original_to_utf8(title)) / 2, 13, original_to_utf8(title));
    sprite_font_.draw(out, 5, 235, original_to_utf8(state_.original_stadium_footer.empty() ?
        state_.club_name : state_.original_stadium_footer));
    if (panorama_) render_panorama(out);
    else {
        // ConstructionPricePercent draws ColorBottomRightBevel, then the opaque KB38 crop.
        source_shadow(out, 17, 23, 179, 125);
        copy_crop(out, interior_, 0, 0, 17, 23, 179, 125);

        // Original's eight separate block rows (ConstructionPricePercent). These read-only facts
        // retain the block branch's real records and never act as facility rows.
        for (unsigned i = 0; i < state_.blocks.size(); ++i) {
            const int row_y = 22 + int(i) * 24;
            if (state_.solid_panel_background) {
                // LoadGameEndTaxAnimationAssets's alternate panel mode fills the inclusive Source
                // bounds before PaintsFourBorderEdges paints the four border edges.
                fill(out, 219, row_y, 100, 24, 0x42);
            } else {
                // Source crop: (left, top-19) -> (left+1, top+1), 98x22.
                copy_crop(out, panel_, 219, row_y - 19, 220, row_y + 1, 98, 22);
            }
            source_border(out, 219, row_y, 318, row_y + 23);
            const auto& block = state_.blocks[i];
            std::string label = texts_[text_id(::bmh::layout::Stadium_Out_TextPointer)];
            if (label.size() > 6) label[6] = char('A' + i);
            text_at(out, 221, row_y + 7, label, 0x6b);
            if (block.capacity || block.capacity_level) {
                if (!future_information_) {
                    const unsigned available = block.capacity ? 0 : block.capacity_level;
                    const std::string current = format_number(GameState{}, int32_t(available * 1000), 0x85) +
                        texts_[text_id(::bmh::layout::Stadium_FormatError_ExecutableField3)];
                    text_at(out, 221, row_y + 15, current, 0x51);
                    const auto max_places = uint32_t(exe_.u8(::bmh::layout::CareerSession_Invalid_RuntimeField + i)) * 1000;
                    const std::string maximum = texts_[text_id(::bmh::layout::Stadium_Out_TextPointer5)] +
                        format_number(GameState{}, int32_t(max_places), 5) +
                        exe_.cstring(::bmh::layout::Stadium_Out_ExecutableField);
                    text_at(out, 221, row_y + 21, maximum, 0x51);
                    if (block.flags & 1) copy_crop(out, interior_, 180, 1, 307, row_y + 2, 10, 10, true);
                    if (block.flags & 2) copy_crop(out, interior_, 180, 12, 307, row_y + 12, 10, 10, true);
                } else {
                    std::string first;
                    if (block.capacity == 0) {
                        first = texts_[text_id(::bmh::layout::Stadium_Out_TextPointer4)];
                        text_at(out, 221, row_y + 15, first, 0x6a);
                    } else {
                        first = texts_[text_id(::bmh::layout::Stadium_Out_TextPointer2)] +
                            format_number(GameState{}, int32_t(block.capacity * 1000), 5) +
                            exe_.cstring(::bmh::layout::Stadium_Out_ExecutableField);
                        text_at(out, 221, row_y + 15, first, 0x6c);
                        const std::string duration = texts_[text_id(::bmh::layout::Stadium_Out_TextPointer3)] +
                            original_duration_text(GameState{}, texts_, exe_,
                                                   block.construction_weeks, 2, ::bmh::layout::Stadium_FormatError_RuntimeField);
                        text_at(out, 221, row_y + 21, duration, 0x51);
                    }
                    const unsigned pending_flags = (block.flags >> 4) & 3;
                    if (pending_flags & 1) copy_crop(out, interior_, 180, 1, 307, row_y + 2, 10, 10, true);
                    if (pending_flags & 2) copy_crop(out, interior_, 180, 12, 307, row_y + 12, 10, 10, true);
                }
            }
        }

        for (unsigned choice = 0; choice < 8; ++choice) {
            const uint32_t x_table = ::bmh::layout::Stadium_Out_ExecutableField2 + choice * 2;
            const uint32_t y_table = ::bmh::layout::Stadium_Out_ExecutableField3 + choice * 2;
            const int x = exe_.u8(x_table), y = exe_.u8(y_table);
            const int sx = exe_.u16(::bmh::layout::Stadium_Out_RuntimeField + choice * 4);
            const int sy = exe_.u16(::bmh::layout::Stadium_Out_RuntimeField2 + choice * 4);
            const auto& row = state_.investments[choice];
            unsigned visual_level = future_information_ ? (row.current_status >> 4) & 7 : 0;
            if (visual_level == 0) visual_level = row.current_status & 7;
            if (choice == 2 || choice == 3) {
                if (visual_level > 0) --visual_level;
            }
            if (visual_level == 0) {
                copy_crop(out, facilities_, sx, sy, x, y, 0x35, 0x1d);
                copy_crop(out, facilities_, 0x6b, 0x75, x, y, 0x32, 0x1c, true);
            } else {
                const int sprite_y = sy + int(visual_level - 1) * 0x1d;
                copy_crop(out, facilities_, sx, sprite_y, x, y, 0x35, 0x1d);
                if (choice < 2 || choice > 3) {
                    const uint32_t labels = choice < 2 ? ::bmh::layout::Stadium_Out_ExecutableField5 : ::bmh::layout::Stadium_Out_ExecutableField4;
                    const std::string tier = texts_[text_id(labels + visual_level * 4)];
                    const int label_width = font_.text_width(tier);
                    text_at(out, x + (0x33 - label_width) / 2, y + 0x1b, tier, 0);
                    text_at(out, x + (0x35 - label_width) / 2, y + 0x1b, tier, 0x5f);
                }
            }
            if (pressed_ == Pressed::investment && pressed_index_ == int(choice))
                outline(out, x, y, 0x35, 0x1d, 0x27);
            if (future_information_ && row.construction_weeks) {
                const std::string weeks = format_number(GameState{}, row.construction_weeks, 0x82) +
                    texts_[text_id(::bmh::layout::Stadium_Out_TextPointer6)];
                text_at(out, x + 0x20, y + 7, weeks, 0);
                text_at(out, x + 0x1f, y + 6, weeks, 0x5f);
            }
        }
    }
    const std::array<std::string, 3> labels = panorama_
        ? std::array<std::string, 3>{texts_[150], texts_[151], texts_[152]}
        : std::array<std::string, 3>{texts_[153], texts_[154], texts_[155]};
    const std::array<int, 3> bx{2, 125, 224}, bw{115, 90, 90};
    for (int i = 0; i < 3; ++i) {
        // In Source mode 1 the first scene switch is parked off-screen.
        if (i == 0 && !state_.investments_enabled) continue;
        const bool source_selected = (pressed_ == Pressed::scene_button && pressed_index_ == i) ||
            (future_information_ && i == 1);
        original_button(out, bx[size_t(i)], 215, bw[size_t(i)], 9, labels[size_t(i)], source_selected);
    }
    if (!feedback_.empty()) text_at(out, 8, 237, feedback_, 0x5f);
    else if (!state_.action_feedback.empty()) text_at(out, 8, 237, state_.action_feedback, 0x5f);
    return out;
}

Image StadiumScreen::render_investment_dialog(Image out) const {
    if (!state_.selected_investment) return out;
    const unsigned choice = unsigned(*state_.selected_investment);
    if (choice >= state_.investments.size()) return out;
    const auto& row = state_.investments[choice];
    source_frame(out, widgets_, exe_, 1, 22, 214, 128, false); // Front flags18.
    const std::string title = investment_name(row) + texts_[text_id(::bmh::layout::Stadium_RenderInvestmentDialog_TextPointer)];
    const int title_width = heading_font_.text_width(title);
    const int title_y = 33 - heading_font_.baseline;
    if (title_y >= 0) heading_font_.draw(out, 1 + (215 - title_width) / 2, title_y,
                                         title, 0x5f);

    // Dynamic captions come from TEXTD; numeric values are projected by the owner.
    text_at(out, 5, 46, texts_[text_id(::bmh::layout::Stadium_RenderBlockExpansion_TextPointer2)], 0x6c);
    text_at(out, 5, 54, texts_[text_id(::bmh::layout::Stadium_RenderBlockExpansion_TextPointer3)], 0x6c);
    const std::string balance = state_.balance_text.empty() ? std::to_string(state_.balance) : state_.balance_text;
    const int bw = font_.text_width(balance);
    text_at(out, 148 - bw, 46, balance, 0x6c);
    const int weeks_value = exe_.u8(::bmh::layout::CareerSession_DispatchStadium_RuntimeField2 + (choice + 14) * 5);
    const std::string weeks = original_duration_text(GameState{}, texts_, exe_, weeks_value, 0x82, ::bmh::layout::Stadium_FormatError_RuntimeField);
    text_at(out, 148 - font_.text_width(weeks), 54, weeks, 0x6c);
    if (state_.investment_text_variant >= 0) {
        const std::string label = texts_[text_id(::bmh::layout::Stadium_RenderBlockExpansion_TextPointer4)];
        const std::string variant = format_number(GameState{}, state_.investment_text_variant, 0x82) +
            texts_[text_id(::bmh::layout::Stadium_RenderBlockExpansion_TextPointer5)];
        text_at(out, 5, 70, label, 0x6c);
        text_at(out, 148 - font_.text_width(variant), 70, variant, 0x6c);
    }

    for (size_t i = 0; i < row.options.size() && i < 3; ++i) {
        const auto& option = row.options[i];
        const int x = 5 + int(i) * 60;
        const int sx = exe_.u16(::bmh::layout::Stadium_Out_RuntimeField + choice * 4);
        const int source_y = exe_.u16(::bmh::layout::Stadium_Out_RuntimeField2 + choice * 4);
        const int sy = (choice == 2 || choice == 3) ? source_y :
            source_y + int(option.target_level - 1) * 0x1d;
        copy_crop(out, facilities_, sx, sy, x, 74, 0x35, 0x1d);
        if (choice != 2 && choice != 3) source_shadow(out, x, 74, 0x35, 0x1d);
        if ((state_.selected_option && *state_.selected_option == int(i)) ||
            (pressed_ == Pressed::option && pressed_index_ == int(i)))
            outline(out, x, 74, 0x35, 0x1d, 0x27);
        const uint32_t labels = choice < 2 ? ::bmh::layout::Stadium_Out_ExecutableField5 : ::bmh::layout::Stadium_Out_ExecutableField4;
        const std::string tier = (choice == 2 || choice == 3) ? std::string{} :
            texts_[text_id(labels + uint32_t(option.target_level) * 4)];
        if (!tier.empty()) {
            const int w = font_.text_width(tier);
            text_at(out, x + (0x35 - w) / 2, 110, tier, 0x6c);
        }
        const std::string price = i < row.option_price_text.size() ?
            row.option_price_text[i] : std::to_string(option.base_price);
        text_at(out, x + (0x35 - font_.text_width(price)) / 2, 117, price,
                choice == 2 || choice == 3 ? 0x5f : 0x6c);
    }
    original_button(out, 7, 136, 70, 9, texts_[156], pressed_ == Pressed::build);
    original_button(out, 107, 136, 70, 9, texts_[157], pressed_ == Pressed::cancel);
    return out;
}

int StadiumScreen::data_control_at(int x, int y) const {
    for (int curve = 0; curve < 6; ++curve) {
        const int bx = 27 + (curve % 2) * 120, by = 107 + (curve / 2) * 11;
        if (x >= bx && x <= bx + 96 && y >= by && y <= by + 10) return curve;
    }
    for (int scale = 0; scale < 2; ++scale) {
        const int by = 111 + scale * 11;
        if (x >= 265 && x <= 295 && y >= by && y <= by + 9) return 6 + scale;
    }
    for (int price = 10; price <= 25; ++price) {
        const int bx = 59 + (price - 10) * 16;
        if (x >= bx && x <= bx + 14 && y >= 225 && y <= 234) return 8 + price - 10;
    }
    return -1;
}

Image StadiumScreen::render_data() const {
    const auto& data = *state_.data;
    Image out = header_background_;
    if (state_.plain_background) { fill(out, 0, 17, 320, 223, 0);
        fill(out, 0, 4, 320, 13, 0x44); fill(out, 1, 5, 318, 11, 0x39); }
    const auto title = original_to_utf8(state_.original_stadium_title.empty() ? state_.club_name : state_.original_stadium_title);
    sprite_font_.draw(out, 172 + 55 - sprite_font_.text_width(title) / 2, 13, title);
    const auto line = [&](int x, int y, int end_x, int end_y, uint8_t color) {
        const int dx = std::abs(end_x - x), sx = x < end_x ? 1 : -1;
        const int dy = std::abs(end_y - y), sy = y < end_y ? 1 : -1;
        int error = 0;
        for (int step = 0; step <= std::max(dx, dy); ++step) {
            if (x >= 0 && x < 320 && y >= 0 && y < 240) out.at(x, y) = color;
            if (dx > dy) { x += sx; error += dy; if (error >= dx) { error -= dx; y += sy; } }
            else if (dy) { y += sy; error += dx; if (error >= dy) { error -= dy; x += sx; } }
        }
    };
    for (int edge = 0; edge < 4; ++edge)
        line(exe_.u16(::bmh::layout::Analytics_Render_RuntimeField3 + edge * 4 + 2), exe_.u8(::bmh::layout::Analytics_Render_RuntimeField2 + edge * 2 + 1) - 35,
             exe_.u16(::bmh::layout::Analytics_Render_RuntimeField3 + edge * 4), exe_.u8(::bmh::layout::Analytics_Render_RuntimeField2 + edge * 2) - 35, exe_.u8(::bmh::layout::Analytics_Render_RuntimeField + edge));
    for (int curve = 0; curve < 6; ++curve) {
        const int x = 27 + (curve % 2) * 120, y = 107 + (curve / 2) * 11;
        const uint8_t color = exe_.u8(::bmh::layout::Analytics_FormatError_RuntimeField + curve);
        fill(out, x - 7, y + 3, 5, 4, color);
        line(x - 8, y + 2, x - 2, y + 2, 0x68); line(x - 8, y + 2, x - 8, y + 7, 0x68);
        line(x - 2, y + 3, x - 2, y + 7, 0x62); line(x - 8, y + 7, x - 2, y + 7, 0x62);
        out.at(x - 8, y + 2) = 0x6a;
        original_button(out, x, y, 96, 9, exe_.cstring(exe_.u32(::bmh::layout::Analytics_FormatError_ExecutableField + (6 + curve) * 4)),
                        (data.flags & (1 << curve)) != 0 || (pressed_ == Pressed::data_control && pressed_index_ == curve));
        if (!(data.flags & (1 << curve))) continue;
        const auto points = stadium_data_points(data, exe_, curve);
        for (size_t sample = 0; sample < points.size(); ++sample) {
            const auto [px, py] = points[sample];
            if (sample) line(points[sample - 1].first, points[sample - 1].second, px, py, color);
            fill(out, px - 1, py - 1, 3, 3, color);
        }
    }
    for (int scale = 0; scale < 2; ++scale)
        original_button(out, 265, 111 + scale * 11, 30, 9, texts_[text_id(::bmh::layout::ClubPlayerDetail_RenderStatistics_ExecutableField + scale * 4)],
            (data.flags & (1 << (6 + scale))) != 0 || (pressed_ == Pressed::data_control && pressed_index_ == 6 + scale));
    if (state_.solid_panel_background) fill(out, 0, 145, 320, 95, 0x42);
    else copy_crop(out, panel_, 0, 126, 1, 146, 318, 93);
    source_border(out, 0, 145, 319, 239);
    fill(out, 1, 227, 318, 12, 0x42);
    const auto centered = [&](int left, int right, int baseline, std::string_view text, uint8_t color) {
        text_at(out, left + (right - left) / 2 - font_.text_width(text) / 2, baseline, text, color);
    };
    for (int kind = 0; kind < 3; ++kind)
        centered(60 + kind * 85, 145 + kind * 85, 154, texts_[text_id(::bmh::layout::Stadium_CopyCrop_TextPointer + kind * 4)], 0x6a);
    int y = 164;
    for (int row = 0; row < 7; ++row) {
        text_at(out, 3, y, texts_[text_id(::bmh::layout::Stadium_CopyCrop_TextPointer2 + row * 4)], 0x6b);
        for (int kind = 0; kind < 3; ++kind) {
            const auto& text = data.table[size_t(row)][size_t(kind)];
            const int shift = row < 4 && !text.empty() && text[0] == '(' ? 1 : 0;
            centered(60 + kind * 85, 145 + kind * 85, y + shift, text, 0x6c);
        }
        y += row == 1 || row == 3 ? 9 : 7;
    }
    // TextId restores this seven-pixel region from TextId2's saved
    // background; it adds no field frame or shadow.
    const auto needed = texts_[text_id(::bmh::layout::Stadium_CopyCrop_TextPointer4)] + std::to_string(data.ticket_price) + texts_[text_id(::bmh::layout::Stadium_CopyCrop_TextPointer5)];
    text_at(out, 59, 222, needed, 0x6b);
    text_at(out, 313 - font_.text_width(data.required_attendance_text), 222, data.required_attendance_text, 0x6c);
    text_at(out, 3, 231, texts_[text_id(::bmh::layout::Stadium_CopyCrop_TextPointer3)], 0x6a);
    for (int price = 10; price <= 25; ++price)
        original_button(out, 59 + (price - 10) * 16, 225, 14, 9, std::to_string(price),
            data.ticket_price == price || (pressed_ == Pressed::data_control && pressed_index_ == price - 10 + 8));
    return out;
}

Image StadiumScreen::render() const {
    if (state_.data) return render_data();
    if (state_.construction_tip_dialog && tip_dialog_) {
        const auto pressed = pressed_ == Pressed::tip_yes ? GameEndTaxDialog::Button::yes :
            pressed_ == Pressed::tip_no ? GameEndTaxDialog::Button::no : GameEndTaxDialog::Button::none;
        return tip_dialog_->render(pressed);
    }
    Image out = state_.block_expansion
        ? render_block_expansion(render_scene())
        : render_investment_dialog(render_scene());
    if (block_calculator_open_) return render_block_calculator(std::move(out));
    if (periphery_dialog_) {
        std::optional<GameEndTaxDialog::Button> button;
        if (pressed_ == Pressed::periphery_yes) button = GameEndTaxDialog::Button::yes;
        if (pressed_ == Pressed::periphery_no) button = GameEndTaxDialog::Button::no;
        return periphery_dialog_->render(button);
    }
    return out;
}

Image StadiumScreen::construction_notice_backdrop() const {
    return state_.block_expansion ? render_block_expansion(render_scene())
                                  : render_investment_dialog(render_scene());
}

bool StadiumScreen::text(std::string_view input) {
    if (pending_ || calculator_source_waiting() || !block_calculator_open_ ||
        !state_.block_expansion || input.empty()) return false;
    for (const char c : input) if (c < '0' || c > '9') return false;
    const uint32_t maximum = state_.block_expansion->quote.max_places;
    bool changed = false;
    for (const char c : input) {
        if (block_calculator_digits_ >= 10) continue;
        block_calculator_value_ = uint32_t(std::min<uint64_t>(
            uint64_t(block_calculator_value_) * 10 + unsigned(c - '0'), maximum));
        ++block_calculator_digits_;
        // Source recomputes local18 from the numeric text only for nonzero
        // values. Zero input must retain its separately counted digits.
        if (block_calculator_value_ != 0)
            block_calculator_digits_ = unsigned(std::to_string(block_calculator_value_).size());
        changed = true;
    }
    if (!changed) return false;
    block_calculator_text_ = std::to_string(block_calculator_value_);
    block_calculator_edited_ = true;
    return true;
}

bool StadiumScreen::calculator_drag_to(int x, int y) {
    if (!block_calculator_open_ || block_calculator_cancel_wait_) return false;
    x = std::clamp(x, 0, 207);
    y = std::clamp(y, 0, 104);
    if (block_calculator_x_ == x && block_calculator_y_ == y) return false;
    block_calculator_x_ = x; block_calculator_y_ = y;
    return true;
}

void StadiumScreen::calculator_mark_presented() {
    if (block_calculator_pressed_key_ >= 0 || block_calculator_cancel_wait_)
        block_calculator_presented_ = true;
}

bool StadiumScreen::calculator_advance_source_ticks(unsigned ticks) {
    if (!calculator_source_waiting() || !block_calculator_presented_ || ticks == 0) return false;
    const unsigned before = block_calculator_ticks_;
    block_calculator_ticks_ = ticks >= before ? 0 : before - ticks;
    if (block_calculator_ticks_ == 0) {
        block_calculator_presented_ = false;
        if (block_calculator_cancel_wait_) {
            block_calculator_cancel_wait_ = false;
            block_calculator_open_ = false;
            block_calculator_text_.clear();
        }
        else {
            block_calculator_pressed_key_ = -1;
            if (block_calculator_keyboard_j_) {
                block_calculator_keyboard_j_ = false;
                (void)accept_block_calculator_value();
            }
        }
    }
    return block_calculator_ticks_ != before || before == 0;
}

bool StadiumScreen::calculator_source_waiting() const {
    return block_calculator_ticks_ != 0 &&
        (block_calculator_pressed_key_ >= 0 || block_calculator_cancel_wait_);
}

bool StadiumScreen::calculator_waiting_for_key_release() const {
    return pressed_ == Pressed::block_calculator_key && !block_calculator_keyboard_j_;
}

StadiumRequest StadiumScreen::accept_block_calculator_value() {
    if (!state_.block_expansion || pending_ || calculator_source_waiting()) return {};
    const uint32_t minimum = uint32_t(state_.block_expansion->quote.current_capacity_thousands) * 1000;
    if (block_calculator_value_ < minimum) {
        // Source clamps the first confirmation to C and stays in the keypad.
        block_calculator_value_ = minimum;
        block_calculator_text_ = std::to_string(minimum);
        if (minimum != 0) block_calculator_digits_ = unsigned(block_calculator_text_.size());
        block_calculator_edited_ = true;
        return {};
    }
    const auto result = block_request(StadiumRequestKind::expansion_set_total,
                                     block_calculator_value_,
                                     state_.block_expansion->desired_features);
    block_calculator_open_ = false;
    pending_ = result;
    return result;
}

StadiumRequest StadiumScreen::activate_block_calculator_key(int key_index) {
    if (!state_.block_expansion || !block_calculator_open_ || pending_) return {};
    if (key_index >= 0 && key_index <= 9) {
        text(std::string_view("0123456789").substr(size_t(key_index), 1));
        return {};
    }
    switch (key_index) {
    case 10: // D: delete the last digit
        if (block_calculator_digits_ != 0) {
            block_calculator_value_ /= 10;
            --block_calculator_digits_;
        }
        block_calculator_text_ = std::to_string(block_calculator_value_);
        if (block_calculator_value_ != 0) block_calculator_digits_ = unsigned(block_calculator_text_.size());
        block_calculator_edited_ = true;
        return {};
    case 11: // R: restore the Original initial amount
        block_calculator_value_ = state_.block_expansion->quote.new_places;
        block_calculator_text_ = std::to_string(block_calculator_value_);
        block_calculator_digits_ = unsigned(block_calculator_text_.size());
        block_calculator_edited_ = true;
        return {};
    case 12: // C: clear
        block_calculator_value_ = 0;
        block_calculator_text_ = "0";
        block_calculator_digits_ = 0;
        block_calculator_edited_ = true;
        return {};
    case 13: return accept_block_calculator_value(); // J: accept
    case 14: // N: cancel local calculator edit
        block_calculator_value_ = UINT32_MAX;
        block_calculator_text_ = std::to_string(block_calculator_value_);
        block_calculator_edited_ = true;
        block_calculator_pressed_key_ = -1;
        block_calculator_cancel_wait_ = true;
        block_calculator_ticks_ = 50;
        block_calculator_presented_ = false;
        return {};
    default: return {};
    }
}

bool StadiumScreen::pointer_down(int x, int y, bool right) {
    if (pending_ || calculator_source_waiting()) return false;
    pressed_ = Pressed::none; pressed_index_ = -1; pressed_x_ = x; pressed_y_ = y;
    if (state_.data) {
        if (right) { pending_ = request(StadiumRequestKind::close_data); return true; }
        pressed_index_ = data_control_at(x, y);
        if (pressed_index_ >= 0) { pressed_ = Pressed::data_control; return true; }
        return false;
    }
    if (state_.construction_tip_dialog && tip_dialog_) {
        if (right) return false;
        const auto button = tip_dialog_->hit_test(x, y);
        pressed_ = button == GameEndTaxDialog::Button::yes ? Pressed::tip_yes :
            button == GameEndTaxDialog::Button::no ? Pressed::tip_no : Pressed::none;
        return pressed_ != Pressed::none;
    }
    if (block_calculator_open_ && state_.block_expansion) {
        if (right || calculator_source_waiting()) return false;
        const int x0 = block_calculator_x_, y0 = block_calculator_y_;
        for (int i = 0; i < 15; ++i) {
            const int key_x = x0 + exe_.u16(::bmh::layout::Advertising_Click_RuntimeField + size_t(i) * 2);
            const int key_y = y0 + exe_.u8(::bmh::layout::Advertising_Click_RuntimeField2 + size_t(i));
            if (x >= key_x && x <= key_x + 16 && y >= key_y && y <= key_y + 14) {
                if (i == 14) {
                    // Source N jumps straight to4ad4e: restore the backdrop,
                    // wait50 after its Present, and never flash the N key.
                    (void)activate_block_calculator_key(i);
                    return true;
                }
                pressed_ = Pressed::block_calculator_key; pressed_index_ = i;
                block_calculator_pressed_key_ = i;
                block_calculator_ticks_ = 4; block_calculator_presented_ = false;
                return true;
            }
        }
        const auto& quote = state_.block_expansion->quote;
        const uint32_t current = uint32_t(quote.current_capacity_thousands) * 1000;
        for (size_t i = 0; i < 8; ++i) {
            uint32_t value = i < 6 ? current + uint32_t(i) * 1000 :
                i == 6 ? current : quote.max_places;
            if (value > quote.max_places) continue;
            const int key_x = x0 + exe_.u16(::bmh::layout::Advertising_Click_RuntimeField + (i + 15) * 2);
            const int key_y = y0 + exe_.u8(::bmh::layout::Advertising_Click_RuntimeField2 + i + 15);
            if (x >= key_x && x <= key_x + 53 && y >= key_y && y <= key_y + 14) {
                pressed_ = Pressed::block_calculator_preset; pressed_index_ = int(i);
                return true;
            }
        }
        if (x >= x0 && x <= x0 + 113 && y >= y0 && y <= y0 + 16) {
            pressed_ = Pressed::block_calculator_drag;
            return true;
        }
        return false;
    }
    if (state_.block_expansion) {
        if (right) {
            // PrepareStandBlockOffer's nested detail loop treats physical event 8 as an
            // editor exit. It returns to the A–H overview without accepting
            // the quote or consuming any construction RNG.
            pending_ = block_request(StadiumRequestKind::expansion_cancel);
            return true;
        }
        if (x >= 7 && x <= 76 && y >= 136 && y <= 145) pressed_ = Pressed::block_build;
        else if (x >= 107 && x <= 176 && y >= 136 && y <= 145) pressed_ = Pressed::block_cancel;
        else if (x >= 5 && x <= 99 && y >= 74 && y <= 99) pressed_ = Pressed::block_amount;
        else if (x >= 120 && x <= 210 && y >= 73 && y <= 85 &&
                 !(state_.block_expansion->quote.current_raw_flags & 1)) pressed_ = Pressed::block_seats;
        else if (x >= 120 && x <= 210 && y >= 88 && y <= 100 &&
                 !(state_.block_expansion->quote.current_raw_flags & 2)) pressed_ = Pressed::block_roof;
        return pressed_ != Pressed::none;
    }
    if (state_.quoted_object) {
        if (right) return false;
        if (!periphery_dialog_) return false;
        const auto button = periphery_dialog_->hit_test(x, y);
        if (button == GameEndTaxDialog::Button::yes) pressed_ = Pressed::periphery_yes;
        else if (button == GameEndTaxDialog::Button::no) pressed_ = Pressed::periphery_no;
        return pressed_ != Pressed::none;
    }
    if (state_.selected_investment) {
        if (right) return false;
        if (x >= 7 && x <= 76 && y >= 136 && y <= 145) pressed_ = Pressed::build;
        else if (x >= 107 && x <= 176 && y >= 136 && y <= 145) pressed_ = Pressed::cancel;
        else if (y >= 74 && y <= 103 && x >= 5 && x < 5 + int(state_.investments[size_t(*state_.selected_investment)].options.size()) * 60) {
            const int option = (x - 5) / 60;
            pressed_ = Pressed::option; pressed_index_ = option;
        }
        return pressed_ != Pressed::none;
    }
    if (right) {
        // ConstructionPricePercent's outer scene recognizes right-button event 8 and exits the
        // stadium immediately. The detail and tip loops above ignore it.
        pending_ = request(StadiumRequestKind::close);
        return true;
    }
    if (y >= 215 && y <= 224) {
        if (x >= 2 && x < 117) {
            if (!state_.investments_enabled) return false;
            pressed_ = Pressed::scene_button; pressed_index_ = 0; return true;
        }
        if (x >= 125 && x < 215) { pressed_ = Pressed::scene_button; pressed_index_ = 1; return true; }
        if (x >= 224 && x < 314) { pressed_ = Pressed::scene_button; pressed_index_ = 2; return true; }
    }
    if (!panorama_ && y >= 153 && y <= 211 && x >= 3 && x <= 217) {
        const int col = (x - 3) / 54, row = (y - 153) / 30;
        const int choice = row * 4 + col;
        if (choice >= 0 && choice < 8 && state_.investments_enabled && state_.investments[size_t(choice)].available) {
            pressed_ = Pressed::investment; pressed_index_ = choice; return true;
        }
        return false;
    }
    if (panorama_) {
        periphery_hover_ = periphery_object_at(x, y);
        if (future_information_ && periphery_available(state_, periphery_hover_)) {
            pressed_ = Pressed::periphery_item; pressed_index_ = periphery_hover_; return true;
        }
        return false;
    }
    if (!panorama_ && x >= 219 && x <= 319 && y >= 22 && y <= 213) {
        const int slot = (y - 22) / 24;
        if (slot >= 0 && slot < 8 && state_.blocks[size_t(slot)].construction_weeks == 0) {
            pressed_ = Pressed::block_row; pressed_index_ = slot;
            return true;
        }
        return false;
    }
    return false;
}

bool StadiumScreen::pointer_move(int x, int y) {
    if (calculator_source_waiting()) return false;
    if (pressed_ == Pressed::none) {
        if (!panorama_ || pending_ || state_.quoted_object || state_.data) return false;
        const int hover = periphery_object_at(x, y);
        if (hover == periphery_hover_) return false;
        periphery_hover_ = hover;
        return true;
    }
    if (pressed_ == Pressed::block_calculator_drag) {
        calculator_drag_to(block_calculator_x_ + (x - pressed_x_), block_calculator_y_ + (y - pressed_y_));
        pressed_x_ = x; pressed_y_ = y;
        return true;
    }
    bool inside = false;
    switch (pressed_) {
    case Pressed::scene_button:
        inside = y >= 215 && y <= 224 &&
            ((pressed_index_ == 0 && x >= 2 && x < 117) || (pressed_index_ == 1 && x >= 125 && x < 215) ||
             (pressed_index_ == 2 && x >= 224 && x < 314)); break;
    case Pressed::investment:
        inside = !panorama_ && x >= 3 && x <= 217 && y >= 153 && y <= 211 &&
            ((y - 153) / 30) * 4 + (x - 3) / 54 == pressed_index_; break;
    case Pressed::option:
        inside = x >= 5 + pressed_index_ * 60 && x < 5 + (pressed_index_ + 1) * 60 && y >= 74 && y <= 103; break;
    case Pressed::build: inside = x >= 7 && x <= 76 && y >= 136 && y <= 145; break;
    case Pressed::cancel: inside = x >= 107 && x <= 176 && y >= 136 && y <= 145; break;
    case Pressed::tip_yes: inside = x >= 62 && x <= 137 && y >= 166 && y <= 176; break;
    case Pressed::tip_no: inside = x >= 175 && x <= 250 && y >= 166 && y <= 176; break;
    case Pressed::periphery_item: inside = panorama_ && future_information_ &&
        periphery_object_at(x, y) == pressed_index_; break;
    case Pressed::periphery_yes: inside = periphery_dialog_ && periphery_dialog_->hit_test(x, y) == GameEndTaxDialog::Button::yes; break;
    case Pressed::periphery_no: inside = periphery_dialog_ && periphery_dialog_->hit_test(x, y) == GameEndTaxDialog::Button::no; break;
    case Pressed::close: inside = x < 50 && y > 218; break;
    case Pressed::block_row: inside = !panorama_ && x >= 219 && x <= 319 && y >= 22 && y <= 213 &&
        (y - 22) / 24 == pressed_index_; break;
    case Pressed::block_amount: inside = x >= 5 && x <= 99 && y >= 74 && y <= 99; break;
    case Pressed::block_seats: inside = x >= 120 && x <= 210 && y >= 73 && y <= 85; break;
    case Pressed::block_roof: inside = x >= 120 && x <= 210 && y >= 88 && y <= 100; break;
    case Pressed::block_build: inside = x >= 7 && x <= 76 && y >= 136 && y <= 145; break;
    case Pressed::block_cancel: inside = x >= 107 && x <= 176 && y >= 136 && y <= 145; break;
    case Pressed::block_calculator_key: {
        const int x0 = block_calculator_x_, y0 = block_calculator_y_;
        const int key_x = x0 + exe_.u16(::bmh::layout::Advertising_Click_RuntimeField + size_t(pressed_index_) * 2);
        const int key_y = y0 + exe_.u8(::bmh::layout::Advertising_Click_RuntimeField2 + size_t(pressed_index_));
        inside = x >= key_x && x <= key_x + 16 && y >= key_y && y <= key_y + 14;
        break;
    }
    case Pressed::block_calculator_preset: {
        const int x0 = block_calculator_x_, y0 = block_calculator_y_;
        const int key_x = x0 + exe_.u16(::bmh::layout::Advertising_Click_RuntimeField + size_t(pressed_index_ + 15) * 2);
        const int key_y = y0 + exe_.u8(::bmh::layout::Advertising_Click_RuntimeField2 + size_t(pressed_index_ + 15));
        inside = x >= key_x && x <= key_x + 53 && y >= key_y && y <= key_y + 14;
        break;
    }
    case Pressed::data_control: inside = state_.data && data_control_at(x, y) == pressed_index_; break;
    case Pressed::none: break;
    }
    if (!inside) {
        if (pressed_ == Pressed::block_calculator_key || pressed_ == Pressed::block_calculator_preset) {
            block_calculator_pressed_key_ = -1;
            if (pressed_ == Pressed::block_calculator_key) block_calculator_ticks_ = 0;
            block_calculator_presented_ = false;
        }
        pressed_ = Pressed::none; pressed_index_ = -1; return true;
    }
    return false;
}

StadiumRequest StadiumScreen::release(int x, int y, bool right) {
    if (pending_) return {};
    if (calculator_source_waiting()) return {};
    const Pressed pressed = pressed_;
    const int index = pressed_index_;
    const int down_x = pressed_x_, down_y = pressed_y_;
    pressed_ = Pressed::none; pressed_index_ = -1; pressed_x_ = pressed_y_ = -1;
    if (right || pressed == Pressed::none) return {};
    // Validate UP against the same source button, while allowing movement within it.
    const bool released_inside =
        (pressed == Pressed::data_control && state_.data && data_control_at(x, y) == index) ||
        (pressed == Pressed::scene_button && y >= 215 && y <= 224 &&
            ((index == 0 && x >= 2 && x < 117) || (index == 1 && x >= 125 && x < 215) || (index == 2 && x >= 224 && x < 314))) ||
        (pressed == Pressed::investment && !panorama_ && x >= 3 && x <= 217 && y >= 153 && y <= 211 &&
            ((y - 153) / 30) * 4 + (x - 3) / 54 == index) ||
        (pressed == Pressed::option && x >= 5 + index * 60 && x < 5 + (index + 1) * 60 && y >= 74 && y <= 103) ||
        (pressed == Pressed::build && x >= 7 && x <= 76 && y >= 136 && y <= 145) ||
        (pressed == Pressed::cancel && x >= 107 && x <= 176 && y >= 136 && y <= 145) ||
        (pressed == Pressed::tip_yes && x >= 62 && x <= 137 && y >= 166 && y <= 176) ||
        (pressed == Pressed::tip_no && x >= 175 && x <= 250 && y >= 166 && y <= 176) ||
        (pressed == Pressed::periphery_item && panorama_ && future_information_ && periphery_object_at(x, y) == index) ||
        (pressed == Pressed::periphery_yes && periphery_dialog_ && periphery_dialog_->hit_test(x, y) == GameEndTaxDialog::Button::yes) ||
        (pressed == Pressed::periphery_no && periphery_dialog_ && periphery_dialog_->hit_test(x, y) == GameEndTaxDialog::Button::no) ||
        (pressed == Pressed::block_row && !panorama_ && x >= 219 && x <= 319 && y >= 22 && y <= 213 && (y - 22) / 24 == index) ||
        (pressed == Pressed::block_amount && x >= 5 && x <= 99 && y >= 74 && y <= 99) ||
        (pressed == Pressed::block_seats && x >= 120 && x <= 210 && y >= 73 && y <= 85) ||
        (pressed == Pressed::block_roof && x >= 120 && x <= 210 && y >= 88 && y <= 100) ||
        (pressed == Pressed::block_build && x >= 7 && x <= 76 && y >= 136 && y <= 145) ||
        (pressed == Pressed::block_cancel && x >= 107 && x <= 176 && y >= 136 && y <= 145) ||
        (pressed == Pressed::block_calculator_key && x >= block_calculator_x_ + exe_.u16(::bmh::layout::Advertising_Click_RuntimeField + size_t(index) * 2) &&
            x <= block_calculator_x_ + exe_.u16(::bmh::layout::Advertising_Click_RuntimeField + size_t(index) * 2) + 16 &&
            y >= block_calculator_y_ + exe_.u8(::bmh::layout::Advertising_Click_RuntimeField2 + size_t(index)) && y <= block_calculator_y_ + exe_.u8(::bmh::layout::Advertising_Click_RuntimeField2 + size_t(index)) + 14) ||
        (pressed == Pressed::block_calculator_preset && x >= block_calculator_x_ + exe_.u16(::bmh::layout::Advertising_Click_RuntimeField + size_t(index + 15) * 2) &&
            x <= block_calculator_x_ + exe_.u16(::bmh::layout::Advertising_Click_RuntimeField + size_t(index + 15) * 2) + 53 &&
            y >= block_calculator_y_ + exe_.u8(::bmh::layout::Advertising_Click_RuntimeField2 + size_t(index + 15)) && y <= block_calculator_y_ + exe_.u8(::bmh::layout::Advertising_Click_RuntimeField2 + size_t(index + 15)) + 14) ||
        (pressed == Pressed::block_calculator_drag);
    if (!released_inside || down_x < 0 || down_y < 0) return {};
    const auto emit = [this](StadiumRequest result) { pending_ = result; return result; };
    if (pressed == Pressed::data_control) {
        if (index < 6) return emit(request(StadiumRequestKind::toggle_data_curve, index));
        if (index < 8) return emit(request(StadiumRequestKind::toggle_data_scale, index - 6));
        return emit(request(StadiumRequestKind::set_data_ticket_price, index - 8 + 10));
    }
    if (pressed == Pressed::scene_button) {
        if (index == 0) { panorama_ = !panorama_; future_information_ = false; periphery_hover_ = -1; return {}; }
        if (index == 1) { future_information_ = !future_information_; periphery_hover_ = -1; return {}; }
        return emit(request(StadiumRequestKind::open_data));
    }
    if (pressed == Pressed::investment)
        return emit(request(StadiumRequestKind::select_investment, index));
    if (pressed == Pressed::periphery_item)
        return emit(request(StadiumRequestKind::quote_periphery, index));
    if (pressed == Pressed::option && state_.selected_investment) {
        const auto& options = state_.investments[size_t(*state_.selected_investment)].options;
        if (index < 0 || size_t(index) >= options.size()) return {};
        return emit(request(StadiumRequestKind::select_investment_option,
                       int(*state_.selected_investment), index, int32_t(options[size_t(index)].base_price)));
    }
    if (pressed == Pressed::build && state_.selected_investment && state_.selected_option) {
        const auto& row = state_.investments[size_t(*state_.selected_investment)];
        if (*state_.selected_option < 0 || size_t(*state_.selected_option) >= row.options.size()) return {};
        const auto& option = row.options[size_t(*state_.selected_option)];
        return emit(request(StadiumRequestKind::confirm_investment, int(*state_.selected_investment),
                       *state_.selected_option, int32_t(option.base_price)));
    }
    if (pressed == Pressed::cancel && state_.selected_investment)
        return emit(request(StadiumRequestKind::cancel_investment, int(*state_.selected_investment)));
    if (pressed == Pressed::tip_yes || pressed == Pressed::tip_no) {
        if (state_.block_expansion) {
            auto result = block_request(StadiumRequestKind::expansion_answer_tip);
            result.accept_tip = pressed == Pressed::tip_yes;
            return emit(std::move(result));
        }
        int32_t price = 0;
        if (state_.selected_investment && state_.selected_option) {
            const auto& options = state_.investments[size_t(*state_.selected_investment)].options;
            if (*state_.selected_option >= 0 && size_t(*state_.selected_option) < options.size())
                price = int32_t(pressed == Pressed::tip_yes ? state_.construction_tip_price :
                                options[size_t(*state_.selected_option)].base_price);
        }
        return emit(request(StadiumRequestKind::answer_construction_tip,
                       state_.selected_investment ? int(*state_.selected_investment) : -1,
                       state_.selected_option.value_or(-1), price, pressed == Pressed::tip_yes));
    }
    if (pressed == Pressed::periphery_yes && state_.quoted_object)
        return emit(request(StadiumRequestKind::build_periphery, *state_.quoted_object, -1, state_.quoted_price));
    if (pressed == Pressed::periphery_no && state_.quoted_object)
        return emit(request(StadiumRequestKind::decline_periphery, *state_.quoted_object, -1, state_.quoted_price));
    if (pressed == Pressed::block_row) {
        auto result = request(StadiumRequestKind::expansion_select);
        result.block_slot = index;
        return emit(std::move(result));
    }
    if (pressed == Pressed::block_amount && state_.block_expansion) {
        block_calculator_open_ = true;
        block_calculator_value_ = state_.block_expansion->quote.new_places;
        block_calculator_text_ = std::to_string(block_calculator_value_);
        block_calculator_digits_ = block_calculator_value_ == 0 ? 0 : unsigned(block_calculator_text_.size());
        block_calculator_cancel_wait_ = false;
        block_calculator_keyboard_j_ = false;
        block_calculator_pressed_key_ = -1;
        block_calculator_ticks_ = 0;
        block_calculator_presented_ = false;
        block_calculator_edited_ = false;
        return {};
    }
    if (pressed == Pressed::block_seats || pressed == Pressed::block_roof) {
        if (!state_.block_expansion) return {};
        const uint8_t bit = pressed == Pressed::block_seats ? 1 : 2;
        const uint8_t desired = uint8_t(state_.block_expansion->desired_features ^ bit);
        return emit(block_request(StadiumRequestKind::expansion_toggle_feature,
                                  state_.block_expansion->total_target_places, desired));
    }
    if (pressed == Pressed::block_build && state_.block_expansion)
        return emit(block_request(StadiumRequestKind::expansion_confirm));
    if (pressed == Pressed::block_cancel && state_.block_expansion)
        return emit(block_request(StadiumRequestKind::expansion_cancel));
    if (pressed == Pressed::block_calculator_key)
        return activate_block_calculator_key(index);
    if (pressed == Pressed::block_calculator_preset && state_.block_expansion) {
        const auto& quote = state_.block_expansion->quote;
        const uint32_t current = uint32_t(quote.current_capacity_thousands) * 1000;
        block_calculator_value_ = size_t(index) < 6 ? current + uint32_t(index) * 1000 :
            size_t(index) == 6 ? current : quote.max_places;
        block_calculator_text_ = std::to_string(block_calculator_value_);
        if (block_calculator_value_ != 0) block_calculator_digits_ = unsigned(block_calculator_text_.size());
        block_calculator_edited_ = true;
    }
    if (pressed == Pressed::block_calculator_drag) return {};
    return {};
}

StadiumRequest StadiumScreen::click(int x, int y, bool right) {
    if (!pointer_down(x, y, right)) return {};
    return release(x, y, right);
}

StadiumRequest StadiumScreen::key(std::string_view key) {
    if (pending_ || calculator_source_waiting() || pressed_ != Pressed::none) return {};
    if (state_.data) {
        if (key == "ESC") { auto r = request(StadiumRequestKind::close_data); pending_ = r; return r; }
        return {};
    }
    if (state_.construction_tip_dialog) return {};
    if (block_calculator_open_) {
        if (key == "ENTER" || key == "RETURN") {
            block_calculator_keyboard_j_ = true;
            block_calculator_pressed_key_ = 13;
            block_calculator_ticks_ = 4;
            block_calculator_presented_ = false;
            return {};
        }
        int pressed_key = -1;
        if (key == "BACKSPACE") pressed_key = 10;
        else if (key == "SPACE") pressed_key = 12;
        else if (key.size() == 1 && key[0] >= '0' && key[0] <= '9')
            pressed_key = int(key[0] - '0');
        if (pressed_key >= 0) {
            const auto result = activate_block_calculator_key(pressed_key);
            block_calculator_pressed_key_ = pressed_key;
            block_calculator_keyboard_j_ = false;
            block_calculator_ticks_ = 4; block_calculator_presented_ = false;
            return result;
        }
        return {};
    }
    if (key == "ESC") {
        if (state_.selected_investment || state_.quoted_object) return {};
        auto r = request(StadiumRequestKind::close); pending_ = r; return r;
    }
    return {};
}

StadiumRequest StadiumScreen::process_timer_expired() {
    if (pending_ || calculator_source_waiting() || pressed_ != Pressed::none) return {};
    //PresentManagerDialog forces button1 (No) after the shared process clock expires.
    //PrepareStandBlockOffer still performs the already confirmed build at its base price.
    if (state_.construction_tip_dialog || state_.periphery_quote_dialog)
        return expire_construction_tip();
    if (state_.selected_investment || state_.quoted_object) return {};
    if (block_calculator_open_) {
        //BalanceFromGame/event8 restores the editor and returns ffffffff. Unlike N,
        //the Source checks its fifty-tick wait before assigning that sentinel.
        //No accepted total or quote is changed, and no new wait is invented.
        block_calculator_value_ = UINT32_MAX;
        block_calculator_open_ = false;
        block_calculator_text_.clear();
        block_calculator_edited_ = true;
        block_calculator_pressed_key_ = -1;
        block_calculator_presented_ = false;
        block_calculator_keyboard_j_ = false;
        return {};
    }
    if (state_.block_expansion) {
        //ConstructionPricePercent's block-detail poll28259 returns without the build event0.
        pending_ = block_request(StadiumRequestKind::expansion_cancel);
        return *pending_;
    }
    return key("ESC"); // existing scene/data close owners
}

StadiumRequest StadiumScreen::expire_construction_tip() {
    if (pending_) return {};
    // The existing deadline entry also owns the flags1 periphery question:
    // PresentManagerDialog's No returns without PayConstruction or any construction payment.
    if (state_.quoted_object && state_.periphery_quote_dialog) {
        auto r = request(StadiumRequestKind::decline_periphery, *state_.quoted_object,
                         -1, state_.quoted_price);
        pending_ = r;
        pressed_ = Pressed::none; pressed_index_ = -1; pressed_x_ = pressed_y_ = -1;
        return r;
    }
    if (!state_.construction_tip_dialog) return {};
    if (state_.block_expansion) {
        auto r = block_request(StadiumRequestKind::expansion_answer_tip);
        r.accept_tip = false;
        pending_ = r;
        pressed_ = Pressed::none; pressed_index_ = -1; pressed_x_ = pressed_y_ = -1;
        return r;
    }
    if (!state_.selected_investment || !state_.selected_option) return {};
    const auto selected = *state_.selected_investment;
    if (unsigned(selected) >= state_.investments.size()) return {};
    const auto& options = state_.investments[size_t(selected)].options;
    if (*state_.selected_option < 0 || size_t(*state_.selected_option) >= options.size()) return {};
    auto r = request(StadiumRequestKind::answer_construction_tip, int(selected),
                     *state_.selected_option,
                     int32_t(options[size_t(*state_.selected_option)].base_price), false);
    pending_ = r;
    // The timeout itself answers the modal. A later mouse-up from the physical
    // press must not be interpreted as a second answer.
    pressed_ = Pressed::none; pressed_index_ = -1; pressed_x_ = pressed_y_ = -1;
    return r;
}

void StadiumScreen::acknowledge(StadiumState updated) {
    if (!pending_) throw FormatError("stadium: acknowledgement without a pending request");
    const auto& req = *pending_;
    if (req.manager_slot != state_.manager_slot || req.runtime_club != state_.runtime_club ||
        req.stable_club_identity != state_.stable_club_identity || req.revision != state_.revision ||
        updated.manager_slot != state_.manager_slot || updated.runtime_club != state_.runtime_club ||
        updated.stable_club_identity != req.stable_club_identity ||
        state_.revision == UINT64_MAX || updated.revision != req.revision + 1)
        throw FormatError("stadium: acknowledgement identity or revision mismatch");
    if (updated.decorated_stadium_header != !updated.plain_background)
        throw FormatError("stadium: decorated header does not match independent Source plain_background state");
    if (!same_source_stadium_style(state_, updated))
        throw FormatError("stadium: acknowledgement changed immutable Source presentation flags");
    if (req.kind == StadiumRequestKind::open) {
        if (req.scope != 0 || req.sequence != 0 || state_.scope != 0 || state_.sequence != 0 ||
            updated.scope == 0 || updated.sequence != 1)
            throw FormatError("stadium: invalid initial stadium scope acknowledgement");
    } else if (req.sequence == UINT64_MAX || updated.scope != state_.scope ||
               updated.sequence != req.sequence + 1 ||
               req.scope != state_.scope || req.sequence != state_.sequence) {
        throw FormatError("stadium: acknowledgement scope or sequence mismatch");
    }
    const bool block_action = block_request_kind(req.kind);
    if ((!block_action && (req.block_slot != -1 || req.block_total_places != 0 ||
                           req.block_features != 0 || req.expected_block_quote)) ||
        (block_action && (req.object != -1 || req.option != -1 || req.price != 0)) ||
        (req.kind != StadiumRequestKind::expansion_answer_tip &&
         req.kind != StadiumRequestKind::answer_construction_tip && req.accept_tip))
        throw FormatError("stadium: request contains fields outside its action contract");
    if (block_action && (req.block_slot < 0 || req.block_slot >= int(state_.blocks.size())))
        throw FormatError("stadium: invalid block request slot");
    if (req.kind != StadiumRequestKind::open && req.kind != StadiumRequestKind::expansion_answer_tip &&
        !same_block_compensation_context(state_, updated))
        throw FormatError("stadium: immutable Source compensation context changed");

    const StadiumBlockExpansionEditor* old_editor = state_.block_expansion
        ? &*state_.block_expansion : nullptr;
    if (req.kind == StadiumRequestKind::expansion_select) {
        if (req.block_total_places != 0 || req.block_features != 0 || req.expected_block_quote ||
            old_editor || state_.selected_investment || state_.quoted_object ||
            state_.blocks[size_t(req.block_slot)].construction_weeks != 0)
            throw FormatError("stadium: block selection is not an idle Source row transition");
    } else if (block_action) {
        if (!old_editor || req.block_slot != int(old_editor->quote.slot) ||
            old_editor->quote.manager != req.manager_slot || old_editor->quote.manager_club != req.runtime_club ||
            !req.expected_block_quote ||
            *req.expected_block_quote != old_editor->quote || !valid_editor_projection(*old_editor) ||
            ((req.kind != StadiumRequestKind::expansion_set_total &&
              req.kind != StadiumRequestKind::expansion_toggle_feature) &&
             (req.block_total_places != old_editor->total_target_places ||
              req.block_features != old_editor->desired_features)))
            throw FormatError("stadium: block request does not match the accepted editor quote");
    }
    const bool data_action = req.kind == StadiumRequestKind::open_data ||
        req.kind == StadiumRequestKind::close_data || req.kind == StadiumRequestKind::toggle_data_curve ||
        req.kind == StadiumRequestKind::toggle_data_scale || req.kind == StadiumRequestKind::set_data_ticket_price;
    if ((!data_action && state_.data != updated.data) ||
        (data_action && (!updated.opened || updated.selected_investment || updated.quoted_object ||
                        updated.block_expansion || updated.construction_tip_dialog)))
        throw FormatError("stadium: acknowledgement changed independent data modal context");
    bool valid = true;
    switch (req.kind) {
    case StadiumRequestKind::open: valid = updated.opened && !updated.data; break;
    case StadiumRequestKind::open_data:
        valid = !state_.data && updated.data && req.object == -1 && req.option == -1 && req.price == 0 &&
            !state_.selected_investment && !state_.quoted_object && !state_.block_expansion && !state_.construction_tip_dialog;
        break;
    case StadiumRequestKind::close_data:
        valid = state_.data && !updated.data && updated.opened && req.object == -1 && req.option == -1 && req.price == 0;
        break;
    case StadiumRequestKind::toggle_data_curve:
    case StadiumRequestKind::toggle_data_scale:
    case StadiumRequestKind::set_data_ticket_price: {
        valid = state_.data && updated.data && req.option == -1 && req.price == 0;
        if (!valid) break;
        auto expected = *state_.data;
        if (req.kind == StadiumRequestKind::toggle_data_curve) {
            valid = req.object >= 0 && req.object < 6;
            if (valid) expected.flags ^= uint8_t(1 << req.object);
        } else if (req.kind == StadiumRequestKind::toggle_data_scale) {
            valid = req.object == 0 || req.object == 1;
            if (valid) expected.flags ^= 0xc0;
        } else {
            valid = req.object >= 10 && req.object <= 25;
            expected.ticket_price = uint8_t(req.object);
            expected.required_attendance_text = updated.data->required_attendance_text;
            valid = valid && !expected.required_attendance_text.empty();
        }
        valid = valid && expected == *updated.data;
        break;
    }
    case StadiumRequestKind::select_block:
    case StadiumRequestKind::prepare_block_offer:
    case StadiumRequestKind::decline_block_offer:
    case StadiumRequestKind::build_stand_block: valid = false; break;
    case StadiumRequestKind::select_investment:
        valid = updated.selected_investment && unsigned(*updated.selected_investment) == unsigned(req.object); break;
    case StadiumRequestKind::select_investment_option:
        valid = updated.selected_investment && unsigned(*updated.selected_investment) == unsigned(req.object) &&
            updated.selected_option && *updated.selected_option == req.option; break;
    case StadiumRequestKind::cancel_investment:
        valid = !updated.selected_investment && !updated.construction_tip_dialog; break;
    case StadiumRequestKind::confirm_investment:
        valid = updated.construction_tip_dialog.has_value(); break;
    case StadiumRequestKind::answer_construction_tip:
        valid = !updated.construction_tip_dialog; break;
    case StadiumRequestKind::quote_periphery:
        valid = updated.quoted_object && *updated.quoted_object == req.object && updated.quoted_price > 0; break;
    case StadiumRequestKind::decline_periphery: valid = !updated.quoted_object; break;
    case StadiumRequestKind::build_periphery:
        valid = !updated.quoted_object && !updated.periphery_quote_dialog &&
            state_.days[size_t(req.object)] == 0 &&
            (updated.days[size_t(req.object)] != 0 ||
             (updated.days == state_.days && updated.level == state_.level && updated.balance == state_.balance));
        break;
    case StadiumRequestKind::close:
        valid = !updated.opened && !updated.quoted_object && !updated.selected_investment &&
            !updated.selected_option && !updated.construction_tip_dialog &&
            same_block_compensation_context(state_, updated);
        break;
    case StadiumRequestKind::expansion_select: {
        if (!updated.block_expansion) { valid = false; break; }
        const auto& editor = *updated.block_expansion;
        const auto& quote = editor.quote;
        valid = int(quote.slot) == req.block_slot && quote.manager == req.manager_slot &&
            quote.manager_club == req.runtime_club && quote.current_build_weeks == 0 &&
            quote.company_price_percent == state_.construction_percent &&
            quote.current_raw_flags == state_.blocks[size_t(req.block_slot)].flags &&
            quote.current_capacity_thousands == state_.blocks[size_t(req.block_slot)].capacity_level &&
            quote.current_future_capacity_thousands == state_.blocks[size_t(req.block_slot)].capacity &&
            quote.desired_features == uint8_t(quote.current_raw_flags & 3) &&
            quote.total_target_places == uint32_t(quote.current_capacity_thousands) * 1000u &&
            quote.new_places == 0 &&
            editor.total_target_places == quote.total_target_places &&
            editor.desired_features == quote.desired_features &&
            valid_editor_projection(editor) &&
            !editor.offer && !updated.construction_tip_dialog &&
            same_block_overviews(state_.blocks, updated.blocks) && updated.balance == state_.balance &&
            !updated.selected_investment && !updated.quoted_object;
        break;
    }
    case StadiumRequestKind::expansion_set_total: {
        if (!old_editor || !updated.block_expansion) { valid = false; break; }
        const auto& next = *updated.block_expansion;
        const uint32_t minimum = uint32_t(old_editor->quote.current_capacity_thousands) * 1000;
        const uint32_t total = req.block_total_places;
        valid = total >= minimum && total <= old_editor->quote.max_places &&
            req.block_features == old_editor->desired_features &&
            same_block_quote_owner(old_editor->quote, next.quote) &&
            canonical_block_quote(next.quote, total, req.block_features) &&
            next.quote.total_target_places == total && next.quote.desired_features == req.block_features &&
            next.total_target_places == total && next.desired_features == req.block_features &&
            !next.offer && !updated.construction_tip_dialog &&
            same_block_overviews(state_.blocks, updated.blocks) && updated.balance == state_.balance;
        break;
    }
    case StadiumRequestKind::expansion_toggle_feature: {
        if (!old_editor || !updated.block_expansion) { valid = false; break; }
        const auto& next = *updated.block_expansion;
        const uint8_t changed = uint8_t(old_editor->desired_features ^ req.block_features);
        valid = (req.block_features & ~uint8_t(3)) == 0 && (changed == 1 || changed == 2) &&
            !(old_editor->quote.current_raw_flags & changed) &&
            same_block_quote_owner(old_editor->quote, next.quote) &&
            canonical_block_quote(next.quote, old_editor->total_target_places, req.block_features) &&
            next.quote.total_target_places == old_editor->total_target_places &&
            next.quote.desired_features == req.block_features &&
            next.total_target_places == old_editor->total_target_places &&
            next.desired_features == req.block_features && !next.offer && !updated.construction_tip_dialog &&
            same_block_overviews(state_.blocks, updated.blocks) && updated.balance == state_.balance;
        break;
    }
    case StadiumRequestKind::expansion_cancel:
        valid = old_editor && !updated.block_expansion && !updated.construction_tip_dialog &&
            !updated.selected_investment && !updated.quoted_object &&
            same_block_overviews(state_.blocks, updated.blocks) && updated.balance == state_.balance;
        break;
    case StadiumRequestKind::expansion_confirm: {
        if (!old_editor) { valid = false; break; }
        if (old_editor->quote.base_price == 0) {
            valid = !updated.block_expansion && !updated.construction_tip_dialog &&
                same_block_overviews(state_.blocks, updated.blocks) && updated.balance == state_.balance;
            break;
        }
        if (!updated.block_expansion) { valid = false; break; }
        const auto& next = *updated.block_expansion;
        valid = next.offer.has_value() && updated.construction_tip_dialog.has_value() &&
            updated.construction_tip_dialog->kind == TaxDialogKind::question &&
            updated.construction_tip_dialog->compact_dialog_frame == updated.compact_dialog_frame &&
            updated.construction_tip_dialog->source_manager_flags == req.manager_slot &&
            req.manager_slot >= 0 && req.manager_slot <= 3 &&
            updated.construction_tip_dialog->source_flags == 0x204 &&
            next.offer->decision == StandBlockOfferDecision::Pending &&
            valid_editor_projection(next) && next.offer->percent >= 6 && next.offer->percent <= 14 &&
            next.offer->base_price == old_editor->quote.base_price &&
            next.offer->price == old_editor->quote.base_price &&
            next.offer->construction_days == old_editor->quote.base_weeks &&
            next.offer->base_days == old_editor->quote.base_weeks &&
            next.offer->offered_price == uint32_t(uint64_t(old_editor->quote.base_price) * uint32_t(100 + next.offer->percent) / 100u) &&
            next.quote == old_editor->quote &&
            next.total_target_places == old_editor->total_target_places &&
            next.desired_features == old_editor->desired_features &&
            same_block_overviews(state_.blocks, updated.blocks) && updated.balance == state_.balance;
        break;
    }
    case StadiumRequestKind::expansion_answer_tip: {
        if (!old_editor || !old_editor->offer || !state_.construction_tip_dialog) { valid = false; break; }
        valid = old_editor->offer->decision == StandBlockOfferDecision::Pending &&
            state_.construction_tip_dialog->kind == TaxDialogKind::question &&
            state_.construction_tip_dialog->compact_dialog_frame == state_.compact_dialog_frame &&
            state_.construction_tip_dialog->source_manager_flags == req.manager_slot &&
            req.manager_slot >= 0 && req.manager_slot <= 3 &&
            state_.construction_tip_dialog->source_flags == 0x204 &&
            valid_editor_projection(*old_editor) && old_editor->offer->percent >= 6 && old_editor->offer->percent <= 14 &&
            old_editor->offer->base_price == old_editor->quote.base_price &&
            old_editor->offer->price == old_editor->quote.base_price &&
            old_editor->offer->construction_days == old_editor->quote.base_weeks &&
            old_editor->offer->base_days == old_editor->quote.base_weeks &&
            old_editor->offer->offered_price == uint32_t(uint64_t(old_editor->quote.base_price) * uint32_t(100 + old_editor->offer->percent) / 100u) &&
            !updated.construction_tip_dialog && !updated.block_expansion &&
            !updated.selected_investment && !updated.quoted_object;
        if (valid) {
            const auto slot = size_t(req.block_slot);
            const auto& before = state_.blocks[slot];
            const auto& after = updated.blocks[slot];
            const bool unchanged = after.construction_weeks == before.construction_weeks &&
                after.flags == before.flags && after.capacity_level == before.capacity_level &&
                after.capacity == before.capacity;
            const uint8_t expected_flags = uint8_t(before.flags |
                uint8_t(old_editor->desired_features << 4));
            const auto& offer = *old_editor->offer;
            const bool offer_arithmetic_ok = !req.accept_tip ||
                uint64_t(old_editor->quote.base_price) * uint32_t(100 + offer.percent) <=
                    uint64_t(std::numeric_limits<int32_t>::max());
            const uint8_t shortest = uint8_t(old_editor->quote.base_weeks -
                (old_editor->quote.base_weeks >> 1));
            const uint8_t longest = uint8_t(old_editor->quote.base_weeks -
                (old_editor->quote.base_weeks >> 3));
            const bool weeks_match = req.accept_tip
                ? after.construction_weeks >= shortest && after.construction_weeks <= longest
                : after.construction_weeks == old_editor->quote.base_weeks;
            const bool constructed = weeks_match &&
                after.capacity == uint8_t(old_editor->total_target_places / 1000) &&
                after.capacity_level == before.capacity_level && after.flags == expected_flags;
            const uint32_t expected_payment = req.accept_tip ? offer.offered_price : offer.base_price;
            const uint8_t usable = source_usable_capacity_thousands(state_.blocks, slot);
            const int32_t compensation = source_immediate_compensation(
                state_.season_tickets, usable, state_.live_league_round, state_.ticket_refund_rate);
            const uint16_t expected_tickets = source_ticket_cap(state_.season_tickets, usable);
            const uint32_t old_balance_bits = std::bit_cast<uint32_t>(state_.balance);
            const int32_t expected_balance = state_.source_level == 0 ? state_.balance :
                std::bit_cast<int32_t>(old_balance_bits - expected_payment - uint32_t(compensation));
            const bool balance_matches = unchanged ? updated.balance == state_.balance :
                updated.balance == expected_balance;
            const bool ticket_match = unchanged ? updated.season_tickets == state_.season_tickets :
                updated.season_tickets == expected_tickets;
            valid = offer_arithmetic_ok && same_block_compensation_context(state_, updated, constructed) &&
                balance_matches && ticket_match && (unchanged || constructed) &&
                same_other_block_overviews(state_.blocks, updated.blocks, slot);
        }
        break;
    }
    case StadiumRequestKind::none: valid = false; break;
    }
    if (!valid) throw FormatError("stadium: engine projection did not confirm request");
    const bool acknowledged_close = req.kind == StadiumRequestKind::close;
    // Build every nested notice and its restored frame while the accepted
    // owner/pending request is still untouched. A decode/allocation failure
    // therefore leaves the old question retryable.
    std::optional<GameEndTaxDialog> staged_tip, staged_periphery;
    if (updated.periphery_quote_dialog) {
        const GameEndRenderOptions options{updated.plain_background, updated.gradient_text, true};
        staged_periphery.emplace(data_, *updated.periphery_quote_dialog, options, render_scene());
    }
    if (updated.construction_tip_dialog) {
        StadiumScreen staged_screen(data_, updated);
        staged_tip = std::move(staged_screen.tip_dialog_);
    }
    pressed_ = Pressed::none; pressed_index_ = -1; pressed_x_ = pressed_y_ = -1;
    if (block_request_kind(req.kind)) {
        block_calculator_open_ = false;
        block_calculator_text_.clear();
    }
    state_ = std::move(updated); pending_.reset(); feedback_ = "";
    tip_dialog_ = std::move(staged_tip);
    periphery_dialog_ = std::move(staged_periphery);
    if (acknowledged_close) close_requested_ = true;
}

void StadiumScreen::reject_action(std::string reason) {
    if (!pending_ || reason.empty()) throw FormatError("stadium: rejection requires pending request and reason");
    pending_.reset(); pressed_ = Pressed::none; pressed_index_ = -1; feedback_ = std::move(reason);
}

std::string StadiumScreen::state_json() const {
    auto result = stadium_state_json(state_); result.pop_back();
    return result + ",\"pending_action\":" + (pending_ ? stadium_request_json(*pending_) : "null") +
        ",\"panorama\":" + (panorama_ ? "true" : "false") +
        ",\"future_information\":" + (future_information_ ? "true" : "false") +
        ",\"periphery_hover\":" + std::to_string(periphery_hover_) +
        ",\"pressed\":" + std::to_string(int(pressed_)) +
        ",\"calculator\":{\"open\":" + (calculator_open() ? "true" : "false") +
        ",\"frame_visible\":" + (block_calculator_open_ && !block_calculator_cancel_wait_ ? "true" : "false") +
        ",\"value\":" + std::to_string(block_calculator_value_) +
        ",\"text\":" + json_quote(block_calculator_text_) +
        ",\"digit_count\":" + std::to_string(block_calculator_digits_) +
        ",\"x\":" + std::to_string(block_calculator_x_) +
        ",\"y\":" + std::to_string(block_calculator_y_) +
        ",\"pressed_key\":" + std::to_string(block_calculator_pressed_key_) +
        ",\"remaining_ticks\":" + std::to_string(block_calculator_ticks_) +
        ",\"presented\":" + (block_calculator_presented_ ? "true" : "false") +
        ",\"cancel_wait\":" + (block_calculator_cancel_wait_ ? "true" : "false") +
        ",\"keyboard_j\":" + (block_calculator_keyboard_j_ ? "true" : "false") +
        ",\"edited\":" + (block_calculator_edited_ ? "true" : "false") +
        ",\"waiting_for_key_release\":" + (calculator_waiting_for_key_release() ? "true" : "false") + "}" +
        ",\"feedback\":" + (feedback_.empty() ? "null" : json_quote(feedback_)) + '}';
}

bool StadiumScreen::construction_tip_held() const {
    // Both PresentManagerDialog questions retain their physical button until release.
    return pressed_ == Pressed::tip_yes || pressed_ == Pressed::tip_no ||
           pressed_ == Pressed::periphery_yes || pressed_ == Pressed::periphery_no;
}

} // namespace bmh::ui
