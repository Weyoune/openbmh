// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "app/window_actions.hpp"
#include "game/save.hpp"

namespace {
using namespace bmh;
namespace fs = std::filesystem;
constexpr uint32_t kOffer = ::bmh::layout::EditorList + 4 * 0x58;
void require(bool ok, const std::string& reason) {
    if (!ok) throw std::runtime_error(reason);
}
struct TemporaryDirectory {
    fs::path path;
    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("openbmh-offer-class-" + std::to_string(stamp));
        require(fs::create_directory(path), "create isolated save directory");
    }
    ~TemporaryDirectory() { std::error_code ignored; fs::remove_all(path, ignored); }
};

void change_class(app::CareerSession& session, const fs::path& data, bool loan, uint8_t previous_term) {
    uint64_t revision = 1;
    app::TransferActions actions(session);
    const auto market = ui::transfer_market_from_game(session.state(), revision);
    const auto selected = std::find_if(market.items.begin(), market.items.end(),
        [](const auto& item) { return item.player == 118; });
    require(selected != market.items.end() && selected->list_entry == 4 && selected->own_bid,
            "normal input retains BURKARDT's actual existing own bid");
    std::string error;
    auto opened = actions.open_offer(app::TransferActions::opening(market, *selected, loan), revision, error);
    require(opened.has_value(), "normal opposite market action opens editor: " + error);
    require(session.transfer_offer_owner()->existing &&
            session.transfer_offer_owner()->term == previous_term &&
            opened->editor.initial_action_loan == loan && opened->editor.quote_loan == loan,
            "reopening alone retains the existing offer's Native loan class");
    ui::OutgoingTransferScreen screen(data, opened->editor);
    unsigned notices = 0;
    HumanBuyerSaleHooks hooks;
    hooks.notice = [&](const GameState&, const HumanBuyerSaleNotice&) { ++notices; };
    const auto apply = [&](const ui::OutgoingTransferRequest& request) {
        auto result = actions.offer(request, hooks, revision, error);
        require(result.has_value(), "normal editor event kind=" + std::to_string(unsigned(request.kind)) +
            " field=" + std::to_string(unsigned(request.field)) + " revision=" +
            std::to_string(request.identity.revision) + "/" + std::to_string(revision) + ": " + error);
        if (!result->editor.closed) screen.acknowledge(result->editor);
        return *result;
    };

    // legacy routine local38 is the quote flag, while param2 is the retained market action.
    // The actual bit write occurs at code_rdata-field offset, on accepted years input.
    const auto before_toggle = save_stream(session.state());
    apply(screen.click(50, 0x13, false, true));
    require(save_stream(session.state()) == before_toggle &&
            session.transfer_offer_owner()->quote_loan != loan &&
            session.transfer_offer_owner()->request.loan == loan &&
            screen.state().initial_action_loan == loan,
            "title double-click changes only the quote, independently of the market action");

    (void)screen.click(275, 140);
    require(screen.calculator_open(), "normal ModeIII years rectangle opens calculator");
    const auto before_cancel = save_stream(session.state());
    const auto canceled_years = screen.key("ESC");
    require(canceled_years.kind == ui::OutgoingTransferRequestKind::none &&
            save_stream(session.state()) == before_cancel &&
            session.state().memory.u8(kOffer + 0x34) == previous_term,
            "canceling years preserves the existing class");

    (void)screen.click(275, 140);
    require(screen.text("1"), "actual years calculator accepts one year");
    // This copy is only the comparison oracle, never the running session or input FBM.
    auto expected = session.state();
    const uint8_t target = uint8_t(12 | (loan ? 0x80 : 0));
    expected.memory.set8(kOffer + 0x34, target);
    apply(screen.key("ENTER"));
    require(save_stream(session.state()) == save_stream(expected) &&
            session.transfer_offer_owner()->term == target &&
            session.transfer_offer_owner()->request.loan == loan &&
            session.transfer_offer_owner()->quote_loan != loan,
            "accepted Source years event changes exactly the existing term byte, independently of quote");

    const auto native = save_stream(session.state());
    const auto wc = session.rand().calls(), gc = session.game().calls();
    const auto submitted = apply(screen.key("ENTER"));
    require(submitted.action.status == TransferActionStatus::pending &&
            !submitted.editor.closed && notices == 0 &&
            save_stream(session.state()) == native &&
            session.rand().calls() == wc && session.game().calls() == gc,
            "existing bid submit keeps its slot without requalification, settlement or RNG draws");
    const auto closed = apply(screen.key("ESC"));
    require(closed.editor.closed && !session.transfer_offer_owner() &&
            save_stream(session.state()) == native,
            "normal editor close retains the accepted class change");
    std::cout << "BURKARDT118 existing bid: " << unsigned(previous_term) << " -> "
              << unsigned(target) << "; quote independent, submit pending, exact Native/W/G\n";
}
}

int main(int argc, char** argv) try {
    if (argc != 3) return 2;
    if (!*argv[1]) { std::cout << "SKIP: BMH_DATA not configured\n"; return 77; }
    const fs::path data = argv[1];
    const TemporaryDirectory temporary;
    auto session = std::make_unique<app::CareerSession>(data, 8128, argv[2]);
    require(session->state().date == 0 && session->state().season == 0 &&
            session->state().memory.u8(::bmh::layout::GameMode) == 3 && session->state().memory.u8(::bmh::layout::ActiveManager) == 1 &&
            session->state().memory.u16(kOffer) == 54 && session->state().memory.u8(kOffer + 0x34) == 140 &&
            session->state().memory.u32(kOffer + 0x1c) == 1500000 &&
            session->state().memory.u32(kOffer + 0x0c) == 100000,
            "unchanged normal M140 OFFER input");
    change_class(*session, data, false, 140);
    std::string error;
    require(session->save_game(temporary.path / "PURCHASE.FBM", 1, false, error), error);
    const auto purchase = save_stream(session->state());
    session = std::make_unique<app::CareerSession>(data, 8128, temporary.path / "PURCHASE.FBM");
    require(save_stream(session->state()) == purchase, "fresh normal load retains the purchase offer");
    change_class(*session, data, true, 12);
    require(session->save_game(temporary.path / "LOAN.FBM", 1, false, error), error);
    const auto loan = save_stream(session->state());
    session = std::make_unique<app::CareerSession>(data, 8128, temporary.path / "LOAN.FBM");
    require(save_stream(session->state()) == loan && session->state().memory.u8(kOffer + 0x34) == 140,
            "fresh normal load retains the restored loan offer");
    return 0;
} catch (const std::exception& error) {
    std::cerr << "transfer_offer_class_app_tests: " << error.what() << '\n';
    return 1;
}
