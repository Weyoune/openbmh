// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
// Stage 4 of docs/CLI_CONCEPT: the matchday window Match (07-spieltag 3) with a stepped clock.
//
// The engine plays a day inside one synchronous call (CareerSession::dispatch_kickoff ->
// TurnDriver::advance_to_next_desk) and calls TurnDriver::live_minute after every minute.
// The SDL app shows its live screen from inside that callback. The console cannot wait for
// input inside a call, so in time mode "stepped" the kickoff runs on an engine thread: the
// minute callback hands control to the console thread and blocks until the console lets the
// clock run again (Match.Step / Match.Run). Exactly one of the two threads runs at a time
// (a mutex/condition handshake), so the game stays deterministic: the same input gives the
// same game, whatever the timing.
//
// During a pause the console may change the human team as the original allows from the
// match (EndPhase -> line-up screen): Match.Substitute calls manual_substitution on the
// engine's state and MatchEngine::rerate_team, as tests/season_sim.cpp SIM_INTERVENE does.
//
// The events come from the engine's presentation log (MatchEngine::show, presentation.hpp):
// the console points it at its own vector at the first minute; appending draws no random
// numbers. Events of the first simulated minute are therefore not listed (the log is only
// reachable from the minute callback).

#include <algorithm>
#include <array>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

#include "cli/console/console.hpp"
#include "cli/console/detail.hpp"
#include "game/lineup.hpp"
#include "game/match_engine.hpp"
#include "game/presentation.hpp"
#include "game/schedule.hpp"
#include "game/season.hpp"
#include "game/messages.hpp"
#include "game/month.hpp"
#include "game/pending_offer.hpp"
#include "game/weekly.hpp"
#include "ui/incoming_transfer.hpp"
#include "ui/league_table.hpp"
#include "ui/live_matchday.hpp"
#include "ui/pending_offer.hpp"
#include "ui/tactics.hpp"
#include "app/window_actions.hpp"
#include "ui/preparation.hpp"

namespace bmh::cli::console {

using namespace detail;

struct Console::EngineRun {
    std::thread worker;
    std::mutex m;
    std::condition_variable cv;
    bool engine_turn = false;  // true: the engine thread runs and the console waits
    bool finished = false;
    TurnAdvanceResult result;
    std::string error;
    ui::DeskState before;

    // the pause (valid while the engine waits in the minute callback)
    int minute = 0;
    MatchEngine* engine = nullptr;
    std::vector<MatchGroup>* groups = nullptr;
    GameState* state = nullptr;
    bool intro_shown = false;

    // the clock: minutes to run before the next pause, or a minute to run to
    int steps = 0;
    int until = -1;
    bool to_end = false;

    std::vector<ShowEvent> events;  // the engine's presentation log
    std::set<std::pair<int, int>> human_matches;  // (group, match) with a human team
    size_t printed = 0;
    bool quiet = false;             // time mode instant: no minute pauses, no "~" lines

    // a question of the engine during the kickoff (stage 5): the engine thread waits in a
    // hook until the console has the answer (PendingOfferHooks, 07-spieltag 7.3)
    enum class Pause { minute, question, loan_offer } pause = Pause::minute;
    bool aborted = false;
    bool loan_question = false;
    // These capabilities refer to the month caller's staged transaction, never
    // CareerSession's live state. They live only while its synchronous hook waits.
    GameState* loan_state = nullptr;
    GameRandom* loan_game = nullptr;
    WatcomRandom* loan_random = nullptr;
    app::ClubModalServices* loan_services = nullptr;
    TransferOfferEditorState loan_owner;
    HumanBuyerSaleHooks loan_hooks;
    int loan_result = -1;
    int question_player = 0;  // 1-4
    std::string question_text, question_default, answer;
    std::vector<std::string> question_options;
    std::vector<std::string> notices;  // the engine's notices, printed when the console runs

