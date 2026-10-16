// SPDX-License-Identifier: GPL-3.0-only
// State(view): the engine projection behind a view as one structured record (Stufe 2.5 of
// docs/CLI_CONCEPT, "strukturierte Daten"). A graphical front end that talks to the console
// (mailbox --serve, socket --listen) reads its screens from here instead of parsing text:
// the same projections the SDL windows draw, serialised by the ui layer's *_json functions
// (src/ui), plus two small records the ui layer has no serialiser for (line-up, mailbox).
// Text mode prints the JSON after "state <view>:"; with --json it is the line's "data".
// Read-only: no projection here draws random numbers or changes the game.

#include <cstdint>
#include <string>
#include <vector>

#include "cli/console/console.hpp"
#include "cli/console/detail.hpp"
#include "cli/json.hpp"
#include "ui/advertising.hpp"
#include "ui/bank.hpp"
#include "ui/contracts.hpp"
#include "ui/desk_state.hpp"
#include "ui/league_table.hpp"
#include "ui/mailbox_state.hpp"
#include "ui/stadium.hpp"
#include "ui/toto.hpp"
#include "ui/training.hpp"
#include "ui/training_camp.hpp"
#include "ui/transfer_market.hpp"
#include "game/schedule.hpp"
#include "game/table.hpp"
#include <algorithm>

