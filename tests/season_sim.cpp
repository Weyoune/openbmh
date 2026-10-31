// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
// season_sim - plays a new career headless through the turn driver and checks that the
// results stay plausible (the documented simulation metrics).
//
// A career is started like the UI does it (CareerSession: options, one manager, first
// desk); then the manager presses "Anstoss" at every desk until the given number of turns
// is played, the season ends or the engine reports a gap. After every turn the state is
// checked against simple invariants; violations and engine gaps are listed at the end.
//
// Usage: season_sim <BMH data dir> [turns=60] [master club=0] [watcom seed=1] [strength=2]
// Exit code: 0 no violation, 1 violations found, 2 the run stopped early (engine gap).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

#include "app/career_session.hpp"
#include "game/desk_actions.hpp"
#include "game/save.hpp"
#include "game/lineup.hpp"
#include "game/matchday.hpp"
#include "game/month.hpp"
#include "game/schedule.hpp"
#include "game/table.hpp"
#include "ui/desktop.hpp"

using namespace bmh;

namespace {

struct Checks {
    std::map<std::string, int> counts;  // violation kind -> how often
    std::map<std::string, std::string> first;  // violation kind -> first occurrence
    void fail(const std::string& kind, const std::string& where) {
        if (counts[kind]++ == 0) first[kind] = where;
    }
};

int32_t money(const GameState& g, int m) {
    const size_t at = size_t(m) * 0xb0c + 0x314;
    return int32_t(g.manager_records.at(at) | g.manager_records.at(at + 1) << 8 | g.manager_records.at(at + 2) << 16 |
                   uint32_t(g.manager_records.at(at + 3)) << 24);
}

// The invariants of PROJEKTZIEL.md that can be read from the state alone.
void check_state(const GameState& g, const std::string& when, Checks& c) {
    // players: plausible ages and values, nobody in two squad slots
    std::set<int> seen;
    // (only the clubs' squads up to format field = 0xb70: the national squad and the other
    //  lists behind them hold club players a second time)
    for (size_t s = 0; s < g.squad_slots.size() && s < size_t(runtime_dimensions.pool_first); s++) {
        const int id = g.squad_slots[s] & 0xfff;
        if (id == 0xfff) continue;
        if (!seen.insert(id).second) c.fail("player in two squad slots", when + " player " + std::to_string(id));
        const Player& p = g.players.at(size_t(id));
        if (p.age() < 16 || p.age() > 40) c.fail("age outside 16-40", when + " player " + std::to_string(id) + " age " + std::to_string(p.age()));
        if (p.technique() > 99 || p.condition() > 99) c.fail("technique/condition above 99", when + " player " + std::to_string(id));
    }
    // the human managers: money within a sane range, a squad with at least 11 players
    for (size_t m = 0; m < g.manager_teams.size(); m++) {
        const int32_t cash = money(g, int(m));
        if (cash < -50000000 || cash > 500000000) c.fail("manager money out of range", when + " " + std::to_string(cash));
        const SquadRange r = squad_range(g.manager_teams[m]);
        int n = 0;
        for (size_t s = r.first; s < r.first + r.count; s++)
            if ((g.squad_slots.at(s) & 0xfff) != 0xfff) n++;
        if (n < 11) c.fail("human squad below 11 players", when + " " + std::to_string(n));
    }
}

// Optional private investigation: snapshots use their own save-envelope RNG;
// observing a failure never consumes either gameplay stream.
bool capture_duplicate(const GameState& before, const GameState& after,
                       const std::filesystem::path& directory) {
    std::set<int> seen;
    int duplicate = -1;
    for (size_t slot = 0; slot < size_t(runtime_dimensions.pool_first) && slot < after.squad_slots.size(); ++slot) {
        const int id = after.squad_slots[slot] & 0xfff;
        if (id != 0xfff && !seen.insert(id).second) { duplicate = id; break; }
    }
    if (duplicate < 0) return false;
    std::filesystem::create_directories(directory);
    WatcomRandom envelope(1);
    write_savegame(directory / "before_duplicate.FBM", make_savegame(before, envelope));
    write_savegame(directory / "after_duplicate.FBM", make_savegame(after, envelope));
    std::ofstream report(directory / "duplicate.txt");
    report << "player=" << duplicate << " before_day=" << before.date << " after_day=" << after.date << '\n';
    for (const auto* state : {&before, &after}) {
        report << "day=" << state->date << " age=" << state->players.at(size_t(duplicate)).age() << '\n';
        for (size_t slot = 0; slot < state->squad_slots.size(); ++slot)
            if ((state->squad_slots[slot] & 0xfff) == duplicate)
                report << "slot=" << slot << " team=" << slot_team(int(slot)) << " flags=" << (state->squad_slots[slot] & 0xf000) << '\n';
        for (uint32_t row = 0; row < 200; ++row) {
            const auto value = state->memory.u32(layout::Career_RemoveJoinLoans_RuntimeField + row * 4);
            if (value && (value & 0xfff) == uint32_t(duplicate))
                report << "loan_row=" << row << " raw=" << value << '\n';
        }
    }
    return true;
}

// The matchday just played. Cup phase 2 stores the shoot-out goals added to the
// match score (legacy routine). The total-12 plausibility check cannot apply to that
// combined display score: Original week40_capture.jsonl:241 records 11:10.
// Keep high ordinary scores fatal; retain shoot-out outliers as visible diagnostics.
// Evidence: external reference input
int g_high_scores = 0;  // matches with 13-15 goals (outside shoot-outs)
// the human club against all clubs, league matches only (learning session: human clubs
// scored about 0.7 goals per match, the computer clubs about 1.25)
int g_human_team = -1, g_human_for = 0, g_human_against = 0, g_human_matches = 0;
int g_league_goals = 0, g_league_matches = 0;

// Read only the current season's first-league results. A complete German Bundesliga
// season has 34 rounds x 9 fixtures = 306 matches. Unlike the historical snapshot
// counters, this includes background fixtures played while seeking the next desk.
void print_first_league_results(const GameState& g, const char* checkpoint) {
    int goals = 0, matches = 0;
    for (int round = 1; round <= league_matchdays(0); ++round)
        for (int match = 0; match < kLeagueSizes[0] / 2; ++match) {
            const Result r = decode_result(g.results.at(result_index(0, round, match)));
            if (!r.played) continue;
            goals += r.home_goals + r.away_goals;
            ++matches;
        }
    std::printf("league 0 raw current-season results (%s, day %u): %d matches, %d goals, %.3f goals per match\n",
                checkpoint, g.date, matches, goals, matches ? double(goals) / matches : 0.0);
}

void check_results(const TurnResultSnapshot& r, const std::string& when, Checks& c, int& goals, int& matches) {
    if (!r.valid) return;
    for (const MatchGroup& grp : r.groups)
        for (int m = 0; m < grp.matches(); m++) {
            int home = 0, away = 0;
            if (!grp.results.empty()) home = grp.results.at(size_t(m)) >> 4, away = grp.results.at(size_t(m)) & 0xf;
            else if (grp.cup_results.size() == size_t(grp.matches()))
                home = grp.cup_results[size_t(m)] >> 5 & 0x1f, away = grp.cup_results[size_t(m)] & 0x1f;
            // (0xc on a side marks a match not played yet or postponed)
            if (home >= 0xc || away >= 0xc) continue;
            if (home + away > 12) {
                const bool shootout = grp.results.empty() && grp.cup_results.size() == size_t(grp.matches()) &&
                    (grp.flags() & 8) != 0 && (grp.cup_results[size_t(m)] >> 10) == 2;
                const std::string score = when + " " + std::to_string(home) + ":" + std::to_string(away) + " type " +
                    std::to_string(grp.desc.at(0)) + " teams " + std::to_string(grp.fixtures.at(size_t(m) * 2)) + "-" +
                    std::to_string(grp.fixtures.at(size_t(m) * 2 + 1));
                // The original engine has no goal cap (#46: no limit of twelve in the source);
                // a much stronger side wins 9:4 now and then (#57: ratings 78-87 against 55-63,
                // the match replays minute by minute). A single high score is a note; what
                // fails is a score no match should reach, or high scores becoming frequent
                // (checked at the end of the run).
                if (shootout) {
                    std::printf("SCORE NOTE includes shoot-out goals: %s\n", score.c_str());
                } else if (home + away > 15) {
                    c.fail("more than 15 goals, group type " + std::to_string(grp.desc.at(0)), score);
                } else {
                    std::printf("SCORE NOTE more than 12 goals: %s\n", score.c_str());
                    ++g_high_scores;
                }
            }
            if (!grp.results.empty()) {
                g_league_goals += home + away, ++g_league_matches;
                const int a = grp.fixtures.at(size_t(m) * 2), b = grp.fixtures.at(size_t(m) * 2 + 1);
                if (a == g_human_team) g_human_for += home, g_human_against += away, ++g_human_matches;
                if (b == g_human_team) g_human_for += away, g_human_against += home, ++g_human_matches;
            }
            goals += home + away;
            matches++;
        }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: season_sim <BMH data dir> [turns] [master club] [watcom seed] [strength]\n");
        return 2;
    }
    const int turns = argc > 2 ? std::atoi(argv[2]) : 60;
    Checks checks;
    std::string gap;
    int played = 0, goals = 0, matches = 0, seasons = 0;
    const char* duplicate_directory = std::getenv("SIM_DUPLICATE_DIR");
    bool duplicate_captured = false;
    try {
        app::CareerSession session(argv[1], argc > 4 ? uint32_t(std::stoul(argv[4])) : 1u);
        ui::SetupOptions options;  // 5 seasons, level 2, one manager, mode III, league and world
        ui::ManagerChoice choice;
        choice.name = "SIM";
        choice.master_club = argc > 3 ? std::atoi(argv[3]) : 0;
        choice.strength = argc > 5 ? std::atoi(argv[5]) : 2;
        std::string error;
        if (!session.confirm_manager(options, choice, error)) throw std::runtime_error("manager: " + error);
        ui::CareerInitializationResult init;
        if (!session.finish(init, error)) throw std::runtime_error("career start: " + error);
        uint64_t revision = init.desktop.revision;
        int contract_expiry_decisions = 0;
        int contract_expiry_renewals = 0;
        int contract_expiry_rejections = 0;
        int contract_expiry_declines = 0;
        int contract_expiry_errors = 0;
        // The removed assistant default fabricated a salary, a 24-month term, and
        // an unrelated Watcom stream. Keep long simulator runs useful only when
        // explicitly requested: model a manager who checks real demands using
        // Session W and offers exactly the demand for two years when available,
        // otherwise the first valid term within the UI's 200000 salary limit.
        // The real Source offer formula decides acceptance; refusal releases (0).
        // No board fields are written by this policy. Without SIM_CONTRACTS=1 the
        // simulator installs no presenter; an unhandled expiry remains a real gap.
        const char* sim_contracts_env = std::getenv("SIM_CONTRACTS");
        if (sim_contracts_env && std::string(sim_contracts_env) == "1") {
            session.set_contract_expiry_presenter([&](GameState& state, WatcomRandom& random,
                                                      int manager, int row) -> int {
                ++contract_expiry_decisions;
                try {
                    const auto demands = contract_demands(state, manager, row, false, random);
                    const auto valid_term = [&](int i) {
                        const uint32_t demand = demands[size_t(i)];
                        return demand != 0 && demand != UINT32_MAX && demand <= 200000;
                    };
                    int term = valid_term(1) ? 1 : -1; // original two-year column, when admissible
                    if (term < 0)
                        for (int i = 0; i < int(demands.size()); ++i)
                            if (valid_term(i)) { term = i; break; }
                    if (term < 0) {
                        ++contract_expiry_declines;
                        std::printf("contract expiry policy: manager=%d row=%d no valid demand; release=0\n",
                                    manager, row);
                        return 0; // no valid Source demand: deliberately release
                    }
                    const uint32_t demand = demands[size_t(term)];
                    const uint32_t offer = demand;
                    const bool accepted = contract_offer(state, manager, row, term, offer,
                                                         demand, false, true, random, nullptr);
                    std::printf("contract expiry policy: manager=%d row=%d term=%d demand=%u offer=%u accepted=%d\n",
                                manager, row, term + 1, demand, offer, accepted ? 1 : 0);
                    if (accepted) {
                        ++contract_expiry_renewals;
                        return 1;
                    }
                    ++contract_expiry_rejections;
                    return 0; // legacy routine's explicit refusal/release answer
                } catch (const std::exception&) {
                    ++contract_expiry_errors;
                    return -2; // Preserve the genuine unsupported/abort path.
                }
            });
        }
        // SIM_INTERVENE=1: in every match of the human club the manager substitutes once at
        // minute 30 (the last field player for the first bench player) and the side is rated
        // again, as after the line-up screen opened from the match
        int interventions = 0;
        if (std::getenv("SIM_INTERVENE"))
            session.set_live_minute([&](int minute, MatchEngine& engine, std::vector<MatchGroup>& groups, GameState& gs) {
                static int calls = 0;
                if (std::getenv("SIM_DEBUG") && calls++ < 3) std::fprintf(stderr, "live_minute %d\n", minute);
                if (minute != 30) return;
                const int team = gs.manager_teams.at(0);
                for (const MatchGroup& grp : groups)
                    for (int m = 0; m < grp.matches(); m++)
                        if (grp.fixtures.at(size_t(m) * 2) == team || grp.fixtures.at(size_t(m) * 2 + 1) == team) {
                            if (!manual_substitution(gs, team, 10, 11)) {
                                if (std::getenv("SIM_DEBUG")) {
                                    const SquadRange sr = squad_range(team);
                                    std::fprintf(stderr, "substitution refused: slot10 %04x slot11 %04x flags %04x\n", gs.squad_slots.at(sr.first + 10),
                                                 gs.squad_slots.at(sr.first + 11), gs.team_flags.at(team));
                                }
                                return;
                            }
                            MinuteReport rr;
                            engine.rerate_team(team, rr);
                            if (rr.unsupported) checks.fail("intervention: " + rr.reason, "minute 30");
                            interventions++;
                            return;
                        }
            });
        // The simulator is the engine-side automation harness; the app exposes this
        // read-only, so this test borrows the non-const underlying state for desk actions.
        GameState& g = const_cast<GameState&>(session.state());
        std::printf("career started: team %d, date %u, money %d\n", g.manager_teams.at(0), g.date, money(g, 0));
        check_state(g, "start", checks);
        bool listed_for_sale = false;
        int ad_contracts = 0;  // SIM_ADVERTISING: contracts accepted
        for (; played < turns; played++) {
            // SIM_ADVERTISING=1: the manager keeps his advertising places filled, as every
            // player does (the largest income besides the gate): an expired contract is renewed
            // when the sponsor offers it (legacy routine), a free place takes the best acceptable
            // offer (TV, shirt, then the boards). Without it a long run lives on gate money
            // alone and its debts grow without bound (issue #95).
            if (const char* ads = std::getenv("SIM_ADVERTISING"); ads && std::string(ads) == "1") {
                for (int slot = 15; slot >= 0; --slot) {
                    AdvertisingOfferList list = advertising_offers(g, 0, slot);
                    if (list.status == AdvertisingOfferStatus::unsupported_extension) {
                        const auto renewal = check_advertising_renewal(g, 0, slot, session.rand());
                        if (renewal.status == AdvertisingRenewal::Status::offered)
                            answer_advertising_renewal(g, 0, renewal.contract_slot, true);
                        list = advertising_offers(g, 0, slot);
                    }
                    if (list.status != AdvertisingOfferStatus::available) continue;
                    const AdvertisingOfferCandidate* best = nullptr;
                    for (const auto& o : list.offers)
                        if (o.can_accept && (!best || o.monthly_amount > best->monthly_amount)) best = &o;
                    if (!best) continue;
                    AdvertisingOfferRequest request;
                    request.manager = 0;
                    request.display_slot = slot;
                    request.contract_slot = list.contract_slot;
                    request.expected_contract = list.contract_bytes;
                    request.raw_slot = best->raw_slot;
                    request.expected_offer = best->bytes;
                    if (accept_advertising_offer(g, request).status == AdvertisingOfferStatus::accepted) ++ad_contracts;
                }
            }
            if (std::getenv("SIM_TRANSFERS")) {
                MinuteReport transfer_report;
                WatcomRandom& transfer_rand = session.rand();
                if (!listed_for_sale) {
                    const SquadRange squad = squad_range(g.manager_teams.at(0));
                    for (int row = int(squad.count) - 1; row >= 11; --row) {
                        if ((g.squad_slots.at(squad.first + size_t(row)) & 0xfff) == 0xfff) continue;
                        const auto result = list_manager_player(g, 0, row, transfer_rand, transfer_report);
                        if (result.status == TransferActionStatus::accepted) listed_for_sale = true;
                        break;
                    }
                    if (transfer_report.unsupported) checks.fail("transfer listing", transfer_report.reason);
                }
                for (int entry = 0; entry < 40; ++entry) {
                    const int player = g.squad_slots.at(0xb70u + size_t(entry)) & 0xfff;
                    const uint32_t at = ::bmh::layout::EditorListUnsigned + uint32_t(entry) * 0x58;
                    if (player == 0xfff || int16_t(g.memory.u16(at)) != -1) continue;
                    const uint32_t fee = g.memory.u32(at + 0x2c), salary = g.memory.u32(at + 0x30);
                    if (fee == 0 || salary == 0) continue;
                    TransferBidRequest bid;
                    bid.manager = 0; bid.list_entry = entry; bid.player = player; bid.fee = int32_t(uint64_t(fee) * 130 / 100);
                    bid.annual_salary = int32_t(uint64_t(salary) * 120 / 100); bid.contract_years = 2;
                    const auto offer = submit_transfer_bid(g, bid, transfer_rand);
                    if (offer.status == TransferActionStatus::pending || offer.status == TransferActionStatus::accepted) break;
                }
            }
            std::optional<GameState> duplicate_before;
            if (duplicate_directory && !duplicate_captured) duplicate_before = g;
            ui::DeskRequest kickoff;
            kickoff.kind = ui::DeskRequestKind::advance_turn;
            kickoff.manager_slot = int(g.memory.u8(::bmh::layout::ActiveManager));
            kickoff.revision = revision;
            const TurnAdvanceResult r = session.dispatch_kickoff(kickoff, revision, error);
            if (r.kind == TurnDeskKind::Failed || r.kind == TurnDeskKind::WaitingAtDesk) {
                gap = r.reason.empty() ? error : r.reason;
                break;
            }
            if (duplicate_before && capture_duplicate(*duplicate_before, g, duplicate_directory))
                duplicate_captured = true;
            if (r.kind == TurnDeskKind::CareerComplete) {
                ++played;
                std::printf("turn %d (day %u): career complete\n", played, r.date);
                break;
            }
            const std::string when = "turn " + std::to_string(played + 1) + " (day " + std::to_string(r.date) + ")";
            g_human_team = g.manager_teams.at(0);
            check_results(session.last_turn_results(), when, checks, goals, matches);
            check_state(g, when, checks);
            std::printf("%s: %s, place %d, money %d\n", when.c_str(), r.kind == TurnDeskKind::SeasonEndDesk ? "season end desk" : "desk",
                        table_position(g, g.manager_teams.at(0)), money(g, 0));
            if (r.kind == TurnDeskKind::SeasonEndDesk) {
                print_first_league_results(g, "season end");
                ++seasons;
            }  // (the next kickoff runs the season change)
        }
        if (const char* final_save = std::getenv("SIM_FINAL_SAVE")) {
            WatcomRandom envelope(1); // Diagnostic export must not change either gameplay stream.
            write_savegame(final_save, make_savegame(g, envelope));
        }
        print_first_league_results(g, "run end");
        if (std::getenv("SIM_ADVERTISING")) std::printf("%d advertising contracts accepted (SIM_ADVERTISING=1)\n", ad_contracts);
        if (std::getenv("SIM_INTERVENE")) std::printf("%d interventions (substitution at minute 30, re-rated)\n", interventions);
        if (sim_contracts_env && std::string(sim_contracts_env) == "1")
            std::printf("contract expiry policy: %d decisions, %d renewals, %d rejected offers/releases, %d no-demand releases, %d errors (SIM_CONTRACTS=1)\n",
                        contract_expiry_decisions, contract_expiry_renewals, contract_expiry_rejections, contract_expiry_declines,
                        contract_expiry_errors);
    } catch (const std::exception& ex) {
        gap = std::string("exception: ") + ex.what();
    }
    std::printf("\n%d turns, %d season ends; recorded matchday snapshots (all competitions; shoot-outs included): %d matches, %.2f goals per match\n", played, seasons, matches,
                matches ? double(goals) / matches : 0.0);
    if (g_human_matches && g_league_matches)
        std::printf("recorded league snapshots: human club %.2f goals for, %.2f against per match (%d matches); all clubs %.2f per club and match\n",
                    double(g_human_for) / g_human_matches, double(g_human_against) / g_human_matches, g_human_matches,
                    double(g_league_goals) / (2.0 * g_league_matches));
    // the goal gates of the whole run (#57): high scores stay rare (at most one per 2000
    // matches, at least one allowed) and the average stays in a football range
    if (g_high_scores > std::max(1, matches / 2000))
        checks.fail("too many matches with more than 12 goals", std::to_string(g_high_scores) + " of " + std::to_string(matches));
    if (matches >= 500 && (double(goals) / matches < 2.0 || double(goals) / matches > 4.0))
        checks.fail("goals per match outside 2.0-4.0", std::to_string(double(goals) / matches));
    for (const auto& [kind, n] : checks.counts) std::printf("VIOLATION %-32s %4d x, first: %s\n", kind.c_str(), n, checks.first[kind].c_str());
    if (!gap.empty()) std::printf("STOPPED: %s\n", gap.c_str());
    return !gap.empty() ? 2 : checks.counts.empty() ? 0 : 1;
}
