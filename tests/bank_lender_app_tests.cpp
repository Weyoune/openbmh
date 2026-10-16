// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <limits>
#include <stdexcept>

#include "app/controller.hpp"
#include "game/bank_credit.hpp"
#include "game/save.hpp"

using namespace bmh;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

struct TemporarySaves {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("openbmh-lender-app-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporarySaves() { check(std::filesystem::create_directory(path), "isolated lender fixture directory"); }
    ~TemporarySaves() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};

void write32(GameState& g, int slot, size_t field, uint32_t value) {
    for (unsigned b = 0; b < 4; ++b)
        g.manager_records.at(size_t(slot) * 0xb0c + field + b) = uint8_t(value >> (8 * b));
}
int32_t cash(const GameState& g, int slot) {
    const size_t at = size_t(slot) * 0xb0c + 0x314;
    return int32_t(uint32_t(g.manager_records.at(at)) | uint32_t(g.manager_records.at(at + 1)) << 8 |
        uint32_t(g.manager_records.at(at + 2)) << 16 | uint32_t(g.manager_records.at(at + 3)) << 24);
}

struct Snapshot {
    Bytes native;
    WatcomRandom w;
    GameRandom g;
    uint64_t revision;
    Snapshot(app::CareerSession& session, uint64_t rev)
        : native(save_stream(session.state())), w(session.rand()), g(session.game()), revision(rev) {}
    void unchanged(app::CareerSession& session, uint64_t rev) const {
        check(save_stream(session.state()) == native && revision == rev &&
            session.rand().seed() == w.seed() && session.rand().calls() == w.calls() &&
            session.game().seed() == g.seed() && session.game().calls() == g.calls(),
            "rejection, cancellation and replay preserve whole Native state, revision and both RNGs");
    }
};

struct Context {
    std::filesystem::path data;
    TemporarySaves saves;
    ui::StartupFlow flow;
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision = 0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio;
    LeImage exe;
    int borrower = -1, lender = -1, counterparty = -1;

