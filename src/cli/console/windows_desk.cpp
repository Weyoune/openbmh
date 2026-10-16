// SPDX-License-Identifier: GPL-3.0-only
// The desk (docs/CLI_CONCEPT/04-schreibtisch.md): its displays, Open/Key, the widgets
// (ticket price, automation), Kickoff with the day's results, and the mailbox "Kicker".

#include <algorithm>
#include <utility>

#include "app/window_actions.hpp"
#include "app/desktop_actions.hpp"
#include "cli/console/console.hpp"
#include "cli/console/detail.hpp"
#include "game/schedule.hpp"
#include "ui/advertising.hpp"
#include "ui/desk_state.hpp"
#include "ui/desktop.hpp"
#include "ui/league_table.hpp"
#include "ui/matchday_results.hpp"

namespace bmh::cli::console {

using namespace detail;

namespace {

// 02-fenster 6: menu_kind -> window
const char* const kMenuWindows[30] = {
    "LeagueTable", "Fixtures",     "Strengths", "Lineup",     "Achievements", "OtherGames", "Friendlies", "Reschedule",
    "Calendar",    "IndoorTable",  "LoadGame",  "SaveGame",   "Kickoff",      "Toto",       "Bank",       "TrainingCamp",
    "Training",    "Transfer",     "Tactics",   "Settings",   "TopScorers",   "Advertising", "Stadium",   "Statistics",
    "Environment", "FairPlay",     "HighScore", "Contracts",  "Balance",      "NewPlayer"};
constexpr int kMailboxMenu = 63;  // the mailbox shortcut (desk item kind 63)

// 02-fenster 5: the desk's keys (H2 S. 17). The training camp has no key (conflict with L).
const std::map<char, int> kKeys = {{'Z', 28}, {'T', 0},  {'S', 2},  {'X', 5},  {'K', 8},  {'G', 12}, {'B', 14}, {'I', 16},
                                   {'E', 19}, {'W', 21}, {'U', 24}, {'N', 29}, {'L', 1},  {'A', 3},  {'F', 6},  {'H', 9},
                                   {'O', 13}, {'M', 17}, {'D', 18}, {'C', 20}, {'Y', 22}, {'V', 27}};

// The ten warning lamps in the order of DeskState::warnings (Fill, 04-schreibtisch 2).
const char* const kLamps[10] = {"shares",   "overdraft", "freeAdSlots",     "specialTraining", "exhaustedPlayer",
                                "squadSize", "reserve",  "tacticsEmphasis", "contracts",       "license"};
// The five automations in the order of DeskState::automation (ManagerAutomation + manager * 5).
const std::vector<std::string> kAutomation = {"Lineup", "Substitution", "Tactics", "Marking", "Training"};

constexpr int kCompetitionNames = 579;  // CupDraw_FormatError_ExecutableField6: competitionNameDfbCup, UefaCup, ...

const char* const kWeekdays[7] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};

}  // namespace

namespace detail {

std::string menu_window(int menu) {
    if (menu >= 0 && menu < 30) return kMenuWindows[menu];
    if (menu == kMailboxMenu) return "Mailbox";
    return {};
}

int menu_of_window(std::string_view name) {
    for (int i = 0; i < 30; ++i)
        if (canonical(kMenuWindows[i]) == canonical(name)) return i;
    if (canonical(name) == "mailbox") return kMailboxMenu;
    return -1;
}

}  // namespace detail

int Console::active_slot() const { return session_->desktop_projection(revision_).manager_slot; }

void Console::own_view_only(int player, const std::string& command) const {
    if (player && player_ && player != player_)
        detail::fail("E_NOT_YOUR_TURN", command + " shows the manager on turn (player " + std::to_string(player_) +
                                            "), not yours - WaitTurn waits for your turn");
}
ui::DeskState Console::desk() const { return session_->desktop_projection(revision_); }
ui::PreparationState Console::prep() const { return session_->preparation_projection(revision_); }

