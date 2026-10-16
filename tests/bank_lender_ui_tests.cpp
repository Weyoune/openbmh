// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "app/career_session.hpp"
#include "formats/le.hpp"
#include "formats/vga.hpp"
#include "game/bank_credit.hpp"
#include "game/save.hpp"
#include "ui/bank.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

using namespace bmh;
using namespace bmh::ui;

namespace {
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

std::unique_ptr<app::CareerSession> career(const std::filesystem::path& data, int manager_count) {
    auto session = std::make_unique<app::CareerSession>(data, uint32_t(0x31415926 + manager_count));
    SetupOptions options;
    options.managers = manager_count;
    std::string error;
    std::vector<int> selected;
    for (int slot = 0; slot < manager_count; ++slot) {
        const auto context = session->selection_context(uint64_t(slot + 1));
        const auto selected_club = std::find_if(context.club_order.begin(), context.club_order.end(),
            [&](int club) { return std::find(selected.begin(), selected.end(), club) == selected.end(); });
        check(selected_club != context.club_order.end(), "career fixture provides distinct manager clubs");
        ManagerChoice choice;
        choice.slot = slot;
        choice.name = slot == 0 ? "Bank UI Test" : "Lender UI Test";
        choice.master_club = *selected_club;
        choice.start_league = 0;
        choice.strength = 2;
        choice.portrait = slot;
        choice.selection_club = int(selected_club - context.club_order.begin());
        choice.roster_revision = context.revision;
        check(session->confirm_manager(options, choice, error), "career fixture manager setup succeeds");
        selected.push_back(*selected_club);
    }
    CareerInitializationResult initialized;
    check(session->finish(initialized, error), "career fixture reaches its first bank desk");
    return session;
}

void set_record_u32(GameState& game, size_t manager, size_t offset, uint32_t value) {
    const size_t at = manager * 0xb0c + offset;
    for (unsigned b = 0; b < 4; ++b) game.manager_records.at(at + b) = uint8_t(value >> (b * 8));
}

void copy_source_opaque(Image& target, const Image& source, int sx, int sy, int dx, int dy, int width, int height) {
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        if (sx + x < 0 || sy + y < 0 || sx + x >= source.width || sy + y >= source.height ||
            dx + x < 0 || dy + y < 0 || dx + x >= target.width || dy + y >= target.height) continue;
        target.at(dx + x, dy + y) = source.pixels[size_t(sy + y) * size_t(source.width) + size_t(sx + x)];
    }
}

void mark_body_text(std::vector<uint8_t>& mask, int screen_width, int screen_height,
                    const Font& font, int x, int baseline, std::string_view utf8) {
    const std::string bytes = utf8_to_original(utf8);
    const int top = baseline - font.baseline;
    int pen_x = x;
    for (const unsigned char ch : bytes) {
        const auto& glyph = font.glyph(ch);
        for (int row = 0; row < font.height; ++row) {
            for (int col = 0; col < glyph.bit_width; ++col) {
                const int bit = glyph.bit_offset + col;
                if (!(font.bitmap[size_t(row) * font.modulo + size_t(bit / 8)] & (0x80 >> (bit % 8)))) continue;
                const int px = pen_x + col, py = top + row;
                if (px >= 0 && py >= 0 && px < screen_width && py < screen_height)
                    mask[size_t(py) * size_t(screen_width) + size_t(px)] = 1;
            }
        }
        pen_x += glyph.advance;
    }
}

void mark_large_text(std::vector<uint8_t>& mask, int screen_width, int screen_height,
                     const Font& font, int x, int baseline, std::string_view utf8) {
    mark_body_text(mask, screen_width, screen_height, font, x, baseline, utf8);
}