    Context(const std::filesystem::path& d, bool existing) : data(d), flow(d, saves.path),
        exe(load_le(ui::asset_path(d, "BMH.exe"))) {
        app::CareerSession fixture(data, 0xe3100001);
        ui::SetupOptions options;
        options.managers = 2;
        std::string error;
        std::vector<int> chosen;
        for (int slot = 0; slot < 2; ++slot) {
            const auto selection = fixture.selection_context(uint64_t(slot + 1));
            const auto club = std::find_if(selection.club_order.begin(), selection.club_order.end(),
                [&](int id) { return std::find(chosen.begin(), chosen.end(), id) == chosen.end(); });
            check(club != selection.club_order.end(), "distinct real clubs for two human managers");
            ui::ManagerChoice choice;
            choice.slot = slot;
            choice.name = slot == 0 ? "Kreditnehmer" : "Geldgeber";
            choice.master_club = *club;
            choice.start_league = 0;
            choice.strength = 2;
            choice.portrait = slot;
            choice.selection_club = int(club - selection.club_order.begin());
            choice.roster_revision = selection.revision;
            check(fixture.confirm_manager(options, choice, error), "two-manager career confirmation");
            chosen.push_back(*club);
        }
        ui::CareerInitializationResult initialized;
        check(fixture.finish(initialized, error), "two-manager career reaches its real first desk");
        borrower = int(fixture.state().memory.u8(::bmh::layout::ActiveManager));
        lender = 1 - borrower;
        counterparty = lender + 1;
        for (int slot = 0; slot < 2; ++slot) {
            write32(fixture.state(), slot, 0x314, 20000000);
            write32(fixture.state(), slot, 0x31c, 100000);
        }
        if (existing) {
            auto credit = bank_credit_context(fixture.state(), exe, borrower, counterparty);
            check(commit_bank_credit(fixture.state(), exe, borrower, credit.saved,
                {{0, 1000000}, {3, 12}}, false, counterparty).status == BankCreditStatus::accepted,
                "controlled outstanding loan is created by the inherited credit engine");
            credit = bank_credit_context(fixture.state(), exe, borrower, counterparty);
            const uint32_t row = ::bmh::layout::Loans + uint32_t(borrower) * 0x78 + uint32_t(counterparty) * 0x18;
            fixture.state().memory.set32(row + 16, 6);
            fixture.state().memory.set32(row + 20, credit.saved[2] * 6);
        }
        auto save_rng = fixture.rand();
        write_savegame(saves.path / "AUTOSAVE.FBM", make_savegame(fixture.state(), save_rng));
        playback.set_muted(true);
        flow.click(214, 215);
        const auto loading = flow.send_key("ENTER");
        check(loading.request == ui::FlowRequest::file_action, "real Flow requests Native fixture load");
        serve(loading);
        check(session && revision == 1 && flow.screen_name() == "desktop" &&
            session->state().memory.u8(::bmh::layout::ManagerCount) == 2, "actual controller loads the controlled two-manager save");
        serve(flow.send_key(std::string(1, char(exe.u8(::bmh::layout::Desktop_Key_RuntimeField + 14)))));
        check(flow.screen_name() == "bank", "original desk shortcut opens the lender-capable BankScreen");
    }
    void serve(const ui::FlowResult& action) {
        check(app::serve_flow_request(flow, action, session, data, saves.path, 0xe3100001,
            revision, playback, audio, false).empty(), "bank lender action uses the production controller");
    }
    void open_lender() { serve(flow.click(exe.u8(::bmh::layout::Bank_FinishCredit_RuntimeField5 + 5), exe.u8(::bmh::layout::Bank_FinishCredit_RuntimeField6 + 5))); }
    ui::FlowResult request(bool refinance) {
        open_lender();
        serve(flow.click(150, refinance ? 100 : 74));
        serve(flow.send_key("CLEAR"));
        check(flow.text(refinance ? "1" : "100000"), "credit calculator accepts the explicit input");
        serve(flow.send_key("ENTER"));
        auto action = flow.click(80, 178);
        check(action.request == ui::FlowRequest::submenu_action && flow.pending_bank_action(),
            "case5 submits directly without a bank fee question");
        const auto& payload = std::get<ui::BankRequest>(action.submenu_action);
        check(payload.credit_counterparty == counterparty && !payload.accept_credit_fees,
            "owned request identifies the selected manager lender and does not accept bank fees");
        return action;
    }
};

void test_new_loan(const std::filesystem::path& data) {
    Context c(data, false);
    Snapshot before(*c.session, c.revision);
    c.open_lender();
    c.serve(c.flow.click(150, 74)); c.flow.text("100000"); c.serve(c.flow.send_key("ENTER"));
    c.serve(c.flow.click(210, 178));
    before.unchanged(*c.session, c.revision);
    check(!c.flow.pending_bank_action(), "cancelled manager loan has no pending booking");
    auto action = c.request(false);
    auto altered = action;
    std::get<ui::BankRequest>(altered.submenu_action).credit_counterparty = 0;
    c.serve(altered);
    before.unchanged(*c.session, c.revision);
    check(!c.flow.pending_bank_action(), "forged bank-versus-lender payload clears the rejected envelope");
    action = c.flow.click(80, 178);
    check(c.flow.pending_bank_action(), "rejected envelope retains the lender draft for an explicit retry");
    auto expected = c.session->state();
    const auto& request = std::get<ui::BankRequest>(action.submenu_action);
    check(commit_bank_credit(expected, c.exe, c.borrower, request.expected_credit, request.credit_edits,
        false, c.counterparty).status == BankCreditStatus::accepted, "inherited Core supplies the integration expectation");
    const int32_t borrower_before = cash(c.session->state(), c.borrower), lender_before = cash(c.session->state(), c.lender);
    c.serve(action);
    check(save_stream(c.session->state()) == save_stream(expected) && c.revision == before.revision + 1 &&
        cash(c.session->state(), c.borrower) == borrower_before + 100000 &&
        cash(c.session->state(), c.lender) == lender_before - 100000 &&
        c.session->rand().seed() == before.w.seed() && c.session->rand().calls() == before.w.calls() &&
        c.session->game().seed() == before.g.seed() && c.session->game().calls() == before.g.calls(),
        "controller books the complete Core result and mirrored lender cash once without RNG draws");
    Snapshot committed(*c.session, c.revision);
    c.serve(action); committed.unchanged(*c.session, c.revision);

    // A runtime-only national slot must never become a configured human lender.
    c.session->state().manager_teams.push_back(kNoTeam);
    write32(c.session->state(), 2, 0x314, 20000000);
    write32(c.session->state(), 2, 0x31c, 100000);
    Snapshot hidden(*c.session, c.revision);
    ui::BankRequest direct = request;
    direct.revision = c.revision;
    direct.credit_counterparty = 3;
    direct.expected_credit = bank_credit_context(c.session->state(), c.exe, c.borrower, 3).saved;
    auto hidden_core_control = c.session->state();
    check(commit_bank_credit(hidden_core_control, c.exe, c.borrower, direct.expected_credit,
        direct.credit_edits, false, 3).status == BankCreditStatus::accepted,
        "absent configured lender is otherwise Core-valid, so only the App human-lender guard rejects it");
    std::string error;
    for (int cp : {3, c.borrower + 1, -1, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()}) {
        direct.credit_counterparty = cp;
        check(!c.session->dispatch_bank(direct, c.revision, error) && error == "invalid_credit_request",
            "App dispatch excludes hidden/self/out-of-domain manager lenders before Core replay");
        hidden.unchanged(*c.session, c.revision);
    }
}

void test_refinance_rollback_and_notice(const std::filesystem::path& data) {
    Context c(data, true);
    auto action = c.request(true);
    const auto request = std::get<ui::BankRequest>(action.submenu_action);
    auto draft = bank_credit_draft(bank_credit_context(c.session->state(), c.exe, c.borrower, c.counterparty));
    check(edit_bank_credit(bank_credit_context(c.session->state(), c.exe, c.borrower, c.counterparty),
        draft, 3, 1) && draft.interest_adjustment != 0 && draft.refund != 0,
        "controlled six-month outstanding loan reaches a nonzero adjustment and named refund");
    auto& g = c.session->state();
    const size_t name_at = size_t(c.borrower) * 0xb0c + 0xa8;
    const uint8_t name = g.manager_records.at(name_at);
    g.manager_records.at(name_at) = name == 'X' ? 'Y' : 'X';
    Snapshot changed_name(*c.session, c.revision);
    const auto prior_notice = c.session->bank_checkpoint().credit_notice;
    c.serve(action);
    changed_name.unchanged(*c.session, c.revision);
    check(!c.flow.pending_bank_action() && c.session->bank_checkpoint().credit_notice == prior_notice,
        "failed fresh projection ACK rolls back both managers, loan and prior notice");
    g.manager_records.at(name_at) = name;
    action = c.flow.click(80, 178);
    check(c.flow.pending_bank_action(), "failed ACK retains a retryable manager-credit draft");
    Snapshot before(*c.session, c.revision);
    auto expected = g;
    const auto result = commit_bank_credit(expected, c.exe, c.borrower, request.expected_credit,
        request.credit_edits, false, c.counterparty);
    check(result.status == BankCreditStatus::accepted && result.fees == 0 && result.refund == draft.refund,
        "Core manager refinance accepts the nonzero adjustment without bank fees");
    const int32_t borrower_before = cash(g, c.borrower), lender_before = cash(g, c.lender);
    c.serve(action);
    check(save_stream(g) == save_stream(expected) && cash(g, c.borrower) == borrower_before + result.refund &&
        cash(g, c.lender) == lender_before - result.refund && c.revision == before.revision + 1 &&
        c.session->rand().seed() == before.w.seed() && c.session->rand().calls() == before.w.calls() &&
        c.session->game().seed() == before.g.seed() && c.session->game().calls() == before.g.calls(),
        "actual ACK books the whole refinance result and mirrors its refund without the interest fee");
    const auto notice = c.session->preparation_projection(c.revision).bank->credit_notice;
    const std::string lender_name = c.lender == 0 ? "Kreditnehmer" : "Geldgeber";
    check(notice && notice->refund == result.refund && notice->text.find(lender_name) != std::string::npos,
        "Source refund notice identifies the actual manager lender");
    Snapshot committed(*c.session, c.revision);
    c.serve(action); committed.unchanged(*c.session, c.revision);
    c.serve(c.flow.send_key("ENTER")); committed.unchanged(*c.session, c.revision);
    check(c.flow.state_json().find("\"credit_notice_open\":true") == std::string::npos,
        "notice acknowledgement is local and does not replay the mirrored booking");
}
}

int main(int argc, char** argv) {
    try {
        check(argc == 2, "usage: bank_lender_app_tests DATA");
        test_new_loan(argv[1]);
        test_refinance_rollback_and_notice(argv[1]);
        std::puts("manager lender App: typed identity, mirror booking, rollback, refund and replay OK");
    } catch (const std::exception& e) { std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1; }
    return 0;
}