// The engine says why it refused; the console passes it on (E_INSUFFICIENT_FUNDS for money).
// The engine reports refusals in two internal forms: snake_case codes of the session
// ("amount_unavailable") and, from the ported Source functions, "SubmitTransferBidSourceHumanBuyer: <reason>" for a
// case the port does not cover yet. Players and agents get readable text instead: codes
// with a known meaning get a sentence, other codes their words, and a FUN prefix marks an
// engine gap (the reason stays for the bug report, the internal name goes).
void Console::require_engine(bool ok, const std::string& what, const std::string& error) {
    if (ok) return;
    static const std::pair<const char*, const char*> kReadable[] = {
        {"invalid_amount", "the amount is not valid (1 to 99,999,999)"},
        {"amount_unavailable", "not that much money there (account balance or fixed deposit)"},
        {"insufficient_funds", "not enough money"},
        {"invalid_ticket_price", "the ticket price is out of range"},
        {"invalid_share_count", "the number of shares is not valid"},
        {"invalid_fund_count", "the number of fund shares is not valid"},
        {"investment_limit", "the investment limit is reached"},
        {"visit_limit", "no more visits possible"},
        {"career_complete", "the game is over"},
        {"toto_coupon_requires_eleven_matches", "the coupon needs a tip for all eleven matches"},
        {"free_agent_loan_refused", "a free agent (Spielerberater) can only be bought, not loaned - the original refuses it too"},
    };
    std::string text = error.empty() ? std::string("refused") : error;
    if (text.size() > 4 && text.compare(0, 3, "FUN") == 0) {
        const size_t colon = text.find(": ");
        if (colon != std::string::npos && colon < 12)
            fail("E_ENGINE_GAP", what + ": this case is not ported yet (" + text.substr(colon + 2) + ")");
    }
    bool code = !text.empty();
    for (const char c : text) code = code && ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_');
    if (code) {
        std::string readable;
        for (const auto& [key, sentence] : kReadable)
            if (text == key) readable = sentence;
        if (readable.empty()) {
            readable = text;
            std::replace(readable.begin(), readable.end(), '_', ' ');
        }
        text = readable;
    }
    if (error.find("insufficient") != std::string::npos || error.find("funds") != std::string::npos)
        fail("E_INSUFFICIENT_FUNDS", what + ": " + text);
    fail("E_RULE", what + ": " + text);
}

// The desk of the active manager: window, player, and the messages not yet reported.
void Console::enter_desktop() {
    const ui::DeskState d = desk();
    set_window("Desktop", d.manager_slot + 1);
    report_new_mail();
    // Advertising places whose contract ran out stay blocked until the manager answers the
    // renewal (CheckAdvertisingRenewal, on the click = ListAdOffers); the month-change message is easy
    // to miss, so the desk repeats it (issue #44).
    if (!d.national_team) {
        try {
            const ui::AdvertisingState a = ui::advertising_from_game(session_->state(), d.manager_slot, revision_);
            std::string expired;
            for (int slot = 0; slot < 16; ++slot)
                if (a.offers[size_t(slot)].status == bmh::AdvertisingOfferStatus::unsupported_extension)
                    expired += (expired.empty() ? "" : ", ") +
                               (slot < 14 ? "Board" + std::to_string(slot + 1) : std::string(slot == 14 ? "Shirt" : "Tv"));
            if (!expired.empty())
                emit('i', "advertising contracts ran out: " + expired + " (Open(Advertising), ListAdOffers(<slot>) renews or frees them)");
        } catch (const std::exception&) {
        }
    }
}

// New messages show as "i" lines on the desk of the manager they belong to (04-schreibtisch 3).
// The mailbox has no read flag; the console remembers what it has shown per manager.
void Console::report_new_mail() {
    const int slot = active_slot();
    const ui::MailboxState mail = ui::mailbox_state_from_game(session_->state(), slot, revision_);
    auto& seen = seen_mail_[slot];
    for (const auto& m : mail.messages) {
        const std::string key = std::to_string(m.date_counter) + "|" + m.text;
        if (seen.insert(key).second) emit('i', "New message: " + m.text);
    }
}

