// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/stadium.hpp"

#include "game/messages.hpp"
#include "ui/manager.hpp"
#include "formats/palette.hpp"
#include <fstream>

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <utility>

using namespace bmh;
using namespace bmh::ui;

namespace {
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

void copy_crop(Image& dst, const Image& src, int sx, int sy, int x, int y, int w, int h) {
    for (int row = 0; row < h; ++row) for (int col = 0; col < w; ++col) {
        const int dx = x + col, dy = y + row, px = sx + col, py = sy + row;
        if (dx >= 0 && dy >= 0 && dx < dst.width && dy < dst.height &&
            px >= 0 && py >= 0 && px < src.width && py < src.height)
            dst.at(dx, dy) = src.pixels[size_t(py) * src.width + px];
    }
}

void compare_region(const Image& actual, const Image& expected,
                    int left, int top, int width, int height, const char* message) {
    for (int y = top; y < top + height; ++y)
        for (int x = left; x < left + width; ++x)
            check(actual.pixels[size_t(y) * actual.width + x] ==
                  expected.pixels[size_t(y) * expected.width + x], message);
}

void assert_text_pixels(const Image& actual, const Font& font, int x, int baseline,
                        std::string_view text, uint8_t color, const char* message) {
    check(!text.empty(), "Source TEXTD fixture caption is present");
    Image mask(std::max(1, font.text_width(text)), font.height, 0);
    font.draw(mask, 0, 0, text, 1);
    const int top = baseline - font.baseline;
    bool has_glyph = false;
    for (int y = 0; y < mask.height; ++y) for (int xx = 0; xx < mask.width; ++xx) {
        if (mask.pixels[size_t(y) * size_t(mask.width) + size_t(xx)] == 0) continue;
        has_glyph = true;
        check(x + xx >= 0 && x + xx < actual.width && top + y >= 0 && top + y < actual.height &&
              actual.pixels[size_t(top + y) * size_t(actual.width) + size_t(x + xx)] == color,
              message);
    }
    check(has_glyph, "Source font fixture contains visible glyph pixels");
}

void assert_font3_pixels(const Image& actual, const Font& font, const LeImage& exe,
                         int x, int baseline, std::string_view text, uint8_t color, bool gradient) {
    Image mask(std::max(1, font.text_width(text)), font.height);
    font.draw(mask, 0, 0, text, 1);
    unsigned ramp = 0;
    while (ramp < 32 && exe.u8(::bmh::layout::Advertising_Text_RuntimeField2 + ramp) != color) ++ramp;
    for (int row = 0; row < mask.height; ++row) for (int col = 0; col < mask.width; ++col)
        if (mask.at(col, row)) {
            const uint8_t expected = gradient && exe.u8(::bmh::layout::TextGradientMode) == 2 && ramp < 32
                ? exe.u8(::bmh::layout::Advertising_Text_RuntimeField + ramp * 6 + unsigned(row)) : color;
            check(actual.pixels.at(size_t(baseline - font.baseline + row) * actual.width + size_t(x + col)) == expected,
                  "Source Font3 glyph uses the selected row of the Original color ramp");
        }
}

void assert_scene_label(const Image& actual, const SpriteFont& sprite, const LeImage& exe,
                        int button, std::string_view label, bool selected = false) {
    const std::string utf8 = original_to_utf8(label);
    const int width = sprite.text_width(utf8);
    Image ink(width, 7);
    sprite.draw(ink, 0, 6, utf8, exe.u8(::bmh::layout::Advertising_Button_RuntimeField + int(selected)));
    const int left = (button == 0 ? 2 : button == 1 ? 125 : 224) + (selected ? 2 : 0);
    const int button_width = button == 0 ? 115 : 90;
    const int x = left + button_width / 2 - width / 2;
    const int y = 216 + (selected ? 2 : 0);
    for (int row = 0; row < 7; ++row) for (int col = 0; col < width; ++col)
        if (ink.at(col, row)) check(actual.pixels.at(size_t(y + row) * actual.width + size_t(x + col)) == ink.at(col, row),
                                  "Source scene mode selects its own TEXTD button label and sprite mode");
}

Image calculator_base(const StadiumScreen& screen, const std::filesystem::path& data,
                      int x0 = 111, int y0 = 31) {
    Image base = screen.construction_notice_backdrop();
    const Image art = load_vga(data / "KB" / "6.VGA");
    copy_crop(base, art, 0, 0, x0, y0, 113, 136);
    for (int y = y0 + 2; y < y0 + 138; ++y)
        for (int x = x0 + 113; x < x0 + 115; ++x) base.at(x, y) = 0x40;
    for (int y = y0 + 136; y < y0 + 138; ++y)
        for (int x = x0 + 2; x < x0 + 115; ++x) base.at(x, y) = 0x40;
    return base;
}

void assert_readout_source_text(const StadiumScreen& screen, const std::filesystem::path& data,
                                std::string_view text, const char* message,
                                int x0 = 111, int y0 = 31) {
    Image expected = calculator_base(screen, data, x0, y0);
    const Font font = load_font(data / "F" / "BMH3.FNT");
    font.draw(expected, x0 + 106 - font.text_width(text),
              y0 + 110 - font.baseline, text, 0x00);
    compare_region(screen.render(), expected, x0 + 55, y0 + 100, 58, 11, message);
}

void assert_preset_source_cell(const StadiumScreen& screen, const std::filesystem::path& data,
                               const LeImage& exe, size_t preset, uint32_t value) {
    Image expected = calculator_base(screen, data);
    const Font font = load_font(data / "F" / "BMH3.FNT");
    const int x = 111 + exe.u16(::bmh::layout::Advertising_Click_RuntimeField + (preset + 15) * 2);
    const int y = 31 + exe.u8(::bmh::layout::Advertising_Click_RuntimeField2 + preset + 15);
    const std::string text = format_number(GameState{}, int32_t(value), 7);
    font.draw(expected, x + 47 - font.text_width(text), y + 12 - font.baseline, text, 0x62);
    compare_region(screen.render(), expected, x, y, 53, 14,
                   "preset rendering is the Original raw numeric mask on the untouched KB6 cell");
}

void assert_key_source_cell(const StadiumScreen& screen, const std::filesystem::path& data,
                            const LeImage& exe, int key, bool pressed) {
    Image expected = calculator_base(screen, data);
    const Image art = load_vga(data / "KB" / "6.VGA");
    const Font font = load_font(data / "F" / "BMH3.FNT");
    constexpr std::string_view labels = "0123456789DRCJN";
    const int x = 111 + exe.u16(::bmh::layout::Advertising_Click_RuntimeField + size_t(key) * 2);
    const int y = 31 + exe.u8(::bmh::layout::Advertising_Click_RuntimeField2 + size_t(key));
    if (pressed) {
        copy_crop(expected, art, 15, 136, x + 1, y + 1, 14, 13);
        font.draw(expected, x + 6, y + 10 - font.baseline, labels.substr(size_t(key), 1), 0x62);
    } else {
        copy_crop(expected, art, 0, 136, x, y, 15, 14);
        font.draw(expected, x + 5, y + 9 - font.baseline, labels.substr(size_t(key), 1), 0x62);
    }
    compare_region(screen.render(), expected, x, y, 17, 16,
                   "key glyph stays plain on its exact Original pressed or released KB6 crop");
}

constexpr std::array<uint32_t, 8> kPrices{300000, 375000, 300000, 350000,
                                           300000, 375000, 300000, 350000};
constexpr std::array<uint32_t, 8> kMaxima{8000, 18000, 8000, 14000,
                                           8000, 18000, 8000, 14000};
constexpr std::array<uint8_t, 8> kWeeks{30, 37, 30, 25, 30, 37, 30, 25};

StadiumBlockExpansionEditor editor_for(uint8_t slot = 1, uint32_t total = 8000,
                                       uint8_t features = 0, uint8_t raw_flags = 0) {
    StadiumBlockExpansionEditor editor;
    auto& q = editor.quote;
    q.manager = 0; q.manager_club = 3; q.slot = static_cast<StadiumBlockSlot>(slot);
    q.current_build_weeks = 0; q.current_raw_flags = raw_flags;
    q.current_capacity_thousands = 8; q.current_future_capacity_thousands = 8;
    q.total_target_places = total; q.desired_features = features;
    q.company_price_percent = 100; q.price_per_1000 = kPrices[slot];
    q.max_places = kMaxima[slot]; q.base_weeks = kWeeks[slot];
    const uint32_t current = uint32_t(q.current_capacity_thousands) * 1000;
    q.new_places = total - current;
    q.new_places_cost = kPrices[slot] * (q.new_places / 1000);
    if (features & 1) {
        const uint32_t basis = raw_flags & 1 ? q.new_places : total;
        q.seats_cost = kPrices[slot] * (basis / 1000) * 45 / 100;
    }
    if (features & 2) {
        const uint32_t basis = raw_flags & 2 ? q.new_places : total;
        q.roof_cost = kPrices[slot] * (basis / 1000) * 30 / 100;
    }
    q.undiscounted_cost = q.new_places_cost + q.seats_cost + q.roof_cost;
    q.base_price = q.undiscounted_cost * uint32_t(q.company_price_percent) / 100;

    editor.total_target_places = total; editor.desired_features = features;
    editor.header_text_variant = 3 + slot;
    editor.current_places_text = "8000";
    editor.new_places_text = std::to_string(q.new_places);
    editor.target_places_text = std::to_string(total);
    editor.per_thousand_cost_text = std::to_string(q.price_per_1000);
    // Keep synthetic ACK fixtures numeric-only. Currency/discount glyphs in
    // source-sensitive rendering fixtures must come from Original pointers.
    editor.new_places_cost_text = std::to_string(q.new_places_cost);
    editor.seats_cost_text = std::to_string(q.seats_cost);
    editor.roof_cost_text = std::to_string(q.roof_cost);
    editor.total_cost_text = std::to_string(q.base_price);
    editor.discount_text.clear();
    return editor;
}

StadiumState overview_state() {
    StadiumState state;
    state.manager_slot = 0; state.runtime_club = 3; state.stable_club_identity = 17;
    state.revision = 20; state.scope = 44; state.sequence = 3; state.opened = true;
    state.balance = 20000000; state.balance_text = "20000000";
    return state;
}

StadiumState detail_state(uint8_t slot = 1, uint32_t total = 8000,
                          uint8_t features = 0, uint8_t raw_flags = 0) {
    auto state = overview_state();
    state.source_level = 1;
    state.season_tickets = 0;
    state.live_league_round = 0;
    state.ticket_refund_rate = 0;
    auto editor = editor_for(slot, total, features, raw_flags);
    const size_t at = size_t(slot);
    state.blocks[at].flags = raw_flags;
    state.blocks[at].capacity_level = 8;
    state.blocks[at].capacity = 8;
    state.block_expansion = std::move(editor);
    return state;
}

void next_identity(StadiumState& state) {
    ++state.revision;
    ++state.sequence;
}

StadiumState edited_state(const StadiumScreen& screen, uint32_t total, uint8_t features) {
    auto next = screen.state();
    next_identity(next);
    const auto slot = uint8_t(next.block_expansion->quote.slot);
    next.block_expansion = editor_for(slot, total, features,
                                      next.block_expansion->quote.current_raw_flags);
    return next;
}

StadiumState completed_build_state(const StadiumScreen& screen, uint8_t weeks,
                                   int32_t balance, uint16_t tickets) {
    auto next = screen.state(); next_identity(next);
    const auto& editor = *next.block_expansion;
    const auto slot = size_t(editor.quote.slot);
    const uint32_t total = editor.total_target_places;
    const uint8_t features = editor.desired_features;
    const uint8_t prior_flags = next.blocks[slot].flags;
    next.block_expansion.reset(); next.construction_tip_dialog.reset();
    next.blocks[slot].construction_weeks = weeks;
    next.blocks[slot].flags = uint8_t(prior_flags | uint8_t(features << 4) | 0x10);
    next.blocks[slot].capacity = uint8_t(total / 1000);
    next.balance = balance;
    next.season_tickets = tickets;
    return next;
}

StandBlockOffer pending_offer(const StadiumBlockExpansionQuote& quote, int percent = 8) {
    StandBlockOffer offer;
    offer.percent = percent;
    offer.base_price = quote.base_price;
    offer.offered_price = uint32_t(uint64_t(quote.base_price) * uint32_t(100 + percent) / 100);
    offer.price = quote.base_price;
    offer.base_days = quote.base_weeks;
    offer.construction_days = quote.base_weeks;
    offer.decision = StandBlockOfferDecision::Pending;
    return offer;
}

TaxDialogProjection question_for(int manager = 0) {
    TaxDialogProjection dialog;
    dialog.kind = TaxDialogKind::question;
    dialog.source_manager_flags = uint8_t(manager);
    dialog.source_flags = 0x204;
    dialog.manager_name = "Manager";
    dialog.body = "Angebot?";
    return dialog;
}

StadiumRequest keyboard_return_after_source_phase(StadiumScreen& screen) {
    check(screen.key("RETURN").kind == StadiumRequestKind::none &&
          screen.calculator_source_waiting() && !screen.calculator_waiting_for_key_release(),
          "keyboard J begins its Source pressed phase without dispatching early");
    check(screen.state_json().find("\"keyboard_j\":true") != std::string::npos &&
          screen.state_json().find("\"remaining_ticks\":4") != std::string::npos,
          "Flow-visible state records keyboard J timing so the press frame is presented");
    (void)screen.render(); // Root's SDL loop calls this only after the real present.
    screen.calculator_mark_presented();
    check(screen.calculator_advance_source_ticks(4),
          "keyboard J completes only after four presented Source ticks");
    return screen.pending().value_or(StadiumRequest{});
}

StadiumRequest source_key_down_present_ticks_up(StadiumScreen& screen, const LeImage& exe,
                                                 int key, int x0 = 111, int y0 = 31) {
    const int x = x0 + exe.u16(::bmh::layout::Advertising_Click_RuntimeField + size_t(key) * 2);
    const int y = y0 + exe.u8(::bmh::layout::Advertising_Click_RuntimeField2 + size_t(key));
    check(screen.pointer_down(x, y), "physical calculator key accepts Source mouse-down");
    (void)screen.render();
    screen.calculator_mark_presented();
    check(screen.calculator_advance_source_ticks(4),
          "physical calculator key advances through the presented four-tick phase");
    return screen.release(x, y); // The original matching UP arrives after the phase.
}

void row_hit_tests(const std::filesystem::path& data) {
    for (int slot = 0; slot < 8; ++slot) {
        auto state = overview_state();
        StadiumScreen screen(data, state);
        const auto request = screen.click(250, 22 + slot * 24 + 3);
        check(request.kind == StadiumRequestKind::expansion_select && request.block_slot == slot,
              "the eight Source block rows map to the eight A–H slots");
        check(!request.expected_block_quote && request.block_total_places == 0 && request.block_features == 0,
              "Source entry carries only the selected slot before the App quote projection");
    }
    auto busy = overview_state(); busy.blocks[3].construction_weeks = 1;
    StadiumScreen screen(data, busy);
    check(screen.click(250, 22 + 3 * 24 + 3).kind == StadiumRequestKind::none,
          "a busy Source row cannot be opened");
    check(screen.click(140, 219).kind == StadiumRequestKind::none,
          "the current/future footer control is a local presentation action");
    check(screen.click(250, 22 + 2 * 24 + 3).kind == StadiumRequestKind::expansion_select,
          "future-information view leaves idle A–H rows editable");

    auto select_state = overview_state();
    select_state.blocks[1].capacity_level = 8; select_state.blocks[1].capacity = 8;
    StadiumScreen selected(data, select_state);
    const auto select = selected.click(250, 22 + 1 * 24 + 3);
    auto accepted = selected.state(); next_identity(accepted);
    accepted.block_expansion = editor_for(1);
    selected.acknowledge(accepted);
    check(selected.state().block_expansion &&
          selected.state().block_expansion->quote.price_per_1000 == 375000 &&
          select.block_slot == 1,
          "the selected slot ACK accepts only its canonical B quote and table row");
}

void source_stadium_style_flag_tests(const std::filesystem::path& data) {
    auto decorated = overview_state();
    decorated.plain_background = false;
    decorated.compact_dialog_frame = false;
    decorated.decorated_stadium_header = true; // !plain_background
    const auto reference = StadiumScreen(data, decorated).render();

    auto notice_gate_only = decorated;
    notice_gate_only.compact_dialog_frame = true;
    check(StadiumScreen(data, notice_gate_only).render().pixels == reference.pixels,
          "compact_dialog_frame remains independent and cannot control the decorated stadium header");

    auto flat = decorated;
    flat.plain_background = true;
    flat.decorated_stadium_header = false;
    check(StadiumScreen(data, flat).render().pixels != reference.pixels,
          "the format field value alone selects the flat stadium header presentation");
}

void solid_panel_block_tests(const std::filesystem::path& data) {
    const Image kb56 = load_vga(data / "KB" / "56.VGA");
    const auto pixel = [](const Image& image, int x, int y) {
        return image.pixels.at(size_t(y) * size_t(image.width) + size_t(x));
    };
    auto textured_state = overview_state();
    textured_state.solid_panel_background = false;
    const auto textured = StadiumScreen(data, textured_state).render();

    auto flat_state = textured_state;
    flat_state.solid_panel_background = true;
    const auto flat = StadiumScreen(data, flat_state).render();

    // Source legacy routine crop for row s: source (219,row_y-19) ->
    // destination (220,row_y+1), 98x22. Check the unobstructed right side of
    // every block panel, including every pixel.
    for (int slot = 0; slot < 8; ++slot) {
        const int row_y = 22 + slot * 24;
        for (int y = row_y + 1; y <= row_y + 22; ++y) {
            for (int x = 260; x <= 317; ++x) {
                const auto source = pixel(kb56, x - 1, y - 20);
                check(pixel(textured, x, y) == source,
                      "a2560=0 paints the literal KB56 crop over each A-H panel");
                check(pixel(flat, x, y) == 0x42,
                      "a2560!=0 fills the A-H panel interior with Source palette 42");
            }
            check(pixel(textured, 219, y) == 0x46 && pixel(textured, 318, y) == 0x40 &&
                  pixel(flat, 219, y) == 0x46 && pixel(flat, 318, y) == 0x40,
                  "both panel modes preserve Source's inclusive vertical border edges");
        }
        for (int x = 219; x <= 318; ++x) {
            const uint8_t expected_top = x == 318 ? 0x40 : 0x46;
            check(pixel(textured, x, row_y) == expected_top &&
                  pixel(flat, x, row_y) == expected_top &&
                  pixel(textured, x, row_y + 23) == 0x40 &&
                  pixel(flat, x, row_y + 23) == 0x40,
                  "all inclusive top/bottom border pixels match legacy routine edge ordering");
        }
        check(pixel(textured, 219, row_y) == 0x46 && pixel(textured, 318, row_y) == 0x40 &&
              pixel(textured, 219, row_y + 23) == 0x40 && pixel(textured, 318, row_y + 23) == 0x40 &&
              pixel(flat, 219, row_y) == 0x46 && pixel(flat, 318, row_y) == 0x40 &&
              pixel(flat, 219, row_y + 23) == 0x40 && pixel(flat, 318, row_y + 23) == 0x40,
              "all four panel corners preserve Source's last-writer edge colors");
    }

    // SolidPanelBackground is a separate panel selector, not a synonym for the header flag.
    auto split_flags = flat_state;
    split_flags.plain_background = false;
    split_flags.decorated_stadium_header = true;
    check(pixel(StadiumScreen(data, split_flags).render(), 280, 30) == 0x42,
          "a2560 panel fill remains active independently of plain_background header decoration");
    check(stadium_state_json(flat_state).find("\"solid_panel_background\":true") != std::string::npos,
          "stadium JSON exposes the independent Source panel mode");
}

void source_textd_stadium_caption_tests(const std::filesystem::path& data) {
    const TextDb textd = load_textd(data / "TEXTD.BLK");
    const Font font = load_font(data / "F" / "BMH3.FNT");
    const LeImage exe = load_le(data / "BMH.exe");
    const auto text = [&](uint32_t address) -> const std::string& {
        return textd[text_id(address)];
    };

    auto editor_state = detail_state(1, 10000);
    editor_state.block_expansion->header_text_variant = 7;
    const StadiumScreen editor(data, editor_state);
    const Image editor_image = editor.render();
    assert_text_pixels(editor_image, font, 5, 46, text(::bmh::layout::Stadium_RenderBlockExpansion_TextPointer2), 0x6c,
                       "block editor paints the Original TEXTD Kontostand caption");
    assert_text_pixels(editor_image, font, 5, 54, text(::bmh::layout::Stadium_RenderBlockExpansion_TextPointer3), 0x6c,
                       "block editor paints the Original TEXTD duration caption");
    assert_text_pixels(editor_image, font, 5, 70, text(::bmh::layout::Stadium_RenderBlockExpansion_TextPointer4), 0x6c,
                       "block editor paints the Original TEXTD price-variant caption");
    const std::string block_label = "Block B";
    assert_text_pixels(editor_image, font, 7, 81, block_label, 0x6b,
                       "block editor paints the Original TEXTD block prefix");
    assert_text_pixels(editor_image, font, 5, 112, text(::bmh::layout::Stadium_RenderBlockExpansion_TextPointer10), 0x6a,
                       "block editor paints the Original TEXTD per-thousand cost label");
    assert_text_pixels(editor_image, font, 5, 130, text(::bmh::layout::Stadium_RenderBlockExpansion_TextPointer9), 0x6a,
                       "block editor paints the Original TEXTD total-cost label");

    const std::string duration = format_number(GameState{}, 37, 0x82) + text(::bmh::layout::Stadium_FormatError_RuntimeField) +
        exe.cstring(::bmh::layout::Stadium_FormatError_ExecutableField) + exe.cstring(::bmh::layout::CareerSession_FormatNumber_ExecutableField);
    const int duration_x = 148 - font.text_width(duration);
    assert_text_pixels(editor_image, font, duration_x, 54, duration, 0x6c,
                       "duration unit comes from TEXTD and preserves legacy routine static suffixes");

    auto overview = overview_state();
    overview.blocks[0].capacity = 3;
    overview.investments[0].current_status = 1;
    const StadiumScreen current(data, overview);
    const Image current_image = current.render();
    assert_text_pixels(current_image, font, 221, 29, "Block A", 0x6b,
                       "A-H overview uses its Source TEXTD block prefix");
    assert_text_pixels(current_image, font, 221, 22 + 21, text(::bmh::layout::Stadium_Out_TextPointer5) +
                       format_number(GameState{}, int32_t(exe.u8(::bmh::layout::CareerSession_Invalid_RuntimeField) * 1000), 5) +
                       exe.cstring(::bmh::layout::Stadium_Out_ExecutableField), 0x51,
                       "current overview uses the Original TEXTD maximum-capacity label");

    auto future = overview;
    future.blocks[0].capacity = 7;
    future.blocks[0].construction_weeks = 18;
    StadiumScreen future_screen(data, future);
    check(future_screen.click(140, 219).kind == StadiumRequestKind::none,
          "current/future stadium toggle is local presentation state");
    const Image future_image = future_screen.render();
    assert_text_pixels(future_image, font, 221, 22 + 15, text(::bmh::layout::Stadium_Out_TextPointer2) +
                       format_number(GameState{}, 7000, 5) + exe.cstring(::bmh::layout::Stadium_Out_ExecutableField), 0x6c,
                       "future overview paints the Original TEXTD in-construction label");
    const std::string future_duration = text(::bmh::layout::Stadium_Out_TextPointer3) + format_number(GameState{}, 18, 2) +
        text(::bmh::layout::Stadium_FormatError_RuntimeField) + exe.cstring(::bmh::layout::Stadium_FormatError_ExecutableField) + exe.cstring(::bmh::layout::CareerSession_FormatNumber_ExecutableField);
    assert_text_pixels(future_image, font, 221, 22 + 21, future_duration, 0x51,
                       "future overview paints Source TEXTD construction and week labels");

    auto facility = overview_state();
    facility.investments[0].current_status = 1;
    const StadiumScreen facility_screen(data, facility);
    const Image facility_image = facility_screen.render();
    const auto& tier = text(::bmh::layout::FixtureStadiumTierLabel);
    const int x = exe.u8(::bmh::layout::Stadium_Out_ExecutableField2) + (0x35 - font.text_width(tier)) / 2;
    const int baseline = exe.u8(::bmh::layout::Stadium_Out_ExecutableField3) + 0x1b;
    assert_text_pixels(facility_image, font, x, baseline, tier, 0x5f,
                       "facility tier label resolves through its Original TEXTD table slot");

    auto investment = overview_state();
    investment.selected_investment = StadiumInvestment::Floodlights;
    investment.investments[0].original_label = textd[734];
    investment.investment_text_variant = 7;
    const StadiumScreen investment_screen(data, investment);
    const Image investment_image = investment_screen.render();
    const Font heading = load_font(data / "F" / "BMH2.FNT");
    const std::string investment_title = textd[734] + text(::bmh::layout::Stadium_RenderInvestmentDialog_TextPointer);
    const int title_x = 1 + (215 - heading.text_width(investment_title)) / 2;
    assert_text_pixels(investment_image, heading, title_x, 33, investment_title, 0x5f,
                       "investment detail title resolves its dynamic TEXTD suffix");
    assert_text_pixels(investment_image, font, 5, 46, text(::bmh::layout::Stadium_RenderBlockExpansion_TextPointer2), 0x6c,
                       "investment detail paints the same Original TEXTD account caption");
    assert_text_pixels(investment_image, font, 5, 54, text(::bmh::layout::Stadium_RenderBlockExpansion_TextPointer3), 0x6c,
                       "investment detail paints the Original TEXTD duration caption");
}

void source_remaining_draw_paths(const std::filesystem::path& data) {
    const auto exe = load_le(data / "BMH.exe");
    const auto texts = load_textd(data / "TEXTD.BLK");
    const auto font = load_font(data / "F" / "BMH3.FNT");
    const auto heading = load_font(data / "F" / "BMH2.FNT");
    const auto interior = load_vga(data / "KB" / "38.VGA");
    const auto facilities = load_vga(data / "KB" / "39.VGA");
    const auto player = load_vga(data / "KB" / "129.VGA");
    SpriteFont sprite(data);
    for (bool gradient : {false, true}) {
        auto state = overview_state(); state.gradient_text = gradient;
        for (auto& block : state.blocks) { block.capacity_level = 3; block.flags = 3; }
        StadiumScreen overview(data, state);
        auto frame = overview.render();
        for (int y = 23; y <= 149; ++y) for (int x = 17; x <= 197; ++x) {
            const bool picture = x <= 195 && y <= 147;
            const bool shadow = (x >= 196 && y >= 25) || (x >= 19 && y >= 148);
            const uint8_t expected = picture ? interior.pixels.at(size_t(y - 23) * interior.width + size_t(x - 17))
                : shadow ? 0x40 : player.pixels.at(size_t(y) * player.width + size_t(x));
            check(frame.at(x, y) == expected, "Source opaque inside picture and complete legacy routine shadow");
        }
        for (int slot = 0; slot < 8; ++slot) {
            const int top = 22 + slot * 24;
            std::string label = texts[text_id(::bmh::layout::Stadium_Out_TextPointer)]; label[6] = char('A' + slot);
            assert_font3_pixels(frame, font, exe, 221, top + 7, label, 0x6b, gradient);
            assert_font3_pixels(frame, font, exe, 221, top + 15,
                format_number(GameState{}, 3000, 0x85) + texts[text_id(::bmh::layout::Stadium_FormatError_ExecutableField3)], 0x51, gradient);
        }
        for (int i = 0; i < 3; ++i) assert_scene_label(frame, sprite, exe, i, texts[153 + i]);
        check(overview.click(140, 219).kind == StadiumRequestKind::none, "future control stays local");
        frame = overview.render();
        assert_scene_label(frame, sprite, exe, 1, texts[154], true);
        for (int slot = 0; slot < 8; ++slot)
            assert_font3_pixels(frame, font, exe, 221, 37 + slot * 24,
                texts[text_id(::bmh::layout::Stadium_Out_TextPointer4)], 0x6a, gradient);
        check(overview.click(25, 219).kind == StadiumRequestKind::none, "panorama navigation stays local");
        frame = overview.render();
        for (int i = 0; i < 3; ++i) assert_scene_label(frame, sprite, exe, i, texts[150 + i]);
        (void)overview.click(25, 219);
        const auto selected = overview.click(250, 25);
        check(selected.kind == StadiumRequestKind::expansion_select && selected.block_slot == 0,
              "returning from panorama retains the A-H block route");

        for (uint8_t slot = 0; slot < 8; ++slot) {
            auto detail = detail_state(slot, 8000, 3); detail.gradient_text = gradient;
            StadiumScreen screen(data, detail);
            auto image = screen.render();
            const std::string title = texts[742 + slot] + texts[text_id(::bmh::layout::Stadium_RenderInvestmentDialog_TextPointer)];
            assert_text_pixels(image, heading, 108 - heading.text_width(title) / 2, 33, title, 0x5f,
                               "Source block title explicitly uses plain Font2 with gradient both on and off");
            assert_font3_pixels(image, font, exe, 5, 46, texts[text_id(::bmh::layout::Stadium_RenderBlockExpansion_TextPointer2)], 0x6c, gradient);
            assert_font3_pixels(image, font, exe, 148 - font.text_width(detail.block_expansion->total_cost_text),
                                130, detail.block_expansion->total_cost_text, 0x6c, gradient);
            for (int feature = 0; feature < 2; ++feature) {
                const int top = feature == 0 ? 73 : 89, sy = feature == 0 ? 1 : 12;
                for (int y = 0; y < 10; ++y) for (int x = 0; x < 10; ++x) {
                    const auto ink = interior.pixels.at(size_t(sy + y) * interior.width + size_t(180 + x));
                    const auto background = exe.u8(::bmh::layout::Advertising_PatternRect_RuntimeField + unsigned((top + y - 26) & 15) * 16 + unsigned((120 + x - 5) & 15));
                    check(image.at(120 + x, top + y) == (ink ? ink : background),
                          "block feature crop uses Source Comparekey0 over the Original frame pattern");
                }
            }
            if (slot == 1) {
                (void)screen.click(40, 85);
                auto calculator = screen.render();
                const auto& label = texts[text_id(::bmh::layout::FixtureStadiumBlockLabel)];
                assert_font3_pixels(calculator, font, exe, 111 + 56 - font.text_width(label) / 2,
                                    31 + 14, label, 0x62, gradient);
            }
        }

        for (int choice : {0, 2, 3}) {
            auto options = overview_state(); options.gradient_text = gradient;
            options.selected_investment = static_cast<StadiumInvestment>(choice);
            auto& row = options.investments[size_t(choice)];
            row.original_label = texts[734 + choice];
            StadiumInvestmentOption option{}; option.target_level = 1; option.base_price = 1234567;
            row.options = {option}; row.option_price_text = {"1234567"};
            auto image = StadiumScreen(data, options).render();
            const int sx = exe.u16(::bmh::layout::Stadium_Out_RuntimeField + choice * 4), sy = exe.u16(::bmh::layout::Stadium_Out_RuntimeField2 + choice * 4);
            for (int y = 0; y < 29; ++y) for (int x = 0; x < 53; ++x)
                check(image.at(5 + x, 74 + y) == facilities.pixels.at(size_t(sy + y) * facilities.width + size_t(sx + x)),
                      "investment option artwork remains an opaque Source crop");
            assert_font3_pixels(image, font, exe, 5, 46, texts[text_id(::bmh::layout::Stadium_RenderBlockExpansion_TextPointer2)], 0x6c, gradient);
            assert_font3_pixels(image, font, exe, 5 + (53 - font.text_width("1234567")) / 2,
                                117, "1234567", choice == 0 ? 0x6c : 0x5f, gradient);
            if (choice == 0) {
                for (int y = 76; y <= 104; ++y) for (int x = 58; x <= 59; ++x)
                    check(image.at(x, y) == 0x40, "Source multi-option picture has its own right shadow");
                for (int y = 103; y <= 104; ++y) for (int x = 7; x <= 59; ++x)
                    check(image.at(x, y) == 0x40, "Source multi-option picture has its own bottom shadow");
            }
            for (int y = 24; y < 150; ++y) for (int x = 215; x <= 216; ++x)
                check(image.at(x, y) == player.pixels.at(size_t(y) * player.width + size_t(x)),
                      "legacy routine flags18 do not add a shadow outside the investment frame");
        }
    }
}

void source_table_and_ack_tests(const std::filesystem::path& data) {
    // Every Original slot table row is independently exercised; B uses the
    // Source manual nonmultiple T=15333 path and catches floor-before-surcharge.
    for (uint8_t slot = 0; slot < 8; ++slot) {
        const uint32_t total = slot == 1 ? 15333 : kMaxima[slot];
        const uint8_t features = slot == 1 ? 1 : 0;
        auto state = detail_state(slot, total, features);
        StadiumScreen screen(data, state);
        const auto rendered = screen.render();
        check(rendered.width == 320 && rendered.height == 240,
              "all eight canonical Source table quotes render in the original viewport");
    }

    StadiumScreen total_screen(data, detail_state());
    check(total_screen.click(99, 99).kind == StadiumRequestKind::none,
          "the bottom-right inclusive amount pixel opens the calculator without an App action");
    check(total_screen.text("15333"), "manual Source totals accept a nonmultiple of 1000");
    const auto total_req = keyboard_return_after_source_phase(total_screen);
    check(total_req.kind == StadiumRequestKind::expansion_set_total &&
          total_req.block_total_places == 15333 && total_req.block_features == 0 &&
          total_req.expected_block_quote.has_value(),
          "the calculator returns TOTAL T while retaining the immutable old quote");
    auto forged = edited_state(total_screen, 15333, 0);
    forged.block_expansion->quote.base_price += 1;
    bool rejected = false;
    try { total_screen.acknowledge(forged); } catch (const FormatError&) { rejected = true; }
    check(rejected && total_screen.pending().has_value(),
          "a forged derived price preserves the pending request for a real retry");
    total_screen.acknowledge(edited_state(total_screen, 15333, 0));
    check(!total_screen.pending() && total_screen.state().block_expansion->quote.new_places == 7333,
          "a Source-valid manual total ACK updates the quote only after validation");

    StadiumScreen feature_screen(data, detail_state());
    const auto feature = feature_screen.click(160, 78);
    check(feature.kind == StadiumRequestKind::expansion_toggle_feature && feature.block_features == 1,
          "the seat option carries a resulting desired-feature mask");
    feature_screen.acknowledge(edited_state(feature_screen, 8000, 1));
    check(feature_screen.state().block_expansion->quote.seats_cost == 1350000,
          "a canonical feature successor recomputes its source component cost");
    const auto remove_new_seat = feature_screen.click(160, 78);
    check(remove_new_seat.kind == StadiumRequestKind::expansion_toggle_feature &&
          remove_new_seat.block_features == 0,
          "a newly selected absent feature can be toggled back off");
    StadiumScreen existing_seat(data, detail_state(1, 8000, 1, 1));
    StadiumScreen existing_roof(data, detail_state(1, 8000, 2, 2));
    check(!existing_seat.pointer_down(160, 78) && !existing_roof.pointer_down(160, 95),
          "features already present in the Source block record cannot be removed");
    // Both absent flags may be selected in one transition only through the
    // single-bit source control, so this forged successor is rejected.
    StadiumScreen one_feature(data, detail_state());
    one_feature.click(160, 78);
    auto tampered = edited_state(one_feature, 8000, 3);
    rejected = false;
    try { one_feature.acknowledge(tampered); } catch (const FormatError&) { rejected = true; }
    check(rejected && one_feature.pending(), "multi-bit feature jumps are rejected and retryable");
    StadiumScreen roof_screen(data, detail_state());
    const auto roof = roof_screen.click(160, 95);
    check(roof.kind == StadiumRequestKind::expansion_toggle_feature && roof.block_features == 2,
          "roof retrofit is a separate Source single-bit action");
    roof_screen.acknowledge(edited_state(roof_screen, 8000, 2));
    check(roof_screen.state().block_expansion->quote.roof_cost == 900000,
          "the accepted roof successor has the canonical slot-B cost");

    StadiumScreen cancelled(data, detail_state());
    const auto cancel = cancelled.click(140, 140);
    auto returned = cancelled.state(); next_identity(returned); returned.block_expansion.reset();
    cancelled.acknowledge(returned);
    check(cancel.kind == StadiumRequestKind::expansion_cancel && !cancelled.state().block_expansion,
          "a valid quote-bound cancel ACK returns to the A–H overview");
}

void calculator_tests(const std::filesystem::path& data) {
    const auto exe = load_le(data / "BMH.exe");
    {
        StadiumScreen timed(data,detail_state());timed.click(50,80);timed.text("15333");
        const auto editor=timed.state().block_expansion;
        check(timed.process_timer_expired().kind==StadiumRequestKind::none && !timed.calculator_open() &&
              !timed.calculator_source_waiting() && !timed.pending() &&
              timed.state().block_expansion->total_target_places==editor->total_target_places &&
              timed.state().block_expansion->quote==editor->quote,
              "Source expiry cancels unconfirmed digits immediately without changing the accepted editor quote");
        const auto cancel=timed.process_timer_expired();
        check(cancel.kind==StadiumRequestKind::expansion_cancel && cancel.expected_block_quote==editor->quote &&
              cancel.block_total_places==8000 && cancel.block_features==0,
              "next block poll emits the owned drawless cancellation without building");
        const auto pending=timed.state_json();check(timed.process_timer_expired().kind==StadiumRequestKind::none && timed.state_json()==pending,
              "expiry cannot repeat or replace the pending block cancellation");
        auto root=timed.state();next_identity(root);root.block_expansion.reset();timed.acknowledge(root);
        check(timed.process_timer_expired().kind==StadiumRequestKind::close,"only after the block ACK may expiry close the stadium parent");
        StadiumScreen held(data,detail_state());held.pointer_down(50,80);
        check(held.process_timer_expired().kind==StadiumRequestKind::none && !held.pending(),"expiry keeps a held calculator-opening control");
        held.release(50,80);held.pointer_down(116,36);
        check(held.process_timer_expired().kind==StadiumRequestKind::none && held.calculator_open(),"expiry retains calculator dragging until physical release");
        held.release(116,36);check(held.process_timer_expired().kind==StadiumRequestKind::none && !held.calculator_open(),"released calculator resumes its Source expiry return");
        StadiumScreen accepted(data,detail_state());accepted.click(50,80);accepted.text("15000");accepted.key("ENTER");
        const auto flashing=accepted.state_json();check(accepted.process_timer_expired().kind==StadiumRequestKind::none && accepted.state_json()==flashing,"expiry preserves the accepted keyboard J's Source flash");
        accepted.calculator_mark_presented();accepted.calculator_advance_source_ticks(4);
        const auto issued=accepted.state_json();check(accepted.pending()&&accepted.pending()->kind==StadiumRequestKind::expansion_set_total && accepted.process_timer_expired().kind==StadiumRequestKind::none && accepted.state_json()==issued,"accepted quantity keeps its typed ACK before expiry cancellation");
        StadiumScreen n_wait(data,detail_state());n_wait.click(50,80);
        n_wait.pointer_down(111+exe.u16(::bmh::layout::Advertising_Click_RuntimeField+14*2)+1,31+exe.u8(::bmh::layout::Advertising_Click_RuntimeField2+14)+1);
        const auto waiting=n_wait.state_json();check(n_wait.process_timer_expired().kind==StadiumRequestKind::none && n_wait.state_json()==waiting,"expiry retains the N-key's existing fifty-tick wait");
        n_wait.calculator_mark_presented();n_wait.calculator_advance_source_ticks(49);
        check(n_wait.process_timer_expired().kind==StadiumRequestKind::none && n_wait.calculator_source_waiting(),"N keeps its final Source tick");
        n_wait.calculator_advance_source_ticks(1);
        check(n_wait.process_timer_expired().kind==StadiumRequestKind::expansion_cancel,"completed N wait resumes the outer block deadline");
    }
    StadiumScreen screen(data, detail_state());
    check(screen.click(50, 80).kind == StadiumRequestKind::none,
          "amount opens the asset-backed calculator locally");
    check(screen.calculator_open(), "host can query calculator visibility without reading or mutating Native");
    assert_readout_source_text(screen, data, format_number(GameState{}, 0, 7),
                               "initial calculator readout matches Original width-seven FNT3 glyphs");
    assert_preset_source_cell(screen, data, exe, 7, 18000);
    const Image before = screen.render();
    check(screen.pointer_down(116, 36), "the calculator header starts a Source drag");
    screen.pointer_move(1000, 1000);
    check(screen.calculator_drag_to(100, 90), "drag geometry stays within Source bounds");
    screen.release(1000, 1000);
    check(!(screen.render().pixels == before.pixels), "the calculator keeps its dragged caller coordinates");
    assert_readout_source_text(screen, data, format_number(GameState{}, 0, 7),
                               "dragging and redraw preserve the initial width-seven readout mode", 100, 90);
    check(screen.text("15333"), "raw manual text edits the initial amount");
    assert_readout_source_text(screen, data, format_number(GameState{}, 15333, 8),
                               "edited calculator readout matches Original width-eight FNT3 glyphs", 100, 90);

    auto key_state = detail_state(); key_state.gradient_text = true;
    StadiumScreen keys(data, key_state);
    keys.click(50, 80);
    const int kx = 111 + exe.u16(::bmh::layout::Advertising_Click_RuntimeField);
    const int ky = 31 + exe.u8(::bmh::layout::Advertising_Click_RuntimeField2);
    check(keys.pointer_down(kx + 16, ky + 14),
          "calculator key hit testing includes the Source right and bottom endpoints");
    assert_key_source_cell(keys, data, exe, 0, true);
    check(keys.calculator_source_waiting() && !keys.text("9"),
          "pressed keypad phase blocks dependent text input");
    check(!keys.pointer_down(50, 80), "a second pointer action is ignored during the key phase");
    check(keys.release(kx + 16, ky + 14).kind == StadiumRequestKind::none &&
          keys.calculator_source_waiting(),
          "an early physical UP is deferred without consuming its matching key owner");
    (void)keys.render();
    keys.calculator_mark_presented();
    check(keys.calculator_advance_source_ticks(3) && keys.calculator_source_waiting(),
          "the pressed crop remains through the first three 20ms Source ticks");
    check(keys.calculator_advance_source_ticks(1) && !keys.calculator_source_waiting(),
          "the pressed crop restores after four Source ticks");
    check(keys.calculator_waiting_for_key_release(),
          "the deferred physical UP remains bound to the original key after its flash ends");
    assert_key_source_cell(keys, data, exe, 0, false);
    check(keys.release(kx + 16, ky + 14).kind == StadiumRequestKind::none,
          "the retained physical UP commits its digit only after the four-tick phase");

    StadiumScreen keyboard_j(data, detail_state());
    keyboard_j.click(50, 80);
    check(keyboard_j.text("15333") && keyboard_j.key("RETURN").kind == StadiumRequestKind::none &&
          !keyboard_j.pending(),
          "keyboard Return paints J first instead of dispatching a total immediately");
    (void)keyboard_j.render(); keyboard_j.calculator_mark_presented();
    check(keyboard_j.calculator_advance_source_ticks(3) && !keyboard_j.pending(),
          "keyboard Return cannot commit during the first three ticks");
    check(keyboard_j.calculator_advance_source_ticks(1) && keyboard_j.pending() &&
          keyboard_j.pending()->kind == StadiumRequestKind::expansion_set_total &&
          keyboard_j.pending()->block_total_places == 15333,
          "keyboard Return dispatches the typed total after four presented ticks");

    StadiumScreen physical_j(data, detail_state());
    physical_j.click(50, 80);
    check(physical_j.text("15333"), "physical J fixture starts from a Source raw numeric value");
    const auto physical_j_request = source_key_down_present_ticks_up(physical_j, exe, 13);
    check(physical_j_request.kind == StadiumRequestKind::expansion_set_total &&
          physical_j_request.block_total_places == 15333 &&
          !physical_j.calculator_waiting_for_key_release(),
          "physical J keeps matching-UP ownership and dispatches only after its four ticks");

    StadiumScreen preset(data, detail_state());
    preset.click(50, 80);
    const int preset_x = 111 + exe.u16(::bmh::layout::Advertising_Click_RuntimeField + (15 + 7) * 2);
    const int preset_y = 31 + exe.u8(::bmh::layout::Advertising_Click_RuntimeField2 + 15 + 7);
    check(preset.click(preset_x, preset_y).kind == StadiumRequestKind::none &&
          !preset.calculator_source_waiting(),
          "preset selection changes the value without the keypad's invented pressed phase");
    const auto preset_accept = keyboard_return_after_source_phase(preset);
    check(preset_accept.kind == StadiumRequestKind::expansion_set_total &&
          preset_accept.block_total_places == 18000,
          "a preset value remains usable through the timed Source J action");

    StadiumScreen low_total(data, detail_state());
    low_total.click(50, 80);
    check(low_total.text("7") && keyboard_return_after_source_phase(low_total).kind == StadiumRequestKind::none,
          "below-minimum calculator input clamps without an immediate App action");
    const auto clamped = keyboard_return_after_source_phase(low_total);
    check(clamped.kind == StadiumRequestKind::expansion_set_total && clamped.block_total_places == 8000,
          "the clamped minimum needs the second Source confirmation");

    StadiumScreen high_total(data, detail_state());
    high_total.click(50, 80);
    check(high_total.text("999999"), "large typed input is accepted and clamped to Source maximum");
    const auto maximum = keyboard_return_after_source_phase(high_total);
    check(maximum.kind == StadiumRequestKind::expansion_set_total && maximum.block_total_places == 18000,
          "calculator input never exceeds the slot-B maximum");

    StadiumScreen reopened(data, detail_state(1, 15333, 1));
    reopened.click(50, 80);
    check(reopened.text("1"), "reopened calculator accepts another appended digit");
    const auto appended = keyboard_return_after_source_phase(reopened);
    check(appended.kind == StadiumRequestKind::expansion_set_total &&
          appended.block_total_places == 18000,
          "reopening starts from D=7333 and appends instead of replacing it with the first digit");

    StadiumScreen zeroes(data, detail_state());
    zeroes.click(50, 80);
    check(zeroes.text("0000000000") &&
          zeroes.state_json().find("\"digit_count\":10") != std::string::npos,
          "Source local18 counts ten entered zeroes even though their numeric value stays zero");
    check(!zeroes.text("9"), "the eleventh digit is ignored after ten zeroes");
    assert_readout_source_text(zeroes, data, format_number(GameState{}, 0, 8),
                               "the rejected eleventh digit cannot change the places readout");
    zeroes.key("BACKSPACE"); zeroes.calculator_mark_presented();
    check(zeroes.calculator_advance_source_ticks(4) && zeroes.text("9"),
          "Source D frees one zero digit, allowing the next numeric input");
    assert_readout_source_text(zeroes, data, format_number(GameState{}, 9, 8),
                               "a nonzero value recomputes the Source digit count");
    check(zeroes.state_json().find("\"digit_count\":1") != std::string::npos,
          "nonzero digit count comes from the formatted numeric value");

    StadiumScreen cancelled(data, detail_state());
    const Image underlay = cancelled.render();
    cancelled.click(50, 80);
    const int nx = 111 + exe.u16(::bmh::layout::Advertising_Click_RuntimeField + 14 * 2);
    const int ny = 31 + exe.u8(::bmh::layout::Advertising_Click_RuntimeField2 + 14);
    check(cancelled.pointer_down(nx, ny), "N key accepts its Source DOWN");
    check(!cancelled.pending() && cancelled.state().block_expansion->quote.total_target_places == 8000,
          "N cancellation remains a local edit and preserves the accepted total");
    check(cancelled.calculator_source_waiting() && !cancelled.calculator_open() &&
          cancelled.state_json().find("\"frame_visible\":false") != std::string::npos,
          "N restores the editor immediately while keeping input blocked for its exit wait");
    check(cancelled.render().pixels == underlay.pixels,
          "format field restores the exact editor backdrop before the fifty-tick wait");
    check(!cancelled.calculator_advance_source_ticks(50) && cancelled.calculator_source_waiting(),
          "the exit clock cannot advance before its restored frame is actually presented");
    check(!cancelled.text("9") && !cancelled.pointer_down(50, 80),
          "calculator input stays blocked through the separate fifty-tick N wait");
    check(cancelled.release(nx, ny).kind == StadiumRequestKind::none,
          "the paired N UP cannot dispatch an edit or start another key flash");
    cancelled.calculator_mark_presented();
    check(cancelled.calculator_advance_source_ticks(49) && cancelled.calculator_source_waiting(),
          "the restored backdrop remains input-owned through forty-nine of fifty Source ticks");
    check(cancelled.render().pixels == underlay.pixels,
          "the restored editor stays unchanged throughout the exit wait");
    check(cancelled.calculator_advance_source_ticks(1) && !cancelled.calculator_source_waiting() &&
          !cancelled.calculator_open(),
          "input ownership returns to the source editor after the fiftieth N wait tick");
    check(cancelled.render().pixels == cancelled.construction_notice_backdrop().pixels,
          "after N's wait the exact underlying editor frame remains visible");

    StadiumScreen right_edge(data, detail_state());
    check(!right_edge.pointer_down(100, 80), "unpainted amount x=100 is outside Source hit bounds");
    check(!right_edge.pointer_down(50, 100), "amount y=100 is outside Source hit bounds");
}

void offer_ack_tests(const std::filesystem::path& data) {
    auto initial = detail_state(1, 15333, 1);
    StadiumScreen yes_screen(data, initial);
    check(yes_screen.click(50, 140).kind == StadiumRequestKind::expansion_confirm,
          "positive-price quote confirms through the Source initial offer path");
    auto pending = yes_screen.state(); next_identity(pending);
    auto& editor = *pending.block_expansion;
    editor.offer = pending_offer(editor.quote);
    pending.construction_tip_dialog = question_for(0);
    const Image before_confirm_ack = yes_screen.render();
    auto forged_gradient = pending;
    forged_gradient.gradient_text = !pending.gradient_text;
    bool rejected = false;
    try { yes_screen.acknowledge(forged_gradient); } catch (const FormatError&) { rejected = true; }
    check(rejected && yes_screen.pending() && yes_screen.render().pixels == before_confirm_ack.pixels,
          "a one-field forged gradient gate preserves the confirm request and visible editor for retry");
    auto forged_question_gate = pending;
    forged_question_gate.construction_tip_dialog->compact_dialog_frame = !pending.compact_dialog_frame;
    rejected = false;
    try { yes_screen.acknowledge(forged_question_gate); } catch (const FormatError&) { rejected = true; }
    check(rejected && yes_screen.pending() && yes_screen.render().pixels == before_confirm_ack.pixels,
          "a forged nested question compact_dialog_frame gate preserves the confirm request and visible editor for retry");
    auto coherent_a2569 = pending;
    coherent_a2569.plain_background = !pending.plain_background;
    coherent_a2569.decorated_stadium_header = !coherent_a2569.plain_background;
    rejected = false;
    try { yes_screen.acknowledge(coherent_a2569); } catch (const FormatError&) { rejected = true; }
    check(rejected && yes_screen.pending() && yes_screen.render().pixels == before_confirm_ack.pixels,
          "a coherent forged plain_background/header pair is rejected by the immutable accepted context");
    yes_screen.acknowledge(pending);
    const auto yes = yes_screen.click(100, 171);
    check(yes.kind == StadiumRequestKind::expansion_answer_tip && yes.accept_tip,
          "plain manager flag 0 question dialog selects Ja");
    auto forged = yes_screen.state(); next_identity(forged);
    const size_t slot = 1;
    forged.block_expansion.reset(); forged.construction_tip_dialog.reset();
    forged.blocks[slot].construction_weeks = 10; // forged build, outside Source shortening interval
    forged.blocks[slot].flags = uint8_t(forged.blocks[slot].flags | 0x10);
    forged.blocks[slot].capacity = 15;
    rejected = false;
    try { yes_screen.acknowledge(forged); } catch (const FormatError&) { rejected = true; }
    check(rejected && yes_screen.pending(), "forged shortened weeks preserve the pending tip for retry");
    auto built = yes_screen.state(); next_identity(built);
    const auto quote = built.block_expansion->quote;
    const auto offer = *built.block_expansion->offer;
    built.block_expansion.reset(); built.construction_tip_dialog.reset();
    built.blocks[slot].construction_weeks = 25;
    built.blocks[slot].flags = uint8_t(built.blocks[slot].flags | 0x10);
    built.blocks[slot].capacity = 15;
    built.balance -= int32_t(offer.offered_price);
    yes_screen.acknowledge(built);
    check(!yes_screen.pending() && yes_screen.state().blocks[slot].construction_weeks == 25,
          "a shortened Source-valid YES build ACK commits after complete validation");
    (void)quote;

    StadiumScreen no_screen(data, initial);
    no_screen.click(50, 140);
    auto no_pending = no_screen.state(); next_identity(no_pending);
    no_pending.block_expansion->offer = pending_offer(no_pending.block_expansion->quote);
    no_pending.construction_tip_dialog = question_for(0);
    no_screen.acknowledge(no_pending);
    const auto no = no_screen.click(210, 171);
    check(no.kind == StadiumRequestKind::expansion_answer_tip && !no.accept_tip,
          "plain manager flag 0 question dialog selects Nein");
    auto no_build = no_screen.state(); next_identity(no_build);
    const auto no_quote = no_build.block_expansion->quote;
    no_build.block_expansion.reset(); no_build.construction_tip_dialog.reset();
    no_build.blocks[1].construction_weeks = 37;
    no_build.blocks[1].flags = uint8_t(no_build.blocks[1].flags | 0x10);
    no_build.blocks[1].capacity = 15;
    no_build.balance -= int32_t(no_quote.base_price);
    no_screen.acknowledge(no_build);
    check(!no_screen.pending() && no_screen.state().blocks[1].construction_weeks == 37,
          "a valid NO decision constructs at the original base price and full duration");

    auto poor = initial;
    poor.balance = 1000;
    StadiumScreen poor_screen(data, poor);
    poor_screen.click(50, 140);
    auto poor_pending = poor_screen.state(); next_identity(poor_pending);
    poor_pending.block_expansion->offer = pending_offer(poor_pending.block_expansion->quote);
    poor_pending.construction_tip_dialog = question_for(0);
    poor_screen.acknowledge(poor_pending);
    check(poor_screen.click(100, 171).accept_tip, "the insufficient-funds case answers Ja");
    auto insufficient = poor_screen.state(); next_identity(insufficient);
    insufficient.block_expansion.reset(); insufficient.construction_tip_dialog.reset();
    poor_screen.acknowledge(insufficient);
    check(!poor_screen.pending() && poor_screen.state().balance == poor.balance &&
          poor_screen.state().blocks[1].capacity == poor.blocks[1].capacity,
          "an insufficient-funds YES refusal consumes the answer without a forged build");
}

void source_compensation_ack_tests(const std::filesystem::path& data) {
    const auto overbooked_state = [] {
        auto state = detail_state(1, 15333, 1);
        for (size_t i = 0; i < state.blocks.size(); ++i) {
            state.blocks[i].capacity_level = 2;
            state.blocks[i].capacity = 2;
        }
        state.blocks[1].capacity_level = 8;
        state.blocks[1].capacity = 8;
        state.source_level = 2;
        state.season_tickets = 25000;
        state.live_league_round = 4;
        state.ticket_refund_rate = 2;
        return state;
    };
    const auto stage_question = [](StadiumScreen& screen) {
        check(screen.click(50, 140).kind == StadiumRequestKind::expansion_confirm,
              "overbooked fixture enters the ordinary Source offer path");
        auto pending = screen.state(); next_identity(pending);
        pending.block_expansion->offer = pending_offer(pending.block_expansion->quote);
        pending.construction_tip_dialog = question_for(0);
        screen.acknowledge(std::move(pending));
    };

    auto normal = overbooked_state();
    StadiumScreen positive(data, normal);
    stage_question(positive);
    check(positive.click(100, 171).accept_tip, "overbooked positive-price Source question selects Ja");
    const auto offer = *positive.state().block_expansion->offer;
    // Seven other idle blocks at 2,000 places each leave 11,000 tickets
    // without seats. Source has 13 weeks remaining and rate 2: 286,000.
    auto forged_context = completed_build_state(positive, 25,
        normal.balance - int32_t(offer.offered_price) - 286000, 14000);
    ++forged_context.ticket_refund_rate;
    bool rejected = false;
    try { positive.acknowledge(forged_context); } catch (const FormatError&) { rejected = true; }
    check(rejected && positive.pending(),
          "a one-field forged refund-rate context leaves the accepted Source question retryable");
    auto forged_screen_flag = completed_build_state(positive, 25,
        normal.balance - int32_t(offer.offered_price) - 286000, 14000);
    forged_screen_flag.compact_dialog_frame = true;
    rejected = false;
    try { positive.acknowledge(forged_screen_flag); } catch (const FormatError&) { rejected = true; }
    check(rejected && positive.pending(),
          "the independent format field context flag is immutable through the accepted draft");
    auto forged_gradient = completed_build_state(positive, 25,
        normal.balance - int32_t(offer.offered_price) - 286000, 14000);
    forged_gradient.gradient_text = !normal.gradient_text;
    const Image accepted_question_frame = positive.render();
    rejected = false;
    try { positive.acknowledge(forged_gradient); } catch (const FormatError&) { rejected = true; }
    check(rejected && positive.pending() && positive.render().pixels == accepted_question_frame.pixels,
          "a one-field forged gradient after the question preserves its frame and pending answer for retry");
    auto forged_decoration_flag = completed_build_state(positive, 25,
        normal.balance - int32_t(offer.offered_price) - 286000, 14000);
    forged_decoration_flag.plain_background = true;
    forged_decoration_flag.decorated_stadium_header = false;
    rejected = false;
    try { positive.acknowledge(forged_decoration_flag); } catch (const FormatError&) { rejected = true; }
    check(rejected && positive.pending() && positive.render().pixels == accepted_question_frame.pixels,
          "a coherent forged plain_background/header pair preserves the question frame and accepted answer for retry");
    auto forged_panel_flag = completed_build_state(positive, 25,
        normal.balance - int32_t(offer.offered_price) - 286000, 14000);
    forged_panel_flag.solid_panel_background = !normal.solid_panel_background;
    rejected = false;
    try { positive.acknowledge(forged_panel_flag); } catch (const FormatError&) { rejected = true; }
    check(rejected && positive.pending() && positive.render().pixels == accepted_question_frame.pixels,
          "a forged a2560 panel mode cannot replace the accepted Source frame/context");
    auto forged_tickets = completed_build_state(positive, 25,
        normal.balance - int32_t(offer.offered_price) - 286000, 13999);
    rejected = false;
    try { positive.acknowledge(forged_tickets); } catch (const FormatError&) { rejected = true; }
    check(rejected && positive.pending(),
          "a one-ticket forged compensation result leaves the valid build retryable");
    positive.acknowledge(completed_build_state(positive, 25,
        normal.balance - int32_t(offer.offered_price) - 286000, 14000));
    check(!positive.pending() && positive.state().season_tickets == 14000 &&
          positive.state().balance == normal.balance - int32_t(offer.offered_price) - 286000,
          "the canonical positive compensation and sold-seat cap are accepted together");

    auto mismatched_question = overbooked_state();
    mismatched_question.block_expansion->offer = pending_offer(mismatched_question.block_expansion->quote);
    mismatched_question.construction_tip_dialog = question_for(0);
    mismatched_question.construction_tip_dialog->compact_dialog_frame = !mismatched_question.compact_dialog_frame;
    StadiumScreen mismatched_answer(data, mismatched_question);
    check(mismatched_answer.click(100, 171).accept_tip,
          "the forged prior-question fixture emits an otherwise ordinary Source answer request");
    const Image mismatched_frame = mismatched_answer.render();
    bool old_question_rejected = false;
    try {
        mismatched_answer.acknowledge(completed_build_state(mismatched_answer, 25,
            mismatched_question.balance - int32_t(mismatched_question.block_expansion->offer->offered_price) - 286000,
            14000));
    } catch (const FormatError&) { old_question_rejected = true; }
    check(old_question_rejected && mismatched_answer.pending() &&
          mismatched_answer.render().pixels == mismatched_frame.pixels,
          "an accepted question with forged format field cannot authorize an answer or replace its frame");

    auto late = overbooked_state();
    late.live_league_round = 20; // Source permits a signed negative remaining-week result.
    StadiumScreen negative_weeks(data, late);
    stage_question(negative_weeks);
    check(!negative_weeks.click(210, 171).accept_tip,
          "negative-week compensation path can take the ordinary NO construction");
    const auto no_offer = *negative_weeks.state().block_expansion->offer;
    const int32_t negative_compensation = -66000; // 11,000 * (17-20) * rate 2.
    negative_weeks.acknowledge(completed_build_state(negative_weeks, 37,
        late.balance - int32_t(no_offer.base_price) - negative_compensation, 14000));
    check(negative_weeks.state().balance == late.balance - int32_t(no_offer.base_price) + 66000,
          "negative Source weeks credit the account while the ticket cap still applies");

    auto level_zero = overbooked_state();
    level_zero.source_level = 0;
    StadiumScreen no_account_booking(data, level_zero);
    stage_question(no_account_booking);
    check(!no_account_booking.click(210, 171).accept_tip,
          "level-zero Source path can accept a normal NO construction");
    const auto level_zero_offer = *no_account_booking.state().block_expansion->offer;
    no_account_booking.acknowledge(completed_build_state(no_account_booking, 37,
        level_zero.balance, 14000));
    check(no_account_booking.state().balance == level_zero.balance &&
          no_account_booking.state().season_tickets == 14000 &&
          no_account_booking.state().blocks[1].capacity == 15,
          "level zero skips account booking but commits capacity and the Source ticket cap");
    (void)level_zero_offer;
}
}

