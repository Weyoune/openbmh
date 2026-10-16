#include "runtime_fixture_tables.hpp"
#include "test_inputs.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

#include "formats/savestate.hpp"
#include "formats/textd.hpp"
#include "formats/le.hpp"
#include "game/messages.hpp"
#include "game/pending_offer.hpp"
#include "game/save.hpp"
#include "ui/pending_offer.hpp"
#include "ui/stadium.hpp"

using namespace bmh;
namespace {
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

GameState retained_pending_context(const GameState& game) {
    auto national = game;
    std::copy_n(game.manager_records.begin(), 0xb0c,
                national.manager_records.begin() + 0xb0c);
    national.manager_teams = {kNoTeam};
    national.manager_records[0x290] = 0x65;
    national.manager_records[0x291] = 1;
    national.memory.set8(::bmh::layout::CoachActive, 1);
    national.memory.set8(::bmh::layout::ManagerCount, 1);
    national.memory.set8(::bmh::layout::OriginalManagerCount, 2);
    national.memory.set16(::bmh::layout::NextDeskManager, 1);
    national.memory.set8(::bmh::layout::DeskPosition, 0);
    national.memory.set8(::bmh::layout::ActiveManager, 1);
    return national;
}

int main_impl(const std::filesystem::path& data, const std::filesystem::path& save_path) {
    const auto stored = load_savegame(save_path);
    check(stored.checksum_ok, "native fixture checksum");
    GameState game = GameState::from_save(SaveState(stored.stream));
    const auto texts = load_textd(data / "TEXTD.BLK");
    game.texts = &texts;
    const auto native_before = save_stream(game);

    PendingOfferQuestion salary;
    salary.kind = PendingOfferQuestionKind::salary_demand;
    salary.manager = 0;
    salary.source_flags = 1;
    salary.original_message = "N.BROWN fragt";
    const auto salary_projection = ui::pending_offer_question_dialog(game, salary);
    std::string source_manager_name;
    const size_t name_start = 0xa8;
    for (size_t i = name_start; i < name_start + 30 && game.manager_records[i] != 0; ++i)
        source_manager_name.push_back(char(game.manager_records[i]));
    check(salary_projection.kind == ui::TaxDialogKind::question &&
          salary_projection.source_manager_flags == 0 && salary_projection.source_flags == 1 &&
          salary_projection.manager_name == source_manager_name &&
          salary_projection.body == salary.original_message,
          "salary Source question projection retains manager, flags and owned body");
    ui::GameEndTaxDialog salary_dialog(data, salary_projection);
    const auto unpressed = salary_dialog.render();
    check(unpressed.pixels == salary_dialog.render(ui::GameEndTaxDialog::Button::none).pixels,
          "default question rendering remains the legacy unpressed frame");
    // Controlled presentation boundary: format field has swapped record0/4,
    // but format field restores an offer owned by the unchanged record1.
    // No extra runtime manager, squad, money or answer is synthesized.
    {
        auto national = retained_pending_context(game);
        auto retained_question = salary;
        retained_question.manager = 1;
        const auto before = save_stream(national);
        const auto retained = ui::pending_offer_question_dialog(national, retained_question);
        check(retained.source_manager_flags == 1 &&
              retained.manager_name == salary_projection.manager_name &&
              retained.portrait == salary_projection.portrait &&
              retained.badge_sprite == salary_projection.badge_sprite &&
              retained.badge_fallback == salary_projection.badge_fallback &&
              ui::GameEndTaxDialog(data, retained).render().pixels == unpressed.pixels &&
              save_stream(national) == before && national.manager_teams == std::vector<int>{kNoTeam},
              "retained Source owner uses its actual record and identical existing question pixels without adding a runtime manager");
        const auto notice = ui::pending_offer_notice_dialog(national,
            {1, 0, ::bmh::layout::PendingOffer_FormatError_ExecutableField2, "Source agreement"});
        check(notice.source_manager_flags == 0x81 &&
              notice.manager_name == retained.manager_name && notice.portrait == retained.portrait &&
              notice.badge_sprite == retained.badge_sprite,
              "retained-owner pending acknowledgement uses the same explicit Source identity");
        for (int variant = 0; variant < 6; ++variant) {
            auto invalid = national;
            if (variant == 0) invalid.memory.set8(::bmh::layout::CoachActive, 0);
            if (variant == 1) invalid.memory.set8(::bmh::layout::OriginalManagerCount, 1);
            if (variant == 2) invalid.memory.set16(::bmh::layout::NextDeskManager, 0);
            if (variant == 3) invalid.memory.set8(::bmh::layout::ActiveManager, 0);
            if (variant == 4) invalid.memory.set8(::bmh::layout::DeskPosition, 1);
            if (variant == 5) {
                invalid.manager_records[0xb0c + 0x290] = 0x65;
                invalid.manager_records[0xb0c + 0x291] = 1;
            }
            bool rejected = false;
            try { (void)ui::pending_offer_question_dialog(invalid, retained_question); }
            catch (const FormatError&) { rejected = true; }
            check(rejected, "invalid takeover/owner/cursor/club cannot unlock a retained pending heading");
        }
        bool rejected = false;
        try { (void)ui::loan_return_question_dialog(national, 1, "unowned question"); }
        catch (const FormatError&) { rejected = true; }
        check(rejected, "retained pending identity does not unlock unrelated manager dialogs");

        // A format field buyer parked in record4 is an accounting identity,
        // whereas format field(0x84/0x44) still draws its system placeholders.
        auto buyer = national;
        const uint32_t list = ::bmh::layout::EditorList + 40 * 0x58;
        buyer.manager_records[4 * 0xb0c + 0x290] = 17;
        buyer.manager_records[4 * 0xb0c + 0x291] = 0;
        buyer.memory.set32(::bmh::layout::OfferSlots, 0x80000000u | (14u << 10) | 1u);
        buyer.squad_slots[0xb70 + 40] = 14;
        buyer.memory.set16(list, 17);
        buyer.memory.set16(list + 8, uint16_t(game.manager_teams[0]) | 0x8000);
        const PendingOfferNotice agreement{4, 0, ::bmh::layout::PendingOffer_FormatError_ExecutableField2, "Source buyer agreement"};
        const auto system = ui::pending_offer_notice_dialog(buyer, agreement);
        const auto system_image = ui::GameEndTaxDialog(data, system).render();
        check(system.source_system_identity && system.source_manager_flags == 0x84 &&
              system.portrait == 14 && system.badge_fallback && system.manager_name.empty(),
              "qualified parked buyer uses Source system identity rather than record4 heading");
        buyer.manager_records[4 * 0xb0c + 0xa8] = 'X';
        buyer.manager_records[4 * 0xb0c + 0x221] = 7;
        check(ui::GameEndTaxDialog(data, ui::pending_offer_notice_dialog(buyer, agreement)).render().pixels ==
              system_image.pixels, "system notice ignores the parked club manager's name and portrait");
        buyer.memory.set8(::bmh::layout::ActiveManager, 4);
        const auto funds = ui::pending_offer_notice_dialog(buyer, {4, 0, ::bmh::layout::Advertising_FormatError_TextPointer18, "Source buyer funds"});
        check(funds.source_system_identity && funds.source_manager_flags == 0x44,
              "temporarily active parked buyer keeps the Source timed system notice mode");
        for (int variant = 0; variant < 4; ++variant) {
            auto invalid = buyer;
            if (variant == 0) invalid.memory.set8(::bmh::layout::CoachActive, 0);
            if (variant == 1) invalid.memory.set32(::bmh::layout::OfferSlots, 0xffffffffu);
            if (variant == 2) invalid.memory.set16(list, 18);
            if (variant == 3) invalid.squad_slots[0xb70 + 40] = 15;
            bool blocked = false;
            try { (void)ui::pending_offer_notice_dialog(invalid, agreement); }
            catch (const FormatError&) { blocked = true; }
            check(blocked, "invalid takeover/offer/buyer/pool cannot unlock Source system buyer notice");
        }
        buyer.memory.set8(::bmh::layout::ActiveManager, 1);
        TransferOfferListView spare_bid;
        spare_bid.valid = true; spare_bid.list_entry = 40; spare_bid.player = 14;
        spare_bid.seller = game.manager_teams[0]; spare_bid.seller_manager = 1;
        spare_bid.offer_count = 1;
        spare_bid.offers[0].bid_index = 0; spare_bid.offers[0].present = true;
        spare_bid.offers[0].buyer = 17; spare_bid.offers[0].buyer_manager = 4;
        spare_bid.offers[0].contract_months = 24;
        const auto spare_view = ui::pending_offer_transfer_decision_from_game(buyer, spare_bid);
        check(spare_view.viewer_manager == 1 && spare_view.offers[0].buyer_manager == 4 &&
              spare_view.buyer_names[0] == buyer.club_name(17),
              "qualified parked buyer remains visible as its actual club in the retained owner's chooser");
    }
    const auto loan_return = ui::loan_return_question_dialog(game, 0, "Source return? original");
    check(loan_return.kind == ui::TaxDialogKind::question && loan_return.source_flags == 1 &&
          loan_return.source_manager_flags == 0 && loan_return.manager_name == source_manager_name &&
          loan_return.body == "Source return? original",
          "loan return projection preserves legacy routine question owner, mode and Original message");

    PendingOfferQuestion severance = salary;
    severance.kind = PendingOfferQuestionKind::severance;
    severance.source_flags = 0x0305;
    const auto severance_projection = ui::pending_offer_question_dialog(game, severance);
    const size_t legacy_yes = (::bmh::layout::GameEnd_Copy_TextPointer - ::bmh::layout::TextPointerTable) / 4;
    const size_t legacy_no = (::bmh::layout::GameEnd_Copy_TextPointer2 - ::bmh::layout::TextPointerTable) / 4;
    const size_t salary_yes = (::bmh::layout::GameEnd_Copy_TextPointer + 4 - ::bmh::layout::TextPointerTable) / 4;
    const size_t salary_no = (::bmh::layout::GameEnd_Copy_TextPointer2 - ::bmh::layout::TextPointerTable) / 4;
    const size_t severance_yes = (::bmh::layout::GameEnd_Copy_TextPointer + 5 * 4 - ::bmh::layout::TextPointerTable) / 4;
    const size_t severance_no = (::bmh::layout::GameEnd_Copy_TextPointer2 + 3 * 4 - ::bmh::layout::TextPointerTable) / 4;
    check(ui::TaxDialogProjection{}.source_flags == 0 &&
          texts[legacy_yes] == "Ja" && texts[legacy_no] == "Nein" &&
          salary_yes == 484 && salary_no == 490 &&
          severance_projection.source_flags == 0x0305 &&
          severance_yes == 488 && severance_no == 493 &&
          texts[salary_yes] == "Ja" && texts[salary_no] == "Nein" &&
          texts[severance_yes] == "Ja" && texts[severance_no] == "Nein",
          "severance uses low-byte YES and high-byte NO Source label selectors");

    const uint32_t ack_addresses[] = {::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField, ::bmh::layout::PendingOffer_FormatError_ExecutableField2, ::bmh::layout::PendingOffer_ConstructionNoticeDialog_TextPointer, ::bmh::layout::PendingOffer_FormatError_ExecutableField3};
    for (const auto address : ack_addresses) {
        PendingOfferNotice notice{0, 0, address, "Source ACK"};
        const auto projection = ui::pending_offer_notice_dialog(game, notice);
        check(projection.kind == ui::TaxDialogKind::acknowledgement &&
              projection.source_manager_flags == 0x80 && projection.source_flags == 0 &&
              projection.body == notice.original_message,
              "interactive Source notices carry the explicit ACK mode");
        ui::GameEndTaxDialog dialog(data, projection);
        check(!dialog.source_timed_notice(), "ACK notice is not timed");
    }
    const uint32_t timed_addresses[] = {::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField2, ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField3, ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField4, ::bmh::layout::PendingOffer_FormatError_ExecutableField, ::bmh::layout::Advertising_FormatError_TextPointer18,
        ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField5, ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField6, ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField7, ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField8, ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField9, ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField10};
    for (const auto address : timed_addresses) {
        PendingOfferNotice notice{0, 0, address, "Source timed notice"};
        const auto projection = ui::pending_offer_notice_dialog(game, notice);
        check(projection.source_manager_flags == 0x40 && projection.source_flags == 0 &&
              projection.body == notice.original_message,
              "timed Source notice retains timed mode, flags and body");
        ui::GameEndTaxDialog dialog(data, projection);
        check(dialog.source_timed_notice() &&
              dialog.hit_test(120, 170) == ui::GameEndTaxDialog::Button::none,
              "timed notice has no acknowledgement button");
    }
    struct LiveNoticeText { uint32_t address; const char* expected; };
    const LiveNoticeText live_notice_texts[] = {
        {::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField5, bmh::test::expected_text("pending_offer_ui_tests-external-6").c_str()},
        {::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField6, bmh::test::expected_text("pending_offer_ui_tests-external-3").c_str()},
        {::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField7, bmh::test::expected_text("pending_offer_ui_tests-external-5").c_str()},
        {::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField8, bmh::test::expected_text("pending_offer_ui_tests-external-7").c_str()},
        {::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField9, bmh::test::expected_text("pending_offer_ui_tests-external-2").c_str()},
        {::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField10, bmh::test::expected_text("pending_offer_ui_tests-external-4").c_str()},
    };
    for (const auto& expected : live_notice_texts) {
        const auto original = message_text(game, expected.address);
        check(original == expected.expected,
              "live notice address resolves its exact Source TEXTD slot");
        PendingOfferNotice notice{0, -1, expected.address, original};
        const auto projection = ui::pending_offer_notice_dialog(game, notice);
        check(projection.body == expected.expected && !projection.body.empty() &&
              projection.source_manager_flags == 0x40,
              "live notice keeps nonempty runtime TEXTD text and timed Source mode");
    }

    const auto exe = bmh::test::load_executable(data / "BMH.exe");
    ConstructionNotice sponsor;
    sponsor.manager = 0;
    sponsor.kind = ConstructionNoticeKind::SponsorContribution;
    sponsor.gross_price = 123456;
    sponsor.subsidy = 23456;
    const auto contribution = ui::construction_notice_dialog(game, exe, sponsor);
    constexpr size_t sponsor_price_fragment_slot = 1121;
    const auto& expected_sponsor_price_fragment = bmh::test::expected_text("pending_offer_ui_tests-external-1");
    check(texts[sponsor_price_fragment_slot] == expected_sponsor_price_fragment,
          "TEXTD slot 1121 is the Original German sponsor purchase-price fragment");
    const auto contribution_body = message_text(game, ::bmh::layout::PendingOffer_ConstructionNoticeDialog_TextPointer) +
        format_number(game, int32_t(sponsor.subsidy), 0x101) +
        std::string(expected_sponsor_price_fragment) +
        format_number(game, int32_t(sponsor.gross_price), 0x101) + exe.cstring(::bmh::layout::CareerSession_FormatNumber_ExecutableField);
    check(contribution.kind == ui::TaxDialogKind::acknowledgement &&
          contribution.source_manager_flags == 0x80 && contribution.source_flags == 0 &&
          contribution.body == contribution_body,
          "sponsor contribution composes Original TEXTD/EXE and 0x101 values in ISO646-DE with interactive ACK mode");
    ui::GameEndTaxDialog contribution_dialog(data, contribution);
    check(!contribution_dialog.source_timed_notice() &&
          contribution_dialog.hit_test(120, 170) == ui::GameEndTaxDialog::Button::acknowledge,
          "sponsor notice uses the existing interactive ACK button path");

    auto exploited = sponsor;
    exploited.kind = ConstructionNoticeKind::SponsorExploited;
    exploited.percent = 15;
    const auto exploited_projection = ui::construction_notice_dialog(game, exe, exploited);
    const auto exploited_body = exe.cstring(::bmh::layout::Fltd_Layout_ExecutableField3) + format_number(game, exploited.percent, 1) +
        exe.cstring(::bmh::layout::Fltd_Layout_ExecutableField4);
    check(exploited_projection.source_manager_flags == 0x80 &&
          exploited_projection.source_flags == 0 && exploited_projection.body == exploited_body,
          "exploited sponsor message uses the exact saved percentage and Original literals with interactive ACK mode");

    ConstructionNotice funds;
    funds.manager = 0;
    funds.kind = ConstructionNoticeKind::InsufficientFunds;
    const auto funds_projection = ui::construction_notice_dialog(game, exe, funds);
    check(funds_projection.kind == ui::TaxDialogKind::acknowledgement &&
          funds_projection.source_manager_flags == 0x40 && funds_projection.source_flags == 0 &&
          funds_projection.body == message_text(game, ::bmh::layout::Advertising_FormatError_TextPointer18),
          "insufficient-funds notice uses TEXTD aeb14 and timed/no-button Source mode");
    ui::GameEndTaxDialog funds_dialog(data, funds_projection);
    check(funds_dialog.source_timed_notice() &&
          funds_dialog.hit_test(120, 170) == ui::GameEndTaxDialog::Button::none,
          "funds notice has no ACK button in the shared modal presenter");

    // legacy routine -> legacy routine is a synchronous acknowledgement. Keep the
    // signed amount (including Source negative remaining weeks) and direct
    // EXE literals; this path must not inherit the tip question's two buttons.
    for (const int32_t amount : {int32_t(123456), int32_t(-23456), int32_t(0)}) {
        const SeasonTicketCompensationNotice notice{0, 1000, 21000, 15, amount};
        const auto projection = ui::season_ticket_compensation_notice_dialog(game, exe, notice);
        check(projection.kind == ui::TaxDialogKind::acknowledgement &&
              projection.source_manager_flags == 0x80 && projection.source_flags == 0 &&
              projection.manager_name == source_manager_name &&
              projection.body == exe.cstring(::bmh::layout::Fltd_Layout_ExecutableField) + format_number(game, amount, 0x101) +
                  exe.cstring(::bmh::layout::Fltd_Layout_ExecutableField2),
              "compensation retains signed Source amount, manager and exact EXE literal order");
        ui::GameEndTaxDialog dialog(data, projection);
        check(!dialog.source_timed_notice() &&
              dialog.hit_test(120, 170) == ui::GameEndTaxDialog::Button::acknowledge &&
              save_stream(game) == native_before,
              "compensation uses interactive ACK rather than tip/timed mode and is Native-pure");
    }

    const auto notice_stadium = ui::stadium_from_game(game, exe, 4);
    auto notice_state = notice_stadium;
    notice_state.opened = true;
    notice_state.selected_investment = StadiumInvestment::Floodlights;
    notice_state.construction_tip_dialog = contribution;
    ui::StadiumScreen notice_screen(data, notice_state);
    const auto stadium_before_notices = save_stream(game);
    const auto notice_backdrop = notice_screen.construction_notice_backdrop();
    const auto notice_backdrop_again = notice_screen.construction_notice_backdrop();
    const auto notice_tip = notice_screen.render();
    check(notice_backdrop.width == 320 && notice_backdrop.height == 240 &&
          notice_backdrop.pixels == notice_backdrop_again.pixels &&
          notice_backdrop.pixels != notice_tip.pixels && save_stream(game) == stadium_before_notices,
          "stadium notice backdrop is repeatable, pure and excludes the active construction-tip overlay");

    const int team = game.manager_teams.at(0);
    const size_t manager_record = 0xa8;
    std::string manager_name;
    for (size_t i = manager_record; i < manager_record + 30 && game.manager_records[i] != 0; ++i)
        manager_name.push_back(char(game.manager_records[i]));
    const bool expected_badge_fallback = team == 0x0fff || (team > 89 && team < 138) ||
        (team > 167 && team < 357);
    const int expected_badge = expected_badge_fallback ? 0 :
        (int(game.club_heap_78.at(size_t(team) * 2)) |
         int(game.club_heap_78.at(size_t(team) * 2 + 1)) << 8) & 0x1ff;
    check(contribution.manager_name == manager_name &&
          contribution.portrait == game.manager_records.at(0x221) &&
          contribution.badge_sprite == expected_badge &&
          contribution.badge_fallback == expected_badge_fallback,
          "construction notice reuses the current Source manager heading, portrait and badge projection");

    TransferOfferListView source;
    source.valid = true;
    source.list_entry = 40;
    source.player = 14;
    source.seller = 0;
    source.seller_manager = 0;
    source.offer_count = 1;
    source.offers[0].bid_index = 0;
    source.offers[0].present = true;
    source.offers[0].buyer = 1;
    source.offers[0].buyer_manager = -1;
    source.offers[0].contract_months = 12;
    source.offers[0].loan = true;
    source.offers[0].eligible = false;
    source.offers[0].loan_permitted = false;
    source.offers[0].transaction_supported = false;
    source.notice_text_address = ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField2;
    const auto pending = ui::pending_offer_transfer_decision_from_game(game, source);
    check(pending.pending_offer_source && pending.list_entry == 40 && pending.offers[0].present &&
          pending.offers[0].bid_index == 0 && !pending.offers[0].transaction_supported &&
          pending.notice_text_address == 0 && pending.notice_text.empty(),
          "special Source projection preserves raw bid0 and its unsupported lifecycle marker");
    {
        const auto national = retained_pending_context(game);
        auto retained_source = source;
        retained_source.seller_manager = 1;
        const auto retained_view = ui::pending_offer_transfer_decision_from_game(national, retained_source);
        check(retained_view.viewer_manager == 1 && retained_view.viewer_name == pending.viewer_name &&
              retained_view.badge_sprite == pending.badge_sprite &&
              ui::IncomingTransferScreen(data, retained_view).render().pixels ==
                  ui::IncomingTransferScreen(data, pending).render().pixels,
              "retained Source owner can see the existing chooser with identical pixels and its own record identity");
        ui::IncomingTransferDecisionView unrelated;
        unrelated.valid = true; unrelated.list_entry = 0; unrelated.player = source.player;
        unrelated.seller = source.seller; unrelated.viewer_manager = 1;
        bool rejected = false;
        try { (void)ui::incoming_transfer_decision_from_game(national, unrelated); }
        catch (const FormatError&) { rejected = true; }
        check(rejected, "retained pending owner does not unlock unrelated ordinary transfer views");
    }
    ui::IncomingTransferScreen chooser(data, pending);
    check(chooser.click(20, 0x8f) == std::optional<int>{0},
          "special Source route can stage raw bid0 despite generic transaction eligibility metadata");

    auto invalid = source;
    invalid.offers[1].present = true;
    invalid.offers[1].bid_index = 1;
    bool rejected_second_bid = false;
    try { (void)ui::pending_offer_transfer_decision_from_game(game, invalid); }
    catch (const FormatError&) { rejected_second_bid = true; }
    check(rejected_second_bid, "special Source route rejects anything beyond sole raw bid0");

    ui::IncomingTransferDecisionView ordinary;
    ordinary.valid = true;
    ordinary.list_entry = 39;
    ordinary.player = 14;
    ordinary.seller = 0;
    ordinary.seller_manager = 0;
    ordinary.viewer_manager = 0;
    const auto regular = ui::incoming_transfer_decision_from_game(game, ordinary);
    check(!regular.pending_offer_source && regular.list_entry == 39,
          "ordinary transfer-list provenance and 0..39 entry range are unchanged");
    ordinary.list_entry = 40;
    bool ordinary_rejected_40 = false;
    try { (void)ui::incoming_transfer_decision_from_game(game, ordinary); }
    catch (const FormatError&) { ordinary_rejected_40 = true; }
    check(ordinary_rejected_40, "ordinary transfer-list factory rejects special entries");

    check(save_stream(game) == native_before,
          "pure UI projection leaves every serialized native field unchanged");
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("usage: pending_offer_ui_tests ORIGINAL_DATA QA_INPUT.FBM");
        if (!*argv[1]) {
            std::puts("pending offer UI tests: skipped (Original data directory not configured)");
            return 0;
        }
        return main_impl(argv[1], argv[2]);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "pending offer UI tests: %s\n", e.what());
        return 1;
    }
}