void Console::register_desk_commands() {
    // --- Desktop: displays (04-schreibtisch 1) ---------------------------------------------
    auto show_date = [this] {
        const ui::DeskState d = desk();
        std::vector<std::vector<std::string>> rows = {
            {"date", date_text(d.date.day, d.date.month, d.date.year)},
            {"weekday", d.date.weekday >= 0 && d.date.weekday < 7 ? kWeekdays[d.date.weekday] : "?"},
            {"season", std::to_string(d.season_index + 1)},
            {"seasonDay", std::to_string(d.season_day)}};
        if (d.league_round) rows.push_back({"leagueRound", std::to_string(*d.league_round)});
        emit_table({"Date", "Value"}, rows);
    };
    auto show_finance = [this] {
        const ui::DeskState d = desk();
        emit_table({"Finance", "Value"},
                   {{"balance", money(d.balance)}, {"savings", money(d.savings)}, {"ticketPrice", std::to_string(d.ticket_price)}});
    };
    auto show_fans = [this] { emit_table({"Fans", "Value"}, {{"standing", std::to_string(desk().fans)}}); };
    auto show_lamps = [this] {
        const ui::DeskState d = desk();
        std::vector<std::vector<std::string>> rows;
        for (size_t i = 0; i < 10; ++i)
            rows.push_back({std::to_string(i / 5 + 1) + "." + std::to_string(i % 5 + 1), kLamps[i],
                            d.warnings[i] ? on_off(*d.warnings[i]) : "unknown"});
        emit_table({"Lamp", "Name", "State"}, rows);
    };
    auto show_automation = [this] {
        const ui::DeskState d = desk();
        std::vector<std::vector<std::string>> rows;
        for (size_t i = 0; i < 5; ++i) {
            // Lineup holds the number of the saved formation used (0 = off, SizeT6);
            // the others are bit 0 of their option byte
            std::string state = i == 0 ? (d.automation[0] ? "on (formation " + std::to_string(d.automation[0]) + ")" : "off")
                                       : on_off(d.automation[i] & 1);
            rows.push_back({kAutomation[i], state});
        }
        emit_table({"Automation", "State"}, rows);
    };
    auto show_opponent = [this] {
        const ui::DeskState d = desk();
        std::vector<std::vector<std::string>> rows;
        rows.push_back({"competition", utf8(d.next_competition_label) +
                                           (d.next_competition_detail.empty() ? "" : " " + utf8(d.next_competition_detail))});
        rows.push_back({"opponent", d.next_opponent_name.empty() ? "-" : utf8(d.next_opponent_name)});
        if (!d.next_opponent_name.empty()) rows.push_back({"venue", d.next_match_away ? "away" : "home"});
        if (d.table_position) rows.push_back({"ownTablePosition", std::to_string(*d.table_position)});
        if (d.strength) rows.push_back({"ownStrength", std::to_string(*d.strength)});
        emit_table({"Next game", "Value"}, rows);
    };
    add({"Desktop", "ShowDesk", {}, "all displays of the desk", false},
        [this, show_date, show_finance, show_fans, show_lamps, show_automation, show_opponent](const Values&) {
            const ui::DeskState d = desk();
            emit_table({"Manager", "Club"}, {{utf8(d.manager_name), utf8(d.club_name)}});
            show_date();
            show_finance();
            show_fans();
            show_opponent();
            show_lamps();
            show_automation();
            const auto mail = ui::mailbox_state_from_game(session_->state(), d.manager_slot, revision_);
            emit_table({"Mailbox", "Value"}, {{"messages", std::to_string(mail.messages.size())}});
        });
    add({"Desktop", "ShowDate", {}, "date, weekday, season day, league round", false}, [show_date](const Values&) { show_date(); });
    add({"Desktop", "ShowFinance", {}, "balance, savings, ticket price", false}, [show_finance](const Values&) { show_finance(); });
    add({"Desktop", "ShowFanStanding", {}, "standing with the fans", false}, [show_fans](const Values&) { show_fans(); });
    add({"Desktop", "ShowLamps", {}, "the ten warning lamps", false}, [show_lamps](const Values&) { show_lamps(); });
    add({"Desktop", "ShowLastGames", {}, "the last six games", false}, [](const Values&) {
        fail("E_ENGINE_GAP", "the last games are not in the desk projection yet");
    });
    add({"Desktop", "ShowOpponent", {}, "the next game and opponent", false}, [show_opponent](const Values&) { show_opponent(); });
    Param curves = detail::text("which", 0, true);
    curves.display = "[own|opponent]";
    add({"Desktop", "ShowPositionCurves", {curves}, "table positions per matchday", false}, [](const Values&) {
        fail("E_ENGINE_GAP", "the position curves are not in the desk projection yet");
    });
    add({"Desktop", "ShowAutomation", {}, "the five automations", false}, [show_automation](const Values&) { show_automation(); });

    // --- Desktop: actions ------------------------------------------------------------------
    Param target = detail::text("window");
    target.display = "window:enum|menu:int";
    add({"Desktop", "Open", {target}, "open a window (click on its icon); see the lines below", false}, [this](const Values& v) {
        Arg a;
        a.text = v.text("window");
        int menu = menu_of_window(a.text);
        if (menu < 0)
            if (const auto n = to_int(a); n && (*n >= 0 && *n < 30)) menu = int(*n);
        if (menu < 0) fail("E_BAD_ARGUMENT", "no window " + a.text + " on the desk (ListOptions shows them)");
        open_menu(menu);
    });
    add({"Desktop", "Key", {choice("letter", {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P",
                                              "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"})},
         "a key on the desk (G = Kickoff)", false},
        [this](const Values& v) {
            const char letter = char('A' + v.choice("letter"));
            const auto it = kKeys.find(letter);
            if (it == kKeys.end()) fail("E_BAD_ARGUMENT", std::string("the key ") + letter + " has no function on the desk");
            open_menu(it->second);
        });
    // The desk settings without the drawer layout (CareerSession::dispatch_ticket_price /
    // dispatch_automation, issue #11): the same writes as the open widgets.
    auto setting_owner = [this] {
        const ui::DeskState d = desk();
        app::DeskSettingIdentity id;
        id.manager_slot = d.manager_slot;
        id.runtime_club = d.runtime_club;
        if (d.runtime_club >= 0 && size_t(d.runtime_club) < session_->state().club_ids.size())
            id.stable_club_identity = int(session_->state().club_ids[size_t(d.runtime_club)]);
        id.revision = revision_;
        return id;
    };
    add({"Desktop", "SetTicketPrice", {integer("price", 8, 25)}, "the ticket price (price scale)", true}, [this](const Values& v) {
        std::string error;
        require_engine(app::DeskActions(*session_).ticket_price(desk(), int(v.integer("price")), revision_, error), "ticket price", error);
        emit('+', "ticket price: " + std::to_string(desk().ticket_price));
    });
    Param formation = integer("formation", 1, 8, true);
    add({"Desktop", "SetAutomation", {choice("feature", kAutomation), boolean("on"), formation},
         "switch one of the five automations (Lineup: a saved formation 1-8)", true},
        [this, setting_owner](const Values& v) {
            const int feature = v.choice("feature");
            const bool on = v.boolean("on");
            app::AutomationRequest r;
            r.identity = setting_owner();
            r.automation = static_cast<app::DeskAutomation>(feature);
            r.enabled = on;
            if (feature == 0 && on) {
                // the line-up automation uses a saved formation: the given one, or the first available
                int slot = int(v.integer("formation", 0));
                if (!slot) {
                    const auto p = prep();
                    if (p.pitch)
                        for (int f = 0; f < 8 && !slot; ++f)
                            if (p.pitch->formation_mask & (1 << f)) slot = f + 1;
                }
                if (!slot) fail("E_RULE", "no saved formation for the lineup automation");
                r.formation = slot;
            } else if (v.has("formation")) {
                fail("E_BAD_ARGUMENT", "only Lineup on takes a formation");
            }
            std::string error;
            require_engine(session_->dispatch_automation(r, revision_, error), "automation", error);
            emit('+', kAutomation[size_t(feature)] + " " + on_off(on) + (r.formation ? " (formation " + std::to_string(*r.formation) + ")" : ""));
        });
    add({"Desktop", "Kickoff", {}, "hand over to the next player or start the day", true}, [this](const Values&) { kickoff(); });
    add({"Desktop", "OpenMailbox", {}, "open the mailbox (Kicker)", false}, [this](const Values&) { open_menu(kMailboxMenu); });
    add({"Desktop", "OpenDrawer", {}, "open the menu bar", false}, [](const Values&) {
        fail("E_ENGINE_GAP", "the desk layout (drawer, folders) is not ported yet");
    });
    add({"Desktop", "PullPlug", {}, "the plug: quit after a question", false}, [this](const Values&) {
        ask("Quit the game? Unsaved progress is lost.", {"Yes", "No"}, "No", [this](const std::string& answer) {
            if (answer == "Yes") {
                ended_ = true;
                emit('+', "session ended");
            } else {
                emit('+', "quit cancelled");
            }
        });
    });

    // --- Mailbox (04-schreibtisch 3) -------------------------------------------------------
    add({"Mailbox", "ListMessages", {}, "the messages of this manager (msg numbers for Keep/Delete)", false}, [this](const Values&) {
        const ui::MailboxState mail = ui::mailbox_state_from_game(session_->state(), active_slot(), revision_);
        listed_mail_ = mail.messages;
        std::vector<std::vector<std::string>> rows;
        for (size_t i = 0; i < mail.messages.size(); ++i) {
            const auto& m = mail.messages[i];
            std::string to = m.general_broadcast ? "all" : "";
            if (!m.general_broadcast)
                for (int r : m.recipient_managers) to += (to.empty() ? "P" : ",P") + std::to_string(r + 1);
            rows.push_back({std::to_string(i + 1), std::to_string(m.date_counter), m.text, m.kept_for_manager ? "yes" : "no", to});
        }
        emit_table({"Msg", "Day", "Text", "Kept", "To"}, rows);
    });
    auto mail_action = [this](const Values& v, ui::MailboxActionKind kind) {
        const size_t n = size_t(v.integer("msg"));
        if (n < 1 || n > listed_mail_.size()) fail("E_BAD_ARGUMENT", "msg numbers refer to the last ListMessages");
        const auto& m = listed_mail_[n - 1];
        const auto req = app::MailboxActions::request(active_slot(), revision_, kind, m);
        std::string error;
        if (!app::MailboxActions(*session_).dispatch(req, revision_, error)) fail("E_RULE", "mailbox: " + error);
        listed_mail_.clear();  // entries are compacted; numbers are no longer valid
    };
    add({"Mailbox", "KeepMessage", {integer("msg", 1, 1000)}, "keep a message / let it go (at most five)", true},
        [this, mail_action](const Values& v) {
            mail_action(v, ui::MailboxActionKind::toggle_keep);
            emit('+', "keep toggled (list again for new numbers)");
        });
    add({"Mailbox", "DeleteMessage", {integer("msg", 1, 1000)}, "throw a message away", true}, [this, mail_action](const Values& v) {
        mail_action(v, ui::MailboxActionKind::delete_for_manager);
        emit('+', "message deleted (list again for new numbers)");
    });
}