namespace bmh::cli::console {

using namespace detail;

namespace {

const std::vector<std::string> kStateViews = {"Desk",      "Table",     "Lineup", "Mailbox", "Bank",  "Market",
                                              "Advertising", "Contracts", "Stadium", "Toto",   "Training", "Camp"};

// One squad row as the line-up screen knows it (DATEN-A and DATEN-B together): the
// attributes of m3d.hpp, the place (slot flags 0x8000 pitch / 0x4000 bench), the absence
// (w364 bits 16-19 duration, bit 20 ban) and the contract fields of the board row.
Json lineup_record(const ui::LineupState& l) {
    Json::Array players;
    for (const auto& p : l.players) {
        if (p.player_id < 0) continue;
        const auto& a = p.attributes;
        const int duration = int(a.w364 >> 16 & 0xF);
        Json::Array skills;
        for (int i = 0; i < 7; ++i) skills.emplace_back(a.skill(Player::Skill(i)));
        const auto le32 = [&](size_t at) {
            return std::int64_t(uint32_t(p.board[at]) | uint32_t(p.board[at + 1]) << 8 | uint32_t(p.board[at + 2]) << 16 |
                                uint32_t(p.board[at + 3]) << 24);
        };
        players.emplace_back(Json::Object{
            {"row", p.row},
            {"id", p.player_id},
            {"name", utf8(p.name)},
            {"position", a.position()},  // 0 goalkeeper, 1 defender, 2 midfielder, 3 forward
            {"condition", a.condition()},
            {"technique", a.technique()},
            {"form", a.form()},
            {"strength", a.strength()},
            {"age", a.age()},
            {"skills", Json(std::move(skills))},  // 0-3 each: teamplay, speed, shot, pass, tackling, free kick, corner
            {"place", std::string((p.slot & 0x8000) ? "pitch" : (p.slot & 0x4000) ? "bench" : "out")},
            {"injury", duration != 0 && !(a.w364 & 0x100000) ? duration : 0},
            {"ban", duration != 0 && (a.w364 & 0x100000) ? duration : 0},
            {"games", int(a.w364 >> 26)},
            {"goals", int(a.w364 >> 2 & 63)},
            {"assists", int(a.w368 >> 18 & 63)},
            {"yellow_cards", int(a.w368 >> 14 & 15)},
            {"red_cards", int(a.w368 >> 24 & 7)},
            {"contract_months", p.board[0xc] == 255 ? Json(nullptr) : Json(int(p.board[0xc]))},
            {"months_at_club", int(p.board[0x11]) + 1},
            {"salary", le32(0x50)},
        });
    }
    return Json(Json::Object{{"manager_slot", l.manager_slot}, {"players", Json(std::move(players))}});
}

Json mailbox_record(const ui::MailboxState& m) {
    Json::Array messages;
    int index = 0;
    for (const auto& msg : m.messages)
        messages.emplace_back(Json::Object{{"index", ++index}, {"date_counter", int(msg.date_counter)}, {"text", msg.text}});
    return Json(Json::Object{{"manager_slot", m.manager_slot}, {"messages", Json(std::move(messages))}});
}

}  // namespace

// --- Advice and Scoreboard (for agents that learn: docs/CLI_CONCEPT/16-playbook.md) -------
// Advice names what needs a decision now, from the engine's own data; it judges nothing
// the player could not see on his screens, it only collects it. Scoreboard compares the
// human managers. Both read only.
void Console::register_advice_commands() {
    add({"*", "Advice", {}, "situations that need a decision now: form, squad, contracts, advertising, money", false},
        [this](const Values& v) {
            if (!career_) fail("E_NOT_IN_WINDOW", "Advice needs a running game");
            own_view_only(v.player, "Advice");
            const ui::DeskState d = desk();
            if (d.national_team) {
                emit('i', "advice: you coach the national team now; the club's matters wait until it ends");
                return;
            }
            const GameState& g = session_->state();
            const int slot = d.manager_slot;
            const int team = g.manager_teams.at(size_t(slot));
            int alerts = 0;
            auto alert = [&](const std::string& topic, const std::string& text, Json values = nullptr) {
                ++alerts;
                Json::Object o{{"topic", topic}, {"text", text}};
                if (!std::holds_alternative<std::nullptr_t>(values.value)) o["values"] = std::move(values);
                emit('i', "advice [" + topic + "] " + text, Json(std::move(o)));
            };
            // form: the points of the last three matchdays and the place (three points a win)
            const int league = team_league(team);
            if (league >= 0 && league < kLeagueCount) {
                const int rounds = int(int8_t(g.league_rounds.at(size_t(league))));
                const int pos = team - league_first_team(league);
                const ui::LeagueTableState table = ui::league_table_from_game(g, slot, league);
                int rank = 0, points = 0, games = 0;
                for (const auto& r : table.rows)
                    if (r.runtime_team_id == team) rank = r.rank, points = r.points, games = r.games;
                if (rounds >= 3) {
                    const LeagueTable recent = compute_league_table(g, league, rounds - 3, rounds, 3, 1);
                    const int recent_points = recent.points[size_t(pos)], recent_games = recent.games(pos);
                    if (recent_games >= 3 && recent_points <= 1)
                        alert("form", std::to_string(recent_points) + " points from the last " + std::to_string(recent_games) +
                                          " league matches - line-up, tactics or training need a change",
                              Json(Json::Object{{"recent_points", recent_points}, {"recent_games", recent_games}}));
                }
                const int size = int(table.rows.size());
                if (games >= 8 && size > 0 && rank > size - 4)
                    alert("table", "place " + std::to_string(rank) + " of " + std::to_string(size) + " after " +
                                       std::to_string(games) + " matches - relegation zone",
                          Json(Json::Object{{"rank", rank}, {"points", points}, {"games", games}}));
            }
            // tactics: the match engine keeps a human side's attitude (item 0) fixed for the whole
            // match unless the assistant controls it; 0 is the most defensive. New careers
            // start at 0, and human clubs then score far less than the computer clubs (the
            // original does the same: its human Bayern scored 28 goals in a season).
            {
                const ui::TacticsState t = prep().tactics;
                if (t.values[0] == 0 && t.assistant_attitude == 0)
                    alert("tactics", "attitude (tactic item 0) is 0, the most defensive, for whole matches - "
                                     "SetTactic(0, 2) (computer clubs play 2 at home, 1 away) or SetAutomation(Tactics, true)",
                          Json(Json::Object{{"attitude", 0}, {"assistant", false}}));
            }
            // squad: size, goalkeepers, absences in the eleven, weak form of starters
            const ui::LineupState l = prep().lineup;
            int players = 0, keepers = 0, fit_keepers = 0;
            std::vector<std::string> absent, weak, expiring;
            for (const auto& p : l.players) {
                if (p.player_id < 0) continue;
                const auto& a = p.attributes;
                ++players;
                const bool out = (a.w364 >> 16 & 0xF) != 0;
                if (a.position() == 0) ++keepers, fit_keepers += out ? 0 : 1;
                const bool starter = (p.slot & 0x8000) != 0;
                if (starter && out) absent.push_back(utf8(p.name));
                if (starter && !out && a.form() < 45) weak.push_back(utf8(p.name) + " (form " + std::to_string(a.form()) + ")");
                if (p.board[0xc] != 255 && p.board[0xc] <= 2)
                    expiring.push_back(utf8(p.name) + " (" + std::to_string(p.board[0xc]) + " months)");
            }
            auto list = [](const std::vector<std::string>& v) {
                std::string s;
                for (const auto& x : v) s += (s.empty() ? "" : ", ") + x;
                return s;
            };
            if (players < 18) alert("squad", std::to_string(players) + " players - too few for injuries and bans (keep 20-24)");
            if (fit_keepers < 2)
                alert("squad", std::to_string(fit_keepers) + " fit goalkeeper(s) of " + std::to_string(keepers) + " - buy or loan one");
            if (!absent.empty()) alert("lineup", "injured or banned in the starting eleven: " + list(absent));
            if (!weak.empty()) alert("lineup", "starters in weak form: " + list(weak));
            if (!expiring.empty()) alert("contracts", "contracts ending: " + list(expiring) + " - Open(Contracts), OpenNegotiation");
            // advertising: expired contracts (renewal pending) and free places
            const ui::AdvertisingState a = ui::advertising_from_game(g, slot, revision_);
            int expired = 0, free_places = 0;
            for (int s = 0; s < 16; ++s) {
                if (a.offers[size_t(s)].status == bmh::AdvertisingOfferStatus::unsupported_extension) ++expired;
                else if (!(s < 14 ? a.boards.at(size_t(s)) : s == 14 ? a.shirt : a.television).active) ++free_places;
            }
            if (expired) alert("advertising", std::to_string(expired) + " contract(s) ran out - ListAdOffers on the place renews or frees it");
            if (free_places)
                alert("advertising", std::to_string(free_places) + " free advertising place(s) - Open(Advertising), ListAdOffers, AcceptAdOffer");
            // money: overdrawn, or beyond the credit line
            const auto p = prep();
            if (p.bank) {
                const int64_t balance = p.bank->balance, credit = p.bank->credit_line;
                if (balance < -credit)
                    alert("money", "the account (" + std::to_string(balance) + ") is beyond the credit line (" + std::to_string(credit) +
                                       ") - sell, cut wages or take a credit");
                else if (balance < 0)
                    alert("money", "the account is overdrawn (" + std::to_string(balance) + "); interest is charged");
            }
            if (alerts == 0) emit('i', "advice: nothing urgent");
        });

    add({"*", "Scoreboard", {}, "all human managers: league, place, points, balance, strength of the best eleven", false},
        [this](const Values&) {
            if (!career_) fail("E_NOT_IN_WINDOW", "Scoreboard needs a running game");
            const GameState& g = session_->state();
            std::vector<std::vector<std::string>> rows;
            for (size_t m = 0; m < g.manager_teams.size(); ++m) {
                const int team = g.manager_teams[m];
                if (team < 0 || squad_range(team).count == 0) {
                    rows.push_back({std::to_string(m + 1), "(national team)", "-", "-", "-", "-", "-", "-"});
                    continue;
                }
                const int league = team_league(team);
                std::string league_name = "-", rank = "-", points = "-", games = "-";
                if (league >= 0 && league < kLeagueCount) {
                    const ui::LeagueTableState t = ui::league_table_from_game(g, int(m), league);
                    league_name = t.league_name;
                    for (const auto& r : t.rows)
                        if (r.runtime_team_id == team)
                            rank = std::to_string(r.rank), points = std::to_string(r.points), games = std::to_string(r.games);
                }
                const size_t rec = m * 0xb0c;
                const int32_t balance = int32_t(uint32_t(g.manager_records.at(rec + 0x314)) | uint32_t(g.manager_records.at(rec + 0x315)) << 8 |
                                                uint32_t(g.manager_records.at(rec + 0x316)) << 16 | uint32_t(g.manager_records.at(rec + 0x317)) << 24);
                // the strength of the best eleven (displayed strength, as the squad lists show it)
                std::vector<int> strengths;
                const SquadRange range = squad_range(team);
                for (size_t s = range.first; s < range.first + range.count; ++s) {
                    const int id = g.squad_slots.at(s) & 0x0fff;
                    if (id != 0x0fff && size_t(id) < g.players.size()) strengths.push_back(g.players[size_t(id)].strength());
                }
                std::sort(strengths.rbegin(), strengths.rend());
                int sum = 0, n = 0;
                for (; n < 11 && n < int(strengths.size()); ++n) sum += strengths[size_t(n)];
                rows.push_back({std::to_string(m + 1), utf8(g.club_name(team)), league_name, rank, points, games, std::to_string(balance),
                                n ? std::to_string(sum / n) : "-"});
            }
            emit_table({"Manager", "Club", "League", "Place", "Points", "Games", "Balance", "Best11"}, rows);
        });
}

void Console::register_state_commands() {
    add({"*", "State", {choice("view", kStateViews)}, "the full projection of a view as structured (JSON) data", false},
        [this](const Values& v) {
            if (!career_) fail("E_NOT_IN_WINDOW", "State needs a running game");
            own_view_only(v.player, "State");
            const std::string view = kStateViews[size_t(v.choice("view"))];
            const int slot = active_slot();
            Json data;
            switch (v.choice("view")) {
            case 0:
                data = parse_json(ui::desk_json(desk()));
                data.object()->insert_or_assign("engine_attached", true);  // (ui::desk_json marks it false)
                break;
            case 1: {
                const ui::DeskState d = desk();
                data = parse_json(ui::league_table_json(ui::league_table_from_game(session_->state(), slot, d.league >= 0 ? d.league : 0)));
                break;
            }
            case 2: data = lineup_record(prep().lineup); break;
            case 3: data = mailbox_record(ui::mailbox_state_from_game(session_->state(), slot, revision_)); break;
            case 4: {
                const auto p = prep();
                if (!p.bank) fail("E_MODE_LOCKED", "no bank here (national team?)");
                data = parse_json(ui::bank_state_json(*p.bank));
                break;
            }
            case 5: {
                const auto p = prep();
                if (!p.transfer_market) fail("E_MODE_LOCKED", "no transfer market here (national team?)");
                data = parse_json(ui::transfer_market_state_json(*p.transfer_market));
                break;
            }
            case 6: data = parse_json(ui::advertising_state_json(ui::advertising_from_game(session_->state(), slot, revision_))); break;
            case 7: data = parse_json(ui::contracts_state_json(ui::contracts_from_game(session_->state(), slot, revision_))); break;
            case 8: {
                const auto p = prep();
                if (!p.stadium) fail("E_MODE_LOCKED", "no stadium view (national team?)");
                data = parse_json(ui::stadium_state_json(*p.stadium));
                break;
            }
            case 9: {
                const auto p = prep();
                if (!p.toto) fail("E_MODE_LOCKED", "no Toto in this game");
                data = parse_json(ui::toto_state_json(*p.toto));
                break;
            }
            case 10: {
                const auto p = prep();
                if (!p.training) fail("E_MODE_LOCKED", "there is no training in mode I");
                data = parse_json(ui::training_json(*p.training));
                break;
            }
            default: {
                const auto p = prep();
                if (!p.training_camp) fail("E_MODE_LOCKED", "no training camp here");
                data = parse_json(ui::training_camp_json(*p.training_camp));
                break;
            }
            }
            // text mode: the record after the label; --json: the same record as the line's data
            if (options_.json) emit('i', "state " + view, std::move(data));
            else emit('i', "state " + view + ": " + dump_json(data));
        });
}

}  // namespace bmh::cli::console
