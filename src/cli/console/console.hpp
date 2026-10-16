// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The console language of openbmh_cli (docs/CLI_CONCEPT):
//   - input in blocks of command lines, executed by a line "go." (01-grundlagen 1.1)
//   - output lines with a class character (# status, > option, | table, + ok, ! error,
//     ? question, i info, ~ event, c chat), the status lines "# Window:" / "# Player:" printed
//     whenever window or player changed (2.2); --json gives one JSON object per line
//   - global commands ListOptions, GoBack, Chat, ChatLog, Answer, Status, Help, Quit (9)
//
// Stage 0 (console.cpp, windows_start.cpp): options screen NewGame, load browser.
// Stage 1: manager selection NewGame.Select (+ Confirm), the info sequence after the last
//   manager, Desktop with its displays, Open/Key, Kickoff (time "instant"), the file windows
//   LoadGame/SaveGame, Mailbox, LeagueTable and Fixtures (windows_desk.cpp, windows_tables.cpp).
//
// The console owns no game logic: everything that changes the game goes through
// app::CareerSession (including its staged Source modal callbacks), like the SDL app. It never
// draws random numbers itself; where the original draws numbers when a window opens (the
// fixtures window), the console opens it through the same engine call.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "app/career_session.hpp"
#include "game/balance_plan.hpp"
#include "game/bank_credit.hpp"
#include "game/desk_actions.hpp"
#include "cli/console/parser.hpp"
#include "cli/json.hpp"
#include "formats/le.hpp"
#include "formats/textd.hpp"
#include "ui/league_pairings.hpp"
#include "ui/club_info.hpp"
#include "ui/mailbox_state.hpp"
#include "ui/outgoing_transfer.hpp"
#include "ui/preparation.hpp"
#include "ui/manager.hpp"
#include "ui/setup.hpp"

namespace bmh::cli::console {

// How a matchday runs (01-grundlagen 7, 07-spieltag 3.2): stepped = the clock moves only on
// Match.Step / Match.Run and the managers can intervene; instant = the day runs through and
// only the results are printed.
enum class TimeMode { stepped, instant };

struct Options {
    std::filesystem::path data;            // the BMH directory (data files)
    std::filesystem::path save_directory;  // where LoadGame/Save look; empty = data
    std::uint32_t seed = 1;                // Watcom seed of the session (determinism)
    bool json = false;                     // one JSON object per output line
    TimeMode time = TimeMode::stepped;
};

// A command's parameter as ListOptions/Help show it and the argument check uses it.
struct Param {
    enum class Type { integer, boolean, text, choice };
    std::string name;
    Type type = Type::integer;
    long long min = 0, max = 0;           // integer range; text: max length (0 = none)
    bool optional = false;
    std::vector<std::string> choices;     // choice: allowed names (index = number)
    std::string display;                  // overrides the generated "name:type" text
};

struct Spec {
    std::string window;                   // "*" = every window
    std::string name;                     // canonical display name, e.g. "ListOptions"
    std::vector<Param> params;
    std::string description;
    bool changes = false;                 // changes the game: only the active player
};

class Console {
public:
    // `write` receives every finished output line (without the newline).
    Console(Options options, std::function<void(const std::string&)> write);
    ~Console();

    // Prints the first status lines. Call once before feeding input.
    void start();
    // One line of input. Command lines are collected until "go.". Returns false once the
    // session has ended (Quit); later input is ignored.
    bool feed(std::string_view line);
    bool ended() const { return ended_; }
    // Whose turn it is (1-4, 0 = All during a live match), the open window and the player an
    // open question belongs to (0 = none): what the multi-seat mailbox (--seats) waits on.
    int active_player() const { return player_; }
    const std::string& window() const { return window_; }
    int question_player() const { return question_ ? question_->player : 0; }
    // Every finished output line with its meaning, independent of text or JSON format: the
    // class character (#, |, +, !, ?, i, ~, c, >), the player it belongs to (0 = All), the
    // plain text and the formatted line as written. The seat hub (several clients in one game)
    // routes lines by these instead of parsing them.
    struct LineInfo {
        char cls = 0;
        int player = 0;
        std::string text;
        std::string line;
    };
    void set_line_observer(std::function<void(const LineInfo&)> observer) { observer_ = std::move(observer); }

    // The input lines of the last executed block and its output, for --log / --replay.
    // After start(): no input, the start output.
    const std::vector<std::string>& last_block() const { return last_block_; }
    const std::vector<std::string>& last_output() const { return last_output_; }

    // All registered commands (for tests against docs/CLI_CONCEPT/commands.csv).
    const std::vector<Spec>& specs() const { return specs_; }
    // "window/Name" of every command that ran without an error (coverage, 10-agenten 3).
    const std::set<std::string>& succeeded() const { return succeeded_; }

