#include "../../tests/test_inputs.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/outgoing_transfer.hpp"

#include <cstdio>
#include <string>
#include <utility>

using namespace bmh::ui;

namespace {
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

OutgoingTransferCalculatorProfile profile(uint32_t minimum, uint32_t maximum, uint32_t initial,
                                           bool currency = false) {
    OutgoingTransferCalculatorProfile result;
    result.supported = true;
    result.currency = currency;
    result.minimum = minimum;
    result.maximum = maximum;
    result.initial = initial;
    return result;
}

OutgoingTransferEditorState fixture() {
    OutgoingTransferEditorState state;
    auto& panel = state.panel;
    panel.source_outgoing_editor_context = true;
    panel.list_entry = 5;
    panel.player = 7;
    panel.seller = 54;
    panel.seller_manager = 1;
    panel.viewer_manager = 0;
    panel.player_name = "Player";
    panel.seller_name = "Seller";
    panel.negotiator_name = "Seller";
    panel.age = 24;
    panel.position = 2;
    panel.strength = 81;
    panel.asking_fee = 900000;
    panel.asking_monthly_salary = 42000;
    panel.quoted_fee = 850000;
    panel.quoted_monthly_salary = 40000;
    panel.quoted_years = 2;
    panel.offers[2].bid_index = 2;
    panel.offers[2].buyer = 1;
    panel.offers[2].buyer_manager = 0;
    panel.offers[2].fee = 750000;
    panel.offers[2].monthly_salary = 38000;
    panel.offers[2].contract_months = 24;
    panel.offers[2].bonuses = {1,2,3,4,5,6,7};
    panel.offers[2].present = true;
    panel.offers[2].eligible = true;
    panel.buyer_names[2] = "Buyer";

    state.manager_slot = 0;
    state.runtime_club = 1;
    state.revision = 12;
    state.list_entry = 5;
    state.source_slot = 2933;
    state.player = 7;
    state.seller = 54;
    state.bid_slot = 2;
    state.source_mode = 3;
    state.editable = true;
    state.fee = 750000;
    state.monthly_salary = 38000;
    state.duration_years = 2;
    state.bonuses = {1,2,3,4,5,6,7};
    state.money_calculators[0] = profile(0, 5000000, 750000, true);
    state.money_calculators[1] = profile(0, 1000000, 38000, true);
    for (auto& p : state.money_calculators) {
        for (size_t i = 0; i < p.presets.size(); ++i)
            p.presets[i] = uint32_t((i + 1) * 1000);
    }
    for (size_t i = 0; i < state.mode3_calculators.size(); ++i) {
        const uint32_t max = i == 0 ? 4u : (i <= 3 ? 20u : 1000u);
        state.mode3_calculators[i] = profile(i == 0 ? 1u : 0u, max,
            i == 0 ? 2u : uint32_t(state.bonuses[i - 1]), false);
    }
    return state;
}
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    try {
        const auto texts = bmh::load_textd(std::filesystem::path(argv[1]) / "TEXTD.BLK");
        check(texts[227] == bmh::test::expected_text("outgoing_transfer_tests-external-3").c_str() && texts[228] == bmh::test::expected_text("outgoing_transfer_tests-external-2").c_str() &&
              texts[229] == bmh::test::expected_text("outgoing_transfer_tests-external-1").c_str(),
              "Source editor title slots distinguish purchase, loan, and incoming decision captions");
        check(texts[230] == bmh::test::expected_text("outgoing_transfer_tests-external-5").c_str() && texts[231] == bmh::test::expected_text("outgoing_transfer_tests-external-4").c_str() &&
              texts[232] == "Angebot" && texts[233] == "Abbruch",
              "Source footer pointer slots ae198..ae1a4 resolve to the four original TEXTD labels");
        auto state = fixture();
        OutgoingTransferScreen screen(argv[1], state);
        const auto initial = screen.render();
        // Original legacy routine -> legacy routine: the raised footer casts a two-pixel
        // shadow, including below the inclusive y=225 hit rectangle.
        const auto pixel = [&](int x, int y) { return initial.pixels[size_t(y) * 320 + size_t(x)]; };
        for (int x : {1, 80, 156, 232})
            check(pixel(x + 2, 225) == 0x40 && pixel(x + 74, 226) == 0x40 &&
                  pixel(x + 75, 217) == 0x40 && pixel(x + 75, 224) == 0x40,
                  "Original outgoing footer keeps its bottom and right shadows");
        check(screen.state().panel.title == IncomingTransferTitle::player_purchase,
              "ordinary outgoing action uses Source TEXTD 227 regardless of the current bid term");
        auto incoming_panel = state.panel;
        incoming_panel.title = IncomingTransferTitle::player_purchase;
        IncomingTransferScreen asking_reference(argv[1], incoming_panel);
        const auto asking_frame = asking_reference.render();
        bool asking_row_preserved = true;
        for (int y = 119; y <= 123; ++y) for (int x = 0; x < 320; ++x)
            asking_row_preserved &= initial.pixels[size_t(y) * 320 + size_t(x)] ==
                                    asking_frame.pixels[size_t(y) * 320 + size_t(x)];
        bool offer_row_added = false;
        for (int y = 127; y <= 136; ++y) for (int x = 0; x < 320; ++x)
            offer_row_added |= initial.pixels[size_t(y) * 320 + size_t(x)] !=
                               asking_frame.pixels[size_t(y) * 320 + size_t(x)];
        check(asking_row_preserved && offer_row_added,
              "asking line remains at baseline 123 while the selected offer is rendered separately at baseline 130");
        check(initial.width == 320 && initial.height == 240 && initial.pixels.size() == 320u * 240u,
              "editor composes the original 320x240 KB129/KB31 panel");

        OutgoingTransferScreen bare_input(argv[1], state);
        {
            OutgoingTransferScreen bonus_timer(argv[1],state);
            bonus_timer.click(210,0x94);
            check(bonus_timer.calculator_open(),"nonmoney timer fixture opens the Source bonus calculator");
            check(bonus_timer.process_timer_expired().kind==OutgoingTransferRequestKind::none &&
                  !bonus_timer.calculator_open() && !bonus_timer.pending() &&
                  bonus_timer.state().bonuses==state.bonuses,
                  "event8 restores a cancelled nonmoney calculator without a fabricated quote command");
            OutgoingTransferScreen timed(argv[1],state);
            timed.click(40,123);timed.text("900000");
            const auto expired=timed.process_timer_expired();
            check(expired.kind==OutgoingTransferRequestKind::quote_change && !expired.accepted &&
                  expired.field==OutgoingTransferField::fee && expired.value==state.fee &&
                  expired.identity.player==state.player && expired.identity.bid_slot==state.bid_slot,
                  "event8 cancels unconfirmed calculator digits through the original owned quote request");
            const auto owned=timed.state_json();
            check(timed.process_timer_expired().kind==OutgoingTransferRequestKind::none &&
                  timed.state_json()==owned,"expired polls cannot replace a pending calculator cancel");
            auto returned=state;++returned.revision;timed.acknowledge(returned);
            const auto close=timed.process_timer_expired();
            check(close.kind==OutgoingTransferRequestKind::cancel &&
                  close.identity.revision==returned.revision && close.fee==state.fee &&
                  close.monthly_salary==state.monthly_salary && close.bonuses==state.bonuses &&
                  timed.process_timer_expired().kind==OutgoingTransferRequestKind::none,
                  "next event8 closes the acknowledged editor once without submitting an offer");
        }
        check(!bare_input.text("12345"), "Source KB31 ignores text outside a clicked calculator");
        for (const auto key : {"7", "C", "SPACE", "D", "BACKSPACE"})
            check(bare_input.key(key).kind == OutgoingTransferRequestKind::none,
                  "calculator keys outside KB6 cannot create an editor action");
        check(!bare_input.pending() && bare_input.render().pixels == initial.pixels &&
              bare_input.state().fee == state.fee,
              "bare keyboard input leaves KB31 and its Native posted row unchanged");
        const auto bare_submit = bare_input.key("ENTER");
        check(bare_submit.kind == OutgoingTransferRequestKind::submit && bare_submit.fee == state.fee &&
              bare_submit.monthly_salary == state.monthly_salary && bare_submit.bonuses == state.bonuses,
              "ENTER outside KB6 submits the confirmed row without an unintended fee-zero edit");

        auto selected = screen.click(40, 123);
        check(selected.kind == OutgoingTransferRequestKind::none &&
              screen.calculator_open(),
              "fee click only opens the owned row's source KB6 calculator");
        const auto calculator = screen.render();
        check(calculator.pixels != initial.pixels, "KB6 calculator is an asset-backed overlay, not a custom form");
        check(screen.text("900000"), "calculator accepts typed decimal fee input");
        auto fee = screen.key("ENTER");
        check(fee.kind == OutgoingTransferRequestKind::quote_change &&
              fee.field == OutgoingTransferField::fee && fee.value == 900000 && fee.fee == 900000 &&
              fee.monthly_salary == 38000 && fee.duration_years == 2 && fee.bonuses == state.bonuses &&
              fee.quoted_fee == 850000 && fee.quoted_monthly_salary == 40000 &&
              fee.identity.manager_slot == 0 && fee.identity.runtime_club == 1 &&
              fee.identity.list_entry == 5 && fee.identity.player == 7 && fee.identity.seller == 54,
              "quote request carries owner identity, quote context and complete posted row");
        check(screen.pending().has_value(), "owner response is required before another edit");
        auto accepted_state = state;
        accepted_state.fee = 900000;
        accepted_state.panel.quoted_fee = 875000;
        accepted_state.panel.offers[2].fee = 900000;
        ++accepted_state.revision;
        screen.acknowledge(accepted_state);
        check(!screen.pending() && screen.state().revision == 13 && screen.state().panel.quoted_fee == 875000,
              "owner-provided quote projection resumes the editor at its new revision");

        auto duration = screen.click(210, 0x89);
        check(duration.kind == OutgoingTransferRequestKind::none && screen.calculator_open(),
              "mode-3 duration uses its owner-supplied Source KB6 profile");
        check(screen.text("3"), "mode-3 duration accepts typed input");
        auto term = screen.key("ENTER");
        check(term.kind == OutgoingTransferRequestKind::quote_change &&
              term.field == OutgoingTransferField::duration_years && term.value == 3,
              "duration change requests an owner quote refresh");
        auto next = accepted_state;
        next.duration_years = 3;
        next.panel.offers[2].contract_months = 36;
        screen.acknowledge(next);

        (void)screen.click(210, 0x92);
        check(screen.text("9"), "mode-3 bonus field accepts decimal edits");
        auto bonus = screen.key("ENTER");
        check(bonus.kind == OutgoingTransferRequestKind::quote_change &&
              bonus.field == OutgoingTransferField::bonus_0 && bonus.value == 9 && bonus.bonuses[0] == 9,
              "bonus edit carries the full row and requests the Source quote refresh");
        next.bonuses[0] = 9;
        next.panel.offers[2].bonuses[0] = 9;
        screen.acknowledge(next);
        auto submitted = screen.key("ENTER");
        check(submitted.kind == OutgoingTransferRequestKind::submit && submitted.identity.bid_slot == 2 &&
              submitted.fee == 900000 && submitted.duration_years == 3 && submitted.bonuses[0] == 9,
              "submit request carries the complete active raw bid row");

        auto edge = fixture();
        OutgoingTransferScreen edge_screen(argv[1], edge);
        check(edge_screen.click(25, 122).kind == OutgoingTransferRequestKind::none,
              "opening a money calculator never dispatches an edit to Core");
        auto rejected = edge_screen.key("ESC");
        check(rejected.kind == OutgoingTransferRequestKind::quote_change && !rejected.accepted &&
              rejected.field == OutgoingTransferField::fee && rejected.value == edge.fee,
              "Source fee calculator cancellation returns an explicit rejected quote request");

        auto scale = fixture();
        OutgoingTransferScreen scale_screen(argv[1], scale);
        check(scale_screen.click(210, 0x89 + 4 * 9).kind == OutgoingTransferRequestKind::none &&
              scale_screen.calculator_open(), "last-four bonus opens its KB6 calculator");
        check(scale_screen.text("1000"), "last-four bonus accepts the calculator result in 0..1000 units");
        auto bonus_scale = scale_screen.key("ENTER");
        check(bonus_scale.kind == OutgoingTransferRequestKind::quote_change &&
              bonus_scale.value == 1000 && bonus_scale.bonuses[3] == 200,
              "request preserves the KB6 value while local posted row projects value divided by five");

        auto quote = fixture();
        OutgoingTransferScreen quote_screen(argv[1], quote);
        auto toggle = quote_screen.click(50, 0x13, false, true);
        check(toggle.kind == OutgoingTransferRequestKind::quote_change &&
              toggle.field == OutgoingTransferField::quote_loan && toggle.quote_loan &&
              quote_screen.state().panel.title == IncomingTransferTitle::player_purchase,
              "Source header double-click toggles the local quote/loan projection only");

        auto original_loan = fixture();
        original_loan.initial_action_loan = true;
        OutgoingTransferScreen loan_title(argv[1], original_loan);
        const auto loan_title_frame = loan_title.render();
        bool loan_caption_differs = false;
        for (int y = 13; y < 23; ++y) for (int x = 70; x < 250; ++x)
            loan_caption_differs |= loan_title_frame.pixels[size_t(y) * 320 + size_t(x)] !=
                                    initial.pixels[size_t(y) * 320 + size_t(x)];
        check(loan_title.state().panel.title == IncomingTransferTitle::player_loan && loan_caption_differs,
              "original loan action selects TEXTD 228 independently of quote toggle and Native term");
        auto immutable_action = fixture();
        OutgoingTransferScreen immutable_title(argv[1], immutable_action);
        auto wrong_action_ack = immutable_action;
        wrong_action_ack.initial_action_loan = true;
        bool changed_action_rejected = false;
        try { immutable_title.acknowledge(wrong_action_ack); }
        catch (const bmh::FormatError&) { changed_action_rejected = true; }
        check(changed_action_rejected &&
              immutable_title.state().panel.title == IncomingTransferTitle::player_purchase,
              "owner acknowledgement cannot change the original Source action caption mid-editor");

        auto team_info_state = fixture();
        OutgoingTransferScreen team_info(argv[1], team_info_state);
        auto team_info_request = team_info.click(1, 215);
        check(team_info_request.kind == OutgoingTransferRequestKind::team_info &&
              team_info_request.identity.player == 7 && team_info_request.identity.seller == 54,
              "leftmost Original footer button is typed Team-Info navigation, not cancel");
        auto player_info_state = fixture();
        OutgoingTransferScreen player_info(argv[1], player_info_state);
        check(player_info.click(80, 215).kind == OutgoingTransferRequestKind::player_info,
              "second Original footer button requests Player-Info navigation");
        auto footer_submit_state = fixture();
        OutgoingTransferScreen footer_submit(argv[1], footer_submit_state);
        auto footer_submit_request = footer_submit.click(156, 215);
        check(footer_submit_request.kind == OutgoingTransferRequestKind::submit &&
              footer_submit_request.identity.bid_slot == 2 && footer_submit.state_json().find("\"closed\":true") != std::string::npos,
              "third Original footer button submits the selected raw bid row");
        auto footer_cancel_state = fixture();
        OutgoingTransferScreen footer_cancel(argv[1], footer_cancel_state);
        auto footer_cancel_request = footer_cancel.click(307, 225);
        check(footer_cancel_request.kind == OutgoingTransferRequestKind::cancel &&
              footer_cancel.state_json().find("\"closed\":true") != std::string::npos,
              "rightmost Original footer button closes the editor without submitting");

        auto mode_two = fixture();
        mode_two.source_mode = 2;
        OutgoingTransferScreen masked(argv[1], mode_two);
        check(masked.click(210, 0x89).kind == OutgoingTransferRequestKind::none,
              "mode 1/2 masks the mode-3 duration and bonus strip");
        auto canceled = masked.key("ESC");
        check(canceled.kind == OutgoingTransferRequestKind::cancel && canceled.identity.bid_slot == 2,
              "cancel is typed and retains the raw row identity");

        auto new_row = fixture();
        new_row.panel.offers[2] = {};
        new_row.panel.offers[2].bid_index = 2;
        new_row.panel.offers[2].bonuses = {8,7,6,5,4,3,2};
        new_row.bid_slot = 2;
        new_row.editable = true;
        new_row.fee = 0;
        new_row.monthly_salary = 0;
        new_row.duration_years = 2;
        new_row.bonuses = new_row.panel.offers[2].bonuses;
        OutgoingTransferScreen fresh(argv[1], new_row);
        check(fresh.state().fee == 0 && fresh.state().monthly_salary == 0 &&
              fresh.state().duration_years == 2 && fresh.state().bonuses == new_row.panel.offers[2].bonuses,
              "new empty row starts at Source fee/salary 0 and 24 months while preserving all seven raw bonus bytes");

        auto full = fixture();
        full.editable = false;
        full.bid_slot = -1;
        OutgoingTransferScreen readonly(argv[1], full);
        check(readonly.click(40, 123).kind == OutgoingTransferRequestKind::none &&
              !readonly.calculator_open() && readonly.key("ENTER").kind == OutgoingTransferRequestKind::none,
              "noneditable/full context cannot open the editor or submit a fabricated row");

        const auto json = outgoing_transfer_request_json(fee);
        check(json.find("\"source_slot\":2933") != std::string::npos &&
              json.find("\"quoted_fee\":850000") != std::string::npos &&
              json.find("\"bonuses\":[1,2,3,4,5,6,7]") != std::string::npos,
              "typed requests serialize identity, quote context and all bonuses");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "outgoing transfer UI test exception: %s\n", e.what());
        return 2;
    }
    if (failures) std::fprintf(stderr, "%d outgoing transfer UI test(s) failed\n", failures);
    return failures ? 1 : 0;
}