    // Engine thread: hands a question to the console and waits for its answer.
    std::string ask(int player, std::string text, std::vector<std::string> options, std::string fallback) {
        std::unique_lock lock(m);
        if (aborted) throw std::runtime_error("console matchday aborted");
        pause = Pause::question;
        question_player = player;
        question_text = std::move(text);
        question_options = std::move(options);
        question_default = std::move(fallback);
        answer.clear();
        engine_turn = false;
        cv.notify_all();
        cv.wait(lock, [this] { return engine_turn; });
        pause = Pause::minute;
        if (aborted) throw std::runtime_error("console matchday aborted");
        return answer.empty() ? question_default : answer;
    }
};

Console::~Console() {
    if (run_) {
        try {
            stop_matchday();
        } catch (...) {
        }
    }
}

namespace {

// The human teams in this minute's groups.
bool human_playing(const std::vector<MatchGroup>& groups, const GameState& g) {
    for (const auto& group : groups)
        for (size_t side = 0; side < group.fixtures.size(); ++side)
            if (!(group.void_mask() >> (side / 2) & 1) &&
                g.manager_of(group.fixtures[side]) >= 0) return true;
    return false;
}

const char* booking_name(int kind) {
    switch (kind) {
    case 0: return "yellow card";
    case 1: return "red card";
    case 2: return "injury";
    case 3: return "yellow-red card";
    default: return "booking";
    }
}

}  // namespace

void Console::start_matchday(const ui::DeskRequest& request, const ui::DeskState& before) {
    run_ = std::make_shared<EngineRun>();
    run_->before = before;
    EngineRun* run = run_.get();
    if (options_.time == TimeMode::instant) run->quiet = run->to_end = true;
    session_->set_loan_return_presenter([run](GameState&, WatcomRandom&, int manager, const std::string& question) {
        run->loan_question = true;
        struct Reset { bool& flag; ~Reset() { flag = false; } } reset{run->loan_question};
        return run->ask(manager + 1, utf8(question), {"Yes", "No"}, "No") == "Yes" ? 1 : 0;
    });
    session_->set_loan_return_offer_presenter([run](GameState& state, GameRandom& game, WatcomRandom& random,
            app::ClubModalServices& services, int manager, int source_slot, int list_entry,
            const HumanBuyerSaleHooks& hooks) -> int {
        if (run->aborted || manager < 0 || size_t(manager) >= state.manager_teams.size() ||
            list_entry < 40 || list_entry >= 44 || source_slot != 2928 + list_entry ||
            !state.memory.has(::bmh::layout::EditorList + uint32_t(list_entry) * 0x58 + 0x57)) return -1;
        const uint32_t row = ::bmh::layout::EditorList + uint32_t(list_entry) * 0x58;
        TransferOfferEditorRequest input;
        input.manager = manager;
        input.runtime_club = state.manager_teams.at(size_t(manager));
        input.list_entry = list_entry;
        input.source_slot = source_slot;
        input.expected_player = state.squad_slots.at(size_t(source_slot)) & 0x0fff;
        input.expected_seller_word = state.memory.u16(row + 8);
        for (int bid = 0; bid < 4; ++bid) {
            const uint16_t club = state.memory.u16(row + uint32_t(bid) * 2);
            if (club == 0xffff || club == input.runtime_club) { input.expected_bid_slot = bid; break; }
        }
        input.loan_return_entry = true;
        auto owner = open_transfer_offer_editor_source(state, input, game, random);
        if (!owner.supported || owner.closed) return -1;
        std::unique_lock lock(run->m);
        run->loan_state = &state;
        run->loan_game = &game;
        run->loan_random = &random;
        run->loan_services = &services;
        run->loan_owner = std::move(owner);
        run->loan_hooks = hooks;
        run->loan_result = -1;
        run->pause = EngineRun::Pause::loan_offer;
        run->engine_turn = false;
        run->cv.notify_all();
        run->cv.wait(lock, [run] { return run->engine_turn; });
        run->pause = EngineRun::Pause::minute;
        run->loan_state = nullptr;
        run->loan_game = nullptr;
        run->loan_random = nullptr;
        run->loan_services = nullptr;
        run->loan_hooks = {};
        return run->aborted ? -1 : run->loan_result;
    });
    // DayStepRest2 can enter InstallDefaultHooks while advancing the calendar. Bridge
    // that synchronous Source callback through the same engine-question pause
    // used by the kickoff's transfer questions. Offers are the actual four
    // Source demands; no guessed renewal term or salary is applied.
    session_->set_contract_expiry_presenter([run](GameState& state, WatcomRandom& random, int manager, int row) {
        if (manager < 0 || size_t(manager) >= state.manager_teams.size())
            throw std::runtime_error("contract expiry: invalid manager owner");
        const SquadRange squad = squad_range(state.manager_teams[size_t(manager)]);
        if (row < 0 || size_t(row) >= squad.count || squad.first + size_t(row) >= state.squad_slots.size())
            throw std::runtime_error("contract expiry: invalid squad row");
        const size_t board = size_t(manager) * 0x840 + size_t(row) * 0x58;
        if (board + 0x54 > state.tactics.size())
            throw std::runtime_error("contract expiry: missing board row");
        const int player = int(state.squad_slots[squad.first + size_t(row)] & 0x0fff);
        if (player == 0x0fff || size_t(player) >= state.players.size())
            throw std::runtime_error("contract expiry: missing player");
        const std::string player_name = utf8(state.player_name(player));
        const GameState before = state;
        const WatcomRandom before_random = random;

        std::array<uint32_t, 4> demands{};
        try {
            // InstallDefaultHooks passes the physical squad row as param5, so
            // ContractBonuses enters with from_desk=false (the 0x80 desk bit is absent).
            demands = contract_demands(state, manager, row, false, random);
        } catch (const std::exception&) {
            state = before;
            random = before_random;
            throw;
        }
        std::vector<std::string> options;
        std::vector<int> columns;
        for (int year = 0; year < 4; ++year) {
            const uint32_t demand = demands[size_t(year)];
            if (demand == 0 || demand == std::numeric_limits<uint32_t>::max()) continue;
            columns.push_back(year);
            options.push_back(std::to_string(year + 1) + " year" + (year == 0 ? "" : "s") +
                              " — " + std::to_string(demand) + " monthly");
        }
        options.push_back("No offer (release player)");
        const std::string fallback = options.back();
        const std::string prompt = player_name + utf8(message_text(state, ::bmh::layout::Month_BoardMonth_TextPointer4)) +
            " Choose an offer at the player's salary demand or release the player.";
        const std::string answer = run->ask(manager + 1, prompt, options, fallback);
        if (answer == fallback) return 0;
        const auto selected = std::find(options.begin(), options.end() - 1, answer);
        if (selected == options.end() - 1)
            throw std::runtime_error("contract expiry: invalid question answer");
        const size_t index = size_t(selected - options.begin());
        const int column = columns[index];
        bool accepted = false;
        try {
            accepted = contract_offer(state, manager, row, column, demands[size_t(column)], demands[size_t(column)],
                                      false, true, random);
        } catch (const std::exception&) {
            state = before;
            random = before_random;
            throw;
        }
        if (!accepted) {
            // With an exact Source demand the acceptance threshold is necessarily
            // met. Keep an unexpected rejection distinct from the Source close path:
            // ContractBonuses returns to the still-open InstallDefaultHooks screen.
            throw std::runtime_error("contract expiry: unexpected rejection of the exact salary demand");
        }
        run->notices.push_back("Contract renewed: " + player_name);
        return 1;
    });
    // The engine's questions during the kickoff, as the SDL app asks them (src/app/main.cpp
    // make_pending_offer_hooks): a club's salary demand or severance (yes/no) and the
    // choice among the clubs' fee offers for an own player. normal_interactive is the
    // session's mode for a frontend that presents them (legacy_unpresented answers them
    // without asking).
    PendingOfferHooks hooks;
    hooks.answer = [run](const GameState&, const PendingOfferQuestion& q) {
        // the severance a sold player's management asks for: refusing it ends the transfer
        // (QA: sales of 2.7M and 3.4M were lost on a "No" that looked like a side question)
        const std::string hint =
            q.kind == PendingOfferQuestionKind::severance ? " [No = the transfer does not take place]" : "";
        const std::string answer =
            run->ask(q.manager + 1, utf8(q.original_message) + hint, {"Yes", "No"}, "No");
        return answer == "Yes" ? PendingOfferAnswer::yes : PendingOfferAnswer::no;
    };
    hooks.select_transfer = [run](const GameState& staged, const TransferOfferListView& offers) {
        const ui::IncomingTransferDecisionState panel = ui::pending_offer_transfer_decision_from_game(staged, offers);
        std::string text = "Offers for " + utf8(panel.player_name) + ":";
        std::vector<std::string> options;
        std::vector<int> slots;
        for (size_t i = 0; i < panel.offers.size(); ++i) {
            const auto& o = panel.offers[i];
            if (!o.present) continue;
            options.push_back("Accept" + std::to_string(options.size() + 1));
            slots.push_back(int(i));
            text += " " + options.back() + " = " + (o.loan ? "loan to " : "sell to ") + utf8(panel.buyer_names[i]) + " for " + std::to_string(o.fee) + " (" +
                    std::to_string(o.monthly_salary) + " monthly" + (o.loan ? ", loan" : "") + ");";
        }
        options.push_back("Decline");
        // IncomingTransferScreen: the right mouse button declines (IncomingTransferScreen::click)
        const std::string answer = run->ask(panel.viewer_manager + 1, text, options, "Decline");
        for (size_t i = 0; i < slots.size(); ++i)
            if (options[i] == answer) return PendingOfferTransferChoice{PendingOfferTransferChoiceKind::bid, slots[i]};
        return PendingOfferTransferChoice{PendingOfferTransferChoiceKind::decline, -1};
    };
    hooks.notice = [run](const GameState&, const PendingOfferNotice& n) { run->notices.push_back(utf8(n.original_message)); };
    session_->set_pending_offer_hooks(std::move(hooks));
    // IncomingTransferScreen at the weekly list deadline: the bids for a player a manager put on the
    // transfer list. Without this chooser the engine declines every bid for a human seller
    // (GameState::transfer_offer_selector), so listed players could never be sold.
    session_->set_transfer_offer_selector([run](const GameState& g, const TransferOfferListView& view) -> int {
        const std::string notice = view.notice_text_address ? utf8(message_text(g, view.notice_text_address)) : std::string();
        if (view.offer_count <= 0) {  // the final refusal notice; the response is ignored
            if (!notice.empty()) run->notices.push_back(notice);
            return -1;
        }
        std::string text = (notice.empty() ? std::string() : notice + " ") + "Offers for " + utf8(g.player_name(view.player)) + ":";
        std::vector<std::string> options;
        std::vector<int> slots;
        for (const auto& o : view.offers) {
            if (!o.present) continue;
            options.push_back("Accept" + std::to_string(options.size() + 1));
            slots.push_back(o.bid_index);
            text += " " + options.back() + " = " + (o.loan ? "loan to " : "sell to ") + utf8(g.club_name(o.buyer)) + " for " + std::to_string(o.fee) + " (" +
                    std::to_string(o.monthly_salary) + " monthly" + (o.loan ? ", loan" : "") + ");";
        }
        options.push_back("Decline");
        const std::string answer = run->ask(view.seller_manager + 1, text, options, "Decline");
        for (size_t i = 0; i < slots.size(); ++i)
            if (options[i] == answer) return slots[i];
        return -1;
    });
    // IncomingTransferScreen also presents the automatic result when a human bid loses
    // to another club. The engine already supplies this read-only snapshot;
    // relay it at the next thread handoff instead of silently dropping it.
    session_->set_transfer_decision_observer([this, run](const TransferDecisionEvent& event) {
        const auto& g = session_->state();
        const std::string player = utf8(g.player_name(event.offers.player));
        if (event.kind == TransferDecisionEventKind::last_week) {
            run->notices.push_back(player + utf8(message_text(g, event.result_text_address)));
            return;
        }
        if (event.kind == TransferDecisionEventKind::confirmation) {
            run->notices.push_back(player + ": " + utf8(message_text(g, event.result_text_address)));
            return;
        }
        // The Original marks each row NEIN up to the accepted row (OK), or
        // every row NEIN when no bid is accepted. It stops at the OK row.
        for (const auto& offer : event.offers.offers) {
            if (!offer.present) continue;
            const bool accepted = event.accepted && offer.bid_index == event.selected_bid_index;
            run->notices.push_back(player + ": " + utf8(g.club_name(offer.buyer)) + ": " +
                utf8(message_text(g, accepted ? ::bmh::layout::IncomingTransfer_RenderAtElapsed_TextPointer3 : ::bmh::layout::IncomingTransfer_RenderAtElapsed_TextPointer4)));
            if (accepted) break;
        }
        // The rows stop at the accepted bid, so a human bid behind it never shows up; a manager
        // then waits for a player who has gone (learning session, finding G-006). The console
        // says it for every human bidder that lost (OpenBMH deviates from the original screen
        // here: an extra line, nothing in the engine changes).
        for (const auto& offer : event.offers.offers) {
            if (!offer.present || (event.accepted && offer.bid_index == event.selected_bid_index)) continue;
            if (g.manager_of(offer.buyer) < 0) continue;
            std::string line = player + ": the bid of " + utf8(g.club_name(offer.buyer)) + " was not accepted";
            for (const auto& winner : event.offers.offers)
                if (event.accepted && winner.present && winner.bid_index == event.selected_bid_index)
                    line += " - he signs for " + utf8(g.club_name(winner.buyer));
            run->notices.push_back(line);
        }
    });
    session_->set_pending_offer_entry_policy(app::PendingOfferEntryPolicy::normal_interactive);
    // The season change inside the kickoff (season_transition): the tax notice and the
    // question whether to "adjust" the tax return (GameEndTaxAnimationScreen, Season_SeasonApplyProfitTax_TextPointer3; caught or not
    // follows), the promoted/relegated clubs and the cup draws, as the SDL app shows them.
    SeasonTaxHooks tax;
    tax.observe = [run](const GameState&, const SeasonTaxEvent& e) {
        if (!e.text.empty()) run->notices.push_back(utf8(e.text));
    };
    tax.choose = [run](const GameState&, const SeasonTaxEvent& e) {
        return run->ask(e.manager_slot + 1, utf8(e.text), {"Yes", "No"}, "No") == "Yes";
    };
    session_->set_season_tax(std::move(tax));
    session_->set_season_promotion([run](const GameState& g, const SeasonPromotionEvent& e) {
        // The first promotion screen comes before any club changes its league: the tables
        // are final here. The managers' leagues are listed once each (the table window
        // shows the new season afterwards).
        if (e.group == 0) {
            std::set<int> shown;
            for (size_t m = 0; m < g.manager_teams.size(); ++m) {
                const int league = team_league(g.manager_teams[m]);
                if (league < 0 || league >= kLeagueCount || !shown.insert(league).second) continue;
                const ui::LeagueTableState t = ui::league_table_from_game(g, int(m), league);
                std::string line = "final table " + t.league_name + ":";
                for (const auto& r : t.rows)
                    line += " " + std::to_string(r.rank) + ". " + r.name + " " + std::to_string(r.points) + ";";
                run->notices.push_back(line);
            }
        }
        static const char* const kGroups[4] = {"1st division", "2nd division", "3rd divisions", "4th division"};
        std::string up, down;
        for (size_t i = 0; i < e.count && i < e.promoted.size(); ++i) up += (up.empty() ? "" : ", ") + utf8(g.club_name(e.promoted[i]));
        for (size_t i = 0; i < e.count && i < e.relegated.size(); ++i) down += (down.empty() ? "" : ", ") + utf8(g.club_name(e.relegated[i]));
        run->notices.push_back(std::string(kGroups[std::clamp(e.group, 0, 3)]) + ": relegated " + down + "; promoted " + up);
    });
    session_->set_season_cup_draw([run](const GameState& g, const SeasonCupDrawEvent& e) {
        if (e.phase != SeasonCupDrawPhase::draw) return;
        std::string pairs;
        for (size_t i = 0; i + 1 < e.participants.size() && i + 1 < e.count; i += 2)
            pairs += (pairs.empty() ? "" : "; ") + utf8(g.club_name(e.participants[i])) + " - " + utf8(g.club_name(e.participants[i + 1]));
        run->notices.push_back("cup draw (competition " + std::to_string(e.competition) + ", round " + std::to_string(e.round + 1) + "): " + pairs);
    });
    session_->set_live_minute([run](int minute, MatchEngine& engine, std::vector<MatchGroup>& groups, GameState& g) {
        if (run->aborted) throw std::runtime_error("console matchday aborted");
        if (!engine.show) engine.show = &run->events;
        // remember which matches have a human team: only their events are printed (07-spieltag 3.1)
        for (size_t gi = 0; gi < groups.size(); ++gi)
            for (size_t mi = 0; mi * 2 + 1 < groups[gi].fixtures.size(); ++mi)
                for (int side = 0; side < 2; ++side)
                    if (std::find(g.manager_teams.begin(), g.manager_teams.end(), groups[gi].fixtures[mi * 2 + size_t(side)]) !=
                        g.manager_teams.end())
                        run->human_matches.insert({int(gi), int(mi)});
        if (run->to_end || !human_playing(groups, g)) return;
        if (run->until >= 0) {
            if (minute < run->until) return;
            run->until = -1;
        } else if (run->steps > 0 && --run->steps > 0) {
            return;
        }
        // pause: hand the clock to the console until it lets the engine run again
        std::unique_lock lock(run->m);
        run->minute = minute;
        run->engine = &engine;
        run->groups = &groups;
        run->state = &g;
        run->engine_turn = false;
        run->cv.notify_all();
        run->cv.wait(lock, [run] { return run->engine_turn; });
        run->engine = nullptr;
        run->groups = nullptr;
        run->state = nullptr;
        if (run->aborted) throw std::runtime_error("console matchday aborted");
    });
    run->engine_turn = true;
    run->worker = std::thread([this, run, request] {
        TurnAdvanceResult result;
        std::string error;
        try {
            result = session_->dispatch_kickoff(request, revision_, error);
        } catch (const std::exception& e) {
            result.kind = TurnDeskKind::Failed;
            error = std::string("engine exception: ") + e.what();
        }
        std::unique_lock lock(run->m);
        run->result = result;
        run->error = error;
        run->finished = true;
        run->engine_turn = false;
        run->cv.notify_all();
    });
    {
        std::unique_lock lock(run->m);
        run->cv.wait(lock, [run] { return !run->engine_turn; });
    }
    engine_returned();
}

// Lets the engine run until the next pause or the end of the day.
void Console::resume_matchday() {
    EngineRun* run = run_.get();
    if (!run) fail("E_RULE", "no matchday is running");
    if (!run->finished) {
        std::unique_lock lock(run->m);
        run->engine_turn = true;
        run->cv.notify_all();
        run->cv.wait(lock, [run] { return !run->engine_turn; });
    }
    engine_returned();
}

// The engine thread waits (a minute pause or a question) or has finished the kickoff:
// show the pause, ask the question, or finish (results, next desk).
void Console::engine_returned() {
    EngineRun* run = run_.get();
    for (const auto& n : run->notices) emit('i', n);
    run->notices.clear();
    if (!run->finished && run->pause == EngineRun::Pause::question) {
        // the question belongs to the manager concerned (his desk); the answer resumes the engine
        set_window("Desktop", run->question_player);
        ask(run->question_text, run->question_options, run->question_default, [this](const std::string& answer) {
            run_->answer = answer;
            resume_matchday();
        });
        return;
    }
    if (loan_return_offer_active()) {
        const auto& state = *run->loan_state;
        offer_editor_ = app::loan_return_offer_editor_projection(state, run->loan_owner, revision_,
            !state.memory.has(::bmh::layout::TextGradientDisabled) || state.memory.u8(::bmh::layout::TextGradientDisabled) == 0);
        offer_submitted_ = false;
        set_window("Transfer.Negotiate", run->loan_owner.request.manager + 1);
        emit('i', "Loan returned: negotiate a purchase with ShowNegotiation, SetOfferField, SubmitOffer; GoBack cancels the offer");
        return;
    }
    if (!run->finished) {
        show_match_pause();
        return;
    }
    run->worker.join();
    session_->set_live_minute({});
    session_->set_pending_offer_hooks({});
    session_->set_transfer_offer_selector({});
    session_->set_transfer_decision_observer({});
    session_->set_season_tax({});
    session_->set_season_promotion({});
    session_->set_season_cup_draw({});
    session_->set_pending_offer_entry_policy(app::PendingOfferEntryPolicy::legacy_unpresented);
    session_->set_contract_expiry_presenter({});
    session_->set_loan_return_presenter({});
    session_->set_loan_return_offer_presenter({});
    // the events after the last pause
    if (!run->aborted && !run->quiet && run->printed < run->events.size()) show_match_pause();
    const ui::DeskState before = run->before;
    const TurnAdvanceResult result = run->result;
    const std::string error = run->error;
    const bool aborted = run->aborted;
    run_.reset();
    if (aborted) {
        set_window("Desktop", int(session_->state().memory.u8(::bmh::layout::ActiveManager)) + 1);
        return;
    }
    finish_kickoff(before, result, error);
}

void Console::stop_matchday() {
    if (!run_) return;
    run_->to_end = true;
    // Termination must not silently choose a default answer or commit a staged
    // return/purchase. Wake the Source owner with an abort; CareerSession owns rollback.
    run_->aborted = true;
    question_.reset();
    offer_editor_.reset();
    resume_matchday();
}

bool Console::loan_return_offer_active() const {
    return run_ && !run_->finished && run_->pause == EngineRun::Pause::loan_offer;
}

bool Console::loan_return_modal_active() const {
    return run_ && (run_->loan_question || loan_return_offer_active());
}

std::optional<app::CareerSession::TransferOfferEditorDispatch> Console::dispatch_loan_return_offer(
        const ui::OutgoingTransferRequest& request, const HumanBuyerSaleHooks& presenters, std::string& error,
        const app::TransferOfferTeamInfoPresenter& team_info,
        const app::TransferOfferPlayerInfoPresenter& player_info) {
    error.clear();
    if (!loan_return_offer_active()) { error = "no loan-return editor"; return std::nullopt; }
    auto& run = *run_;
    // Match the complete identity/quote to the Source owner. The UI fields may
    // already contain the proposed value, so validate against a fresh projection.
    const auto projected = app::loan_return_offer_editor_projection(*run.loan_state, run.loan_owner, revision_, false);
    const auto expected = app::TransferActions::editor(projected, request.kind, request.field, request.value);
    if (request.identity.manager_slot != expected.identity.manager_slot ||
        request.identity.runtime_club != expected.identity.runtime_club ||
        request.identity.list_entry != expected.identity.list_entry ||
        request.identity.source_slot != expected.identity.source_slot ||
        request.identity.player != expected.identity.player || request.identity.seller != expected.identity.seller ||
        request.identity.bid_slot != expected.identity.bid_slot || request.identity.revision != revision_ ||
        request.quoted_fee != expected.quoted_fee || request.quoted_monthly_salary != expected.quoted_monthly_salary ||
        request.quoted_years != expected.quoted_years) {
        error = "loan return: mismatched editor identity or quote"; return std::nullopt;
    }
    auto next = *run.loan_state;
    auto game = *run.loan_game;
    auto random = *run.loan_random;
    auto owner = run.loan_owner;
    TransferActionResult action;
    int completed = -1;
    const bool information = request.kind == ui::OutgoingTransferRequestKind::team_info ||
                             request.kind == ui::OutgoingTransferRequestKind::player_info;
    try {
        if (request.kind == ui::OutgoingTransferRequestKind::quote_change) {
            owner = app::edit_transfer_offer_from_ui(next, owner, request, game, random);
        } else if (request.kind == ui::OutgoingTransferRequestKind::cancel) {
            owner = cancel_transfer_offer_editor_source(next, owner);
            completed = 0;
        } else if (request.kind == ui::OutgoingTransferRequestKind::team_info ||
                   request.kind == ui::OutgoingTransferRequestKind::player_info) {
            // These services already belong to the staged month transaction.
            const auto valid = cancel_transfer_offer_editor_source(next, owner);
            if (!valid.supported) { error = valid.reason; return std::nullopt; }
            auto& services = *run.loan_services;
            if (request.kind == ui::OutgoingTransferRequestKind::team_info) {
                if (!team_info || !team_info(services, int(owner.request.expected_seller_word & 0x7fff), Image{}))
                    throw std::runtime_error("loan return: TeamInfo presenter stopped");
                next = *run.loan_state; game = *run.loan_game; random = *run.loan_random;
                owner = resume_transfer_offer_team_info_source(next, owner, random);
            } else {
                const auto selection = transfer_offer_player_info_selection_source(next, owner);
                services.set_runtime_byte(::bmh::layout::CareerSession_DispatchPoachUi_ExecutableField, 1);
                if (!player_info || !player_info(services, selection, Image{}))
                    throw std::runtime_error("loan return: PlayerInfo presenter stopped");
                next = *run.loan_state; game = *run.loan_game; random = *run.loan_random;
                owner = resume_transfer_offer_player_info_source(next, owner,
                    services.runtime_byte(::bmh::layout::CareerSession_DispatchTransferOffer_ExecutableField), services.runtime_byte(::bmh::layout::CareerSession_DispatchTransferOffer_ExecutableField2), random);
            }
        } else if (request.kind == ui::OutgoingTransferRequestKind::submit) {
            auto hooks = run.loan_hooks;
            hooks.confirm = presenters.confirm;
            hooks.notice = presenters.notice;
            MinuteReport report;
            auto result = submit_transfer_offer_editor_source(next, owner, random, hooks, report);
            owner = std::move(result.editor); action = result.action;
            if (report.unsupported || action.status == TransferActionStatus::unsupported) {
                error = report.reason; return std::nullopt;
            }
            // DayStepRest2's SDL owner returns after these statuses even when the
            // generic market editor would remain open. The month caller clears its row.
            if (owner.closed || action.status == TransferActionStatus::pending ||
                action.status == TransferActionStatus::rejected || action.status == TransferActionStatus::insufficient_funds)
                completed = 1;
        } else { error = "loan return: unsupported editor event"; return std::nullopt; }
        if (!owner.supported) {
            error = owner.reason;
            if (information) stop_matchday();
            return std::nullopt;
        }
        app::CareerSession::TransferOfferEditorDispatch result;
        result.editor = app::loan_return_offer_editor_projection(next, owner, revision_, false);
        result.editor.closed = owner.closed || completed >= 0;
        result.action = action;
        *run.loan_state = std::move(next);
        *run.loan_game = game;
        *run.loan_random = random;
        run.loan_owner = std::move(owner);
        run.loan_result = completed;
        return result;
    } catch (const std::exception& e) {
        error = e.what();
        // ClubModalServices also owns opaque transient bytes. Once an info
        // presenter has entered, a failed event must discard the whole month
        // transaction, not leave its partial effects for a later Cancel/Submit.
        if (information) {
            stop_matchday();
            throw;  // A presenter exception is an internal failure, not a rule refusal.
        }
        return std::nullopt;
    }
}

void Console::finish_loan_return_offer() {
    if (loan_return_offer_active() && run_->loan_result >= 0) resume_matchday();
}

// The new events since the last pause as "~" lines, then the human matches' scores.
void Console::show_match_pause() {
    EngineRun* run = run_.get();
    set_window("Match", 0);
    if (!run->intro_shown && run->groups && run->state) {
        // Info.MatchdayIntro (07-spieltag 2): the day's human matches
        run->intro_shown = true;
        emit('i', "Matchday: the clock stops after every minute; Step(n) or Run(Halftime|End)");
    }
    for (; run->printed < run->events.size(); ++run->printed) {
        const ShowEvent& e = run->events[run->printed];
        if (!run->human_matches.count({int(e.group), int(e.match)})) continue;
        std::string what;
        switch (e.what) {
        case Show::Booking: what = std::string(booking_name(e.kind)) + ": "; break;
        case Show::Substitution: what = "substitution: "; break;
        case Show::Chance: what = e.flag ? "GOAL: " : "chance: "; break;
        case Show::PenaltyKick: what = std::string("penalty ") + (e.flag ? "scored: " : "missed: "); break;
        case Show::ShootoutEnd: what = "shoot-out: "; break;
        default: continue;  // set pieces and shots are picture data only
        }
        std::string text = utf8(e.text);
        if (e.what == Show::Substitution && run->state && e.player >= 0 && e.player < 0xfff && e.other >= 0 && e.other < 0xfff)
            text = utf8(run->state->player_name(e.other)) + " for " + utf8(run->state->player_name(e.player));
        if (text.empty() && run->state && e.player >= 0 && e.player < 0xfff) text = utf8(run->state->player_name(e.player));
        Json::Object data{{"kind", int(e.what)}, {"group", int(e.group)}, {"match", int(e.match)}, {"side", int(e.side)},
                          {"team", int(e.team)}, {"player", int(e.player)}, {"other", int(e.other)}};
        emit('~', std::to_string(e.minute) + "' [match " + std::to_string(e.group) + "." + std::to_string(e.match) + "] " + what + text,
             Json(std::move(data)));
    }
    if (!run->groups || !run->state) return;
    const ui::LiveMatchdayFrame frame = ui::live_matchday_frame(run->minute, *run->groups, *run->state);
    const auto& teams = run->state->manager_teams;
    for (const auto& m : frame.matches) {
        const bool human = std::find(teams.begin(), teams.end(), m.home_team) != teams.end() ||
                           std::find(teams.begin(), teams.end(), m.away_team) != teams.end();
        if (!human) continue;
        emit('i', std::to_string(run->minute) + "' " + utf8(m.home) + " " + std::to_string(m.home_goals) + ":" +
                      std::to_string(m.away_goals) + " " + utf8(m.away));
    }
}

void Console::register_match_commands() {
    auto paused = [this]() -> EngineRun& {
        if (!run_ || run_->finished || !run_->state) fail("E_RULE", "no match is running");
        return *run_;
    };
    add({"Match", "Step", {integer("minutes", 1, 130, true)}, "let the clock run n minutes (default 1)", true}, [this, paused](const Values& v) {
        paused().steps = int(v.integer("minutes", 1));
        resume_matchday();
    });
    add({"Match", "Run", {choice("until", {"Halftime", "End"}, true)}, "run to half time or to the end of the day", true},
        [this, paused](const Values& v) {
            EngineRun& run = paused();
            if (v.has("until") && v.choice("until") == 0) {
                // the indoor tournament plays 2 x 20 minutes (driver.cpp): its half time is at
                // 20; waiting for 45 there ran the whole day through (learning session F-run2-004)
                const int half = run.state && run.state->indoor ? 20 : 45;
                if (run.minute >= half) fail("E_RULE", "half time is over");
                run.until = half;
            } else {
                run.to_end = true;
            }
            resume_matchday();
        });
    add({"Match", "ShowMatchStats", {integer("match", 0, 99, true)}, "the scoreboard: score, chances, cards, spectators, weather", false},
        [this, paused](const Values&) {
            EngineRun& run = paused();
            const ui::LiveMatchdayFrame frame = ui::live_matchday_frame(run.minute, *run.groups, *run.state);
            std::vector<std::vector<std::string>> rows;
            for (const auto& m : frame.matches)
                rows.push_back({utf8(m.home), utf8(m.away), std::to_string(m.home_goals) + ":" + std::to_string(m.away_goals),
                                std::to_string(m.home_stats[1]) + "/" + std::to_string(m.away_stats[1]),
                                m.spectators_known ? std::to_string(m.spectators) : "-", utf8(m.temperature_text)});
            emit_table({"Home", "Away", "Score", "Chances", "Spectators", "Weather"}, rows);
        });
    // the human team a command acts for: @n, or the only human team playing
    auto team_of = [this, paused](const Values& v) {
        EngineRun& run = paused();
        const auto& teams = run.state->manager_teams;
        if (v.player) {
            if (size_t(v.player) > teams.size()) fail("E_BAD_ARGUMENT", "there is no player " + std::to_string(v.player));
            return teams[size_t(v.player - 1)];
        }
        if (teams.size() > 1) fail("E_BAD_ARGUMENT", "with several managers write @n before the command");
        return teams.front();
    };
    // the squad rows of a team in the running match: 0-10 on the pitch, bench rows have 0x4000
    auto squad_refs = [this, paused](int team) {
        EngineRun& run = paused();
        const SquadRange range = squad_range(team);
        std::vector<PlayerRef> refs;
        for (size_t row = 0; row < range.count; ++row) {
            const uint16_t slot = run.state->squad_slots.at(range.first + row);
            if ((slot & 0x0fff) == 0x0fff || !(slot & 0xc000)) continue;
            refs.push_back({int(row), slot & 0x0fff, utf8(run.state->player_name(slot & 0x0fff))});
        }
        return refs;
    };
    add({"Match", "ShowSquad", {}, "your team on the pitch and on the bench", false}, [this, paused, team_of, squad_refs](const Values& v) {
        EngineRun& run = paused();
        const int team = team_of(v);
        const SquadRange range = squad_range(team);
        std::vector<std::vector<std::string>> rows;
        for (const auto& r : squad_refs(team)) {
            const uint16_t slot = run.state->squad_slots.at(range.first + size_t(r.row));
            rows.push_back({"row:" + std::to_string(r.row), r.name, (slot & 0x8000) ? "pitch" : "bench"});
        }
        emit_table({"Row", "Player", "Place"}, rows);
    });
    Param out = detail::text("out"), in = detail::text("in");
    out.display = "out:Player";
    in.display = "in:Player";
    add({"Match", "Substitute", {out, in}, "a substitution: a player leaves, a bench player comes on", true},
        [this, paused, team_of, squad_refs](const Values& v) {
            EngineRun& run = paused();
            const int team = team_of(v);
            const auto refs = squad_refs(team);
            const PlayerRef leaving = refs[resolve_player(v.text("out"), refs)];
            const PlayerRef coming = refs[resolve_player(v.text("in"), refs)];
            const SquadRange range = squad_range(team);
            if (!(run.state->squad_slots.at(range.first + size_t(leaving.row)) & 0x8000))
                fail("E_RULE", leaving.name + " is not on the pitch");
            if (!(run.state->squad_slots.at(range.first + size_t(coming.row)) & 0x4000))
                fail("E_RULE", coming.name + " is not on the bench");
            // manual_substitution refuses after kMaxSubstitutions or for a player who already played
            if (!manual_substitution(*run.state, team, leaving.row, coming.row))
                fail("E_RULE", "no substitution possible (" + std::to_string(kMaxSubstitutions) + " used, or the player already played)");
            MinuteReport report;
            run.engine->rerate_team(team, report);
            if (report.unsupported) fail("E_INTERNAL", "re-rating after the substitution: " + report.reason);
            emit('+', std::to_string(run.minute) + "' substitution: " + coming.name + " for " + leaving.name);
        });
    // --- the tactics board during the match (the original's live tactics, mode 8) ---------
    // The tactics of the manager's team as the match sees them (ActiveManager set to him, as
    // the SDL live tactics screen does).
    auto live_tactics = [this, paused](int manager) {
        EngineRun& run = paused();
        GameState view = *run.state;
        if (view.memory.has(::bmh::layout::ActiveManager)) view.memory.set8(::bmh::layout::ActiveManager, uint8_t(manager));
        return ui::tactics_from_game(view, exe_, revision_);
    };
    add({"Match", "ShowTactics", {}, "the eight tactic settings of your team in this match", false},
        [this, paused, team_of, live_tactics](const Values& v) {
            EngineRun& run = paused();
            const int team = team_of(v);
            const ui::TacticsState t = live_tactics(run.state->manager_of(team));
            std::vector<std::vector<std::string>> rows;
            for (size_t i = 0; i < 8; ++i) rows.push_back({std::to_string(i), detail::kTacticNames[i], std::to_string(t.values[i])});
            emit_table({"Item", "Setting", "Value"}, rows);
            if (t.assistant_attitude) emit('i', "the assistant adapts the attitude (item 0) to the score");
        });
    // One change of a tactic setting in the running match: the session opens the live
    // intervention (begin_live_intervention, mode 8), applies the request like the tactics
    // window (dispatch_live_tactic re-rates the side) and closes it; a refused change aborts
    // the intervention, so the match goes on as before. From minute 3 on, as in the SDL app.
    add({"Match", "SetTactic", {integer("item", 0, 7), integer("value", 0, 11)},
         "change a tactic setting during the match (item 0 ends the assistant's D/O control)", true},
        [this, paused, team_of, live_tactics](const Values& v) {
            EngineRun& run = paused();
            const int team = team_of(v);
            const int manager = run.state->manager_of(team);
            if (manager < 0) fail("E_RULE", "not a human team");
            if (run.minute <= 2) fail("E_RULE", "tactics can be changed from minute 3 on");
            const int item = int(v.integer("item")), value = int(v.integer("value"));
            std::string error;
            if (!session_->begin_live_intervention(run.minute, 8, manager, team, *run.engine, revision_, error))
                require_engine(false, "live tactics", error);
            const ui::TacticsState t = live_tactics(manager);
            ui::TacticsRequest req;
            req.kind = ui::TacticsRequestKind::set_tactic;
            req.manager_slot = t.manager_slot;
            req.runtime_club = t.runtime_club;
            req.revision = revision_;
            req.index = item;
            req.desired_value = value;
            if (item == 0) req.desired_assistant_attitude = 0;
            if (!session_->dispatch_live_tactic(req, run.minute, 8, *run.engine, revision_, error)) {
                session_->abort_live_intervention(run.minute, 8, manager, team, *run.engine, revision_);
                require_engine(false, "live tactics", error);
            }
            if (!session_->finish_live_intervention(run.minute, 8, manager, team, *run.engine, revision_, error)) {
                session_->abort_live_intervention(run.minute, 8, manager, team, *run.engine, revision_);
                require_engine(false, "live tactics", error);
            }
            emit('+', std::to_string(run.minute) + "' tactics: item " + std::to_string(item) + " = " + std::to_string(value));
        });
    // Re-arranging the side in the running match (the live line-up, mode 2). The original's
    // PitchScreen offers no formation templates during a match (its param_2 is nonzero:
    // formations and automatic weights belong to the preparation only); a starter can be
    // placed on another depth/lane, as Lineup.Pitch MovePlayer does before the match.
    Param mover = detail::text("player");
    mover.display = "player:Player";
    add({"Match", "MovePlayer", {mover, integer("depth", 0, 19), integer("lane", 0, 8)},
         "place a starter on another position for the rest of the match (from minute 3)", true},
        [this, paused, team_of](const Values& v) {
            EngineRun& run = paused();
            const int team = team_of(v);
            const int manager = run.state->manager_of(team);
            if (manager < 0) fail("E_RULE", "not a human team");
            if (run.minute <= 2) fail("E_RULE", "positions can be changed from minute 3 on");
            std::string error;
            if (!session_->begin_live_intervention(run.minute, 2, manager, team, *run.engine, revision_, error))
                require_engine(false, "live position", error);
            auto abort = [&](const std::string& why) {
                session_->abort_live_intervention(run.minute, 2, manager, team, *run.engine, revision_);
                require_engine(false, "live position", why);
            };
            // the projection of the paused match for the manager (as the SDL live line-up)
            GameState view = *run.state;
            if (view.memory.has(::bmh::layout::ActiveManager)) view.memory.set8(::bmh::layout::ActiveManager, uint8_t(manager));
            ui::PreparationState p = ui::preparation_from_game(view, exe_, revision_);
            p.lineup.in_match = true;
            p.lineup.entry_prepared = true;
            if (!p.pitch || !p.pitch->indoor.has_value()) abort("no pitch projection in this match");
            std::vector<PlayerRef> refs;
            for (const auto& pl : p.lineup.players)
                if (pl.player_id >= 0) refs.push_back({pl.row, pl.player_id, utf8(pl.name)});
            const int index = resolve_player(v.text("player"), refs);
            const ui::LineupPlayer& player = p.lineup.players.at(size_t(refs[size_t(index)].row));
            if (player.row >= 11 || !(player.slot & 0x8000)) abort(utf8(player.name) + " is not on the pitch");
            const ui::PitchRequest r = app::LineupActions::move(p, player, int(v.integer("depth")), int(v.integer("lane")));
            const app::LiveSubstitutionResult result = session_->dispatch_live_pitch_result(r, run.minute, 2, *run.engine, revision_);
            if (result.status == app::LiveSubstitutionStatus::rejected) abort(result.error.empty() ? "refused" : result.error);
            if (!session_->finish_live_intervention(run.minute, 2, manager, team, *run.engine, revision_, error)) abort(error);
            emit('+', std::to_string(run.minute) + "' " + utf8(player.name) + " to depth " + std::to_string(v.integer("depth")) +
                          ", lane " + std::to_string(v.integer("lane")));
        });
    add({"Match", "SetFormation", {integer("slot", 1, 8)}, "not during a match (move single players with MovePlayer)", true},
        [](const Values&) {
            fail("E_RULE", "the original offers no formation templates during a match; MovePlayer places single players");
        });
    for (const char* name : {"SetMarking", "SetOrder", "Open", "Watch", "Pause", "Resume"})
        add({"Match", name, {}, "not in the console yet", false},
            [name](const Values&) { fail("E_ENGINE_GAP", std::string(name) + " during the match is not in the console yet"); });
    // GoBack in the match: the original opens the tactics of the own team (02-fenster 3);
    // until Match.Tactics exists the clock simply keeps waiting
    closers_["Match"] = [this] { emit('+', "the match waits: Step(n) or Run(Halftime|End)"); };
}

std::string Console::match_status() const {
    if (!run_) return {};
    if (loan_return_offer_active()) return "matchday paused for loan-return negotiation";
    return "matchday running, minute " + std::to_string(run_->minute);
}

}  // namespace bmh::cli::console