    // The engine session (tests compare it with season_sim / save files).
    const app::CareerSession& session() const { return *session_; }

    struct Values;  // bound arguments of one call (detail.hpp)

private:
    struct Question {
        int id = 0;
        int player = 1;
        std::string text;
        std::vector<std::string> options;
        std::string default_option;  // the answer GoBack (right mouse button) gives
        std::function<void(const std::string&)> on_answer;
    };
    struct ChatEntry {
        int from = 0, to = 0;
        std::string text;
    };
    using Handler = std::function<void(const Values&)>;

    // registry (one function per command file)
    void add(Spec spec, Handler handler);
    void register_global_commands();
    void register_start_commands();   // windows_start.cpp
    void register_file_commands();    // windows_start.cpp: NewGame.LoadGame, LoadGame, SaveGame
    void register_desk_commands();    // windows_desk.cpp: Desktop, Mailbox
    void register_table_commands();   // windows_tables.cpp: LeagueTable, Fixtures
    void register_club_commands();    // windows_club.cpp: Tactics, Training, TrainingCamp
    void register_money_commands();   // windows_money.cpp: Bank, Stadium, Toto
    void register_office_commands();  // windows_office.cpp: Advertising, Contracts, Settings
    void register_squad_commands();   // windows_squad.cpp: Lineup, Lineup.Pitch, PlayerCard
    void register_transfer_commands(); // windows_transfer.cpp: Transfer, Transfer.Negotiate
    void register_info_commands();    // windows_info.cpp: TeamInfo, HighScore
    void register_match_commands();   // windows_match.cpp: Match (stage 4)
    void register_friendly_commands(); // windows_friendly.cpp: Friendlies (stage 5)
    void register_reschedule_commands(); // windows_reschedule.cpp: Reschedule (stage 5)
    void register_balance_commands(); // windows_balance.cpp: Balance (stage 5)
    void register_environment_commands(); // windows_environment.cpp: Environment (stage 5)
    void register_state_commands();       // windows_state.cpp: State(view), structured data (stage 2.5)
    void register_advice_commands();      // windows_state.cpp: Advice, Scoreboard (learning agents)
    const Spec* find(std::string_view window, std::string_view key) const;

    // execution
    void execute_block();
    void execute(const Command& cmd);
    Values bind(const Spec& spec, const Command& cmd) const;

    // output
    void emit(char cls, const std::string& text, Json data = nullptr);
    void emit_table(const std::vector<std::string>& header, const std::vector<std::vector<std::string>>& rows);
    void flush_status();
    std::string option_line(const Spec& spec) const;
    std::vector<const Spec*> window_specs(bool include_locked) const;
    // A TEXTD text by its index (docs/formats/textd_inventory.csv), as UTF-8.
    std::string text(int index) const;

    // windows and questions
    void set_window(std::string window, int player);
    // Opens a sub window; GoBack returns to the window it was opened from.
    void open_window(std::string window);
    // Leaves the current sub window (GoBack). False if there is nothing to go back to.
    bool close_window();
    void ask(std::string text, std::vector<std::string> options, std::string default_option,
             std::function<void(const std::string&)> on_answer, std::string text_key = {});

    // start (windows_start.cpp)
    std::string options_text() const;
    void new_game_continue();
    void begin_selection(int slot);
    void confirm_selection();
    void finish_career();
    void load_game(const std::filesystem::path& path);
    void save_game(const std::filesystem::path& path, bool overwrite);
    std::filesystem::path save_directory() const;
    std::string shown_directory() const;
    std::filesystem::path browser_file(const std::string& name) const;
    int resolve_club(const std::string& text) const;  // master club index

    // desk (windows_desk.cpp)
    int active_slot() const;
    // The desk views (Advice, State) show the manager on turn; a seat that is not on turn
    // would read another club as its own (learning session, October 2026), so it is refused.
    void own_view_only(int player, const std::string& command) const;
    ui::DeskState desk() const;
    // The views of the action windows (training, bank, ...) of the active manager.
    ui::PreparationState prep() const;
    // Runs one engine action of an action window; `ok` is the dispatch result.
    void require_engine(bool ok, const std::string& what, const std::string& error);
    void enter_desktop();
    void open_menu(int menu);
    void kickoff();
    void finish_kickoff(const ui::DeskState& before, const TurnAdvanceResult& advanced, const std::string& error);
    // stage 4 (windows_match.cpp): the kickoff on the engine thread and its pauses
    void start_matchday(const ui::DeskRequest& request, const ui::DeskState& before);
    void resume_matchday();     // let the engine run until its next pause or the end
    void engine_returned();     // the engine waits (minute, question) or has finished
    void show_match_pause();
    void stop_matchday();       // abort and join a started day (Quit, destructor)
    bool loan_return_offer_active() const;
    bool loan_return_modal_active() const;
    std::optional<app::CareerSession::TransferOfferEditorDispatch> dispatch_loan_return_offer(
        const ui::OutgoingTransferRequest&, const HumanBuyerSaleHooks&, std::string&,
        const app::TransferOfferTeamInfoPresenter&, const app::TransferOfferPlayerInfoPresenter&);
    void finish_loan_return_offer();
    std::string match_status() const;
    struct EngineRun;
    std::shared_ptr<EngineRun> run_;  // shared_ptr: EngineRun is complete only in windows_match.cpp
    void report_new_mail();
    void flush_info();
    // Engine hooks of a new session (cup draws -> Info.CupDraw).
    void install_hooks();