namespace {
void source_periphery_hit_tests(const std::filesystem::path& data) {
    // Independent fixed Original object centers (format field), including the
    // overlapping fan-shop chain. Existing objects hide all unrelated choices.
    constexpr std::array<std::array<int, 2>, 14> centers{{
        {239,172}, {266,125}, {80,165}, {224,128}, {61,131}, {172,169},
        {148,136}, {138,136}, {166,135}, {176,135}, {284,188}, {279,176},
        {270,163}, {261,152}}};
    for (int object = 0; object < 14; ++object) {
        auto state = overview_state(); state.level.fill(1); state.level[size_t(object)] = 0;
        StadiumScreen screen(data, state);
        screen.click(30,220);
        const auto [x,y] = centers[size_t(object)];
        check(screen.click(x,y).kind == StadiumRequestKind::none && !screen.pending(),
              "panorama opens in the Original read-only mode");
        screen.click(160,220);
        check(screen.pointer_down(x,y) && !screen.pending(),
              "an Original building hit depresses without quoting before release");
        const auto quote = screen.release(x,y);
        check(quote.kind == StadiumRequestKind::quote_periphery && quote.object == object,
              "each Original building center emits its matching owned quote request");
        check(screen.click(x,y).kind == StadiumRequestKind::none,
              "pending quote blocks duplicate physical actions");
    }
    auto state = overview_state(); state.level.fill(1); state.level[1] = 0;
    StadiumScreen dragged(data,state); dragged.click(30,220); dragged.click(160,220);
    check(dragged.pointer_down(266,125), "Original arcade rectangle owns DOWN");
    check(dragged.release(240,125).kind == StadiumRequestKind::none && !dragged.pending(),
          "release outside that building cancels the quote");
    check(dragged.click(30,52).kind == StadiumRequestKind::none,
          "the removed synthetic fourteen-row list is not an invisible click target");
    state.level[10] = 0; state.level[11] = 0; state.level[1] = 1;
    StadiumScreen chain(data,state); chain.click(30,220); chain.click(160,220);
    check(chain.click(279,164).kind == StadiumRequestKind::none,
          "later fan shop requires its preceding shop exactly as Source");
    state.days[1] = 30; state.level[1] = 0;
    StadiumScreen busy(data,state); busy.click(30,220);
    const auto before_hover = busy.render().pixels;
    check(busy.pointer_move(266,125) && busy.render().pixels != before_hover,
          "a live construction has the Source hover information in read-only panorama");
    busy.click(160,220);
    check(busy.click(266,125).kind == StadiumRequestKind::none,
          "construction in progress never creates a new quote");
    // Use the real shared Source dialog renderer and its flags1 geometry.
    auto offered = overview_state(); offered.quoted_object = 1; offered.quoted_price = 1700000;
    offered.periphery_quote_dialog = question_for(0);
    offered.periphery_quote_dialog->source_flags = 1;
    offered.periphery_quote_dialog->body = "Eine Baufirma bietet den Bau f[r 1.700.000 EU an.";
    StadiumScreen no(data,offered);
    check(no.key("ESC").kind == StadiumRequestKind::none,
          "quote is resolved only through its Original JA/NEIN question");
    auto answer = no.click(200,170);
    check(answer.kind == StadiumRequestKind::decline_periphery && answer.object == 1 && answer.price == 1700000,
          "Original NEIN emits the exact drawless owned quote decline");
    StadiumScreen yes(data,offered); answer = yes.click(100,170);
    check(answer.kind == StadiumRequestKind::build_periphery && answer.object == 1 && answer.price == 1700000,
          "Original JA preserves the authoritative price and object on build");
}
}

