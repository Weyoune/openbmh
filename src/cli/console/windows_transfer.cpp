// SPDX-License-Identifier: GPL-3.0-only
// The transfer market of docs/CLI_CONCEPT/06-verein.md 5 (menu 17): the market list, listing an
// own player for sale or loan, and the offer editor Transfer.Negotiate for a purchase or a loan.
//
// The engine calls are the SDL app's (src/app/controller.cpp serve_transfer_action /
// serve_transfer_offer): CareerSession::dispatch_transfer_market, open_transfer_offer and
// dispatch_transfer_offer through app::TransferActions. Request binding and rollback
// belong to that shared boundary, including aborted questions and acknowledgements. The
// original's notices (agreement, refusal) arrive through the sale hook and are printed as
// i lines after the call.

#include <algorithm>

#include "app/window_actions.hpp"

#include "cli/console/console.hpp"
#include "cli/console/detail.hpp"
#include "game/desk_actions.hpp"
#include "game/human_buyer_sale.hpp"
#include "game/poach.hpp"
#include "game/schedule.hpp"
#include "ui/club_info.hpp"
#include "ui/desk_state.hpp"
#include "ui/league_table.hpp"
#include "ui/outgoing_transfer.hpp"
#include "ui/transfer_market.hpp"

namespace bmh::cli::console {

using namespace detail;

namespace {

// The editor of a market player is closed when his four bid places are taken by other clubs
// (OpenTransferEditor: editor_slot finds no free or own place; the original takes at most four bids).
// Agents read the old "this offer cannot be changed" as a fault (learning session B2-004/005).
constexpr const char* kNoBidSlot =
    "four other clubs have bid for this player already - there is no room for another offer "
    "(at most four bids per player); try another player or wait until he has decided";

constexpr int kPositionNames = 629;  // positionGoalkeeperCaps "TOR", ABW, MIT, STU
const std::vector<std::string> kPositionKeys = {"TOR", "ABW", "MIT", "STU"};
// the ten editor fields in the order of OutgoingTransferField
const std::vector<std::string> kOfferFields = {"Fee", "Salary", "Years", "BonusGoal", "BonusPoints", "BonusAppearance",
                                               "BonusChampion", "Bonus2to5", "Bonus6to10", "BonusAvoidRelegation"};

}  // namespace

void Console::register_transfer_commands() {
    auto market = [this] {
        const auto p = prep();
        if (!p.transfer_market) fail("E_MODE_LOCKED", "no transfer market here (national team?)");
        return *p.transfer_market;
    };
    // the original's notices during a transfer call, printed after it
    auto notices = std::make_shared<std::vector<std::string>>();
    // The yes/no questions of a poaching offer (PresentManagerDialog: the selling manager, the DFB
    // fee) come from inside the engine call. The console answers them from `answers`; an
    // unanswered one is recorded in `pending` and stops the call (nothing is committed), the
    // player is asked and the same call runs again with the answers so far.
    struct Questions {
        std::vector<bool> answers;
        size_t next = 0;
        std::optional<std::pair<int, std::string>> pending;  // manager, text
    };
    auto questions = std::make_shared<Questions>();
    auto hooks = [notices, questions] {
        HumanBuyerSaleHooks h;
        h.notice = [notices](const GameState&, const HumanBuyerSaleNotice& n) { notices->push_back(utf8(n.original_message)); };
        h.confirm = [questions](const GameState&, int manager, const std::string& text) -> bool {
            if (questions->next < questions->answers.size()) return questions->answers[questions->next++];
            questions->pending = std::make_pair(manager, utf8(text));
            throw std::runtime_error("question to player " + std::to_string(manager + 1));
        };
        return h;
    };
    auto flush_notices = [this, notices] {
        for (const auto& n : *notices) emit('i', n);
        notices->clear();
    };
    auto filter = std::make_shared<std::array<bool, 4>>(std::array<bool, 4>{true, true, true, true});
    auto show_market = [this, market, filter](int page) {
        const ui::TransferMarketState m = market();
        std::vector<const ui::TransferMarketItem*> items;
        for (const auto& item : m.items)
            if (item.position >= 0 && item.position < 4 && (*filter)[size_t(item.position)]) items.push_back(&item);
        constexpr size_t kPage = 20;
        const size_t pages = std::max<size_t>(1, (items.size() + kPage - 1) / kPage);
        if (page < 1 || size_t(page) > pages) fail("E_BAD_ARGUMENT", "the market has " + std::to_string(pages) + " pages");
        std::vector<std::vector<std::string>> rows;
        for (size_t i = (size_t(page) - 1) * kPage; i < items.size() && i < size_t(page) * kPage; ++i) {
            const auto& it = *items[i];
            std::string mark = it.own_listing ? "[own]" : it.own_bid_pending ? "[your bid]" : "";
            rows.push_back({"id:" + std::to_string(it.player), utf8(it.name), text(kPositionNames + it.position), std::to_string(it.age),
                            std::to_string(it.strength), utf8(it.seller_name), money(it.asking_fee), std::to_string(it.offers), mark});
        }
        emit_table({"Id", "Player", "Pos", "Age", "Str", "Club", "Fee", "Offers", ""}, rows);
        emit('i', "page " + std::to_string(page) + "/" + std::to_string(pages) + ", balance " + money(m.balance) + ", credit line " +
                      money(m.credit_line));
    };
    openers_[17] = [this, show_market] {
        open_window("Transfer");
        show_market(1);
    };
    add({"Transfer", "ShowMarket", {integer("page", 1, 100, true)}, "the offered players (20 per page)", false},
        [show_market](const Values& v) { show_market(int(v.integer("page", 1))); });
    Param positions = detail::text("positions");
    positions.display = "positions:list(TOR,ABW,MIT,STU)|All";
    add({"Transfer", "SetMarketFilter", {positions}, "ALLE / TOR / ABW / MIT / STU", false}, [this, filter, show_market](const Values& v) {
        std::string list = v.text("positions");
        if (lower(list) == "all") {
            filter->fill(true);
        } else {
            // a single position (TOR) counts as the list [TOR]
            if (list.size() >= 2 && list.front() == '[' && list.back() == ']') list = list.substr(1, list.size() - 2);
            for (char& c : list) c = char(std::toupper(static_cast<unsigned char>(c)));
            filter->fill(false);
            size_t start = 0;
            while (start <= list.size()) {
                const size_t comma = list.find(',', start);
                std::string item = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                item.erase(0, item.find_first_not_of(' '));
                item.erase(item.find_last_not_of(' ') + 1);
                const auto it = std::find(kPositionKeys.begin(), kPositionKeys.end(), item);
                if (it == kPositionKeys.end()) fail("E_BAD_ARGUMENT", "unknown position " + item);
                (*filter)[size_t(it - kPositionKeys.begin())] = true;
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
        }
        show_market(1);
    });

    // own squad rows by id:/row:/name, market players by id:/name
    auto squad_item = [market](const std::string& reference) {
        const ui::TransferMarketState m = market();
        std::vector<PlayerRef> refs;
        for (const auto& s : m.squad) refs.push_back({s.row, s.player, utf8(s.name)});
        return m.squad[resolve_player(reference, refs)];
    };
    auto market_item = [market](const std::string& reference) {
        const ui::TransferMarketState m = market();
        std::vector<PlayerRef> refs;
        for (const auto& it : m.items) refs.push_back({it.list_entry, it.player, utf8(it.name)});
        return m.items[resolve_player(reference, refs)];
    };
    auto list_player = [this, market, squad_item, hooks, flush_notices](const std::string& reference, bool loan) {
        const ui::TransferMarketState m = market();
        const ui::TransferMarketSquadItem item = squad_item(reference);
        if (item.already_listed) fail("E_RULE", utf8(item.name) + " is already on the list");
        const auto r = app::TransferActions::listing(m, item, loan);
        std::string error;
        std::optional<app::TransferActions::MarketDispatch> outcome;
        try {
            outcome = app::TransferActions(*session_).market(r, hooks(), revision_, error);
        } catch (const std::exception& e) {
            fail("E_INTERNAL", std::string("transfer: ") + e.what());
        }
        if (!outcome) {
            require_engine(false, "transfer", error);
        }
        flush_notices();
        emit('+', utf8(item.name) + (loan
            ? " steht auf der Transferliste; der Entleiher wählt 12 oder 24 Monate im Angebot"
            : " is on the transfer list"));
    };
    Param player = detail::text("player");
    player.display = "player:Player";
    add({"Transfer", "SellPlayer", {player}, "VER-KAUFEN: put an own player on the list", true},
        [list_player](const Values& v) { list_player(v.text("player"), false); });
    add({"Transfer", "LoanOutPlayer", {player, integer("months", 12, 24, true)}, "VER-LEIHEN: offer an own player for loan", true},
        [list_player](const Values& v) {
            if (v.integer("months", 24) != 12 && v.integer("months", 24) != 24)
                fail("E_BAD_ARGUMENT", "Leihangebote verwenden 12 oder 24 Monate");
            list_player(v.text("player"), true);
        });

    // --- Transfer.Negotiate: the offer editor (06-verein 5.2) -----------------------------
    auto show_offer = [this] {
        if (!offer_editor_) fail("E_INTERNAL", "no offer editor");
        const auto& e = *offer_editor_;
        const auto& p = e.panel;
        emit('i', utf8(p.player_name) + (p.seller_name.empty() ? std::string() : " (" + utf8(p.seller_name) + ")") + ", age " + std::to_string(p.age) + ", strength " +
                      std::to_string(p.strength) + (e.quote_loan ? ", loan" : ""));
        std::vector<std::vector<std::string>> rows = {
            {"demand", money(p.asking_fee), money(p.asking_monthly_salary), "-"},
            {"quote", money(p.quoted_fee), money(p.quoted_monthly_salary), std::to_string(p.quoted_years)},
            {"offer", money(e.fee), money(e.monthly_salary), std::to_string(e.duration_years)}};
        emit_table({"", "Fee", "Monthly salary", "Years"}, rows);
        std::vector<std::vector<std::string>> bonuses;
        for (size_t i = 0; i < 7; ++i) bonuses.push_back({kOfferFields[3 + i], std::to_string(e.bonuses[i] * (i >= 3 ? 5 : 1))});
        emit_table({"Bonus", "Percent"}, bonuses);
        if (!e.editable) emit('i', kNoBidSlot);
    };
    auto offer_request = [this](ui::OutgoingTransferRequestKind kind, ui::OutgoingTransferField field, uint32_t value) {
        return app::TransferActions::editor(*offer_editor_, kind, field, value);
    };
    auto dispatch_offer = [this, hooks, flush_notices](const ui::OutgoingTransferRequest& r,
                                                     const app::TransferOfferTeamInfoPresenter& team_info = {},
                                                     const app::TransferOfferPlayerInfoPresenter& player_info = {}) {
        std::string error;
        std::optional<app::CareerSession::TransferOfferEditorDispatch> result;
        try {
            result = loan_return_offer_active()
                ? dispatch_loan_return_offer(r, hooks(), error, team_info, player_info)
                : app::TransferActions(*session_).offer(r, hooks(), revision_, error, team_info, player_info);
        } catch (const std::exception& e) {
            fail("E_INTERNAL", std::string("transfer offer: ") + e.what());
        }
        if (!result) {
            require_engine(false, "transfer offer", error);
        }
        flush_notices();
        return *result;
    };
    auto open_offer = [this, market, market_item, show_offer, dispatch_offer](
            const std::string& reference, bool loan, int months = 24) {
        const ui::TransferMarketState m = market();
        if (loan && months != 12 && months != 24)
            fail("E_BAD_ARGUMENT", "Leihangebote verwenden 12 oder 24 Monate");
        if (loan && months == 12 && m.source_market_mode != 3)
            fail("E_RULE", "Die Laufzeit kann nur im Original-Verhandlungsmodus III geändert werden");
        const ui::TransferMarketItem item = market_item(reference);
        if (item.own_listing) fail("E_RULE", "that is your own player");
        const auto r = app::TransferActions::opening(m, item, loan);
        std::string error;
        std::optional<app::CareerSession::TransferOfferEditorDispatch> result;
        try {
            result = app::TransferActions(*session_).open_offer(r, revision_, error);
        } catch (const std::exception& e) {
            fail("E_INTERNAL", std::string("transfer offer: ") + e.what());
        }
        if (!result) {
            require_engine(false, "transfer offer", error);
        }
        offer_editor_ = result->editor;
        offer_submitted_ = false;
        open_window("Transfer.Negotiate");
        if (loan && offer_editor_->editable && offer_editor_->duration_years != months / 12) {
            auto updated = *offer_editor_;
            updated.duration_years = uint8_t(months / 12);
            const auto action = app::TransferActions::editor(updated,
                ui::OutgoingTransferRequestKind::quote_change,
                ui::OutgoingTransferField::duration_years, uint32_t(months / 12));
            offer_editor_ = dispatch_offer(action).editor;
        }
        show_offer();
    };
    add({"Transfer", "BuyPlayer", {player}, "VER-KAUFEN on a market player: the offer editor", true},
        [open_offer](const Values& v) { open_offer(v.text("player"), false); });
    add({"Transfer", "HirePlayer", {player, integer("months", 12, 24, true)}, "VER-LEIHEN on a market player: the loan offer", true},
        [open_offer](const Values& v) { open_offer(v.text("player"), true, int(v.integer("months", 24))); });
    add({"Transfer.Negotiate", "ShowNegotiation", {}, "demand, quote and your offer", false}, [show_offer](const Values&) { show_offer(); });
    auto set_field = [this, offer_request, dispatch_offer, show_offer](int field, uint32_t value) {
        if (!offer_editor_) fail("E_INTERNAL", "no offer editor");
        if (!offer_editor_->editable) fail("E_RULE", kNoBidSlot);
        // a refused value must not stay in the editor: every later request posts the whole row
        // and the engine would reject it as differing from its single field event
        const auto before = *offer_editor_;
        auto& e = *offer_editor_;
        // the screen stores the edited value, then asks for a fresh quote (LAB_0001bad7);
        // bonuses 4-7 are entered in steps of five and stored divided by five
        const auto f = static_cast<ui::OutgoingTransferField>(field);
        if (field == 0) e.fee = value;
        else if (field == 1) e.monthly_salary = value;
        else if (field == 2) e.duration_years = uint8_t(value);
        else e.bonuses[size_t(field - 3)] = uint8_t(field >= 6 ? value / 5 : value);
        try {
            auto result = dispatch_offer(offer_request(ui::OutgoingTransferRequestKind::quote_change, f, value));
            offer_editor_ = result.editor;
        } catch (...) {
            offer_editor_ = before;
            throw;
        }
    };
    Param field = choice("field", kOfferFields);
    add({"Transfer.Negotiate", "SetOfferField", {field, integer("value", 0, 100000000)}, "change one field (a fresh quote follows)", true},
        [set_field, show_offer](const Values& v) {
            set_field(v.choice("field"), uint32_t(v.integer("value")));
            show_offer();
        });
    add({"Transfer.Negotiate", "SetOffer",
         {integer("fee", 0, 100000000), integer("salary", 0, 10000000), integer("years", 1, 4), integer("bonusGoal", 0, 255),
          integer("bonusPoints", 0, 255), integer("bonusAppearance", 0, 255), integer("bonusChampion", 0, 1275),
          integer("bonus2to5", 0, 1275), integer("bonus6to10", 0, 1275), integer("bonusAvoidRelegation", 0, 1275)},
         "all ten fields", true},
        [set_field, show_offer](const Values& v) {
            const char* names[10] = {"fee", "salary", "years", "bonusGoal", "bonusPoints", "bonusAppearance", "bonusChampion",
                                     "bonus2to5", "bonus6to10", "bonusAvoidRelegation"};
            for (int i = 0; i < 10; ++i) set_field(i, uint32_t(v.integer(names[i])));
            show_offer();
        });
    // ANGEBOT; reruns itself when a poaching question was answered (see Questions)
    auto submit = std::make_shared<std::function<void()>>();
    *submit = [this, offer_request, dispatch_offer, questions, notices, self = std::weak_ptr<std::function<void()>>(submit)] {
        if (!offer_editor_) fail("E_INTERNAL", "no offer editor");
        if (!offer_editor_->editable) fail("E_RULE", kNoBidSlot);
        questions->next = 0;
        questions->pending.reset();
        std::optional<app::CareerSession::TransferOfferEditorDispatch> answered;
        try {
            answered = dispatch_offer(offer_request(ui::OutgoingTransferRequestKind::submit, ui::OutgoingTransferField::fee,
                                                    offer_editor_->fee));
        } catch (...) {
            if (!questions->pending) throw;
            notices->clear();  // the call was rolled back; its notices come again
            const auto [manager, text] = *questions->pending;
            const int previous = player_;
            player_ = manager + 1;
            ask(text, {"Yes", "No"}, "No", [this, questions, self, previous](const std::string& answer) {
                player_ = previous;
                questions->answers.push_back(answer == "Yes");
                if (const auto again = self.lock()) (*again)();
            });
            return;
        }
        questions->answers.clear();
        const auto& result = *answered;
        // after a low offer the original keeps the editor open for a better one (the engine's
        // owner stays); only a closed owner ends Transfer.Negotiate
        if (result.editor.closed) {
            offer_editor_.reset();
            close_window();
        } else {
            offer_editor_ = result.editor;
        }
        const char* status = "submitted";
        switch (result.action.status) {
        case TransferActionStatus::accepted: status = "accepted: the player moves to your club"; break;
        case TransferActionStatus::pending:
            status = "pending: the player decides in one of the next weeks";
            offer_submitted_ = true;
            break;
        case TransferActionStatus::rejected: status = "rejected"; break;
        default: break;
        }
        emit('+', std::string("offer ") + status);
        finish_loan_return_offer();
    };
    add({"Transfer.Negotiate", "SubmitOffer", {}, "ANGEBOT: hand in the offer", true}, [submit](const Values&) { (*submit)(); });
    closers_["Transfer.Negotiate"] = [this, offer_request, dispatch_offer] {
        // closing the editor (right button) ends the negotiation screen only: a bid already
        // handed in stays on the list until the player decides (Handbuch S. 60)
        if (offer_editor_) dispatch_offer(offer_request(ui::OutgoingTransferRequestKind::cancel, ui::OutgoingTransferField::fee, offer_editor_->fee));
        offer_editor_.reset();
        close_window();
        emit('+', offer_submitted_ ? "negotiation closed; your offer stands until the player decides" : "offer cancelled");
        offer_submitted_ = false;
        finish_loan_return_offer();
    };
    // TEAM-INFO and SPIELER-INFO of the offer editor (OpenTransferEditor): modal screens the engine
    // opens inside the editor event; the console prints them and closes them at once (the
    // presenters return true), after which the editor is projected again.
    add({"Transfer.Negotiate", "ShowTeamInfo", {}, "TEAM-INFO: your club's squad, as the editor shows it", false},
        [this, offer_request, dispatch_offer](const Values&) {
            if (!offer_editor_) fail("E_INTERNAL", "no offer editor");
            auto presenter = [this](app::ClubModalServices& services, int club, const Image&) {
                const ClubInfoEntry entry = services.enter_club(club);
                const ui::ClubInfoProjection t = ui::club_info_from_game(services.state(), club, entry, exe_, texts_);
                emit('i', utf8(t.original_club_name));
                std::vector<std::vector<std::string>> rows;
                for (const auto& p : t.players) {
                    if (p.player_id < 0) continue;
                    const auto& a = p.attributes;
                    rows.push_back({"id:" + std::to_string(p.player_id), utf8(p.original_name), text(kPositionNames + a.position()),
                                    std::to_string(a.age()), std::to_string(a.strength())});
                }
                emit_table({"Id", "Player", "Pos", "Age", "Str"}, rows);
                return true;
            };
            const auto result = dispatch_offer(
                offer_request(ui::OutgoingTransferRequestKind::team_info, ui::OutgoingTransferField::fee, offer_editor_->fee), presenter);
            offer_editor_ = result.editor;
        });
    add({"Transfer.Negotiate", "ShowPlayerInfo", {}, "SPIELER-INFO: the player's card with his market value", false},
        [this, offer_request, dispatch_offer](const Values&) {
            if (!offer_editor_) fail("E_INTERNAL", "no offer editor");
            auto presenter = [this](app::ClubModalServices& services, const ClubPlayerDetailSelection& selection, const Image&) {
                const ClubPlayerDetailRedraw card = services.redraw_player(selection);
                const GameState& g = services.state();
                const Player& p = g.players.at(size_t(selection.player_id));
                emit_table({"Player", "Value"},
                           {{"name", utf8(g.player_name(selection.player_id))},
                            {"position", text(kPositionNames + p.position())},
                            {"age", std::to_string(p.age())},
                            {"condition", std::to_string(p.condition())},
                            {"technique", std::to_string(p.technique())},
                            {"form", std::to_string(p.form())},
                            {"strength", std::to_string(p.strength())},
                            {"marketValue", card.market_value ? money(*card.market_value) : "-"}});
                return true;
            };
            const auto result = dispatch_offer(
                offer_request(ui::OutgoingTransferRequestKind::player_info, ui::OutgoingTransferField::fee, offer_editor_->fee), {},
                presenter);
            offer_editor_ = result.editor;
        });

    // --- Transfer.Poach: ABWERBEN (06-verein 5.6, src/game/poach.hpp) ----------------------
    add({"Transfer", "OpenPoach", {}, "ABWERBEN: pick a player of another club (Transfer.Poach)", false}, [this, market](const Values&) {
        market();
        open_window("Transfer.Poach");
        emit('+', "poach: ListPoachClubs([league]), ListClubPlayers(club), PoachPlayer(player); " +
                      std::to_string(poach_attempts_left(session_->state(), active_slot())) + " attempts left this season");
    });
    add({"Transfer.Poach", "ListPoachClubs", {integer("league", 0, kLeagueCount - 1, true)}, "the clubs of a league (default: own)", false},
        [this](const Values& v) {
            const ui::DeskState d = desk();
            const int league = v.has("league") ? int(v.integer("league")) : std::max(0, d.league);
            const ui::LeagueTableState t = ui::league_table_from_game(session_->state(), d.manager_slot, league);
            std::vector<std::vector<std::string>> rows;
            for (const auto& r : t.rows)
                if (r.runtime_team_id != d.runtime_club) rows.push_back({"club:" + std::to_string(r.runtime_team_id), r.name});
            emit('i', t.league_name);
            emit_table({"Club", "Name"}, rows);
        });
    // the squad of a club (runtime id, as ListPoachClubs prints it)
    auto club_squad = [this](const std::string& reference) {
        std::string number = lower(reference).rfind("club:", 0) == 0 ? reference.substr(5) : reference;
        Arg a;
        a.text = number;
        const auto id = to_int(a);
        if (!id || *id < 0 || *id >= 138) fail("E_BAD_ARGUMENT", "club is club:<n> (ListPoachClubs)");
        return int(*id);
    };
    Param club = detail::text("club");
    club.display = "club:Club";
    add({"Transfer.Poach", "ListClubPlayers", {club}, "the players of the club", false}, [this, club_squad](const Values& v) {
        const int team = club_squad(v.text("club"));
        const GameState& g = session_->state();
        const SquadRange range = squad_range(team);
        std::vector<std::vector<std::string>> rows;
        for (size_t s = range.first; s < range.first + range.count; ++s) {
            const int p = g.squad_slots.at(s) & 0xfff;
            if (p == 0xfff) continue;
            const Player& pl = g.players.at(size_t(p));
            rows.push_back({"id:" + std::to_string(p), utf8(g.player_name(p)), text(kPositionNames + pl.position()), std::to_string(pl.age()),
                            std::to_string(pl.strength(true))});
        }
        emit('i', utf8(g.club_name(team)));
        emit_table({"Id", "Player", "Pos", "Age", "Str"}, rows);
    });
    add({"Transfer.Poach", "PoachPlayer", {player}, "the offer to a player of another club (decided at once)", true},
        [this, show_offer](const Values& v) {
            // id:<player> of ListClubPlayers: his slot in a club squad
            const std::string reference = v.text("player");
            Arg a;
            a.text = lower(reference).rfind("id:", 0) == 0 ? reference.substr(3) : reference;
            const auto id = to_int(a);
            if (!id) fail("E_BAD_ARGUMENT", "player is id:<n> (ListClubPlayers)");
            const GameState& g = session_->state();
            int slot = -1;
            for (size_t s = 0; s < 0xb70 && slot < 0; ++s)
                if ((g.squad_slots.at(s) & 0xfff) == *id) slot = int(s);
            if (slot < 0) fail("E_BAD_ARGUMENT", "no club player id:" + std::to_string(*id));
            std::string error;
            std::optional<app::CareerSession::PoachDispatch> result;
            try {
                result = app::TransferActions(*session_).poach(active_slot(), slot, revision_, error);
            } catch (const std::exception& e) {
                fail("E_INTERNAL", std::string("poach: ") + e.what());
            }
            if (!result) {
                require_engine(false, "poach", error);
            }
            for (const auto& m : result->start.messages) emit('i', utf8(m));
            switch (result->start.status) {
            case PoachStatus::ready: break;
            case PoachStatus::invalid: fail("E_RULE", "not a player of another club");
            case PoachStatus::attempts_used: fail("E_RULE", "five poaching attempts this season");
            case PoachStatus::transfer_ban: fail("E_RULE", "transfer ban");
            default: fail("E_RULE", "the player cannot be poached");
            }
            offer_editor_ = result->editor->editor;
            offer_submitted_ = false;
            open_window("Transfer.Negotiate");
            emit('+', "asking " + std::to_string(result->start.asking_percent) + " % of his value; SubmitOffer decides at once");
            show_offer();
        });

    // the rest of the market (06-verein 5): later steps of stage 3 or engine gaps
    for (const char* name : {"SetSquadView", "SetSquadSheet", "SetMarketVisible", "ShowPlayer", "OpenOffers", "OpenSearch",
                             "ShowTransferNews", "ShowTeamInfo"})
        add({"Transfer", name, {}, "not in the console yet", false},
            [name](const Values&) { fail("E_ENGINE_GAP", std::string(name) + " is not in the console yet"); });
}

}  // namespace bmh::cli::console