void source_lender_crop_rectangles(const std::filesystem::path& data, const LeImage& exe,
                                  const BankState& projection) {
    struct Crop {
        int sx1, sy1, dx1, dy1, w1, h1;
        int sx2, sy2, dx2, dy2, w2, h2;
    };
    // Literal legacy routine emitted-lender crop tuples, independently transcribed
    // from Original EXE rows 0, 1, and 2. The separate hit-box rows are 5, 6,
    // and 7 and are tested by manager_count_projection_and_hitboxes().
    constexpr std::array<Crop, 3> source{{
        {0, 0, 24, 122, 91, 80, 179, 95, 1, 103, 55, 38},
        {91, 0, 45, 113, 88, 89, 91, 89, 1, 68, 81, 73},
        {179, 0, 74, 107, 70, 90, 0, 80, 24, 62, 70, 79},
    }};
    const Image kb64 = load_vga(asset_path(data / "KB", "64.VGA"));
    const Image background = load_vga(asset_path(data / "KB", "23.VGA"));
    Image expected = background;
    check(projection.credit_lenders.size() <= source.size(), "literal crop fixture has at most three emitted lender alternatives");
    if (projection.credit_lenders.empty()) {
        const Image actual = BankScreen(data, projection).render();
        for (int y = 45; y < 187; ++y) for (int x = 49; x < 261; ++x)
            check(actual.pixels[size_t(y) * size_t(actual.width) + size_t(x)] ==
                  background.pixels[size_t(y) * size_t(background.width) + size_t(x)],
                  "no-lender room keeps the unpatched KB/23 image");
        return;
    }
    std::vector<uint8_t> covered(expected.pixels.size());
    for (size_t ordinal = 0; ordinal < projection.credit_lenders.size(); ++ordinal) {
        const Crop& crop = source[ordinal];
        copy_source_opaque(expected, kb64, crop.sx1, crop.sy1, crop.dx1, crop.dy1, crop.w1, crop.h1);
        for (int y = 0; y < crop.h1; ++y) for (int x = 0; x < crop.w1; ++x) {
            const size_t target = size_t(crop.dy1 + y) * size_t(expected.width) + size_t(crop.dx1 + x);
            covered[target] = 1;
        }
    }
    BankScreen screen(data, projection);
    const Image actual = screen.render();
    for (size_t pixel = 0; pixel < covered.size(); ++pixel)
        if (covered[pixel]) check(actual.pixels[pixel] == expected.pixels[pixel],
            "room lender pass matches literal Source crop/destination rectangle and opaque pixel value");

    // The second KB/64 pass modifies KB/5 for bank subviews; it is not
    // composited over the room. Derive the frame from literal tuples and
    // require a visible 0xff source pixel to survive the opaque copy (0xff is
    // deliberately included because a transparent-key blit would drop it).
    Image expected_frame = load_vga(asset_path(data / "KB", "5.VGA"));
    std::vector<uint8_t> panel_covered(expected_frame.pixels.size());
    for (size_t ordinal = 0; ordinal < projection.credit_lenders.size(); ++ordinal) {
        const Crop& crop = source[ordinal];
        copy_source_opaque(expected_frame, kb64, crop.sx2, crop.sy2, crop.dx2, crop.dy2, crop.w2, crop.h2);
        for (int y = 0; y < crop.h2; ++y) for (int x = 0; x < crop.w2; ++x) {
            const int px = crop.dx2 + x, py = crop.dy2 + y;
            if (px >= 0 && py >= 0 && px < expected_frame.width && py < expected_frame.height)
                panel_covered[size_t(py) * size_t(expected_frame.width) + size_t(px)] = 1;
        }
    }
    BankState no_lenders = projection;
    no_lenders.credit_lenders.clear();
    BankScreen control(data, no_lenders);
    control.click(exe.u16(::bmh::layout::Bank_FinishCredit_RuntimeField + 2), exe.u8(::bmh::layout::Bank_FinishCredit_RuntimeField3 + 1));
    const Image control_view = control.render();
    BankScreen finance(data, projection);
    finance.click(exe.u16(::bmh::layout::Bank_FinishCredit_RuntimeField + 2), exe.u8(::bmh::layout::Bank_FinishCredit_RuntimeField3 + 1));
    const Image view = finance.render();
    // Build the expected visible room independently from Source's emitted
    // crop tuples. The control render supplies the same text/KB/5 subview;
    // pass 1 is then applied only where the modal does not cover the room.
    Image expected_visible_base = control_view;
    for (size_t ordinal = 0; ordinal < projection.credit_lenders.size(); ++ordinal) {
        const Crop& crop = source[ordinal];
        for (int y = 0; y < crop.h1; ++y) for (int x = 0; x < crop.w1; ++x) {
            const int px = crop.dx1 + x, py = crop.dy1 + y;
            if (px < 0 || py < 0 || px >= expected_visible_base.width || py >= expected_visible_base.height)
                continue;
            const bool covered_by_modal = px >= 49 && px < 261 && py >= 45 && py < 187;
            if (!covered_by_modal)
                expected_visible_base.at(px, py) = kb64.pixels[size_t(crop.sy1 + y) * size_t(kb64.width) +
                                                               size_t(crop.sx1 + x)];
        }
    }

    // Mask only Source's known finance-view glyph pixels: the centered title,
    // the seven labels/currency markers/values, and any displayed holdings.
    // Every other screen pixel is compared exactly either to the unpatched
    // control render or to the independently composed opaque KB/5 crop.
    std::vector<uint8_t> overlay(size_t(view.width) * size_t(view.height));
    const Font body = load_font(asset_path(data / "F", "BMH3.FNT"));
    const Font large = load_font(asset_path(data / "F", "BMH2.FNT"));
    const TextDb textd = load_textd(asset_path(data, "TEXTD.BLK"));
    if (textd.strings.size() > 1272)
        mark_large_text(overlay, view.width, view.height, large, 111, 54,
                        original_to_utf8(textd[1272]));
    for (int x = 51; x <= 258; ++x) overlay[size_t(55) * size_t(view.width) + size_t(x)] = 1;
    int held_row = 0;
    for (size_t i = 0; i < projection.funds.size(); ++i) if (projection.funds[i].owned) {
        const int baseline = 85 + held_row * 6;
        mark_body_text(overlay, view.width, view.height, body, 53, baseline, projection.finance_fund_labels[i]);
        mark_body_text(overlay, view.width, view.height, body, 169, baseline, projection.finance_fund_prices[i]);
        ++held_row;
    }
    held_row = 0;
    for (size_t i = 0; i < projection.stocks.size(); ++i) if (projection.stocks[i].owned) {
        const int x = held_row < 6 ? 53 : 156;
        const int baseline = 131 + (held_row % 6) * 6;
        mark_body_text(overlay, view.width, view.height, body, x, baseline, projection.finance_stock_labels[i]);
        mark_body_text(overlay, view.width, view.height, body, x + 70, baseline, projection.finance_stock_prices[i]);
        ++held_row;
    }
    for (int i = 0; i < 7; ++i) {
        const int x = exe.u8(::bmh::layout::Bank_OriginalToUtf8_RuntimeField2 + size_t(i));
        const int baseline = exe.u8(::bmh::layout::Bank_OriginalToUtf8_RuntimeField3 + size_t(i));
        const size_t label_id = size_t((::bmh::layout::Bank_OriginalToUtf8_RuntimeField6 - ::bmh::layout::TextPointerTable) / 4) + size_t(i);
        if (label_id < textd.strings.size())
            mark_body_text(overlay, view.width, view.height, body, x, baseline,
                           original_to_utf8(textd[label_id]));
        mark_body_text(overlay, view.width, view.height, body, x + 42, baseline, exe.cstring(::bmh::layout::Bank_OriginalToUtf8_ExecutableField));
        const int value_x = x + ((i == 4 || i == 6) ? 44 : 49);
        mark_body_text(overlay, view.width, view.height, body, value_x, baseline, projection.finance_value_text[size_t(i)]);
    }

    bool opaque_ff = false;
    size_t visible_outer_source_pixels = 0;
    for (int y = 0; y < view.height; ++y) for (int x = 0; x < view.width; ++x) {
        const size_t screen_at = size_t(y) * size_t(view.width) + size_t(x);
        const bool in_frame = x >= 49 && y >= 45 && x < 49 + expected_frame.width && y < 45 + expected_frame.height;
        const int fx = x - 49, fy = y - 45;
        const size_t frame_at = in_frame ? size_t(fy) * size_t(expected_frame.width) + size_t(fx) : 0;
        if (in_frame && panel_covered[frame_at] && !overlay[screen_at]) {
            if (view.pixels[screen_at] != expected_frame.pixels[frame_at])
                throw std::runtime_error("KB/5 literal crop mismatch at screen " + std::to_string(x) + "," +
                    std::to_string(y) + " expected=" + std::to_string(expected_frame.pixels[frame_at]) +
                    " actual=" + std::to_string(view.pixels[screen_at]));
            opaque_ff |= expected_frame.pixels[frame_at] == 0xff;
        } else {
            const bool visible_pass1 = x >= 24 && x < 115 && y >= 122 && y < 202 &&
                !(x >= 49 && x < 261 && y >= 45 && y < 187);
            if (visible_pass1 && !overlay[screen_at]) ++visible_outer_source_pixels;
            if (view.pixels[screen_at] != expected_visible_base.pixels[screen_at])
                throw std::runtime_error("unexpected pixel outside declared KB/5 crop/glyph regions at screen " +
                    std::to_string(x) + "," + std::to_string(y) + " expected=" +
                    std::to_string(expected_visible_base.pixels[screen_at]) + " actual=" +
                    std::to_string(view.pixels[screen_at]));
        }
    }
    check(opaque_ff, "literal KB/5 projection preserves visible 0xff crop pixels opaquely");
    check(visible_outer_source_pixels > 2500,
          "ordinary bank subview retains the visible outer region of Source lender-room artwork");
}