    // tables (windows_tables.cpp)
    void show_league_table();
    void open_fixtures();
    void show_fixtures();
    void fixtures_action(LeaguePairingsAction kind, int value = -1);
    bool quiet_fixtures_ = false;  // SetMatchday pages through without printing each page
    bool squad_view_b_ = false;    // Lineup: DATEN-B (age, skills, career numbers) instead of DATEN-A

    Options options_;
    std::function<void(const std::string&)> write_;
    std::function<void(const LineInfo&)> observer_;
    std::vector<Spec> specs_;
    std::map<std::string, Handler> handlers_;  // window + "/" + key
    // what Open(menu) does for the windows of the command files (menu_kind -> opener)
    std::map<int, std::function<void()>> openers_;
    // what GoBack does in windows whose closing is an engine step (Fixtures, Stadium)
    std::map<std::string, std::function<void()>> closers_;
    TextDb texts_;
    LeImage exe_;

    std::unique_ptr<app::CareerSession> session_;
    uint64_t revision_ = 0;        // CareerSession request revision
    bool career_ = false;          // a career is running (after finish or load)
    ui::SetupOptions setup_;
    std::string window_ = "NewGame";
    int player_ = 1;               // 1-4, 0 = All
    std::vector<std::string> back_;  // windows GoBack returns to
    std::string printed_window_;
    int printed_player_ = -1;
    std::optional<Question> question_;
    int next_question_ = 1;
    std::vector<ChatEntry> chat_;
    bool ended_ = false;

    // manager selection: the original picker model for the player choosing (tab ranges,
    // defaults), the confirmed choices and the draft of the current player
    std::unique_ptr<ui::ManagerScreen> picker_;
    std::vector<ui::ManagerChoice> confirmed_;
    ui::ManagerChoice draft_;

    // file windows
    std::filesystem::path browse_directory_;
    std::string selected_file_;

    // info windows produced inside engine calls (cup draws), printed after the call
    std::vector<std::function<void()>> pending_info_;

    // mailbox: the messages of the last ListMessages (msg numbers refer to it) and the
    // messages every player has already been told about
    std::vector<ui::MailboxMessage> listed_mail_;
    std::map<int, std::set<std::string>> seen_mail_;

    // request ids of the engine calls that need increasing ids (advertising, contracts)
    uint64_t next_request_id_ = 1;
    // the open contract negotiation (Contracts.Negotiate)
    struct Negotiation {
        int row = -1, player_id = -1;
        std::string name;
        uint64_t quote_id = 0;
        std::array<uint32_t, 4> demands{};
        int years = -1;          // offered term, -1 = no salary offer yet
        uint32_t salary = 0;     // offered monthly salary
        std::array<uint8_t, 7> bonuses{};
    };
    std::optional<Negotiation> negotiation_;

    // squad windows (windows_squad.cpp)
    void show_player_card();
    void show_player_statistics(int chart, bool absolute);
    void show_pitch();
    void show_pairs();
    // the open offer editor of Transfer.Negotiate
    std::optional<ui::OutgoingTransferEditorState> offer_editor_;
    bool offer_submitted_ = false;  // a bid of this editor stands (closing does not withdraw it)
    // the bank's credit editor draft (Bank.Credit)
    std::optional<BankCreditDraft> credit_draft_;
    int credit_counterparty_ = 0;  // Bank.Credit: 0 the bank, else the lending manager + 1
    // the friendly offer of the Friendlies window
    std::optional<FriendlyBookingQuote> friendly_quote_;
    std::optional<BalancePlanDraft> balance_draft_;  // the Balance window (BalanceFromGame2 locals)
    // the open club information (TeamInfo)
    std::optional<ui::ClubInfoProjection> team_info_;
    int card_row_ = -1;           // the squad row of the open player card
    int chart_ = 0;               // PlayerCard.Statistics: Value, Form, Strength, Total
    bool chart_absolute_ = true;

    // tables
    int table_league_ = -1;
    std::optional<ui::LeaguePairingsState> pairings_;

    std::set<std::string> succeeded_;
    std::vector<std::string> pending_;  // command lines before "go."
    std::vector<std::string> last_block_, last_output_;
};

}  // namespace bmh::cli::console