namespace {
void source_stadium_data_controls(const std::filesystem::path& data) {
    const auto exe = load_le(asset_path(data, "BMH.exe"));
    auto state = overview_state();
    StadiumScreen overview(data, state);
    check(overview.pointer_down(265, 220), "Original stadium data button binds on DOWN");
    const auto held_overview=overview.state_json();
    check(overview.key("ESC").kind==StadiumRequestKind::none && !overview.pending() &&
          overview.state_json()==held_overview,"back cannot steal the held stadium data selector");
    check(overview.pointer_move(100, 100) && overview.release(265,220).kind == StadiumRequestKind::none,
          "dragging out cancels the stadium data button");
    const auto open = overview.click(265, 220);
    check(open.kind == StadiumRequestKind::open_data && open.object == -1,
          "right Original scene button emits a typed data opening");
    state.revision++; state.sequence++; state.data.emplace();
    state.data->flags = 0x7f; state.data->ticket_price = 13;
    state.data->required_attendance_text = "34.932";
    for (int i=0;i<6;++i) { state.data->minimum[i]=0;state.data->maximum[i]=100; }
    overview.acknowledge(state);
    check(overview.state().data && overview.render().width == 320,
          "accepted stadium data replaces the scene with its Source projection");
    StadiumScreen invalid_ack(data,state);
    check(invalid_ack.click(30,110).kind==StadiumRequestKind::toggle_data_curve,
          "independent curve ownership is pending before its acknowledgement");
    auto wrong=state;wrong.revision++;wrong.sequence++;wrong.data->flags^=1;wrong.data->ticket_price=25;
    bool rejected=false;try { invalid_ack.acknowledge(wrong); } catch(const FormatError&) { rejected=true; }
    check(rejected && invalid_ack.pending() && invalid_ack.state().data==state.data,
          "a curve acknowledgement cannot smuggle in a ticket-price change or consume pending ownership");
    for (int control=0;control<24;++control) {
        StadiumScreen screen(data,state);
        const int x = control < 6 ? 27+(control%2)*120+3 : control < 8 ? 268 : 59+(control-8)*16+3;
        const int y = control < 6 ? 107+(control/2)*11+3 : control < 8 ? 111+(control-6)*11+3 : 228;
        check(screen.pointer_down(x,y) && !screen.pending(), "data controls act only after matching UP");
        const auto request=screen.release(x,y);
        check(request.kind==(control<6?StadiumRequestKind::toggle_data_curve:control<8?StadiumRequestKind::toggle_data_scale:StadiumRequestKind::set_data_ticket_price) &&
              request.object==(control<6?control:control<8?control-6:control-8+10),
              "all six toggles, both scale buttons and sixteen entry prices route exact Source coordinates");
        check(screen.click(x,y).kind==StadiumRequestKind::none, "pending data request suppresses a duplicate UP");
    }
    StadiumScreen price_drag(data,state);
    check(price_drag.pointer_down(62,228) && price_drag.pointer_move(100,200) &&
          price_drag.release(62,228).kind==StadiumRequestKind::none, "price drag-out preserves the current price");
    check(price_drag.click(57,228).kind==StadiumRequestKind::none,
          "no invented admission zero-nine target precedes the sixteen Original buttons");
    check(price_drag.key("ESC").kind==StadiumRequestKind::close_data,
          "ESC returns to stadium while the nested data owner stays open until ACK");
    StadiumScreen right(data,state);check(right.pointer_down(300,200,true) && right.pending()->kind==StadiumRequestKind::close_data,
          "Source RMB closes the data child before closing its stadium parent");
    auto plot=*state.data;plot.series[0]={0,50,100};
    check(stadium_data_points(plot,exe,0)==std::vector<std::pair<int,int>>{{13,101},{21,60},{30,18}},
          "first four Source curves use thirty-four columns and truncate 83-pixel half-height");
    plot.flags=0xbf;plot.series[5]={-100,0};
    check(stadium_data_points(plot,exe,5)==std::vector<std::pair<int,int>>{{13,60},{30,60}},
          "Source REL maximum zero forces the constant fifty-percent series even with a negative minimum");
    plot.series[5]={-100,100};
    check(stadium_data_points(plot,exe,5)==std::vector<std::pair<int,int>>{{13,101},{30,18}},
          "weekly curves use seventeen columns and their own relative extrema");
    plot.flags=0x7f;plot.minimum[0]=plot.maximum[0]=0;plot.series[0]={100,200};
    check(stadium_data_points(plot,exe,0)==std::vector<std::pair<int,int>>{{13,101},{21,101}},
          "zero usable capacity preserves the Source x87-indefinite low-short-zero bottom line");
    plot.maximum[5]=100;plot.series[5].assign(43,50);
    check(stadium_data_points(plot,exe,5).back().first==304,
          "a forty-three-week history extends the Source horizon beyond seventeen samples");
}
}