// A click on a desk icon. Mode-locked icons answer E_MODE_LOCKED (02-fenster 4); windows the
// console does not have yet answer E_ENGINE_GAP.
void Console::open_menu(int menu) {
    const ui::DeskState d = desk();
    if (menu == 12) {
        kickoff();
        return;
    }
    const bool shortcut = menu == kMailboxMenu && d.mailbox_shortcut;
    const auto item = std::find_if(d.items.begin(), d.items.end(), [menu](const ui::DeskItem& i) { return i.kind == menu; });
    // An icon is dimmed by the game mode, at the national coach's desk, or for the moment
    // (HasNoEquivalentCondition's button states); name the reason that applies.
    if (!shortcut && (item == d.items.end() || item->disabled))
        fail("E_MODE_LOCKED", menu_window(menu) +
                                  (d.national_team ? " is not available while you coach the national team"
                                   : item == d.items.end()
                                       ? " is not available in this game (mode " + std::to_string(d.options.mode) + ")"
                                       : " is not available at the moment"));
    switch (menu) {
    case 0:
        open_window("LeagueTable");
        table_league_ = -1;
        show_league_table();
        return;
    case 1:
        open_fixtures();
        return;
    case 10:
    case 11:
        browse_directory_ = std::filesystem::absolute(save_directory()).lexically_normal();
        selected_file_.clear();
        open_window(menu == 10 ? "LoadGame" : "SaveGame");
        emit('+', "directory: " + shown_directory());
        return;
    case kMailboxMenu:
        open_window("Mailbox");
        listed_mail_.clear();
        emit('+', "mailbox opened (ListMessages)");
        return;
    default:
        if (const auto it = openers_.find(menu); it != openers_.end()) {
            it->second();
            return;
        }
        fail("E_ENGINE_GAP", menu_window(menu) + " is not in the console yet (stage 2/3 of docs/CLI_CONCEPT)");
    }
}

