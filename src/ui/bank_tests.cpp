// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/bank.hpp"
#include "ui/source_notice_test_helpers.hpp"

#include <cstdio>
#include <filesystem>

#include "ui/setup.hpp"

using namespace bmh;
using namespace bmh::ui;

namespace {
int failures = 0;
void check(bool ok, const char* name) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", name); ++failures; } }
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    try {
        BankState state; state.manager_slot = 0; state.runtime_club = 3; state.manager_name = "Nobody";
        state.club_name = "Deutschland"; state.revision = 8; state.balance = 80000; state.deposit = 120000;
        state.source = "engine_projection";
        BankScreen screen(argv[1], state);
        check(screen.render().width == 320, "original bank room canvas");
        check(screen.click(270, 90).kind == BankRequestKind::none && screen.state_json().find("\"deposit_open\":true") != std::string::npos,
              "Kasse opens the deposit view locally");
        screen.click(90, 180);
        check(screen.calculator_open(), "Einzahlen button opens the original calculator");
        check(!screen.text("-500"), "rejects non-digit input");
        check(screen.text("50000"), "accepts numeric amount");
        auto request = screen.key("ENTER");
        check(request.kind == BankRequestKind::move_to_deposit && request.amount == 50000 &&
              request.manager_slot == 0 && request.runtime_club == 3 && request.revision == 8,
              "deposit request carries context");
        check(screen.key("ENTER").kind == BankRequestKind::none, "duplicate submit suppressed");
        BankState updated = state; updated.revision = 9; updated.balance = 30000; updated.deposit = 170000;
        screen.acknowledge(updated);
        check(screen.state().balance == 30000 && screen.state().deposit == 170000 && !screen.pending() &&
              !screen.calculator_open() && screen.state_json().find("\"deposit_open\":true") != std::string::npos,
              "authoritative acknowledgement returns to the Sparbuch view");

        BankScreen withdrawal(argv[1], state);
        withdrawal.click(270, 90);
        withdrawal.click(210, 180); withdrawal.text("20000");
        check(withdrawal.calculator_open(), "Abheben button opens the original calculator");
        request = withdrawal.key("ENTER");
        check(request.amount == -20000, "withdrawal request sign");
        withdrawal.reject_action("amount_unavailable");
        check(withdrawal.state().balance == state.balance && withdrawal.state().deposit == state.deposit && !withdrawal.pending(),
              "rejection keeps projection unchanged");

        BankScreen capped(argv[1], state);
        capped.click(270, 90); capped.click(90, 180); capped.text("80001");
        check(capped.state_json().find("\"calculator_value\":80000") != std::string::npos,
              "calculator clamps entries to the funded account amount");
        request = capped.key("ENTER");
        check(request.kind == BankRequestKind::move_to_deposit && request.amount == 80000,
              "funded maximum remains a valid deposit request");
        BankScreen escape(argv[1], state);
        escape.click(270, 90); escape.click(90, 180);
        escape.key("ESC");
        check(!escape.calculator_open() && !escape.close_requested(), "escape closes calculator locally");
        escape.key("ESC");
        check(!escape.close_requested() && escape.state_json().find("\"deposit_open\":false") != std::string::npos,
              "second escape returns from the Sparbuch to the room");
        escape.key("ESC");
        check(escape.close_requested(), "third escape exits the bank room");

        BankScreen passbook_left(argv[1], state), passbook_right(argv[1], state);
        passbook_left.click(270, 90); passbook_right.click(270, 90);
        const auto source_passbook = passbook_left.render();
        passbook_right.click(210, 180);
        passbook_right.key("ESC");
        check(passbook_right.render().pixels == source_passbook.pixels,
              "withdrawal mode keeps the Source passbook balance unchanged until ACK");

        BankScreen finance(argv[1], state);
        finance.click(70, 80);
        check(finance.state_json().find("\"finance_open\":true") != std::string::npos &&
              finance.render().width == 320, "Source finance hitbox opens the seven-row read-only overview");
        check(finance.click(70, 80).kind == BankRequestKind::none &&
              finance.state_json().find("\"finance_open\":false") != std::string::npos,
              "finance overview closes locally without a game request");

        state.stocks[2].price = 1000; state.stocks[2].owned = 5; state.stocks[2].max_to_buy = 20;
        BankScreen exchange(argv[1], state);
        exchange.key("B");
        const auto before_render = exchange.state_json();
        const auto rendered = exchange.render();
        check(rendered.width == 320 && rendered.pixels == exchange.render().pixels &&
              exchange.state_json() == before_render,
              "stock screen renders repeatably without changing its projection");
        check(exchange.state_json().find("\"selected_stock\":11") != std::string::npos, "Original opens SKYNET row 11");
        exchange.click(300, 64 + 2 * 8);
        check(exchange.state_json().find("\"selected_stock\":2") != std::string::npos,
              "Source row hitbox selects by y without an invented x gate");
        exchange.click(70, 178);
        check(exchange.calculator_open() && exchange.state_json().find("\"calculator_value\":0") != std::string::npos,
              "KAUFEN opens the Source share-count calculator at zero");
        exchange.text("3");
        request = exchange.key("ENTER");
        check(request.kind == BankRequestKind::buy_shares && request.asset_index == 2 && request.count == 3 &&
              request.revision == state.revision && !exchange.calculator_open(),
              "share purchase request carries selected stock, count and current revision");

        exchange.reject_action("investment_limit");
        check(exchange.state_json().find("\"investment_warning_open\":true") != std::string::npos && !exchange.pending(),
              "Source board rejection opens the information dialog");
        bmh::ui::test::expect_notice_symbols(exchange.render(),load_vga(asset_path(std::filesystem::path(argv[1])/"KB","0.VGA")));
        check(exchange.timed_notice_open(),"investment warning uses Source timed mode");
        auto expired_warning=exchange;
        check(expired_warning.expire_timed_notice()&&!expired_warning.timed_notice_open()&&
              !expired_warning.pending()&&!expired_warning.close_requested(),"warning expiry leaves its caller open without a request");
        auto selected_badge=state;selected_badge.badge_sprite=91;
        BankScreen real_badge(argv[1],selected_badge);real_badge.key("B");real_badge.click(300,80);real_badge.click(70,178);
        real_badge.text("1");real_badge.key("ENTER");real_badge.reject_action("investment_limit");
        const auto badge_image=real_badge.render();const auto w11=load_vga(asset_path(std::filesystem::path(argv[1])/"W","11.VGA"));
        for(int yy=0;yy<38;++yy)for(int xx=0;xx<38;++xx)
            check(badge_image.pixels[size_t(89+yy)*320+54+xx]==w11.pixels[size_t(yy)*w11.width+3*38+xx],
                  "a real manager's badge selector91 must remain W11 tile3, not neutral art");
        exchange.key("ESC");
        check(exchange.state_json().find("\"investment_warning_open\":false") != std::string::npos &&
              exchange.state_json().find("\"exchange_open\":true") != std::string::npos,
              "warning escape dismisses the notice and keeps the exchange open");
        exchange.key("ESC"); exchange.key("B");
        check(exchange.state_json().find("\"selected_stock\":11") != std::string::npos,
              "reopening exchange restores Source default selection");
        exchange.click(205,178);
        check(!exchange.calculator_open(), "zero holdings cannot open a sale calculator");

        BankScreen sale(argv[1], state);
        sale.key("B"); sale.click(55, 64 + 2 * 8); sale.click(200, 178);
        check(sale.calculator_open() && sale.state_json().find("\"calculator_value\":5") != std::string::npos,
              "VERKAUFEN starts at the full Source-owned share count");
        request = sale.key("ENTER");
        check(request.kind == BankRequestKind::sell_shares && request.asset_index == 2 && request.count == 5,
              "share sale emits the selected raw stock and count");

        BankScreen max_buy(argv[1], state);
        max_buy.key("B"); max_buy.click(55, 80); max_buy.click(70, 178);
        max_buy.text("999");
        check(max_buy.state_json().find("\"calculator_value\":20") != std::string::npos,
              "share-count input clamps to the Engine-projected purchasable maximum");

        BankScreen tip(argv[1], state);
        tip.key("B"); tip.click(55, 64 + 2 * 8);
        const auto tip_request = tip.click(100, 164);
        check(tip_request.kind == BankRequestKind::stock_tip && tip_request.asset_index == 2 && tip_request.count == 0,
              "expert tip uses the Original caption hitbox");
        check(tip.click(100, 164).kind == BankRequestKind::none, "expert tip cannot be duplicated while pending");

        state.funds[1].price = 100000;
        state.funds[1].paid = 180000;
        state.funds[1].owned = 2;
        state.funds[1].max_to_buy = 4;
        state.funds[1].name = "REIHENHAUS";
        state.funds[1].unit_price_text = "100.000 EU.";
        state.funds[1].average_paid_text = "90.000 EU.";
        state.funds[1].quantity_text = "2.";
        state.funds[1].one_quantity_text = "1.";
        state.funds[1].singular_suffix = "";
        state.funds[1].plural_suffix = "";
        BankScreen property(argv[1], state);
        check(property.key("I").kind == BankRequestKind::none &&
              property.state_json().find("\"property_open\":true") != std::string::npos,
              "I opens the original Immobilien screen locally");
        const auto property_pixels = property.render().pixels;
        check(property_pixels == property.render().pixels &&
              property.state_json().find("\"selected_owned_property\":1") != std::string::npos,
              "property cards render repeatably with the first owned type selected");
        const auto exe = load_le(asset_path(std::filesystem::path(argv[1]), "BMH.exe"));
        const int property_x = (exe.u16(::bmh::layout::Bank_FinishCredit_RuntimeField + 2 * 2) + exe.u16(::bmh::layout::Bank_FinishCredit_RuntimeField2 + 2 * 2)) / 2;
        const int property_y = (exe.u8(::bmh::layout::Bank_FinishCredit_RuntimeField3 + 2) + exe.u8(::bmh::layout::Bank_FinishCredit_RuntimeField4 + 2)) / 2;
        BankScreen property_hotspot(argv[1], state);
        property_hotspot.click(property_x, property_y);
        check(property_hotspot.state_json().find("\"property_open\":true") != std::string::npos,
              "Original bank hotspot index 2 opens Immobilien");
        property.click(200, 130);
        check(property.state_json().find("\"selected_property_catalog\":1") != std::string::npos,
              "lower Source preview advances the catalog type only");
        property.click(80, 178);
        check(property.calculator_open() && property.state_json().find("\"calculator_value\":0") != std::string::npos,
              "property purchase opens the Source count calculator at zero");
        check(property.key("ENTER").kind == BankRequestKind::none && !property.pending(),
              "zero property count closes without a mutation request");
        property.click(80, 178); property.text("3");
        request = property.key("ENTER");
        check(request.kind == BankRequestKind::buy_fund && request.asset_index == 1 && request.count == 3 &&
              request.revision == state.revision,
              "property purchase emits selected raw type, count and revision only");
        property.reject_action("test_reject");

        BankScreen property_sale(argv[1], state);
        property_sale.key("I"); property_sale.click(200, 178);
        check(property_sale.calculator_open() && property_sale.state_json().find("\"calculator_value\":0") != std::string::npos,
              "property sale also starts its count calculator at zero");
        property_sale.text("2");
        request = property_sale.key("ENTER");
        check(request.kind == BankRequestKind::sell_fund && request.asset_index == 1 && request.count == 2,
              "property sale binds to the selected owned raw type");

        BankScreen no_property_room(argv[1], state);
        no_property_room.key("I"); no_property_room.click(80, 178);
        check(!no_property_room.calculator_open(), "zero affordable quantity does not open a property calculator");

        BankState two_owned = state;
        two_owned.funds[0].owned = 1;
        BankScreen cards(argv[1], two_owned);
        cards.key("I");
        cards.click(200, 80);
        check(cards.state_json().find("\"selected_owned_property\":1") != std::string::npos &&
              cards.state_json().find("\"selected_property_catalog\":0") != std::string::npos,
              "owned-card click advances only through held types");
        cards.click(200, 80);
        check(cards.state_json().find("\"selected_owned_property\":0") != std::string::npos,
              "owned-card selection wraps to the first held type");
        for (int i = 0; i < 6; ++i) cards.click(200, 130);
        check(cards.state_json().find("\"selected_property_catalog\":0") != std::string::npos &&
              cards.state_json().find("\"selected_owned_property\":0") != std::string::npos &&
              !cards.pending(), "catalog selection wraps across all six types without an action");

        BankState insertion = state;
        insertion.funds[0].max_to_buy = 1;
        BankScreen insert_owned(argv[1], insertion);
        insert_owned.key("I"); insert_owned.click(124, 184); insert_owned.text("1");
        check(insert_owned.key("ENTER").kind == BankRequestKind::buy_fund,
              "Source inclusive button boundary submits the flat purchase");
        insertion.revision += 1;
        insertion.funds[0].owned = 1;
        insert_owned.acknowledge(insertion);
        check(insert_owned.state_json().find("\"selected_owned_property\":0") != std::string::npos,
              "Source held-list rebuild retains position when an earlier type is inserted");

        BankScreen property_notice(argv[1], state);
        property_notice.key("I"); property_notice.click(200, 130);
        property_notice.click(80, 178); property_notice.text("1");
        request = property_notice.key("ENTER");
        BankState notice_state = state;
        notice_state.revision = state.revision + 1;
        notice_state.balance -= 105000;
        notice_state.funds[1].owned += 1;
        notice_state.funds[1].paid += 105000;
        notice_state.property_notice = BankState::PropertyNotice{1, 5000, "Renovierungskosten 5.000 EU."};
        property_notice.acknowledge(notice_state);
        check(property_notice.state_json().find("\"property_notice_open\":true") != std::string::npos &&
              property_notice.state().property_notice->additional_cost == 5000,
              "only an authoritative property ACK opens the manager notice");
        property_notice.key("ESC");
        check(property_notice.state_json().find("\"property_notice_open\":false") != std::string::npos &&
              !property_notice.state().property_notice,
              "property notice dismissal is local and creates no follow-up request");

        BankScreen credit(argv[1], state);
        const auto credit_before = credit.state_json();
        credit.click(70, 100); // EXE bank hotspot slot 4: x64..86, y96..107
        check(credit.credit_open() && credit.render().width == 320 &&
              credit.state_json().find("\"credit_open\":true") != std::string::npos,
              "Source bank hotspot 4 opens the original credit view");
        const auto credit_frame = credit.render();
        check(credit_frame.pixels == credit.render().pixels && !credit.pending() &&
              credit.state().balance == state.balance && credit.state().credit_line == state.credit_line,
              "credit view rerender and field selection are readonly");
        credit.click(10, 77); // Source row input has a y gate, no invented x gate
        check(credit.state_json().find("\"credit_selected_row\":0") != std::string::npos && !credit.pending(),
              "credit row selection uses the original row baseline without a request");
        credit.key("ESC");
        check(!credit.credit_open() && credit.state().balance == state.balance && !credit.pending(),
              "closing the credit projection cannot mutate balances or create a loan request");
        check(credit_before.find("\"revision\":8") != std::string::npos,
              "credit view starts from the same bank projection revision");

        BankScreen credit_cancel(argv[1], state);
        credit_cancel.click(70, 100);
        const auto credit_room_balance = credit_cancel.state().balance;
        const auto credit_room_revision = credit_cancel.state().revision;
        credit_cancel.click(200, 178); // Original ABBRUCH button, no request.
        check(!credit_cancel.credit_open() && !credit_cancel.pending() &&
              credit_cancel.state().balance == credit_room_balance &&
              credit_cancel.state().revision == credit_room_revision,
              "credit cancel returns to the bank room without booking or advancing revision");

        BankRequest credit_envelope;
        credit_envelope.kind = BankRequestKind::bank_credit;
        credit_envelope.manager_slot = 1; credit_envelope.runtime_club = 12; credit_envelope.revision = 19;
        credit_envelope.expected_credit = {100, 200, 300, 4, 1, 2};
        credit_envelope.credit_edits = {{0, 1000}, {3, 12}};
        credit_envelope.accept_credit_fees = true;
        const auto encoded_credit = bank_request_json(credit_envelope);
        check(encoded_credit.find("\"kind\":\"bank_credit\"") != std::string::npos &&
              encoded_credit.find("\"expected_credit\":[100,200,300,4,1,2]") != std::string::npos &&
              encoded_credit.find("\"field\":3,\"value\":12") != std::string::npos &&
              encoded_credit.find("\"accept_credit_fees\":true") != std::string::npos,
              "credit envelope preserves snapshot, ordered edits and explicit fee consent");

        // Exercise the live Source rate table and a valid, zero-debt credit context.
        GameState credit_game;
        credit_game.manager_teams = {0};
        credit_game.manager_records.assign(0xb0c, 0);
        credit_game.manager_records[0x314] = 0x20;
        credit_game.manager_records[0x315] = 0xa1; // cash = 41,000
        credit_game.memory.add_block(::bmh::layout::Loans, 0x78);
        const auto data = std::filesystem::path(argv[1]);
        const auto executable = load_le(asset_path(data, "BMH.exe"));
        const auto valid_credit = bank_credit_context(credit_game, executable, 0);
        check(valid_credit.available, "real executable supplies a valid credit rate table");
        BankState valid_credit_state = state;
        valid_credit_state.credit = valid_credit;
        BankScreen valid_credit_screen(data, valid_credit_state);
        valid_credit_screen.click(70, 100);
        valid_credit_screen.click(10, 70);
        check(valid_credit_screen.calculator_open(), "valid empty loan context opens the bounded principal calculator");
        check(valid_credit_screen.key("ENTER").kind == BankRequestKind::none &&
              valid_credit_screen.credit_open() && !valid_credit_screen.pending() &&
              valid_credit_screen.state_json().find("\"credit_edit_count\":1") != std::string::npos,
              "calculator acceptance stages a local edit without an engine request");
        const auto valid_credit_request = valid_credit_screen.click(70, 178);
        check(valid_credit_request.kind == BankRequestKind::bank_credit &&
              valid_credit_request.expected_credit == valid_credit.saved &&
              valid_credit_request.credit_edits == std::vector<BankCreditEdit>{{0, 1000}} &&
              !valid_credit_request.accept_credit_fees,
              "OK emits the source-bounded principal edit with original identity snapshot");
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); ++failures; }
    return failures ? 1 : 0;
}
