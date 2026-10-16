// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/advertising.hpp"
#include "ui/source_notice_test_helpers.hpp"

#include <array>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "app/career_session.hpp"
#include "formats/textd.hpp"
#include "game/messages.hpp"
#include "game/state.hpp"
#include "ui/manager.hpp"
#include "ui/setup.hpp"

using namespace bmh;
using namespace bmh::ui;

namespace {
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
void rejected(const std::function<void()>& action) {
    try { action(); } catch (const FormatError&) { return; }
    throw std::runtime_error("expected invalid advertising projection rejection");
}
void put32(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes.at(at + size_t(i)) = uint8_t(value >> (8 * i));
}
void set_contract(GameState& game, int slot, uint32_t amount, uint8_t months,
                  uint8_t flags, uint8_t sponsor) {
    const size_t at = 0xa34 + size_t(slot) * 0xc;
    put32(game.manager_records, at, amount);
    game.manager_records.at(at + 5) = months;
    game.manager_records.at(at + 6) = flags;
    game.manager_records.at(at + 7) = sponsor;
}
void set_offer(GameState& game, int group, int raw_slot, uint32_t amount,
               uint8_t bundle, uint8_t months, uint8_t sponsor) {
    const size_t at = 0x8cc + size_t(group) * 0x78 + size_t(raw_slot) * 0x0c;
    put32(game.manager_records, at, amount);
    game.manager_records.at(at + 4) = bundle;
    game.manager_records.at(at + 5) = months;
    game.manager_records.at(at + 7) = sponsor;
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: ui_advertising_tests <BMH data>");
        const std::filesystem::path data(argv[1]);
        app::CareerSession session(data, 9941);
        SetupOptions options;
        ManagerChoice choice;
        choice.name = "SIM";
        choice.master_club = 0;
        choice.strength = 2;
        std::string error;
        check(session.confirm_manager(options, choice, error), "engine manager setup failed");
        CareerInitializationResult initialized;
        check(session.finish(initialized, error), "engine career initialization failed");
        GameState game = session.state();
        check(game.manager_records.size() >= 0xb0c && game.manager_teams.size() == 1,
              "initialized manager record is available");

        set_contract(game, 0, 25000, 12, 0, 0);
        set_contract(game, 1, 8000, 7, 0x80, 1);
        set_contract(game, 14, 5000, 10, 0x03, 2);
        set_contract(game, 15, 12000, 11, 0x80, 3);
        const auto records_before = game.manager_records;
        const auto state = advertising_from_game(game, 0);
        check(state.boards.size() == 14 && state.boards[0].record_slot == 0 &&
              state.boards[0].monthly_amount == 25000 && state.boards[0].sponsor_id == 0 &&
              state.boards[0].amount_text == original_to_utf8(format_number(game, 25000, 0x108)),
              "the source ordering keeps regular active board deals ahead of options");
        check(state.boards[1].record_slot == 1 && state.boards[1].option_marker &&
              state.boards[1].months_remaining == 7,
              "board contract flags and remaining months are projected without reinterpretation");
        check(state.shirt.record_slot == 14 && state.shirt.monthly_amount == 5000 &&
              state.shirt.extension_months == 3 && state.television.record_slot == 15 &&
              state.television.monthly_amount == 12000,
              "shirt and TV contract groups use the two source-supported extra surface records");
        check(state.boards[0].sponsor_name == original_to_utf8(message_text(game, ::bmh::layout::Advertising_FormatError_ExecutableField3)) &&
              state.television.sponsor_name == original_to_utf8(message_text(game, ::bmh::layout::Advertising_FormatError_ExecutableField3 + 13 * 4)),
              "sponsor labels resolve through the matching TEXTD pointer table");
        check(game.manager_records == records_before,
              "projection reads advertising fields without changing the game record");
        check(state.finance_preview.balance == state.finance_preview.income - state.finance_preview.expenses,
              "the owned financial preview follows the exact Original income-minus-expenses relationship");
        check(state.finance_preview.income_text == original_to_utf8(
                  format_number(game, state.finance_preview.income, 0x88) + " EU.") &&
              state.finance_preview.expenses_text == original_to_utf8(
                  format_number(game, state.finance_preview.expenses, 0x88) + " EU.") &&
              state.finance_preview.balance_text == original_to_utf8(
                  format_number(game, state.finance_preview.balance, 0x88) + " EU."),
              "finance display strings use the Original 0x188 formatter and format field suffix");

        // Controlled source fixture: zero all finance/ownership inputs, leave the
        // 14 always-charged facility costs, and one 1,234 EU advertising contract.
        GameState finance_fixture = game;
        const size_t rec = 0;
        for (int i = 0; i < 6; ++i) finance_fixture.manager_records[size_t(i)] = 0;
        for (int i = 0x3c; i < 0x44; ++i) finance_fixture.manager_records[size_t(i)] = 0;
        for (int i = 0; i < 16; ++i) put32(finance_fixture.manager_records, 0xa34 + size_t(i) * 0xc, 0);
        put32(finance_fixture.manager_records, rec + 0x314, 0);
        put32(finance_fixture.manager_records, rec + 0x318, 0);
        put32(finance_fixture.manager_records, rec + 0x31c, 0);
        put32(finance_fixture.manager_records, rec + 0x854, 0);
        put32(finance_fixture.manager_records, rec + 0x858, 0);
        put32(finance_fixture.manager_records, rec + 0x880, 0);
        finance_fixture.manager_records.at(rec + 0x179) = 0;
        put32(finance_fixture.manager_records, rec + 0x290, uint32_t(game.manager_teams[0]));
        set_contract(finance_fixture, 0, 1234, 1, 0, 0);
        for (size_t slot = 0; slot < 24; ++slot)
            finance_fixture.tactics[slot * 0x58 + 0x3d] = 0xff;
        finance_fixture.memory.set8(::bmh::layout::ManagerCount, 1);
        for (int i = 0; i < 6; ++i) {
            finance_fixture.memory.set32(::bmh::layout::FundBlock + uint32_t(i) * 0x1c, 0);
            finance_fixture.memory.set16(::bmh::layout::Advertising_OriginalShareAdjustment_RuntimeField + uint32_t(i) * 0x1c, 0);
            finance_fixture.memory.set32(::bmh::layout::Spending + uint32_t(i) * 4, 0);
        }
        for (uint32_t at = ::bmh::layout::Loans; at < ::bmh::layout::FixtureAdvertisingWorkspaceEnd; ++at)
            if (finance_fixture.memory.has(at)) finance_fixture.memory.set8(at, 0);
        const auto finance_record_before = finance_fixture.manager_records;
        const auto finance_tactics_before = finance_fixture.tactics;
        auto finance_memory = [](const GameState& value) {
            std::vector<uint8_t> bytes;
            for (const auto [first, last] : {std::pair<uint32_t,uint32_t>{::bmh::layout::FundBlock,::bmh::layout::FixtureAdvertisingRatingsEnd},
                                             {::bmh::layout::Spending,::bmh::layout::FixtureAdvertisingCountersEnd}, {::bmh::layout::Loans,::bmh::layout::FixtureAdvertisingWorkspaceEnd}})
                for (uint32_t address = first; address < last; ++address)
                    if (value.memory.has(address)) bytes.push_back(value.memory.u8(address));
            return bytes;
        };
        const auto finance_memory_before = finance_memory(finance_fixture);
        const auto preview = advertising_from_game(finance_fixture, 0).finance_preview;
        check(preview.income == 1234 && preview.expenses == 43400 && preview.balance == -42166,
              "a controlled fixture matches Original contract income, fixed facility costs, and negative balance");
        check(finance_fixture.manager_records == finance_record_before &&
              finance_fixture.tactics == finance_tactics_before && finance_fixture.date == game.date &&
              finance_memory(finance_fixture) == finance_memory_before,
              "the financial projection is readonly across records, board, date, and source cost/share blocks");
        GameState shares_fixture = finance_fixture;
        shares_fixture.memory.set32(::bmh::layout::FundBlock, 10000);
        shares_fixture.memory.set16(::bmh::layout::Advertising_OriginalShareAdjustment_RuntimeField, 2);
        check(advertising_from_game(shares_fixture, 0).finance_preview.income == 1264,
              "share income uses the saved share count and the Original 1.5-EU unit factor");
        GameState multi_share_fixture = finance_fixture;
        multi_share_fixture.memory.set32(::bmh::layout::FundBlock, 10000);
        multi_share_fixture.memory.set16(::bmh::layout::Advertising_OriginalShareAdjustment_RuntimeField, 2); // 15 EU * 2
        multi_share_fixture.memory.set32(::bmh::layout::FundBlock + 0x1c, 20000);
        multi_share_fixture.memory.set16(::bmh::layout::Advertising_OriginalShareAdjustment_RuntimeField + 0x1c, 3); // 36 EU * 3
        multi_share_fixture.memory.set32(::bmh::layout::FundBlock + 5 * 0x1c, 10000);
        multi_share_fixture.memory.set16(::bmh::layout::Advertising_OriginalShareAdjustment_RuntimeField + 5 * 0x1c, 2); // 30 EU * 2
        check(advertising_from_game(multi_share_fixture, 0).finance_preview.income == 1432,
              "all six legacy routine rows use their 0x1c-byte record stride");
        GameState interest_fixture = shares_fixture;
        interest_fixture.date = 0; // July; source day table factor 31.
        put32(interest_fixture.manager_records, 0x314, 3650000);
        check(advertising_from_game(interest_fixture, 0).finance_preview.income == 4364,
              "the Original param-0 balance interest is projected with the current month-length factor");
        GameState cap_fixture = finance_fixture;
        cap_fixture.memory.set32(::bmh::layout::Spending + 4, 99);
        cap_fixture.memory.set32(::bmh::layout::Spending, 111);
        cap_fixture.memory.set32(::bmh::layout::Spending + 8, 222);
        check(advertising_from_game(cap_fixture, 0).finance_preview.expenses == 1543400,
              "costs 0/2 stay zero while the youth-cost item uses the source league cap");
        put32(cap_fixture.manager_records, 0x880, 1500000);
        const auto no_cap_preview = advertising_from_game(cap_fixture, 0).finance_preview;
        check(no_cap_preview.expenses == 43400,
              "the youth-cost item is suppressed once funds reach the strict source threshold");
        GameState ignored_cost_fixture = finance_fixture;
        ignored_cost_fixture.memory.set32(::bmh::layout::Spending, 111);
        ignored_cost_fixture.memory.set32(::bmh::layout::Spending + 8, 222);
        check(advertising_from_game(ignored_cost_fixture, 0).finance_preview.expenses == 43400,
              "legacy routine clears cost items zero and two regardless of their saved values");
        GameState loan_fixture = finance_fixture;
        loan_fixture.memory.set32(::bmh::layout::Loans + 8, 100);
        const auto loan_preview = advertising_from_game(loan_fixture, 0).finance_preview;
        check(loan_preview.expenses == 43500,
              "the Original preview adds the outstanding-share repayment to expenses");
        GameState min_loan_fixture = finance_fixture;
        min_loan_fixture.memory.set32(::bmh::layout::Loans + 8, 0x80000000u);
        check(advertising_from_game(min_loan_fixture, 0).finance_preview.expenses == 0,
              "share repayment negation wraps definedly at INT_MIN before the Original zero clamp");
        GameState min_finance_fixture = finance_fixture;
        put32(min_finance_fixture.manager_records, 0x314, 0x80000000u);
        put32(min_finance_fixture.manager_records, 0x318, 0x80000000u);
        put32(min_finance_fixture.manager_records, 0x31c, 0x80000000u);
        (void)advertising_from_game(min_finance_fixture, 0);
        const auto preview_json = advertising_state_json(advertising_from_game(finance_fixture, 0));
        check(preview_json.find("\"finance_preview\":{\"income\":1234,\"expenses\":43400,\"balance\":-42166") != std::string::npos &&
              preview_json.find("\"income_text\":") != std::string::npos,
              "the owned UI report exposes original preview totals without creating actions");
        GameState invalid_manager_count = finance_fixture;
        invalid_manager_count.memory.set8(::bmh::layout::ManagerCount, 5);
        rejected([&] { (void)advertising_from_game(invalid_manager_count, 0); });

        // Independent wrap example: 1234 * INT_MAX wraps to -1234, then /2.
        GameState currency_fixture = finance_fixture;
        currency_fixture.memory.add_block(::bmh::layout::CurrencyMultiplier, 8);
        currency_fixture.memory.set32(::bmh::layout::CurrencyMultiplier, 0x7fffffffu);
        currency_fixture.memory.set32(::bmh::layout::CurrencyDivisor, 2);
        put32(currency_fixture.manager_records, 0x854, 4);
        const auto currency_state = advertising_from_game(currency_fixture, 0);
        check(currency_state.finance_preview.income_text ==
                  original_to_utf8(format_number(currency_fixture, -617, 0x88) + " EU.") &&
              currency_state.boards[0].amount_text ==
                  original_to_utf8(format_number(currency_fixture, -617, 0x08) + " EU") &&
              currency_state.auxiliary_text[1] ==
                  original_to_utf8(format_number(currency_fixture, -2, 0x81) + " EU."),
              "currency conversion preserves Original signed wrap in totals, contracts and spending bars");
        currency_fixture.memory.set32(::bmh::layout::CurrencyDivisor, 0);
        rejected([&] { (void)advertising_from_game(currency_fixture, 0); });
        currency_fixture.memory.set32(::bmh::layout::CurrencyMultiplier, 0x80000000u);
        currency_fixture.memory.set32(::bmh::layout::CurrencyDivisor, 0xffffffffu);
        set_contract(currency_fixture, 0, 1235, 1, 0, 0);
        rejected([&] { (void)advertising_from_game(currency_fixture, 0); });

        GameState tied = game;
        for (int slot = 0; slot < 14; ++slot)
            set_contract(tied, slot, 25000, 12, 0, uint8_t(slot % 10));
        const auto tied_records = tied.manager_records;
        const auto tied_state = advertising_from_game(tied, 0);
        constexpr std::array<int, 14> source_tie_order{9, 7, 8, 12, 13, 10, 11,
                                                        2, 0, 1, 5, 6, 3, 4};
        for (size_t i = 0; i < source_tie_order.size(); ++i)
            check(tied_state.boards[i].record_slot == source_tie_order[i],
                  "equal keys follow the Original midpoint-partition swap order");
        check(tied.manager_records == tied_records,
              "Original ordering rearranges only the owned projection, not manager records");

        auto invalid_manager = [&] { (void)advertising_from_game(game, -1); };
        rejected(invalid_manager);
        GameState invalid = game;
        invalid.manager_records[0xa34 + 7] = 0xff;
        rejected([&] { (void)advertising_from_game(invalid, 0); });

        AdvertisingScreen screen(data, state);
        auto duplicate = state;
        duplicate.boards[1].record_slot = duplicate.boards[0].record_slot;
        rejected([&] { AdvertisingScreen invalid(data, duplicate); });
        auto corrupt = state;
        corrupt.shirt.sponsor_id = 10;
        rejected([&] { AdvertisingScreen invalid(data, corrupt); });
        corrupt = state;
        corrupt.television.record_slot = 14;
        rejected([&] { AdvertisingScreen invalid(data, corrupt); });
        auto invalid_text = state;
        invalid_text.finance_labels[0] = std::string(1, char(0xff));
        rejected([&] { AdvertisingScreen bad(data, invalid_text); });
        invalid_text = state;
        invalid_text.finance_preview.income_text = std::string(1, char(0xff));
        rejected([&] { AdvertisingScreen bad(data, invalid_text); });
        invalid_text = state;
        invalid_text.auxiliary_text[1] = std::string(1, char(0xff));
        rejected([&] { AdvertisingScreen bad(data, invalid_text); });
        const auto first = screen.render();
        check(first.width == 320 && first.height == 240 && first.pixels == screen.render().pixels,
              "original-asset advertising view renders deterministically at native resolution");
        const auto exe = load_le(asset_path(data, "BMH.exe"));
        const auto art = load_vga(asset_path(data / "KB", "4.VGA"));
        const auto logos = load_vga(asset_path(data / "KB", "34.VGA"));
        auto pixel = [](const Image& image, int x, int y) { return image.pixels[size_t(y) * image.width + x]; };
        check(pixel(first, 0, 19) == pixel(art, 0, 0x90),
              "the upper sponsor-board strip is cropped from KB/4 at legacy routine coordinates");
        auto plain_state = state;
        plain_state.decorated_background = false;
        AdvertisingScreen plain(data, plain_state);
        const auto plain_image = plain.render();
        check(pixel(plain_image, 0, 4) == 0x44 && pixel(plain_image, 1, 5) == 0x39 &&
              pixel(plain_image, 0, 228) == 0x44 && pixel(plain_image, 1, 229) == 0x39 &&
              pixel(plain_image, 319, 239) == 0,
              "plain_background uses the Original plain header/footer instead of KB129");
        for (int y = 0; y < 22; ++y)
            for (int x = 0; x < 32; ++x)
                check(pixel(first, exe.u16(::bmh::layout::Advertising_Click_RuntimeField3) + x, exe.u16(::bmh::layout::Advertising_Click_RuntimeField4) + y) ==
                          pixel(logos, x, y),
                      "active board logos use the original KB/34 partner sprite and hitbox coordinates");
        check(screen.click(exe.u16(::bmh::layout::Advertising_Click_RuntimeField3) + 1, exe.u16(::bmh::layout::Advertising_Click_RuntimeField4) + 1) &&
              screen.selected_contract() == 0,
              "a board click selects a local read-only details panel");
        const auto selected_json = screen.state_json();
        check(selected_json.find("\"selected_contract\":0") != std::string::npos &&
              selected_json.find(state.club_name) != std::string::npos,
              "owned screen state retains selection and UTF-8 club text");
        check(!screen.click(319, 239) && screen.selected_contract() == 0,
              "unsupported areas do not create an action request or clear the local selection");
        if (screen.offer_open()) {
            check(screen.key("ESC") && !screen.close_requested() && !screen.offer_open() &&
                  screen.key("ESC") && screen.close_requested() && !screen.key("LEFT"),
                  "ESC closes an existing offer browser before the screen");
        } else {
            check(screen.key("ESC") && screen.close_requested() && !screen.key("LEFT"),
                  "without a saved offer, ESC closes the contract details directly");
        }

        // Existing saved offer pool: raw slot 5 is the highest amount, but an
        // empty lower raw slot remains an explicit stop when paging backward.
        GameState offer_game = game;
        for (size_t i = 0; i < 10 * 0x0c; ++i) offer_game.manager_records[0x8cc + i] = 0;
        set_offer(offer_game, 0, 0, 8000, 1, 24, 1);
        set_offer(offer_game, 0, 5, 10904, 1, 24, 2);
        const auto offer_records_before = offer_game.manager_records;
        const auto offer_state = advertising_from_game(offer_game, 0, 41);
        check(offer_state.offers[0].offers.size() == 10 &&
              offer_state.offers[0].offers[5].present &&
              offer_state.offers[0].offers[5].monthly_amount == 10904 &&
              offer_state.offers[0].offers[5].can_accept,
              "the owned view projects a saved raw-pool offer without generating one");
        check(offer_game.manager_records == offer_records_before,
              "offer projection leaves manager contracts and raw pool bytes unchanged");
        AdvertisingScreen offers(data, offer_state, 0x200000001ULL);
        const bool opened = offers.open_offers(0);
        check(opened && offers.offer_open() && offers.selected_offer() == 5,
              "opening the source offer browser selects the highest saved raw offer");
        check(offers.click(140, 164) && offers.selected_offer() == 4 &&
              !offers.click(190, 195) && !offers.pending(),
              "an empty raw slot cannot create an acceptance request");
        check(offers.click(140, 164) && offers.selected_offer() == 3,
              "raw-slot browsing steps across empty rows without skipping them");
        check(offers.click(140, 180) && offers.selected_offer() == 4 &&
              offers.click(140, 180) && offers.selected_offer() == 5,
              "the opposite source control returns through the same raw slots");
        for (int i = 0; i < 5; ++i) check(offers.click(140, 164), "page raw slot toward zero");
        check(offers.selected_offer() == 0 && !offers.click(140, 164) && offers.selected_offer() == 0,
              "raw-slot navigation does not underflow below slot zero");
        for (int i = 0; i < 5; ++i) check(offers.click(140, 180), "restore selected raw slot");
        check(offers.selected_offer() == 5, "selected raw slot is restored before submission");
        check(offers.click(190, 195) && offers.pending().has_value(),
              "accept creates a request envelope rather than mutating GameState");
        const auto request = *offers.pending();
        check(request.request_id == 0x200000001ULL && request.manager_slot == 0 &&
              request.runtime_club == offer_state.runtime_team_id && request.revision == 41 &&
              request.offer.manager == 0 && request.offer.display_slot == 0 &&
              request.offer.contract_slot == offer_state.offers[0].contract_slot &&
              request.offer.raw_slot == 5 &&
              request.offer.expected_contract == offer_state.offers[0].contract_bytes &&
              request.offer.expected_offer == offer_state.offers[0].offers[5].bytes,
              "request binds manager, revision, visible/physical row, raw slot and full byte identities");
        check(offer_game.manager_records == offer_records_before && !offers.click(190, 195),
              "pending request cannot be replayed by clicking and has no engine-side write");
        check(offers.reject_action("veraltete Ansicht") && !offers.pending() &&
              offers.state_json().find("veraltete Ansicht") != std::string::npos,
              "controller rejection clears the pending request and exposes owned feedback");
        check(offers.click(110, 195) && !offers.offer_open() && offers.key("ESC") &&
              offers.close_requested(),
              "cancel closes the offer browser first; a second ESC closes the screen");

        // The three source spending controls open the original calculator. All
        // editing remains local; only J/Enter on a changed value creates a
        // manager/revision-bound request for the app controller.
        GameState spending_game = game;
        put32(spending_game.manager_records, 0x850, 0);
        put32(spending_game.manager_records, 0x854, 10000);
        put32(spending_game.manager_records, 0x858, 15000);
        spending_game.manager_records.at(0x287) = 0;
        const auto spending_records = spending_game.manager_records;
        const auto spending_state = advertising_from_game(spending_game, 0, 73);
        check(spending_state.spending[0].target_slot == 16 && spending_state.spending[0].available &&
              spending_state.spending[0].maximum_amount == 350000 &&
              spending_state.spending[1].target_slot == 17 && spending_state.spending[1].current_amount == 10000 &&
              spending_state.spending[1].maximum_amount == 35000 &&
              spending_state.spending[2].current_amount == 15000,
              "the three read-only spend projections use the source record values and limits");
        check(spending_state.notification_ack_label == original_to_utf8(message_text(spending_game, ::bmh::layout::Advertising_FormatError_TextPointer17)),
              "the Original manager modal button label resolves from its actual EXE text pointer");
        AdvertisingScreen merchandise(data, spending_state, 0x300000001ULL);
        const int merchandise_x = exe.u16(::bmh::layout::Advertising_Click_RuntimeField3 + 16 * 2) + 1;
        const int merchandise_y = exe.u16(::bmh::layout::Advertising_Click_RuntimeField4 + 16 * 2) + 1;
        check(merchandise.click(merchandise_x, merchandise_y) && merchandise.spending_calculator_open() &&
              merchandise.selected_spending_slot() == 16 && merchandise.spending_input() == 0,
              "the original merchandise hitbox opens its calculator with the saved value");
        const auto calculator_art = load_vga(asset_path(data / "KB", "6.VGA"));
        const auto calculator_image = merchandise.render();
        check(calculator_image.pixels[size_t(55) * 320 + 103] == calculator_art.pixels[0],
              "the calculator opens over the original advertising screen using KB/6 at source coordinates");
        check(merchandise.text("200000") && merchandise.spending_input() == 200000 &&
              merchandise.key("ENTER") && merchandise.pending().has_value(),
              "calculator text entry and Enter create one merchandise request");
        const auto spend_request = *merchandise.pending();
        check(spend_request.kind == AdvertisingActionKind::set_spending &&
              spend_request.request_id == 0x300000001ULL && spend_request.manager_slot == 0 &&
              spend_request.runtime_club == spending_state.runtime_team_id && spend_request.revision == 73 &&
              spend_request.spending.manager == 0 && spend_request.spending.target_slot == 16 &&
              spend_request.spending.amount == 200000 && spend_request.spending.expected_amount == 0 &&
              spend_request.spending.expected_delivery_weeks == 0,
              "merchandise request carries manager, revision, target, chosen amount and stale guards");
        check(spending_game.manager_records == spending_records &&
              !merchandise.text("1") && !merchandise.key("ESC") &&
              !merchandise.click(merchandise_x, merchandise_y),
              "editing/request display is read-only and a pending request cannot be changed or replayed");
        check(merchandise.reject_action("veraltete Ansicht") && !merchandise.pending() &&
              !merchandise.spending_calculator_open(),
              "a controller rejection clears the request and dismisses the calculator");

        AdvertisingScreen clamped_input(data, spending_state);
        check(clamped_input.click(merchandise_x, merchandise_y) && clamped_input.text("9999999") &&
              clamped_input.spending_input() == 350000 && clamped_input.key("SPACE") &&
              clamped_input.spending_input() == 0 && clamped_input.key("ESC") && !clamped_input.pending(),
              "typed digits clamp at the source maximum and SPACE clears the calculator without booking");

        AdvertisingScreen unchanged_budget(data, spending_state, 0x300000010ULL);
        const int budget_x = exe.u16(::bmh::layout::Advertising_Click_RuntimeField3 + 17 * 2) + 1;
        const int budget_y = exe.u16(::bmh::layout::Advertising_Click_RuntimeField4 + 17 * 2) + 1;
        check(unchanged_budget.click(budget_x, budget_y) && unchanged_budget.spending_calculator_open() &&
              unchanged_budget.spending_input() == 10000 && unchanged_budget.key("J") &&
              !unchanged_budget.pending() && !unchanged_budget.spending_calculator_open(),
              "accepting an unchanged monthly budget closes locally without an engine request");

        AdvertisingScreen preset_budget(data, spending_state, 0x300000018ULL);
        const int preset0_x = 103 + exe.u16(::bmh::layout::Advertising_Click_RuntimeField + 15 * 2);
        const int preset0_y = 55 + exe.u8(::bmh::layout::Advertising_Click_RuntimeField2 + 15);
        check(preset_budget.click(budget_x, budget_y) &&
              preset_budget.click(preset0_x + 2, preset0_y + 2) &&
              preset_budget.spending_input() == 5000 && preset_budget.key("J") &&
              preset_budget.pending() && preset_budget.pending()->spending.amount == 5000 &&
              preset_budget.pending()->spending.expected_amount == 10000,
              "the first monthly-budget preset uses the original executable hitbox and value");

        AdvertisingScreen clear_budget(data, spending_state, 0x300000020ULL);
        check(clear_budget.click(budget_x, budget_y) && clear_budget.key("C") &&
              clear_budget.spending_input() == 0 && clear_budget.key("J") && clear_budget.pending() &&
              clear_budget.pending()->spending.target_slot == 17 && clear_budget.pending()->spending.amount == 0 &&
              clear_budget.pending()->spending.expected_amount == 10000,
              "clearing a monthly budget creates a request bound to the previous amount");

        AdvertisingScreen empty_merchandise(data, spending_state, 0x300000030ULL);
        check(empty_merchandise.click(merchandise_x, merchandise_y) && empty_merchandise.key("C") &&
              empty_merchandise.key("J") && !empty_merchandise.pending() &&
              !empty_merchandise.spending_calculator_open(),
              "zero merchandise spending is a local no-op and does not ask the engine to purchase");

        GameState delivery_pending = spending_game;
        delivery_pending.manager_records.at(0x287) = 3;
        const auto blocked_state = advertising_from_game(delivery_pending, 0, 74);
        check(!blocked_state.spending[0].available && blocked_state.spending[0].delivery_weeks == 3,
              "a saved delivery countdown makes the merchandise target unavailable");
        AdvertisingScreen blocked_merchandise(data, blocked_state);
        check(blocked_merchandise.click(merchandise_x, merchandise_y) &&
              blocked_merchandise.notification_open() && !blocked_merchandise.spending_calculator_open() &&
              blocked_merchandise.state_json().find(blocked_state.spending_blocked_text) != std::string::npos &&
              blocked_merchandise.click(10, 10) && !blocked_merchandise.notification_open() &&
              !blocked_merchandise.close_requested(),
              "a blocked delivery shows the owned source warning and dismisses without closing the desk");
        AdvertisingScreen timed_merchandise(data,blocked_state);
        check(timed_merchandise.click(merchandise_x,merchandise_y)&&timed_merchandise.timed_notice_open(),
              "delivery warning uses Source timed mode");
        bmh::ui::test::expect_notice_symbols(timed_merchandise.render(),load_vga(asset_path(data/"KB","0.VGA")));
        check(timed_merchandise.expire_timed_notice()&&!timed_merchandise.notification_open()&&
              !timed_merchandise.pending()&&!timed_merchandise.close_requested(),"timed delivery notice expires locally");
        GameState malformed_budget = spending_game;
        put32(malformed_budget.manager_records, 0x854, 36000);
        const auto malformed_budget_state = advertising_from_game(malformed_budget, 0, 75);
        AdvertisingScreen invalid_budget(data, malformed_budget_state);
        check(malformed_budget_state.spending[1].available &&
              malformed_budget_state.spending[1].current_amount == 36000,
              "a legacy monthly value above the new maximum remains available to the source calculator");
        check(invalid_budget.click(budget_x, budget_y) && invalid_budget.spending_calculator_open() &&
              invalid_budget.spending_input() == 35000 && invalid_budget.key("J") &&
              invalid_budget.pending() && invalid_budget.pending()->spending.amount == 35000 &&
              invalid_budget.pending()->spending.expected_amount == 36000,
              "the calculator's maximum clamps a legacy value and binds its old value in the request");
        std::cout << "Advertising projection, request binding and source offer navigation passed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