// The calendar (key G): the active manager hands over; after the last one the day runs
// (04-schreibtisch 4). Time mode "instant": the matches run without intervention and the
// results are printed afterwards (Info.FinalResults, 07-spieltag).
void Console::kickoff() {
    const ui::DeskState before = desk();
    ui::DeskRequest req;
    req.kind = ui::DeskRequestKind::advance_turn;
    req.manager_slot = before.manager_slot;
    req.revision = revision_;
    // The day runs on the engine thread (windows_match.cpp): a human match pauses it in the
    // Match window (time mode stepped), a question of the engine (offers for own players)
    // pauses it in any mode; finish_kickoff follows when the engine is done.
    start_matchday(req, before);
}

// After the engine finished a kickoff: the info windows, the day's results and the next desk.
void Console::finish_kickoff(const ui::DeskState& before, const TurnAdvanceResult& advanced, const std::string& error) {
    if (advanced.kind == TurnDeskKind::Failed || advanced.kind == TurnDeskKind::WaitingAtDesk) {
        pending_info_.clear();
        fail("E_INTERNAL", "kickoff failed: " + (error.empty() ? advanced.reason : error));
    }
    flush_info();
    if (const auto& results = session_->completed_matchday_results(); results && results->valid && !results->groups.empty()) {
        set_window("Info.FinalResults", 0);
        emit('i', "Results " + results->date_label);
        for (const auto& group : results->groups) {
            // the group's own competition: a league by its table title, a cup by the TEXTD
            // competition names CupDraw_FormatError_ExecutableField6 (competitionNameDfbCup ..), as the cup draw does
            std::string name = group.competition;
            if (group.league_id)
                name = ui::league_table_from_game(session_->state(), active_slot(), *group.league_id).league_name;
            // (the indoor tournament, 0xb, has a runtime name: TEXTD only holds the placeholder
            // "...xxx"; the projection already carries the name frozen for these results)
            else if (group.cup_id && *group.cup_id >= 0 && *group.cup_id < 13 && (*group.cup_id != 0xb || bmh::kEnglishCompetitions))
                name = text(kCompetitionNames + *group.cup_id);
            if (group.matches.empty()) continue;
            emit('i', name + ":");
            std::vector<std::vector<std::string>> rows;
            for (const auto& m : group.matches) {
                std::string result;
                switch (m.status) {
                case ui::MatchResultStatus::played:
                    result = (m.home_goals ? std::to_string(*m.home_goals) : "?") + ":" + (m.away_goals ? std::to_string(*m.away_goals) : "?");
                    // a cup match beyond 90 minutes; on penalties the score holds the
                    // shoot-out goals too (as the original stores it)
                    if (m.decided == 1) result += " aet";
                    if (m.decided == 2) result += " pen (incl. shoot-out)";
                    break;
                case ui::MatchResultStatus::postponed: result = "postponed"; break;
                case ui::MatchResultStatus::not_played: result = "not played"; break;
                case ui::MatchResultStatus::unknown: result = "?"; break;
                }
                rows.push_back({m.home, m.away, result});
            }
            emit_table({"Home", "Away", "Result"}, rows);
        }
    }
    if (advanced.kind == TurnDeskKind::CareerComplete) {
        set_window("GameEnd", 0);
        emit('+', "Game over (" + std::to_string(session_->state().season) + " seasons)");
        career_ = false;
        return;
    }
    const ui::DeskState after = desk();
    set_window("Desktop", after.manager_slot + 1);
    if (after.day_counter == before.day_counter && after.manager_slot != before.manager_slot)
        emit('+', "Next: Player " + std::to_string(after.manager_slot + 1));
    else
        emit('+', std::string(advanced.kind == TurnDeskKind::SeasonEndDesk ? "Season end: " : "Next day: ") +
                      date_text(after.date.day, after.date.month, after.date.year));
    report_new_mail();
}

}  // namespace bmh::cli::console