namespace {
void construction_backdrop_tests(const std::filesystem::path& data,const std::filesystem::path& output) {
    const auto save_image=[&](const Image& image,const std::string& name) {
        if(output.empty())return;
        std::filesystem::create_directories(output);const auto palette=load_palette(data/"M3PC.COL");
        std::ofstream file(output/(name+".ppm"),std::ios::binary);file<<"P6\n320 240\n255\n";
        for(auto pixel:image.pixels){const auto color=palette.argb[pixel];file.put(char(color>>16));file.put(char(color>>8));file.put(char(color));}
    };
    for(bool compact:{false,true})for(bool periphery:{false,true}) {
        auto state=periphery?overview_state():detail_state(1,15333,1);
        state.compact_dialog_frame=compact;
        StadiumScreen screen(data,state);
        if(periphery){screen.click(30,220);screen.click(160,220);}
        const auto request=screen.click(periphery?266:50,periphery?125:140);
        check(request.kind==(periphery?StadiumRequestKind::quote_periphery:StadiumRequestKind::expansion_confirm),"construction backdrop fixture did not enter its real request path");
        const Image underlay=screen.render();
        state=screen.state();next_identity(state);
        auto question=question_for();question.compact_dialog_frame=compact;
        if(periphery){state.quoted_object=request.object;state.quoted_price=1700000;question.source_flags=1;state.periphery_quote_dialog=question;}
        else {state.block_expansion->offer=pending_offer(state.block_expansion->quote);state.construction_tip_dialog=question;}
        screen.acknowledge(state);const auto actual=screen.render();
        const auto check_outside=[&](const Image& image) {
            for(int y=0;y<240;++y)if(y<80 || y>=190)for(int x=0;x<320;++x)
                if(image.pixels[size_t(y*320+x)]!=underlay.pixels[size_t(y*320+x)])throw std::runtime_error(std::string(periphery?"periphery":"stands")+" compact="+std::to_string(compact)+" backdrop differs at "+std::to_string(x)+","+std::to_string(y));
        };
        check_outside(actual);
        screen.pointer_down(100,171);check_outside(screen.render());
        GameEndRenderOptions options{state.plain_background,state.gradient_text,true};
        GameEndTaxDialog notice(data,question,options,screen.construction_notice_backdrop());
        check(notice.backdrop().pixels==screen.construction_notice_backdrop().pixels,"construction notice completion restores a black frame");
        check_outside(notice.render(GameEndTaxDialog::Button::yes));
        if(compact) {
            const auto name=periphery?"periphery":"stands";
            GameEndTaxDialog previous(data,question,{state.plain_background,state.gradient_text},underlay);
            check(previous.render().pixels[0]==0,"default compact dialog behavior changed outside construction menus");
            save_image(previous.render(),std::string(name)+"-before");save_image(actual,std::string(name)+"-after");
        }
    }
}
}

int main(int argc, char** argv) try {
    if (argc < 2 || !*argv[1]) throw std::runtime_error("expected Original BMH data directory");
    const std::filesystem::path data = argv[1];
    construction_backdrop_tests(data,argc>2?std::filesystem::path(argv[2]):std::filesystem::path{});
    source_stadium_data_controls(data);
    source_periphery_hit_tests(data);
    row_hit_tests(data);
    source_stadium_style_flag_tests(data);
    solid_panel_block_tests(data);
    source_textd_stadium_caption_tests(data);
    source_remaining_draw_paths(data);
    source_table_and_ack_tests(data);
    calculator_tests(data);
    offer_ack_tests(data);
    source_compensation_ack_tests(data);
    std::cout << "Source-valid A–H tables, calculator phases, compensation ACKs, and plain-manager tip retry checks passed\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "stadium_block_ui_tests: " << e.what() << '\n';
    return 1;
}
