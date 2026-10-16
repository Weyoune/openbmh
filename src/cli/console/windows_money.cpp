// SPDX-License-Identifier: GPL-3.0-only
// The money windows of docs/CLI_CONCEPT/06-verein.md: Bank (menu 14) with savings, shares and
// real estate, Stadium (menu 22) with the periphery and the stand blocks, and Toto (menu 13).
// Views from CareerSession::preparation_projection; changes through dispatch_bank /
// dispatch_stadium / dispatch_toto with the requests the SDL screens build.
//
// Opening the stadium draws random numbers (CareerSession::dispatch_stadium "open":
// construction_price_percent restarts the game generator), so the console opens it through
// the engine exactly when the window opens, never as a side effect of a read command.

#include <algorithm>
#include <limits>

#include "app/window_actions.hpp"
#include "cli/console/console.hpp"
#include "cli/console/detail.hpp"
#include "game/bank_credit.hpp"

namespace bmh::cli::console {

using namespace detail;

namespace {

// TEXTD indices (docs/formats/textd_inventory.csv)
constexpr int kFinanceLabels = 339;     // Bank_OriginalToUtf8_RuntimeField6: the seven lines of the finance overview
constexpr int kCreditLabels = 352;      // Bank_OriginalToUtf8_TextPointer4: the credit view's eight lines
constexpr int kShareNames = 322;      // Bank_FormatError_ExecutableField2: the twelve listed companies (sponsorNameC01..)
constexpr int kShareTips = 334;       // Bank_FormatError_RuntimeField19: schnell verk. / verkaufen / halten / kaufen ...
constexpr int kPeripheryNames = 720;    // Stadium_RenderPanorama_TextPointer: stadiumFeatureTrafficAccess .. FanShop4 (14)

constexpr int32_t kMaxMoney = std::numeric_limits<int32_t>::max();

}  // namespace

void Console::register_money_commands() {
    // --- Bank (06-verein: Bank) -----------------------------------------------------------
    auto bank = [this] {
        const auto p = prep();
        if (!p.bank) fail("E_MODE_LOCKED", "no bank available (national team?)");
        return *p.bank;
    };
    auto dispatch_bank = [this](const ui::BankRequest& r) {
        std::string error;
        require_engine(app::BankActions(*session_).dispatch(r, revision_, error), "bank", error);
    };
    auto show_bank = [this, bank] {
        const ui::BankState b = bank();
        emit_table({"Bank", "Value"},
                   {{"balance", money(b.balance)}, {"savings", money(b.deposit)}, {"creditLine", money(b.credit_line)},
                    // record +0x324: the balance the season result is measured from (season start,
                    // moved with deposits and share deals; result = balance - this, season.cpp)
                    {"seasonBase", money(b.investment_balance)}});
    };
    openers_[14] = [this, show_bank] {
        open_window("Bank");
        show_bank();
    };
    add({"Bank", "ShowBank", {}, "balance, savings, credit line, season base", false}, [show_bank](const Values&) { show_bank(); });
    auto show_overview = [this, bank] {
        const ui::BankState b = bank();
        std::vector<std::vector<std::string>> rows;
        for (size_t i = 0; i < 7; ++i) rows.push_back({text(kFinanceLabels + int(i)), money(b.finance_values[i])});
        emit_table({"Finance", "Value"}, rows);
    };
    // opening a bank page shows its content at once (as the original screen does)
    add({"Bank", "OpenAccount", {}, "the finance overview (Bank.Account)", false}, [this, show_overview](const Values&) {
        open_window("Bank.Account");
        show_overview();
    });
    add({"Bank", "OpenSavings", {}, "the fixed deposit (Bank.Savings)", false}, [this](const Values&) { open_window("Bank.Savings"); emit('+', "savings (ShowSavings, Deposit, Withdraw)"); });
    add({"Bank", "OpenShares", {}, "the stock market (Bank.Shares)", false}, [this](const Values&) { open_window("Bank.Shares"); emit('+', "shares (ListShares, ShowShare, BuyShares, SellShares)"); });
    add({"Bank", "OpenRealEstate", {}, "real estate funds (Bank.RealEstate)", false}, [this](const Values&) { open_window("Bank.RealEstate"); emit('+', "real estate (ListRealEstate shows the funds)"); });
    Param lender = choice("lender", {"Bank", "Manager1", "Manager2", "Manager3", "Manager4"}, true);
    add({"Bank", "OpenCredit", {lender}, "the credit view (Bank.Credit)", false}, [this](const Values& v) {
        // case 5 (MaxSharesToBuy): a credit from another human manager (row lender + 1)
        const int lender = v.has("lender") ? v.choice("lender") - 1 : -1;
        if (lender >= 0 && (size_t(lender) >= session_->state().manager_teams.size() || lender == active_slot()))
            fail("E_RULE", "no other manager " + std::to_string(lender + 1) + " in this game");
        credit_counterparty_ = lender + 1;
        credit_draft_.reset();
        open_window("Bank.Credit");
        emit('+', std::string("credit ") + (lender >= 0 ? "from player " + std::to_string(lender + 1) : "from the bank") + " (ShowCredit)");
    });

    add({"Bank.Account", "ShowFinanceOverview", {}, "assets, debts and net worth", false},
        [show_overview](const Values&) { show_overview(); });
    add({"Bank.Account", "ShowAssetProfitLoss", {integer("asset", 0, 11)}, "profit/loss of a share", false}, [this, bank](const Values& v) {
        const ui::BankState b = bank();
        const auto& s = b.stocks[size_t(v.integer("asset"))];
        emit_table({"Share", "Owned", "Paid", "Price", "Value"},
                   {{text(kShareNames + int(v.integer("asset"))), std::to_string(s.owned), money(s.paid), std::to_string(s.price),
                     money(int64_t(s.owned) * s.price)}});
    });

    auto show_savings = [this, bank] {
        const ui::BankState b = bank();
        emit_table({"Savings", "Value"}, {{"balance", money(b.balance)}, {"savings", money(b.deposit)}});
    };
    add({"Bank.Savings", "ShowSavings", {}, "account and fixed deposit", false}, [show_savings](const Values&) { show_savings(); });
    add({"Bank.Savings", "Deposit", {integer("amount", 1, kMaxMoney)}, "account -> fixed deposit", true},
        [bank, dispatch_bank, show_savings](const Values& v) {
            const auto r = app::BankActions::savings(bank(), int32_t(v.integer("amount")));
            dispatch_bank(r);
            show_savings();
        });
    add({"Bank.Savings", "Withdraw", {integer("amount", 1, kMaxMoney)}, "fixed deposit -> account", true},
        [bank, dispatch_bank, show_savings](const Values& v) {
            const auto r = app::BankActions::savings(bank(), -int32_t(v.integer("amount")));
            dispatch_bank(r);
            show_savings();
        });

    auto share_row = [this](const ui::BankState& b, size_t i) {
        const auto& s = b.stocks[i];
        std::string trend = s.price > s.previous_price ? "up" : s.price < s.previous_price ? "down" : "=";
        return std::vector<std::string>{std::to_string(i), text(kShareNames + int(i)), std::to_string(s.price), trend,
                                        std::to_string(s.owned), money(s.paid), std::to_string(s.max_to_buy)};
    };
    const std::vector<std::string> share_header = {"Asset", "Share", "Price", "Trend", "Owned", "Paid", "MaxBuy"};
    add({"Bank.Shares", "ListShares", {}, "the twelve shares", false}, [this, bank, share_row, share_header](const Values&) {
        const ui::BankState b = bank();
        std::vector<std::vector<std::string>> rows;
        for (size_t i = 0; i < 12; ++i) rows.push_back(share_row(b, i));
        emit_table(share_header, rows);
    });
    add({"Bank.Shares", "ShowShare", {integer("asset", 0, 11)}, "one share with its last 30 prices", false},
        [this, bank, share_row, share_header](const Values& v) {
            const ui::BankState b = bank();
            const size_t i = size_t(v.integer("asset"));
            emit_table(share_header, {share_row(b, i)});
            std::string history;
            for (uint16_t p : b.stocks[i].history) history += (history.empty() ? "" : " ") + std::to_string(p);
            emit('i', "history: " + history);
        });
    add({"Bank.Shares", "ShareAdvice", {integer("asset", 0, 11)}, "the bank's tip for a share", true},
        [this, bank, dispatch_bank](const Values& v) {
            const auto r = app::BankActions::investment(bank(), ui::BankRequestKind::stock_tip, int(v.integer("asset")));
            dispatch_bank(r);
            // the screen shows the TEXTD advice Bank_FormatError_RuntimeField19 + tip * 4 (src/ui/bank.cpp)
            const ui::BankState b = bank();
            if (!b.last_tip) fail("E_INTERNAL", "the bank gave no tip");
            emit('+', text(kShareNames + r.asset_index) + ": " + text(kShareTips + *b.last_tip));
        });
    auto trade = [this, bank, dispatch_bank, share_row, share_header](const Values& v, ui::BankRequestKind kind) {
        const auto r = app::BankActions::investment(bank(), kind, int(v.integer("asset")), int(v.integer("count")));
        dispatch_bank(r);
        emit_table(share_header, {share_row(bank(), size_t(r.asset_index))});
    };
    add({"Bank.Shares", "BuyShares", {integer("asset", 0, 11), integer("count", 1, 9999)}, "buy shares", true},
        [trade](const Values& v) { trade(v, ui::BankRequestKind::buy_shares); });
    add({"Bank.Shares", "SellShares", {integer("asset", 0, 11), integer("count", 1, 9999)}, "sell shares", true},
        [trade](const Values& v) { trade(v, ui::BankRequestKind::sell_shares); });

    auto show_funds = [this, bank] {
        const ui::BankState b = bank();
        std::vector<std::vector<std::string>> rows;
        for (size_t i = 0; i < 6; ++i) {
            const auto& f = b.funds[i];
            // the original writes the name stem plus a singular/plural ending (Bank_OriginalToUtf8_RuntimeField)
            rows.push_back({std::to_string(i), utf8(f.name) + f.plural_suffix, money(f.price), std::to_string(f.owned), money(f.paid), std::to_string(f.max_to_buy)});
        }
        emit_table({"Type", "Fund", "Price", "Owned", "Paid", "MaxBuy"}, rows);
        if (b.property_notice) emit('i', utf8(b.property_notice->text));
    };
    add({"Bank.RealEstate", "ListRealEstate", {}, "the six real estate funds", false}, [show_funds](const Values&) { show_funds(); });
    add({"Bank.RealEstate", "BuyRealEstate", {integer("type", 0, 5), integer("units", 1, 10)}, "buy units", true},
        [bank, dispatch_bank, show_funds](const Values& v) {
            const auto r = app::BankActions::investment(bank(), ui::BankRequestKind::buy_fund,
                int(v.integer("type")), int(v.integer("units")));
            dispatch_bank(r);
            show_funds();
        });
    add({"Bank.RealEstate", "SellRealEstate", {integer("type", 0, 5), integer("units", 1, 10)}, "sell units", true},
        [bank, dispatch_bank, show_funds](const Values& v) {
            const auto r = app::BankActions::investment(bank(), ui::BankRequestKind::sell_fund,
                int(v.integer("type")), int(v.integer("units")));
            dispatch_bank(r);
            show_funds();
        });

    // --- Bank.Credit: the bank's credit editor (MaxSharesToBuy case 4, H2 p. 60-61) ----------
    // As the screen does it (src/ui/bank.cpp): a local draft of the eight rows, edited field
    // by field within the engine's bounds (bank_credit_bounds / edit_bank_credit, read-only);
    // ConfirmCredit books the edits through dispatch_bank (commit_bank_credit replays them
    // against the saved loan). Fees (5,000 EU for a change, interest adjustment) need a yes.
    // the loan row of the open credit: the bank's from the bank projection, a manager's from the
    // engine (bank_credit_context with the counterparty)
    auto credit_context = [this, bank] {
        if (credit_counterparty_ == 0) return bank().credit;
        return bank_credit_context(session_->state(), exe_, active_slot(), credit_counterparty_);
    };
    auto credit_draft = [this, credit_context] {
        if (!credit_draft_) credit_draft_ = bank_credit_draft(credit_context());
        return *credit_draft_;
    };
    auto show_credit = [this, credit_context, credit_draft] {
        const BankCreditDraft d = credit_draft();
        std::vector<std::vector<std::string>> rows;
        for (int i = 0; i < 8; ++i) {
            const BankCreditBounds bounds = bank_credit_bounds(credit_context(), d, i);
            rows.push_back({std::to_string(i), text(kCreditLabels + i), money(d.values[size_t(i)]),
                            bounds.available ? money(bounds.minimum) + ".." + money(bounds.maximum) : "-"});
        }
        emit_table({"Row", "Credit", "Value", "Editable"}, rows);
        // only the bank charges fees and the interest adjustment (case 5 has none)
        const int32_t fees = credit_counterparty_ ? 0 : d.fees + d.interest_adjustment;
        if (!d.edits.empty()) emit('i', "changes not booked yet" + (fees ? ", fees and interest " + money(fees) : std::string()));
    };
    // the editable rows: 0 amount paid out, 1 total amount, 3 term, 6 repayment
    auto edit_credit = [this, credit_context, credit_draft, show_credit](int field, long long value) {
        BankCreditDraft d = credit_draft();
        const BankCreditBounds bounds = bank_credit_bounds(credit_context(), d, field);
        if (!bounds.available) fail("E_RULE", text(kCreditLabels + field) + " cannot be changed now");
        if (value < bounds.minimum || value > bounds.maximum)
            fail("E_BAD_ARGUMENT", text(kCreditLabels + field) + " must be " + money(bounds.minimum) + ".." + money(bounds.maximum));
        if (!edit_bank_credit(credit_context(), d, field, uint32_t(value))) fail("E_RULE", "the bank does not accept this value");
        credit_draft_ = d;
        show_credit();
    };
    add({"Bank.Credit", "ShowCredit", {lender}, "the credit rows with the editable ranges", false},
        [show_credit](const Values&) { show_credit(); });
    add({"Bank.Credit", "SetCreditAmount", {integer("amount", 0, kMaxMoney)}, "AUSGEZAHLTER BETRAG: the credit", false},
        [edit_credit](const Values& v) { edit_credit(0, v.integer("amount")); });
    add({"Bank.Credit", "SetCreditPayment", {integer("monthly", 0, kMaxMoney)},
         "the monthly payment: within 20 % of the annuity for a credit from a manager", false},
        [this, edit_credit](const Values& v) {
            if (credit_counterparty_ == 0) fail("E_RULE", "the monthly payment follows from the amount and the term (SetCreditTerm)");
            edit_credit(2, v.integer("monthly"));
        });
    add({"Bank.Credit", "SetCreditTerm", {integer("months", 1, 48)}, "LAUFZEIT in months", false},
        [edit_credit](const Values& v) { edit_credit(3, v.integer("months")); });
    Param repay = detail::text("amount");
    repay.display = "amount:money|All";
    add({"Bank.Credit", "RepayCredit", {repay}, "RÜCKZAHLUNG: pay back (All = as much as possible)", false},
        [this, credit_context, credit_draft, edit_credit](const Values& v) {
            Arg a;
            a.text = v.text("amount");
            long long amount = 0;
            if (lower(a.text) == "all") {
                const BankCreditBounds bounds = bank_credit_bounds(credit_context(), credit_draft(), 6);
                if (!bounds.available) fail("E_RULE", "nothing to repay");
                amount = bounds.maximum;
            } else if (const auto n = to_int(a)) {
                amount = *n;
            } else {
                fail("E_BAD_ARGUMENT", "amount is a number or All");
            }
            edit_credit(6, amount);
        });
    auto book = [this, bank, credit_context, credit_draft, dispatch_bank](bool accept_fees) {
        const BankCreditDraft d = credit_draft();
        const auto r = app::BankActions::credit(bank(), credit_context(), d, accept_fees, credit_counterparty_);
        dispatch_bank(r);
        credit_draft_.reset();
        const ui::BankState after = bank();
        if (after.credit_notice) emit('i', after.credit_notice->text);
        emit('+', "credit booked, balance " + money(after.balance));
    };
    add({"Bank.Credit", "ConfirmCredit", {}, "book the changes (asks about fees)", true}, [this, credit_draft, book](const Values&) {
        const BankCreditDraft d = credit_draft();
        if (d.edits.empty()) fail("E_RULE", "nothing changed");
        const int32_t fees = credit_counterparty_ ? 0 : d.fees + d.interest_adjustment;
        if (fees == 0) {
            book(false);
            return;
        }
        // the screen's fee confirmation (credit_fee_confirmation_open_); the console's wording
        ask("The change costs " + money(fees) + " in fees and interest. Book it?", {"Yes", "No"}, "No", [this, book](const std::string& answer) {
            if (answer == "Yes") book(true);
            else emit('+', "credit not changed (the draft stays)");
        });
    });
    add({"Bank.Credit", "CancelCredit", {}, "discard the changes", false}, [this](const Values&) {
        credit_draft_.reset();
        emit('+', "credit changes discarded");
    });

    // --- Stadium (06-verein 8) ------------------------------------------------------------
    auto stadium = [this] {
        const auto p = prep();
        if (!p.stadium) fail("E_MODE_LOCKED", "no stadium view (national team?)");
        return *p.stadium;
    };
    // Requests carry the engine's stadium scope (stable club, scope, sequence) as the SDL
    // screen's do (StadiumScreen::request); a stale request is refused by the engine.
    auto stadium_request = [](const ui::StadiumState& observed, ui::StadiumRequestKind kind,
                               int object = -1, int option = -1, int32_t price = 0, bool accept_tip = false) {
        return app::StadiumActions::request(observed, kind, object, option, price, accept_tip);
    };
    auto dispatch_stadium = [this](const ui::StadiumRequest& r) {
        std::string error;
        require_engine(app::StadiumActions(*session_).dispatch(r, revision_, error), "stadium", error);
    };
    auto show_stadium = [this, stadium] {
        const ui::StadiumState s = stadium();
        std::vector<std::vector<std::string>> rows;
        for (size_t i = 0; i < 8; ++i) {
            const auto& row = s.investments[i];
            std::string offers;
            for (size_t o = 0; o < row.options.size(); ++o)
                offers += (o ? ", " : "") + std::string("level ") + std::to_string(row.options[o].target_level) + " for " +
                          money(row.options[o].base_price);
            rows.push_back({std::to_string(i), utf8(row.original_label), std::to_string(row.current_level),
                            row.construction_weeks ? std::to_string(row.construction_weeks) + " weeks" : "-",
                            row.available && !row.options.empty() ? offers : "-"});
        }
        emit_table({"Item", "Name", "Level", "Building", "Options"}, rows);
        std::vector<std::vector<std::string>> blocks;
        for (size_t b = 0; b < 8; ++b)
            blocks.push_back({std::string(1, char('A' + b)), std::to_string(int(s.blocks[b].capacity_level) * 1000),
                              s.blocks[b].construction_weeks ? std::to_string(int(s.blocks[b].capacity) * 1000) : "-",
                              s.blocks[b].construction_weeks ? std::to_string(s.blocks[b].construction_weeks) + " weeks" : "-"});
        emit_table({"Block", "Places", "PlannedPlaces", "Building"}, blocks);
        emit('i', "balance " + money(s.balance) + ", construction prices " + std::to_string(s.construction_percent) + " %" +
                      (s.action_feedback.empty() ? "" : ", " + utf8(s.action_feedback)));
    };
    auto show_periphery = [this, stadium] {
        const ui::StadiumState s = stadium();
        std::vector<std::vector<std::string>> rows;
        for (size_t o = 0; o < 14; ++o) {
            const bool building = s.days[o] != 0;
            const bool built = o == 0 ? s.level[o] >= 0xab : s.level[o] != 0;  // as dispatch_stadium checks
            rows.push_back({std::to_string(o), text(kPeripheryNames + int(o)), building ? "building" : built ? "built" : "-",
                            std::to_string(s.days[o])});
        }
        emit_table({"Object", "Name", "State", "Days"}, rows);
        emit('i', "balance " + money(s.balance));
    };
    openers_[22] = [this, stadium, stadium_request, dispatch_stadium, show_stadium] {
        const ui::StadiumState s = stadium();
        open_window("Stadium");
        if (!s.opened) dispatch_stadium(stadium_request(s, ui::StadiumRequestKind::open));
        show_stadium();
    };
    // leaving the stadium is an engine step (close), refused while an offer is open
    closers_["Stadium"] = [this, stadium, stadium_request, dispatch_stadium] {
        const ui::StadiumState s = stadium();
        if (s.opened) dispatch_stadium(stadium_request(s, ui::StadiumRequestKind::close));
        close_window();
        emit('+', "stadium closed");
    };
    add({"Stadium", "ShowStadium", {}, "facilities, maintenance and blocks", false}, [show_stadium](const Values&) { show_stadium(); });
    add({"Stadium", "OpenPeriphery", {}, "SICHT: PANORAMA (Stadium.Periphery)", false}, [this, show_periphery](const Values&) {
        open_window("Stadium.Periphery");
        show_periphery();
    });
    // AUSBAUEN for one of the eight investments (Front / PayConstruction): a tier is
    // chosen, confirmed, then the builder asks about a tip; each answer is one engine step.
    auto invest = [this, stadium, stadium_request, dispatch_stadium](int item) {
        ui::StadiumState s = stadium();
        const auto& row = s.investments[size_t(item)];
        if (!row.available || row.options.empty())
            fail("E_RULE", utf8(row.original_label) + (row.construction_weeks ? " is being built" : " cannot be built now"));
        const std::string label = utf8(row.original_label);
        dispatch_stadium(stadium_request(s, ui::StadiumRequestKind::select_investment, item));
        s = stadium();
        std::vector<std::string> choices;
        std::string list;
        for (const auto& option : s.investments[size_t(item)].options) {
            choices.push_back("Level" + std::to_string(option.target_level));
            list += (list.empty() ? "" : ", ") + choices.back() + " = " + money(option.base_price);
        }
        choices.push_back("Cancel");
        ask(label + ": " + list, choices, "Cancel", [this, item, label, stadium, stadium_request, dispatch_stadium](const std::string& answer) {
            ui::StadiumState s = stadium();
            if (answer == "Cancel") {
                dispatch_stadium(stadium_request(s, ui::StadiumRequestKind::cancel_investment, item));
                emit('+', "nothing built");
                return;
            }
            const auto& options = s.investments[size_t(item)].options;
            int index = -1;
            for (size_t o = 0; o < options.size(); ++o)
                if ("Level" + std::to_string(options[o].target_level) == answer) index = int(o);
            if (index < 0) fail("E_INTERNAL", "the offer changed");
            const int32_t price = int32_t(options[size_t(index)].base_price);
            dispatch_stadium(stadium_request(s, ui::StadiumRequestKind::select_investment_option, item, index, price));
            dispatch_stadium(stadium_request(stadium(), ui::StadiumRequestKind::confirm_investment, item, index, price));
            s = stadium();
            if (!s.construction_tip_dialog) fail("E_INTERNAL", "the builder asked nothing");
            const int32_t tip_price = int32_t(s.construction_tip_price);
            // the original's composed TEXTD sentence: a small tip of n percent makes them faster
            ask(utf8(s.construction_tip_dialog->body) + " (" + money(tip_price) + " instead of " + money(price) + ")", {"Yes", "No"}, "No",
                [this, item, index, price, tip_price, label, stadium, stadium_request, dispatch_stadium](const std::string& tip) {
                    const bool accept = tip == "Yes";
                    dispatch_stadium(stadium_request(stadium(), ui::StadiumRequestKind::answer_construction_tip, item, index,
                                                     accept ? tip_price : price, accept));
                    const ui::StadiumState after = stadium();
                    if (!after.action_feedback.empty()) fail("E_INSUFFICIENT_FUNDS", utf8(after.action_feedback));
                    emit('+', label + " ordered for " + money(accept ? tip_price : price));
                });
        });
    };
    add({"Stadium", "BuildFacility", {choice("facility", {"Floodlights", "Scoreboard", "PitchHeating", "VipLounge"})},
         "technical expansion (AUSBAUEN): tier and tip questions", true},
        [invest](const Values& v) { invest(v.choice("facility")); });
    add({"Stadium", "Maintain", {choice("item", {"Pitch", "Comfort", "Condition", "Safety"})}, "maintenance: tier and tip questions", true},
        [invest](const Values& v) { invest(4 + v.choice("item")); });
    // The GUI and CareerSession already implement the A-H block editor and stadium
    // data. These console commands still lack their own argument/projection adapter
    // (the stage-2 placeholders from issue #16); this is a console-only gap.
    for (const char* name : {"QuoteBlockExpansion", "ConfirmBlockExpansion", "DeclineBlockExpansion", "ShowFutureExpansion", "OpenData"})
        add({"Stadium", name, {}, "not in the console yet", false},
            [name](const Values&) { fail("E_ENGINE_GAP", std::string(name) + ": the block editor / stadium data are not ported yet"); });
    add({"Stadium", "SetTicketPrice", {integer("price", 8, 25)}, "the ticket price (as on the desk)", true}, [this](const Values& v) {
        // the same engine write as the desk's price scale (CareerSession::dispatch_ticket_price)
        const ui::DeskState d = desk();
        std::string error;
        require_engine(app::DeskActions(*session_).ticket_price(d, int(v.integer("price")), revision_, error), "ticket price", error);
        emit('+', "ticket price: " + std::to_string(desk().ticket_price));
    });

    add({"Stadium.Periphery", "ListPeriphery", {}, "the 14 objects: built, building, days", false}, [show_periphery](const Values&) { show_periphery(); });
    auto periphery = [this, stadium, stadium_request, dispatch_stadium](int object, int32_t price, bool build) {
        try {
            dispatch_stadium(stadium_request(stadium(), build ? ui::StadiumRequestKind::build_periphery : ui::StadiumRequestKind::decline_periphery,
                                             object, -1, price));
        } catch (...) {
            // a refused build (no money) must not leave the offer open: the stadium could not
            // be closed any more. It ends like NEIN, then the refusal is reported.
            if (build && stadium().quoted_object) {
                try {
                    dispatch_stadium(stadium_request(stadium(), ui::StadiumRequestKind::decline_periphery, object, -1, price));
                } catch (...) {}
            }
            throw;
        }
        emit('+', text(kPeripheryNames + object) + (build ? " is being built for " + money(price) : " not built"));
    };
    add({"Stadium.Periphery", "QuotePeriphery", {integer("object", 0, 13)}, "the building firm's current offer (question)", true},
        [this, stadium, stadium_request, dispatch_stadium, periphery](const Values& v) {
            dispatch_stadium(stadium_request(stadium(), ui::StadiumRequestKind::quote_periphery, int(v.integer("object"))));
            const ui::StadiumState s = stadium();
            if (!s.quoted_object) fail("E_INTERNAL", "the engine gave no offer");
            const int object = *s.quoted_object;
            const int32_t price = s.quoted_price;
            // OK / NEIN of the original; the console's own wording, the original shows a
            // composed TEXTD sentence with the price
            ask(text(kPeripheryNames + object) + ": offer " + money(price) + ". Accept?", {"Yes", "No"}, "No",
                [periphery, object, price](const std::string& answer) { periphery(object, price, answer == "Yes"); });
        });
    add({"Stadium.Periphery", "BuildPeriphery", {integer("object", 0, 13), integer("price", 1, kMaxMoney)}, "accept the offer", true},
        [periphery](const Values& v) { periphery(int(v.integer("object")), int32_t(v.integer("price")), true); });
    add({"Stadium.Periphery", "DeclinePeriphery", {integer("object", 0, 13), integer("price", 1, kMaxMoney)}, "decline the offer", true},
        [periphery](const Values& v) { periphery(int(v.integer("object")), int32_t(v.integer("price")), false); });
    add({"Stadium.Periphery", "OpenInterior", {}, "SICHT: INNEN (back to Stadium)", false}, [this](const Values&) { close_window(); });

    // --- Toto (05/06: Toto, H2 p. 56-57) --------------------------------------------------
    auto toto = [this] {
        const auto p = prep();
        if (!p.toto) fail("E_MODE_LOCKED", "no Toto in this game");
        return *p.toto;
    };
    // tip draft of the open coupon (the original keeps the picks in the screen until MACH ICH)
    auto tips = std::make_shared<std::array<uint8_t, 11>>();
    auto stake = std::make_shared<uint32_t>(0);
    auto show_toto = [this, toto, tips, stake] {
        const ui::TotoState t = toto();
        if (!t.available) {
            emit('i', "no Toto coupon this week" + (t.feedback.empty() ? std::string() : ": " + utf8(t.feedback)));
            return;
        }
        std::vector<std::vector<std::string>> rows;
        for (size_t i = 0; i < t.matches.size(); ++i) {
            const uint8_t pick = i < tips->size() ? (*tips)[i] : 0;
            rows.push_back({std::to_string(i + 1), utf8(t.matches[i].home_name), utf8(t.matches[i].away_name), pick == 0 ? "1" : pick == 1 ? "X" : "2"});
        }
        emit_table({"Match", "Home", "Away", "Tip"}, rows);
        emit('i', "stake " + money(*stake) + ", balance " + money(t.balance) + (t.feedback.empty() ? "" : ", " + utf8(t.feedback)));
    };
    openers_[13] = [this, toto, tips, stake, show_toto] {
        const ui::TotoState t = toto();
        for (size_t i = 0; i < tips->size(); ++i) (*tips)[i] = i < t.matches.size() ? t.matches[i].pick : 0;
        *stake = t.stake;
        open_window("Toto");
        show_toto();
    };
    add({"Toto", "ShowToto", {}, "the eleven matches with your tips", false}, [show_toto](const Values&) { show_toto(); });
    // tips are written 1 (home), X or 0 (draw), 2 (away); stored 0, 1, 2 (src/ui/toto.cpp)
    auto parse_tip = [](const std::string& s) -> int {
        const std::string t = lower(s);
        if (t == "1") return 0;
        if (t == "x" || t == "0") return 1;
        if (t == "2") return 2;
        fail("E_BAD_ARGUMENT", "a tip is 1, X (or 0) or 2");
    };
    Param tip_param = detail::text("tip");
    tip_param.display = "tip:enum(1,X,2)";
    add({"Toto", "SetTip", {integer("match", 1, 11), tip_param}, "the tip for one match", false}, [this, tips, parse_tip](const Values& v) {
        (*tips)[size_t(v.integer("match") - 1)] = uint8_t(parse_tip(v.text("tip")));
        emit('+', "match " + std::to_string(v.integer("match")) + ": " + v.text("tip"));
    });
    add({"Toto", "SetStake", {integer("amount", 0, kMaxMoney)}, "the stake", false}, [this, stake](const Values& v) {
        *stake = uint32_t(v.integer("amount"));
        emit('+', "stake " + money(*stake));
    });
    Param tip_list = detail::text("tips", 0, true);
    tip_list.display = "[tips:list(11)]";
    add({"Toto", "SubmitToto", {integer("stake", 0, kMaxMoney, true), tip_list}, "MACH ICH: hand in the coupon", true},
        [this, toto, tips, stake, parse_tip, show_toto](const Values& v) {
            const ui::TotoState t = toto();
            if (!t.available) fail("E_RULE", "no Toto coupon this week");
            if (v.has("stake")) *stake = uint32_t(v.integer("stake"));
            // submit_pools_coupon: no stake, or 10.000 to 400.000
            if (*stake != 0 && (*stake < 10000 || *stake > 400000)) fail("E_BAD_ARGUMENT", "the stake is 0 or 10000 to 400000");
            if (v.has("tips")) {
                std::string list = v.text("tips");
                if (list.size() < 2 || list.front() != '[' || list.back() != ']') fail("E_BAD_ARGUMENT", "tips is a list [1, X, 2, ...]");
                list = list.substr(1, list.size() - 2);
                std::vector<int> parsed;
                size_t start = 0;
                while (start <= list.size()) {
                    size_t comma = list.find(',', start);
                    std::string item = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                    item.erase(0, item.find_first_not_of(' '));
                    item.erase(item.find_last_not_of(' ') + 1);
                    parsed.push_back(parse_tip(item));
                    if (comma == std::string::npos) break;
                    start = comma + 1;
                }
                if (parsed.size() != 11) fail("E_BAD_ARGUMENT", "tips needs 11 values");
                for (size_t i = 0; i < 11; ++i) (*tips)[i] = uint8_t(parsed[i]);
            }
            ui::TotoRequest r;
            r.kind = ui::TotoRequestKind::submit;
            r.manager_slot = t.manager_slot;
            r.runtime_club = t.runtime_club;
            r.revision = revision_;
            r.stake = *stake;
            r.tips = *tips;
            std::string error;
            require_engine(session_->dispatch_toto(r, revision_, error), "Toto", error);
            emit('+', "coupon handed in, stake " + money(*stake));
            show_toto();
        });
    add({"Toto", "ClearToto", {}, "all tips back to 1, stake 0", false}, [this, tips, stake](const Values&) {
        tips->fill(0);
        *stake = 0;
        emit('+', "coupon cleared");
    });
}

}  // namespace bmh::cli::console