void manager_count_projection_and_hitboxes(const std::filesystem::path& data, const LeImage& exe, int count) {
    auto session = career(data, count);
    auto& game = session->state();
    const int active = int(game.memory.u8(::bmh::layout::ActiveManager));
    check(game.memory.u8(::bmh::layout::ManagerCount) == count, "career fixture saves configured human manager count");
    // Give every potential lender ample Source liquidity so case-5 editing is testable.
    for (int slot = 0; slot < count; ++slot) {
        set_record_u32(game, size_t(slot), 0x314, 2'000'000);
        set_record_u32(game, size_t(slot), 0x31c, 100'000);
    }
    const auto native = save_stream(game);
    const auto w_seed = session->rand().seed();
    const auto w_calls = session->rand().calls();
    const auto g_seed = session->game().seed();
    const auto g_calls = session->game().calls();
    const BankState projection = bank_from_game(game, exe, 17);
    check(int(projection.credit_lenders.size()) == count - 1, "configured count yields one to three lenders");
    if (!projection.credit_lenders.empty()) {
        BankState forged_bank = projection;
        forged_bank.credit.counterparty = projection.credit_lenders.front().counterparty;
        bool rejected_bank_context = false;
        try { (void)BankScreen(data, forged_bank); } catch (const FormatError&) { rejected_bank_context = true; }
        check(rejected_bank_context, "constructor refuses a lender context forged into the bank base identity");
    }
    for (size_t i = 0; i < projection.credit_lenders.size(); ++i) {
        const auto& lender = projection.credit_lenders[i];
        const int expected_slot = int(i) < active ? int(i) : int(i) + 1;
        check(lender.manager_slot == expected_slot && lender.counterparty == expected_slot + 1,
              "lender alternatives retain ascending Source manager slots and slot-plus-one IDs");
        check(lender.credit.counterparty == lender.counterparty,
              "each lender context uses the selected counterparty loan record");
    }
    source_lender_crop_rectangles(data, exe, projection);
    BankScreen stable(data, projection);
    const Image first = stable.render();
    check(first.width == 320 && first.height == 240 && stable.render().pixels == first.pixels,
          "opaque selector rendering is repeatable");
    check(save_stream(game) == native && session->rand().seed() == w_seed && session->rand().calls() == w_calls &&
          session->game().seed() == g_seed && session->game().calls() == g_calls,
          "projection and rendering preserve full Native and both random streams");
    if (count >= 2) {
        GameState switched = game;
        const int next_active = (active + 1) % count;
        switched.memory.set8(::bmh::layout::ActiveManager, uint8_t(next_active));
        const auto other_owner = bank_from_game(switched, exe, 18);
        check(other_owner.credit_lenders.size() == size_t(count - 1) &&
              std::none_of(other_owner.credit_lenders.begin(), other_owner.credit_lenders.end(),
                  [&](const auto& lender) { return lender.manager_slot == next_active; }),
              "current borrower exclusion follows the live active-manager slot");
        GameState no_liquidity = game;
        const int first_lender = projection.credit_lenders.front().manager_slot;
        set_record_u32(no_liquidity, size_t(first_lender), 0x314, 0);
        set_record_u32(no_liquidity, size_t(first_lender), 0x31c, 0);
        const auto no_funds_projection = bank_from_game(no_liquidity, exe, 19);
        const auto no_funds = std::find_if(no_funds_projection.credit_lenders.begin(),
            no_funds_projection.credit_lenders.end(),
            [&](const auto& lender) { return lender.manager_slot == first_lender; });
        check(no_funds != no_funds_projection.credit_lenders.end() && no_funds->credit.lender_liquidity == 0 &&
              !bank_credit_bounds(no_funds->credit, bank_credit_draft(no_funds->credit), 0).available,
              "Source still shows a manager lender with no credit limit; affordability is enforced in its editor");
    }

    for (size_t i = 0; i < projection.credit_lenders.size(); ++i) {
        const size_t table = i + 5;
        const int left = exe.u8(::bmh::layout::Bank_FinishCredit_RuntimeField5 + table), top = exe.u8(::bmh::layout::Bank_FinishCredit_RuntimeField6 + table);
        for (const auto [x, y] : std::array<std::pair<int, int>, 4>{{
                 {left, top}, {left + 0x11, top}, {left, top + 0x11}, {left + 0x11, top + 0x11}}}) {
            BankScreen screen(data, projection);
            screen.click(x, y);
            check(screen.credit_open() && screen.state().credit_counterparty == projection.credit_lenders[i].counterparty,
                  "all inclusive lender hitbox corners select their projected counterparty");
            check(screen.state().credit.saved == projection.credit_lenders[i].credit.saved,
                  "lender selection opens that lender’s live loan matrix, not the bank’s");
            screen.key("ESC");
            check(!screen.credit_open() && screen.state().credit_counterparty == 0 &&
                  screen.state().credit.saved == projection.credit.saved,
                  "cancel returns to bank context without mutating the loan projection");
        }
    }
    if (count >= 2) {
        const auto& lender = projection.credit_lenders.front();
        const size_t table = 5;
        BankScreen screen(data, projection);
        screen.click(exe.u8(::bmh::layout::Bank_FinishCredit_RuntimeField5 + table), exe.u8(::bmh::layout::Bank_FinishCredit_RuntimeField6 + table));
        screen.click(80, 74); // case-5 principal (field 0)
        check(screen.calculator_open(), "case 5 opens principal input from the first editable row");
        screen.key("ENTER");
        screen.click(80, 92); // case-5 monthly payment (field 2, bank case 4 leaves this row inert)
        check(screen.calculator_open(), "only the selected manager-credit screen enables monthly-payment input");
        screen.key("ENTER");
        const BankRequest request = screen.click(80, 178);
        check(request.kind == BankRequestKind::bank_credit && request.credit_counterparty == lender.counterparty &&
              !request.accept_credit_fees && request.expected_credit == lender.credit.saved,
              "case-5 typed credit request carries fresh lender identity and no bank fee acceptance");
        check(std::any_of(request.credit_edits.begin(), request.credit_edits.end(),
              [](const BankCreditEdit& edit) { return edit.field == 0; }) &&
              std::any_of(request.credit_edits.begin(), request.credit_edits.end(),
              [](const BankCreditEdit& edit) { return edit.field == 2; }),
              "case-5 editor request replays principal and the separately adjustable monthly payment");
        check(bank_request_json(request).find("\"credit_counterparty\":" + std::to_string(lender.counterparty)) != std::string::npos,
              "JSON protocol preserves manager-credit counterparty");

        const std::string shown_before_bad_ack = screen.state_json();
        BankState wrong_counterparty = projection;
        ++wrong_counterparty.revision;
        wrong_counterparty.credit_counterparty = lender.counterparty + 1;
        bool wrong_cp_rejected = false;
        try { screen.acknowledge(wrong_counterparty); } catch (const FormatError&) { wrong_cp_rejected = true; }
        check(wrong_cp_rejected && screen.pending().has_value() && screen.state_json() == shown_before_bad_ack,
              "ACK naming a different lender preserves the exact shown state and pending request");
        BankState stale = projection;
        ++stale.revision;
        stale.credit_lenders.clear();
        bool rejected = false;
        try { screen.acknowledge(stale); } catch (const FormatError&) { rejected = true; }
        check(rejected && screen.pending().has_value() && screen.credit_open() &&
              screen.state_json() == shown_before_bad_ack,
              "ACK without the pending lender in its fresh projection is rejected and retryable");

        GameState accepted = game;
        const auto booked = commit_bank_credit(accepted, exe, active, request.expected_credit,
            request.credit_edits, request.accept_credit_fees, request.credit_counterparty);
        check(booked.status == BankCreditStatus::accepted, "test lender request is accepted by existing Core path");
        BankState next = bank_from_game(accepted, exe, projection.revision + 1);
        const std::string before_forged_bank_ack = screen.state_json();
        BankState forged_bank_ack = next;
        forged_bank_ack.credit.counterparty = lender.counterparty;
        bool forged_bank_rejected = false;
        try { screen.acknowledge(forged_bank_ack); } catch (const FormatError&) { forged_bank_rejected = true; }
        check(forged_bank_rejected && screen.pending().has_value() &&
              screen.state_json() == before_forged_bank_ack,
              "ACK rejects nonzero bank base context without consuming the selected-lender request");
        screen.acknowledge(next);
        check(!screen.pending() && !screen.credit_open() && screen.state().credit_counterparty == 0 &&
              screen.state().credit.saved == bank_credit_context(accepted, exe, active).saved,
              "accepted ACK rebuilds and retains the fresh lender context without default-row leakage");
    }
    check(save_stream(game) == native && session->rand().seed() == w_seed && session->rand().calls() == w_calls &&
          session->game().seed() == g_seed && session->game().calls() == g_calls,
          "selection, editor, cancel, request and ACK probes leave owner Native and W/G unchanged");

    // A non-credit ACK still carries the bank base credit context. Exercise a
    // real deposit request, reject a one-field forged nested counterparty,
    // then retry the same valid changed projection successfully.
    BankScreen deposit(data, projection);
    deposit.click(exe.u16(::bmh::layout::Bank_FinishCredit_RuntimeField + 3 * 2), exe.u8(::bmh::layout::Bank_FinishCredit_RuntimeField3 + 3));
    deposit.click(80, 180);
    check(deposit.calculator_open() && deposit.text("1"), "deposit action opens its source calculator and accepts a typed amount");
    const int accept_x = 103 + exe.u16(::bmh::layout::Advertising_Click_RuntimeField + 13 * 2);
    const int accept_y = 55 + exe.u8(::bmh::layout::Advertising_Click_RuntimeField2 + 13);
    const BankRequest deposit_request = deposit.click(accept_x, accept_y);
    check(deposit_request.kind == BankRequestKind::move_to_deposit && deposit_request.amount == 1 &&
          deposit.pending().has_value(), "deposit calculator emits a pending non-credit transaction");
    BankState accepted_deposit = projection;
    ++accepted_deposit.revision;
    --accepted_deposit.balance;
    ++accepted_deposit.deposit;
    BankState forged_deposit = accepted_deposit;
    forged_deposit.credit.counterparty = projection.credit_lenders.empty()
        ? 1 : projection.credit_lenders.front().counterparty;
    const std::string before_forged_deposit = deposit.state_json();
    bool forged_deposit_rejected = false;
    try { deposit.acknowledge(forged_deposit); } catch (const FormatError&) { forged_deposit_rejected = true; }
    check(forged_deposit_rejected && deposit.pending().has_value() &&
          deposit.state_json() == before_forged_deposit,
          "non-credit ACK rejects a forged lender nested in the bank context without consuming pending state");
    deposit.acknowledge(accepted_deposit);
    check(!deposit.pending() && deposit.state().balance == accepted_deposit.balance &&
          deposit.state().deposit == accepted_deposit.deposit,
          "valid non-credit ACK retries and caches the canonical bank context");
}

void configured_count_beats_hidden_manager(const std::filesystem::path& data, const LeImage& exe) {
    auto session = career(data, 4);
    auto game = session->state();
    game.manager_teams.push_back(kNoTeam); // session-only national manager can extend the runtime vector
    game.memory.set8(::bmh::layout::ManagerCount, 4);
    const auto projected = bank_from_game(game, exe, 1);
    check(projected.credit_lenders.size() == 3 &&
          std::none_of(projected.credit_lenders.begin(), projected.credit_lenders.end(),
              [](const auto& lender) { return lender.manager_slot == 4; }),
          "lender scan follows saved configured count, not hidden national manager vector size");
    game.memory.set8(::bmh::layout::ManagerCount, 5);
    bool rejected = false;
    try { (void)bank_from_game(game, exe, 1); } catch (const FormatError&) { rejected = true; }
    check(rejected, "unsupported five-manager lender domain fails closed");
}

void manager_loan_refinance_and_monthly_presets(const std::filesystem::path& data, const LeImage& exe) {
    auto session = career(data, 2);
    GameState game = session->state();
    const int borrower = int(game.memory.u8(::bmh::layout::ActiveManager));
    const int counterparty = borrower == 0 ? 2 : 1;
    for (int slot = 0; slot < 2; ++slot) {
        set_record_u32(game, size_t(slot), 0x314, 2'000'000);
        set_record_u32(game, size_t(slot), 0x31c, 100'000);
    }

    auto context = bank_credit_context(game, exe, borrower, counterparty);
    auto draft = bank_credit_draft(context);
    check(edit_bank_credit(context, draft, 0, 1'000'000), "controlled case-5 loan principal is valid");
    auto opened = commit_bank_credit(game, exe, borrower, context.saved, draft.edits, false, counterparty);
    check(opened.status == BankCreditStatus::accepted, "existing lender loan fixture uses the shared Core commit");

    context = bank_credit_context(game, exe, borrower, counterparty);
    draft = bank_credit_draft(context);
    check(edit_bank_credit(context, draft, 3, 12), "controlled case-5 loan term can be set to twelve months");
    auto shortened = commit_bank_credit(game, exe, borrower, context.saved, draft.edits, false, counterparty);
    check(shortened.status == BankCreditStatus::accepted, "controlled case-5 loan term uses the shared Core commit");
    const size_t loan = ::bmh::layout::Loans + size_t(borrower) * 0x78 + size_t(counterparty) * 0x18;
    context = bank_credit_context(game, exe, borrower, counterparty);
    game.memory.set32(uint32_t(loan + 16), 6);
    game.memory.set32(uint32_t(loan + 20), context.saved[2] * 6);
    context = bank_credit_context(game, exe, borrower, counterparty);
    check(context.saved[0] == 1'000'000 && context.saved[3] == 12 && context.saved[4] == 6,
          "fixture exposes Source-shaped existing principal, term and elapsed months");
    auto refinance = bank_credit_draft(context);
    check(edit_bank_credit(context, refinance, 3, 1) && refinance.interest_adjustment != 0,
          "existing case-5 refinance produces the nonzero interest adjustment that must not ask bank fees");

    BankState projection = bank_from_game(game, exe, 40);
    const auto lender = std::find_if(projection.credit_lenders.begin(), projection.credit_lenders.end(),
        [&](const auto& candidate) { return candidate.counterparty == counterparty; });
    check(lender != projection.credit_lenders.end() && lender->credit.saved == context.saved,
          "fresh screen projection carries the outstanding manager loan");
    BankScreen editor(data, projection);
    const int lender_index = int(lender - projection.credit_lenders.begin());
    const size_t hitbox_table = size_t(5 + lender_index);
    editor.click(exe.u8(::bmh::layout::Bank_FinishCredit_RuntimeField5 + hitbox_table), exe.u8(::bmh::layout::Bank_FinishCredit_RuntimeField6 + hitbox_table));
    editor.click(80, 100); // Source term row, away from the preceding inclusive field-2 edge.
    editor.key("ENTER"); // Source minimum term is accepted.
    check(editor.state_json().find("\"credit_fee_confirmation_open\":true") == std::string::npos,
          "case-5 nonzero interest adjustment never opens the bank-only fee prompt");
    const BankRequest request = editor.click(80, 178);
    check(request.kind == BankRequestKind::bank_credit && request.credit_counterparty == counterparty &&
          !request.accept_credit_fees &&
          editor.state_json().find("\"credit_fee_confirmation_open\":true") == std::string::npos,
          "existing manager refinance submits directly without fee acceptance");

    const auto bounds = bank_credit_bounds(context, bank_credit_draft(context), 2);
    check(bounds.available, "controlled outstanding loan permits monthly-payment editing");
    if (bounds.initial != 93296 || bounds.minimum != 74637 || bounds.maximum != 111955)
        throw std::runtime_error("monthly fixture bounds differ from its pinned Source inputs: initial=" +
            std::to_string(bounds.initial) + " min=" + std::to_string(bounds.minimum) +
            " max=" + std::to_string(bounds.maximum));
    const std::array<uint32_t, 8> source_presets{{74637, 83966, 93295, 102624, 111953, UINT32_MAX, 74637, 111955}};
    std::array<uint32_t, 8> clicked_values = source_presets;
    clicked_values[5] = bounds.initial; // UINT32_MAX is Source's disabled cell; clicking leaves the annuity selected.
    for (size_t button = 0; button < clicked_values.size(); ++button) {
        BankScreen presets(data, projection);
        presets.click(exe.u8(::bmh::layout::Bank_FinishCredit_RuntimeField5 + hitbox_table), exe.u8(::bmh::layout::Bank_FinishCredit_RuntimeField6 + hitbox_table));
        presets.click(80, 92); // case-5 monthly row, away from the preceding inclusive field-1 edge.
        const size_t cell = button + 15;
        const int x = 103 + exe.u16(::bmh::layout::Advertising_Click_RuntimeField + cell * 2);
        const int y = 55 + exe.u8(::bmh::layout::Advertising_Click_RuntimeField2 + cell);
        presets.click(x, y);
        const std::string shown = presets.state_json();
        if (shown.find("\"calculator_value\":" + std::to_string(clicked_values[button])) == std::string::npos)
            throw std::runtime_error("monthly Source preset mismatch at cell " + std::to_string(button) +
                " expected clicked value=" + std::to_string(clicked_values[button]) + " state=" + shown);
    }
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("bank_lender_ui_tests requires the BMH data directory");
        const std::filesystem::path data = argv[1];
        const LeImage exe = load_le(asset_path(data, "BMH.exe"));
        for (int count = 1; count <= 4; ++count) manager_count_projection_and_hitboxes(data, exe, count);
        configured_count_beats_hidden_manager(data, exe);
        manager_loan_refinance_and_monthly_presets(data, exe);
        std::cout << "Source lender crop rectangles and opaque pixels, no-lender baseline, configured count/order, inclusive hitboxes, case-5 fee/preset behavior, typed counterparty ACK, and read-only Native/W/G invariants passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
