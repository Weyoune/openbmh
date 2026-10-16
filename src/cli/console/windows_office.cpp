// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
// The office windows of docs/CLI_CONCEPT: Advertising (menu 21, 06-verein 9), Contracts
// (menu 27, 06-verein 6) and Settings (menu 19, 08-einstellungen). Views from the pure
// projections ui::advertising_from_game / contracts_from_game / settings_from_game, changes
// through dispatch_advertising / dispatch_contract / dispatch_settings with the requests
// the SDL screens build (src/ui/advertising.cpp, contracts.cpp, settings.cpp).

#include <algorithm>
#include <limits>

#include "app/desktop_actions.hpp"
#include "cli/console/console.hpp"
#include "cli/console/detail.hpp"
#include "ui/advertising.hpp"
#include "ui/contracts.hpp"
#include "ui/settings.hpp"

namespace bmh::cli::console {

using namespace detail;

namespace {

const std::vector<std::string> kAdSlots = {"Board1", "Board2", "Board3",  "Board4",  "Board5",  "Board6", "Board7", "Board8",
                                           "Board9", "Board10", "Board11", "Board12", "Board13", "Board14", "Shirt", "Tv"};
// the seven bonuses, in the order of the manager record / ContractRequest::proposed_bonuses
// the three spending targets 16-18 (AdvertisingSpendingView::target_slot); the original
// labels them with pictures, its TEXTD labels ("Monatsausgaben:") do not tell PR from fan care
const std::vector<std::string> kSpending = {"FanItems", "PublicRelations", "FanCare"};
const std::vector<std::string> kBonuses = {"Goal", "Points", "Appearance", "Champion", "Place2to5", "Place6to10", "AvoidRelegation"};
// the first three bonuses are percentages up to 20, the others up to 200 (dispatch_contract)
int bonus_max(int kind) { return kind < 3 ? 20 : 200; }

constexpr int kPositionNames = 629;  // positionGoalkeeperCaps "TOR", ABW, MIT, STU

// Settings: row A-T, column 1-9 (08-einstellungen); A1 = row 0, column 0.
std::pair<int, int> switch_key(const std::string& key) {
    if (key.size() < 2 || key.size() > 2) fail("E_BAD_ARGUMENT", "a switch is A1 ... T9");
    const int row = std::toupper(static_cast<unsigned char>(key[0])) - 'A';
    const int column = key[1] - '1';
    if (row < 0 || row >= 20 || column < 0 || column >= 9) fail("E_BAD_ARGUMENT", "a switch is A1 ... T9");
    return {row, column};
}

}  // namespace

void Console::register_office_commands() {
    // --- Advertising ----------------------------------------------------------------------
    auto adverts = [this] { return ui::advertising_from_game(session_->state(), active_slot(), revision_); };
    auto contract_of = [](const ui::AdvertisingState& a, int slot) -> const ui::AdvertisingContract& {
        return slot < 14 ? a.boards.at(size_t(slot)) : slot == 14 ? a.shirt : a.television;
    };
    auto show_adverts = [this, adverts, contract_of] {
        const ui::AdvertisingState a = adverts();
        std::vector<std::vector<std::string>> rows;
        for (int slot = 0; slot < 16; ++slot) {
            const auto& c = contract_of(a, slot);
            rows.push_back({kAdSlots[size_t(slot)], c.active ? utf8(c.sponsor_name) : "free", c.active ? money(c.monthly_amount) : "-",
                            c.active ? std::to_string(c.months_remaining) : "-", c.option_marker ? "option" : ""});
        }
        emit_table({"Slot", "Sponsor", "Monthly", "Months", ""}, rows);
        std::vector<std::vector<std::string>> spending;
        for (size_t i = 0; i < 3; ++i)
            spending.push_back({kSpending[i], money(a.spending[i].current_amount), money(a.spending[i].maximum_amount),
                                a.spending[i].available ? "" : "locked"});
        emit_table({"Spending", "Amount", "Maximum", ""}, spending);
        emit('i', "monthly: income " + money(a.finance_preview.income) + ", expenses " + money(a.finance_preview.expenses) +
                      ", balance " + money(a.finance_preview.balance));
        if (!a.notification_text.empty()) emit('i', utf8(a.notification_text));
    };
    openers_[21] = [this, show_adverts] {
        open_window("Advertising");
        show_adverts();
    };
    add({"Advertising", "ShowAdvertising", {}, "all boards, shirt, TV and the spending", false}, [show_adverts](const Values&) { show_adverts(); });
    auto offers_of = [](const ui::AdvertisingState& a, int slot) {
        std::vector<const AdvertisingOfferCandidate*> list;
        for (const auto& o : a.offers[size_t(slot)].offers)
            if (o.present && o.monthly_amount != 0) list.push_back(&o);
        return list;
    };
    add({"Advertising", "ListAdOffers", {choice("slot", kAdSlots)}, "the offers for a board, the shirt or TV (an expired contract: renewal first)", true},
        [this, adverts, offers_of](const Values& v) {
            const int slot = v.choice("slot");
            // A contract whose months ran out: the click on it asks for the renewal first
            // (CheckAdvertisingRenewal) - the sponsor may decline, otherwise the manager decides; a
            // declined or refused renewal frees the place for the offers below.
            if (adverts().offers[size_t(slot)].status == bmh::AdvertisingOfferStatus::unsupported_extension) {
                std::string error;
                const auto renewal = session_->check_advertising_renewal(adverts().manager_slot, slot, revision_, error);
                if (!renewal) require_engine(false, "advertising", error);
                if (renewal->status == AdvertisingRenewal::Status::declined_by_sponsor) {
                    emit('i', utf8(renewal->notice));
                } else if (renewal->status == AdvertisingRenewal::Status::offered) {
                    const int manager = adverts().manager_slot, contract = renewal->contract_slot;
                    ask(utf8(renewal->question), {"Yes", "No"}, "No", [this, manager, contract, slot](const std::string& answer) {
                        std::string err;
                        if (!session_->answer_advertising_renewal(manager, contract, answer == "Yes", revision_, err))
                            require_engine(false, "advertising", err);
                        emit('+', answer == "Yes" ? "contract renewed" : "contract not renewed: ListAdOffers(" +
                                                                             kAdSlots[size_t(slot)] + ") shows the offers");
                    });
                    return;
                }
            }
            const ui::AdvertisingState a = adverts();
            const auto& list = a.offers[size_t(slot)];
            const auto& names = list.pool_group == 2 ? a.television_sponsor_names : a.board_sponsor_names;
            std::vector<std::vector<std::string>> rows;
            int n = 0;
            for (const auto* o : offers_of(a, slot))
                rows.push_back({std::to_string(++n), utf8(names[size_t(std::clamp(int(o->sponsor_id), 0, 9))]), money(o->monthly_amount),
                                std::to_string(o->months), std::to_string(o->bundle_units), o->can_accept ? "" : "not possible"});
            if (rows.empty()) emit('i', "no offers for " + kAdSlots[size_t(slot)]);
            else emit_table({"Offer", "Sponsor", "Monthly", "Months", "Boards", ""}, rows);
        });
    add({"Advertising", "AcceptAdOffer", {choice("slot", kAdSlots), integer("offer", 1, 10)}, "ANNEHMEN", true},
        [this, adverts, offers_of, show_adverts](const Values& v) {
            const ui::AdvertisingState a = adverts();
            const int slot = v.choice("slot");
            const auto list = offers_of(a, slot);
            const size_t n = size_t(v.integer("offer"));
            if (n > list.size()) fail("E_BAD_ARGUMENT", "no offer " + std::to_string(n) + " (ListAdOffers)");
            if (a.offers[size_t(slot)].status == bmh::AdvertisingOfferStatus::unsupported_extension)
                fail("E_RULE", "the contract on " + kAdSlots[size_t(slot)] + " ran out: ListAdOffers(" + kAdSlots[size_t(slot)] +
                                   ") asks for the renewal first");
            if (!list[n - 1]->can_accept) fail("E_RULE", "this offer cannot be accepted");
            std::string error;
            if (!session_->accept_advertising_selection(a, slot, list[n - 1]->raw_slot,
                                                        next_request_id_++, revision_, error))
                require_engine(false, "advertising", error);
            emit('+', "offer accepted for " + kAdSlots[size_t(slot)]);
            show_adverts();
        });
    add({"Advertising", "DeclineAdOffer", {}, "ABBRECHEN", false}, [this](const Values&) { emit('+', "no offer accepted"); });
    auto spend = [this, adverts](int target, uint32_t amount) {
        const ui::AdvertisingState a = adverts();
        const auto& view = a.spending[size_t(target - 16)];
        if (!view.available) fail("E_RULE", utf8(a.spending_blocked_text.empty() ? "not available now" : a.spending_blocked_text));
        if (amount > view.maximum_amount) fail("E_BAD_ARGUMENT", "at most " + money(view.maximum_amount));
        AdvertisingSpendingResult result;
        std::string error;
        if (!session_->set_advertising_budget(a, target, amount, next_request_id_++, revision_, error, result))
            require_engine(false, "advertising", error);
        const std::string message = result.message_address >= ::bmh::layout::TextPointerTable ? text(int((result.message_address - ::bmh::layout::TextPointerTable) / 4)) : "";
        if (result.status == AdvertisingSpendingStatus::insufficient_funds) fail("E_INSUFFICIENT_FUNDS", message);
        emit('+', kSpending[size_t(target - 16)] + ": " + money(amount) + (message.empty() ? "" : ". " + message));
    };
    add({"Advertising", "SetSpending", {choice("kind", {"PublicRelations", "FanCare"}), integer("amount", 0, std::numeric_limits<int32_t>::max())},
         "monthly budget for press work or fan care", true},
        [spend](const Values& v) { spend(17 + v.choice("kind"), uint32_t(v.integer("amount"))); });
    add({"Advertising", "BuyFanItems", {integer("amount", 1, std::numeric_limits<int32_t>::max())}, "buy fan articles (sold in 6-9 weeks)", true},
        [spend](const Values& v) { spend(16, uint32_t(v.integer("amount"))); });

    // --- Contracts ------------------------------------------------------------------------
    auto contracts = [this] { return ui::contracts_from_game(session_->state(), active_slot(), revision_); };
    auto dispatch_contract = [this](const ui::ContractRequest& r) {
        ui::ContractActionResult result;
        std::string error;
        require_engine(app::ContractActions(*session_).dispatch(r, revision_, result, error), "contracts", error);
        return result;
    };
    auto show_contracts = [this, contracts] {
        const ui::ContractsState c = contracts();
        std::vector<std::vector<std::string>> rows;
        for (const auto& p : c.players)
            rows.push_back({"row:" + std::to_string(p.row), "id:" + std::to_string(p.player_id), utf8(p.name),
                            p.position >= 0 && p.position < 4 ? text(kPositionNames + p.position) : "?", std::to_string(p.age),
                            money(p.current_monthly_salary), std::to_string(p.remaining_months),
                            p.insurance ? std::to_string(p.insurance) : "-", p.offer_blocked ? "[sulking]" : ""});
        emit_table({"Row", "Id", "Player", "Pos", "Age", "Salary", "Months", "Insurance", ""}, rows);
    };
    openers_[27] = [this, show_contracts] {
        session_->clear_contract_quote();
        open_window("Contracts");
        show_contracts();
    };
    add({"Contracts", "ShowContracts", {choice("sheet", {"A", "B"}, true)}, "the squad with salaries and contract months", false},
        [show_contracts](const Values&) { show_contracts(); });
    add({"Contracts", "ShowTeamBonuses", {}, "the seven team bonuses (percent of the salary)", false}, [this, contracts](const Values&) {
        const ui::ContractsState c = contracts();
        if (!c.mode_three) fail("E_MODE_LOCKED", "team bonuses only in mode III");
        std::vector<std::vector<std::string>> rows;
        for (size_t i = 0; i < 7; ++i) rows.push_back({kBonuses[i], std::to_string(c.team_premiums[i]), std::to_string(bonus_max(int(i)))});
        emit_table({"Bonus", "Percent", "Max"}, rows);
    });
    add({"Contracts", "SetTeamBonus", {choice("kind", kBonuses), integer("percent", 0, 200)}, "a team bonus; it can only be raised", true},
        [this, contracts, dispatch_contract](const Values& v) {
            const ui::ContractsState c = contracts();
            if (!c.mode_three) fail("E_MODE_LOCKED", "team bonuses only in mode III");
            const int kind = v.choice("kind"), percent = int(v.integer("percent"));
            if (percent > bonus_max(kind)) fail("E_BAD_ARGUMENT", kBonuses[size_t(kind)] + " is at most " + std::to_string(bonus_max(kind)));
            const auto r = app::ContractActions::team_bonus(c, kind, uint8_t(percent), next_request_id_++);
            const auto result = dispatch_contract(r);
            if (!result.accepted) fail("E_RULE", result.feedback.empty() ? "the team does not accept this" : result.feedback);
            emit('+', result.feedback.empty() ? kBonuses[size_t(kind)] + " = " + std::to_string(percent) : result.feedback);
        });
    Param player = detail::text("player");
    player.display = "player:Player";
    add({"Contracts", "OpenNegotiation", {player}, "the player's demands (Contracts.Negotiate)", true},
        [this, contracts, dispatch_contract](const Values& v) {
            const ui::ContractsState c = contracts();
            std::vector<PlayerRef> refs;
            for (const auto& p : c.players) refs.push_back({p.row, p.player_id, utf8(p.name)});
            const auto& p = c.players[resolve_player(v.text("player"), refs)];
            if (p.offer_blocked) fail("E_RULE", utf8(p.name) + " does not negotiate at the moment");
            const auto r = app::ContractActions::demands(c, p, next_request_id_++);
            const auto result = dispatch_contract(r);
            negotiation_ = Negotiation{p.row, p.player_id, utf8(p.name), result.quote_id, result.demands, -1, 0, p.team_premiums};
            open_window("Contracts.Negotiate");
            std::vector<std::vector<std::string>> rows;
            for (size_t y = 0; y < 4; ++y) rows.push_back({std::to_string(y + 1), money(result.demands[y])});
            emit('i', utf8(p.name) + ": " + std::to_string(p.remaining_months) + " months left, salary " + money(p.current_monthly_salary));
            emit_table({"Years", "Demand"}, rows);
        });
    add({"Contracts.Negotiate", "ShowDemands", {}, "the demands for 1-4 years", false}, [this](const Values&) {
        if (!negotiation_) fail("E_INTERNAL", "no negotiation");
        std::vector<std::vector<std::string>> rows;
        for (size_t y = 0; y < 4; ++y) rows.push_back({std::to_string(y + 1), money(negotiation_->demands[y])});
        emit_table({"Years", "Demand"}, rows);
        if (negotiation_->years > 0)
            emit('i', "offer: " + std::to_string(negotiation_->years) + " years, " + money(negotiation_->salary) + " monthly");
    });
    add({"Contracts.Negotiate", "SetContractOffer", {integer("years", 1, 4), integer("salary", 1, 200000)}, "the salary offer (monthly)", false},
        [this](const Values& v) {
            if (!negotiation_) fail("E_INTERNAL", "no negotiation");
            negotiation_->years = int(v.integer("years"));
            negotiation_->salary = uint32_t(v.integer("salary"));
            emit('+', "offer: " + std::to_string(negotiation_->years) + " years, " + money(negotiation_->salary) + " monthly");
        });
    add({"Contracts.Negotiate", "SetInsurance", {choice("form", {"A", "B", "C", "D"}), integer("months", 1, 60)}, "insure the player (no question)", true},
        [this, contracts, dispatch_contract](const Values& v) {
            if (!negotiation_) fail("E_INTERNAL", "no negotiation");
            const auto r = app::ContractActions::insurance(contracts(), negotiation_->row, negotiation_->player_id,
                negotiation_->quote_id, uint8_t(v.choice("form")), uint8_t(v.integer("months")), next_request_id_++);
            const auto result = dispatch_contract(r);
            negotiation_->quote_id = result.quote_id;
            emit('+', "insured: form " + std::string(1, char('A' + v.choice("form"))) + ", " + std::to_string(v.integer("months")) + " months");
        });
    add({"Contracts.Negotiate", "SetPlayerBonus", {choice("kind", kBonuses), integer("percent", 0, 200)}, "a bonus for this player (PRÄMIEN)", false},
        [this](const Values& v) {
            if (!negotiation_) fail("E_INTERNAL", "no negotiation");
            const int kind = v.choice("kind"), percent = int(v.integer("percent"));
            if (percent > bonus_max(kind)) fail("E_BAD_ARGUMENT", kBonuses[size_t(kind)] + " is at most " + std::to_string(bonus_max(kind)));
            negotiation_->bonuses[size_t(kind)] = uint8_t(percent);
            emit('+', kBonuses[size_t(kind)] + " = " + std::to_string(percent));
        });
    add({"Contracts.Negotiate", "MakeOffer", {}, "ANGEBOT MACHEN: the player decides", true},
        [this, contracts, dispatch_contract](const Values&) {
            if (!negotiation_) fail("E_INTERNAL", "no negotiation");
            const Negotiation n = *negotiation_;
            const auto r = app::ContractActions::offer(contracts(), n.row, n.player_id, n.quote_id,
                n.bonuses, n.years, n.salary, n.demands, next_request_id_++);
            const auto result = dispatch_contract(r);
            negotiation_.reset();
            close_window();
            emit('+', std::string(result.accepted ? "accepted: " : "rejected: ") + result.feedback);
        });

    // --- Settings -------------------------------------------------------------------------
    auto settings = [this] { return ui::settings_from_game(session_->state(), active_slot(), revision_, exe_); };
    // A cell as the screen shows it (SettingsScreen::render): columns 1-7 are switches, the
    // stored 1 is OFF, 0 is ON, 2 is an empty cell; column 8 shows raw/2, column 9 the raw
    // value; values >= 200 are empty cells. Bits 6/7 mark a switch unavailable.
    auto cell_text = [](const ui::SettingsState& s, int row, int column) -> std::string {
        const uint8_t raw = s.display_cells ? (*s.display_cells)[size_t(row * 9 + column)] : s.cells[size_t(row * 9 + column)];
        if (column < 7) {
            const uint8_t value = raw & 0x3f;
            if (value == 2) return " ";
            return std::string(value == 1 ? "off" : "on") + ((raw & 0xc0) ? "*" : "");
        }
        if (raw >= 200) return " ";
        return std::to_string(column == 8 ? raw / 2 : raw);  // column 9 shows half the raw value (SettingsScreen)
    };
    auto show_settings = [this, settings, cell_text] {
        const ui::SettingsState s = settings();
        std::vector<std::vector<std::string>> rows;
        for (int row = 0; row < 20; ++row) {
            std::vector<std::string> cells = {std::string(1, char('A' + row))};
            for (int column = 0; column < 9; ++column) cells.push_back(cell_text(s, row, column));
            rows.push_back(cells);
        }
        emit_table({"", "1", "2", "3", "4", "5", "6", "7", "8", "9"}, rows);
        emit('i', "* = not available in this game; keys A1 ... T9 (08-einstellungen 3)");
    };
    openers_[19] = [this, show_settings] {
        open_window("Settings");
        show_settings();
    };
    add({"Settings", "ShowSettings", {detail::text("group", 0, true)}, "the switch field A1-T9", false}, [show_settings](const Values& v) {
        if (v.has("group")) fail("E_ENGINE_GAP", "the named groups of 08-einstellungen 3 are not in the console yet; ShowSettings shows all");
        show_settings();
    });
    add({"Settings", "ShowSwitch", {detail::text("key")}, "one switch", false}, [this, settings, cell_text](const Values& v) {
        const auto [row, column] = switch_key(v.text("key"));
        emit('+', v.text("key") + " = " + cell_text(settings(), row, column));
    });
    auto edit = [this, settings](int row, int column, ui::SettingsEdit operation) {
        const ui::SettingsState s = settings();
        std::string error;
        require_engine(app::DeskSettingsActions(*session_).edit(s, row, column, operation, revision_, error), "settings", error);
    };
    add({"Settings", "SetSwitch", {detail::text("key"), detail::text("value")}, "set a switch (on/off or a number)", true},
        [this, settings, cell_text, edit](const Values& v) {
            const auto [row, column] = switch_key(v.text("key"));
            Arg value;
            value.text = v.text("value");
            if (column < 7) {
                const auto on = to_bool(value);
                if (!on) fail("E_BAD_ARGUMENT", "a switch is on or off");
                const std::string now = cell_text(settings(), row, column);
                if (now == " ") fail("E_RULE", v.text("key") + " is an empty cell");
                if ((now.rfind("on", 0) == 0) != *on) edit(row, column, ui::SettingsEdit::toggle_binary);
            } else {
                const auto target = to_int(value);
                if (!target) fail("E_BAD_ARGUMENT", "the value is a number");
                for (int step = 0; step < 200; ++step) {
                    const std::string now = cell_text(settings(), row, column);
                    if (now == " ") fail("E_RULE", v.text("key") + " is an empty cell");
                    const int current = std::stoi(now);
                    if (current == *target) break;
                    edit(row, column, current < *target ? ui::SettingsEdit::increment : ui::SettingsEdit::decrement);
                    if (std::stoi(cell_text(settings(), row, column)) == current) fail("E_BAD_ARGUMENT", "the value cannot go further");
                }
            }
            emit('+', v.text("key") + " = " + cell_text(settings(), row, column));
        });
    add({"Settings", "IncreaseSwitch", {detail::text("key")}, "the plus field (columns 8, 9)", true}, [this, settings, cell_text, edit](const Values& v) {
        const auto [row, column] = switch_key(v.text("key"));
        if (column < 7) fail("E_BAD_ARGUMENT", "only columns 8 and 9 have values");
        edit(row, column, ui::SettingsEdit::increment);
        emit('+', v.text("key") + " = " + cell_text(settings(), row, column));
    });
    add({"Settings", "DecreaseSwitch", {detail::text("key")}, "the minus field (columns 8, 9)", true}, [this, settings, cell_text, edit](const Values& v) {
        const auto [row, column] = switch_key(v.text("key"));
        if (column < 7) fail("E_BAD_ARGUMENT", "only columns 8 and 9 have values");
        edit(row, column, ui::SettingsEdit::decrement);
        emit('+', v.text("key") + " = " + cell_text(settings(), row, column));
    });
    for (const char* name : {"LoadSettings", "SaveSettings", "ShowReadme"})
        add({"Settings", name, {}, "not in the console yet", false},
            [name](const Values&) { fail("E_ENGINE_GAP", std::string(name) + ": the .EIN files are not ported yet (C4)"); });
}

}  // namespace bmh::cli::console
