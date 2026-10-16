// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "test_inputs.hpp"
// Tests of the console language of openbmh_cli (docs/CLI_CONCEPT, stage 0):
//   1. parser: names, @n, positional/named arguments, strings, syntax errors
//   2. golden transcripts tests/cli_console/*.txt, run from program start. Format:
//        "< line"  an input line, fed as written (commands, "go.", "clear.", comments)
//        ";; ..."  a comment of the transcript itself
//        other non-empty lines: the expected output lines in order (text mode)
//      The save directory is the third argument (the game-end validation folder with INPUT.FBM).
//   3. JSON mode: every line is an object with class/window/player/text
//   4. replay/determinism: the same input gives the same output twice
//   5. registry: every registered command exists in docs/CLI_CONCEPT/commands.csv
//   6. stage 1: a console career equals the direct CareerSession career (season_sim's
//      start and loop) after 40 kickoffs, byte for byte in the saved game; Load + Save keeps it
//
// Arguments: <BMH data> <tests/cli_console> <save directory> <docs/CLI_CONCEPT/commands.csv>

#include "cli/console/console.hpp"
#include "cli/console/detail.hpp"

#include "formats/fbm.hpp"
#include "game/lineup.hpp"
#include "game/cup.hpp"
#include "game/save.hpp"
#include "game/match_engine.hpp"
#include "game/reschedule.hpp"
#include "game/turn_driver.hpp"
#include "game/weekly.hpp"
#include "game/messages.hpp"
#include "ui/desktop.hpp"
#include "ui/flow.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
using namespace bmh;
using namespace bmh::cli;
using namespace bmh::cli::console;

void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void parser_tests() {
    Command c = parse_command("  @2 List.Options ( all ) ");
    check(c.player == 2 && c.key == "listoptions" && c.args.size() == 1 && c.args[0].text == "all", "parser: @n and name");
    c = parse_command(R"(Chat(3, "Hallo, \"Welt\"", to=1))");
    check(c.args.size() == 3 && c.args[1].quoted && c.args[1].text == "Hallo, \"Welt\"" && c.args[2].name == "to",
          "parser: quoted string with comma and escapes, named argument");
    c = parse_command("SetX([1, 2], name_with.dots = 5)");
    check(c.args.size() == 2 && c.args[0].text == "[1, 2]" && c.args[1].name == "namewithdots", "parser: list, canonical name");
    check(parse_command("Status").args.empty() && parse_command("Status()").args.empty(), "parser: no arguments");
    for (const char* bad : {"Chat(to=1, \"x\")", "Chat(\"x)", "Chat(1,)", "Chat x", "@5 Status", "@ Status", "(1)", "A([1)"}) {
        bool thrown = false;
        try {
            parse_command(bad);
        } catch (const ParseError&) {
            thrown = true;
        }
        check(thrown, std::string("parser accepts: ") + bad);
    }
    check(classify_line(" GO. ") == LineKind::go && classify_line("Clear.") == LineKind::clear &&
              classify_line("// x") == LineKind::skip && classify_line("") == LineKind::skip &&
              classify_line("go") == LineKind::command,
          "classify_line");
    Arg a;
    a.text = "1_500_000";
    check(to_int(a) == 1500000, "to_int with _");
    a.text = "-3";
    check(to_int(a) == -3, "to_int negative");
    a.text = "3x";
    check(!to_int(a), "to_int garbage");
    a.text = "Yes";
    check(to_bool(a) == true, "to_bool yes");
    a.quoted = true;
    check(!to_bool(a), "quoted is not a bool");
}

Options options(const std::filesystem::path& data, const std::filesystem::path& saves, bool json = false) {
    Options o;
    o.data = data;
    o.save_directory = saves;
    o.seed = 1;
    o.json = json;
    o.time = TimeMode::instant;  // the tests that step through a match say so
    return o;
}

std::vector<std::string> run(const Options& o, const std::vector<std::string>& input) {
    std::vector<std::string> out;
    Console console(o, [&](const std::string& line) { out.push_back(line); });
    console.start();
    for (const auto& line : input) console.feed(line);
    return out;
}

std::string trim_right(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

void golden_tests(const std::filesystem::path& data, const std::filesystem::path& dir, const std::filesystem::path& saves) {
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::directory_iterator(dir))
        if (e.path().extension() == ".txt") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    check(!files.empty(), "no golden transcripts in " + dir.string());
    for (const auto& file : files) {
        std::ifstream in(file, std::ios::binary);
        std::vector<std::string> input, expected;
        std::string line;
        while (std::getline(in, line)) {
            line = trim_right(line);
            if (line.rfind("< ", 0) == 0) input.push_back(line.substr(2));
            else if (line == "<") input.push_back("");
            else if (!line.empty() && line.rfind(";;", 0) != 0) expected.push_back(line);
        }
        std::vector<std::string> actual = run(options(data, saves), input);
        for (auto& l : actual) l = trim_right(l);
        for (size_t i = 0; i < std::max(actual.size(), expected.size()); ++i) {
            const std::string a = i < actual.size() ? actual[i] : "<end>";
            const std::string e = i < expected.size() ? expected[i] : "<end>";
            if (a != e) {
                std::cerr << "--- actual output of " << file.filename().string() << ":\n";
                for (const auto& l : actual) std::cerr << l << '\n';
                throw std::runtime_error(file.filename().string() + " line " + std::to_string(i + 1) + ": expected \"" + e +
                                         "\", got \"" + a + "\"");
            }
        }
    }
}

void json_tests(const std::filesystem::path& data, const std::filesystem::path& saves) {
    const auto out = run(options(data, saves, true), {"Bogus", "go.", "NewGame(5, 3, 1, 3, true, false)", "go."});
    check(out.size() >= 4, "json: too few lines");
    std::set<std::string> classes;
    for (const auto& line : out) {
        const Json j = parse_json(line);
        const auto* o = j.object();
        check(o && o->count("class") && o->count("window") && o->count("player") && o->count("text"), "json: fields in " + line);
        classes.insert(*o->at("class").string());
    }
    check(classes.count("status") && classes.count("error") && classes.count("ok"), "json: classes");
    const Json last = parse_json(out.back());
    check(*last.object()->at("window").string() == "NewGame.Select", "json: window after NewGame");
}

void replay_tests(const std::filesystem::path& data, const std::filesystem::path& saves) {
    const std::vector<std::string> input = {"Chat(\"hi\")", "SetDifficulty(7)", "ShowOptions", "go.", "OpenLoadBrowser", "ListFiles",
                                            "Load(INPUT)", "go.", "Status", "Quit", "go.", "Answer(1, No)", "go."};
    const auto first = run(options(data, saves), input);
    const auto second = run(options(data, saves), input);
    check(first == second, "replay: output differs between two identical runs");
}

std::vector<char> file_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    check(bool(in), "cannot read " + path.string());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Stage 1 acceptance (10-agenten-und-tests 3): a career played through the console equals
// the same career driven directly through CareerSession like tests/season_sim.cpp (default
// options, one manager, club 0, upper third, seed 1), and a saved game survives Load/Save.
//
// The engine asks questions during a kickoff (a player's salary demand, the offers for an own
// player). The console asks them as "?" lines and the test answers each with GoBack, which is
// its default answer (No / decline); the direct run gets the same answers through the same
// hooks in the session's interactive mode.
void default_answers(app::CareerSession& session) {
    PendingOfferHooks hooks;
    hooks.answer = [](const GameState&, const PendingOfferQuestion&) { return PendingOfferAnswer::no; };
    hooks.select_transfer = [](const GameState&, const TransferOfferListView&) {
        return PendingOfferTransferChoice{PendingOfferTransferChoiceKind::decline, -1};
    };
    hooks.notice = [](const GameState&, const PendingOfferNotice&) {};
    session.set_pending_offer_hooks(std::move(hooks));
    session.set_pending_offer_entry_policy(app::PendingOfferEntryPolicy::normal_interactive);
    session.set_transfer_offer_selector([](const GameState&, const TransferOfferListView&) { return -1; });
}

// Feeds one block and then GoBack while a question is open; returns the whole output.
std::vector<std::string> play(Console& console, std::vector<std::string>& out, const std::vector<std::string>& lines,
                              bool answer_questions = true) {
    std::vector<std::string> all;
    out.clear();
    for (const auto& l : lines) console.feed(l);
    console.feed("go.");
    for (int guard = 0; guard < 50; ++guard) {
        all.insert(all.end(), out.begin(), out.end());
        const bool question = std::any_of(out.begin(), out.end(), [](const std::string& l) { return l.rfind("? Q", 0) == 0; });
        if (!question || !answer_questions) break;
        out.clear();
        console.feed("GoBack");
        console.feed("go.");
    }
    return all;
}

// Issue36: deterministic owned special-entry bid, without waiting for an AI offer.
// The fixture writes only before the worker starts; it is not a production API.
void install_modal_bid(app::CareerSession& session) {
    auto& g = session.state();
    const int manager = g.memory.u8(::bmh::layout::ActiveManager);
    const int seller = g.manager_teams.at(size_t(manager));
    const auto squad = squad_range(seller);
    check(squad.count > 5 && squad.first + 5 < g.squad_slots.size(), "modal fixture: squad");
    const int player = g.squad_slots.at(squad.first + 5) & 0xfff;
    check(player != 0xfff && size_t(player) < g.players.size(), "modal fixture: player");
    constexpr uint32_t list = ::bmh::layout::EditorList + 40 * 0x58;
    for (int raw = 0; raw < 4; ++raw) {
        g.memory.set32(::bmh::layout::OfferSlots + uint32_t(raw) * 4, 0xffffffffu);
        g.squad_slots.at(0xb70 + 40 + size_t(raw)) = 0x0fff;
    }
    for (uint32_t i = 0; i < 0x58; ++i) g.memory.set8(list + i, 0);
    g.memory.set16(list, uint16_t(seller == 0 ? 1 : 0));
    for (uint32_t i = 2; i < 8; i += 2) g.memory.set16(list + i, 0xffff);
    g.memory.set16(list + 8, uint16_t(seller));
    g.memory.set32(list + 0x0c, 50000);
    g.memory.set32(list + 0x1c, 1000000);
    g.memory.set8(list + 0x34, 24);
    g.squad_slots.at(0xb70 + 40) = uint16_t(player);
    g.memory.set32(::bmh::layout::OfferSlots, uint32_t(player) << 10 | uint32_t(manager));
}

void check_modal_checkpoint(const app::CareerSession::ManagerJoinCheckpoint& before,
                            const app::CareerSession::ManagerJoinCheckpoint& after,
                            const std::string& where) {
    // These are concrete exposed invariants, not a claim to compare every owner.
    check(save_stream(before.state) == save_stream(after.state), where + ": Native drift");
    check(before.state.date == after.state.date && before.state.indoor == after.state.indoor &&
          before.state.transfer_notice_suppressed == after.state.transfer_notice_suppressed,
          where + ": qualified runtime drift");
    check(before.game.seed() == after.game.seed() && before.game.calls() == after.game.calls() &&
          before.rand.seed() == after.rand.seed() && before.rand.calls() == after.rand.calls(), where + ": RNG drift");
    check(before.driver.stage == after.driver.stage && before.driver.manager_position == after.driver.manager_position &&
          before.driver.revision == after.driver.revision && before.driver.calendar_entry == after.driver.calendar_entry &&
          before.driver.loaded_matchday_resume == after.driver.loaded_matchday_resume &&
          before.driver.suppress_remaining_pre_match_offers == after.driver.suppress_remaining_pre_match_offers &&
          before.driver.enable_national_nomination_desks == after.driver.enable_national_nomination_desks &&
          before.driver.next_event_id == after.driver.next_event_id && before.desk_callargs == after.desk_callargs,
          where + ": turn cursor drift");
}

void modal_batch_tests(const std::filesystem::path& data) {
    std::vector<std::string> out;
    Console console(options(data, std::filesystem::temp_directory_path()), [&](const std::string& l) { out.push_back(l); });
    auto feed = [&](const std::vector<std::string>& lines) {
        out.clear();
        for (const auto& l : lines) console.feed(l);
        console.feed("go.");
        return out;
    };
    auto contains = [](const std::vector<std::string>& lines, const std::string& needle) {
        return std::any_of(lines.begin(), lines.end(), [&](const auto& l) { return l.find(needle) != std::string::npos; });
    };
    auto require_no_error = [](const std::vector<std::string>& lines) {
        for (const auto& l : lines) check(l.rfind("! ", 0) != 0, "modal: " + l);
    };
    auto question_id = [](const std::vector<std::string>& lines) {
        for (const auto& l : lines) if (l.rfind("? Q", 0) == 0) return std::stoi(l.substr(3));
        return 0;
    };
    console.start();
    require_no_error(feed({"NewGame(5, 3, 1, 3, true, false)", "SelectTeam(Tester, \"Schalke\", Bundesliga, Middle, 2)"}));
    require_no_error(feed({"Answer(1, Yes)"}));
    // Console owns a non-const Session. This test-only cast installs a fixed bid
    // before Kickoff; no console-thread mutation is used during the engine pause.
    install_modal_bid(const_cast<app::CareerSession&>(console.session()));
    const auto before = console.session().manager_join_checkpoint();
    const auto first = feed({"Kickoff", "ShowDate", "ShowOpponent", "Open(Lineup)", "ShowSquad", "GoBack"});
    const int qid = question_id(first);
    check(qid > 0 && contains(first, "Offers for"), "modal: fixed bid did not open transfer question");
    check(contains(first, "E_PENDING_QUESTION") && contains(first, "E_BLOCK_ABORTED: 2 commands skipped"),
          "modal: navigation must abort the remaining block");
    check(console.window() == "Desktop" && console.question_player() == 1, "modal: question/window owner lost");
    check_modal_checkpoint(before, console.session().manager_join_checkpoint(), "modal batch rejection");
    check(!console.session().preparation_projection(1).lineup.entry_prepared, "modal: Lineup entry ran");
    for (const char* command : {"Open(12)", "Key(G)", "OpenMailbox", "PullPlug", "Open(SaveGame)", "Open(LoadGame)", "Quit(force=true)"}) {
        const auto lines = feed({command, "GoBack"});
        check(contains(lines, "E_PENDING_QUESTION") && contains(lines, "E_BLOCK_ABORTED: 1 command skipped"),
              std::string("modal: unsafe false-Spec accepted: ") + command);
        check(console.question_player() == 1 && console.window() == "Desktop", "modal: unsafe command consumed question");
        check_modal_checkpoint(before, console.session().manager_join_checkpoint(), command);
    }
    const auto stale = feed({"Answer(999, Decline)"});
    check(contains(stale, "E_BAD_ARGUMENT") && console.question_player() == 1, "modal: bad ID consumed question");
    const auto wrong_owner = feed({"@2 Answer(" + std::to_string(qid) + ", Decline)"});
    check(contains(wrong_owner, "E_NOT_YOUR_TURN") && console.question_player() == 1, "modal: wrong owner consumed question");
    require_no_error(feed({"Status", "Help", "ListOptions", "Chat(\"question still open\")", "ChatLog", "ShowDate", "ShowOpponent"}));
    check(console.question_player() == 1, "modal: observer consumed question");
    check_modal_checkpoint(before, console.session().manager_join_checkpoint(), "modal observers");
    auto lines = feed({"Answer(" + std::to_string(qid) + ", Decline)"});
    require_no_error(lines);
    for (int i = 0; console.question_player() && i < 20; ++i) require_no_error(feed({"GoBack"}));
    check(console.question_player() == 0 && console.window() == "Desktop", "modal: answer did not finish to next desk");
    require_no_error(feed({"Kickoff"}));
    for (int i = 0; console.question_player() && i < 20; ++i) require_no_error(feed({"GoBack"}));
    check(console.question_player() == 0, "modal: next kickoff did not finish");

    // Exact control: same seed/bid/answers, with no rejected navigation commands.
    std::vector<std::string> control_out;
    Console control(options(data, std::filesystem::temp_directory_path()), [&](const auto& l) { control_out.push_back(l); });
    control.start();
    auto control_feed = [&](const std::vector<std::string>& commands) {
        control_out.clear();
        for (const auto& c : commands) control.feed(c);
        control.feed("go.");
        require_no_error(control_out);
    };
    control_feed({"NewGame(5, 3, 1, 3, true, false)", "SelectTeam(Tester, \"Schalke\", Bundesliga, Middle, 2)"});
    control_feed({"Answer(1, Yes)"});
    install_modal_bid(const_cast<app::CareerSession&>(control.session()));
    for (int turn = 0; turn < 2; ++turn) {
        control_feed({"Kickoff"});
        for (int i = 0; control.question_player() && i < 20; ++i) control_feed({"GoBack"});
        check(control.question_player() == 0, "modal control: question did not complete");
    }
    check_modal_checkpoint(control.session().manager_join_checkpoint(), console.session().manager_join_checkpoint(),
                           "modal rejected batch vs answered control");
}

// Issue41: a human bidder must see the Source result even if an AI club wins.
// Install an ordinary deadline before the worker starts, then use actual Kickoff
// and the console's engine-thread handoff (not a direct observer invocation).
void transfer_result_notice_tests(const std::filesystem::path& data) {
    for (bool accepted : {true, false}) {
        std::vector<std::string> out;
        Console console(options(data, std::filesystem::temp_directory_path()),
                        [&](const auto& line) { out.push_back(line); });
        auto feed = [&](const std::vector<std::string>& commands) {
            out.clear();
            for (const auto& command : commands) console.feed(command);
            console.feed("go.");
            for (const auto& line : out) check(line.rfind("! ", 0) != 0, "bid notice: " + line);
            return out;
        };
        console.start();
        feed({"NewGame(5, 3, 1, 3, true, false)", "SelectTeam(Tester, \"Schalke\", Bundesliga, Middle, 2)"});
        feed({"Answer(1, Yes)"});
        auto& g = const_cast<app::CareerSession&>(console.session()).state();
        int seller = -1, player = -1, buyer = -1;
        for (int team = 0; team < 18 && player < 0; ++team) {
            if (g.manager_of(team) >= 0) continue;
            const auto squad = squad_range(team);
            for (size_t row = 11; row < squad.count; ++row) {
                const int candidate = g.squad_slots.at(squad.first + row) & 0xfff;
                if (candidate != 0xfff && nationality(g, candidate) == 0) {
                    seller = team; player = candidate; break;
                }
            }
        }
        for (int team = 0; team < 18 && buyer < 0; ++team) {
            if (team != seller && g.manager_of(team) < 0 &&
                squad_count(g, team, 3) < int(squad_range(team).count) - 1) buyer = team;
        }
        check(player >= 0 && buyer >= 0, "bid notice: domestic AI seller/player/buyer fixture");
        constexpr uint32_t list = ::bmh::layout::EditorList;
        for (uint32_t i = 0; i < 0x58; ++i) g.memory.set8(list + i, 0);
        const int human = g.manager_teams.at(0);
        g.memory.set16(list, uint16_t(human)); // rejected human row precedes the AI winner
        g.memory.set16(list + 2, uint16_t(buyer));
        g.memory.set16(list + 4, 0xffff); g.memory.set16(list + 6, 0xffff);
        g.memory.set16(list + 8, uint16_t(seller));
        g.memory.set32(list + 0x2c, 50000); g.memory.set32(list + 0x30, 1000);
        g.memory.set32(list + 0x20, accepted ? 150000 : 0);
        g.memory.set32(list + 0x10, accepted ? 10000 : 0);
        g.memory.set8(list + 0x34, 12); g.memory.set8(list + 0x35, 12);
        g.memory.set8(list + 0x55, 100); g.memory.set8(list + 0x56, 1);
        g.squad_slots.at(0xb70) = uint16_t(player);
        const std::string prefix = bmh::cli::console::detail::utf8(g.player_name(player)) + ": ";
        const std::string human_result = prefix + bmh::cli::console::detail::utf8(g.club_name(human)) + ": " +
            bmh::cli::console::detail::utf8(message_text(g, ::bmh::layout::IncomingTransfer_RenderAtElapsed_TextPointer4));
        const std::string ai_result = prefix + bmh::cli::console::detail::utf8(g.club_name(buyer)) + ": " +
            bmh::cli::console::detail::utf8(message_text(g, accepted ? ::bmh::layout::IncomingTransfer_RenderAtElapsed_TextPointer3 : ::bmh::layout::IncomingTransfer_RenderAtElapsed_TextPointer4));
        std::vector<std::string> notices;
        for (int turn = 0; turn < 8; ++turn) {
            auto lines = feed({"Kickoff"});
            notices.insert(notices.end(), lines.begin(), lines.end());
            for (int q = 0; console.question_player() && q < 20; ++q) {
                lines = feed({"GoBack"});
                notices.insert(notices.end(), lines.begin(), lines.end());
            }
            if (std::any_of(notices.begin(), notices.end(), [&](const auto& line) {
                    return line.find(ai_result) != std::string::npos; })) break;
        }
        for (const auto& expected : {human_result, ai_result}) {
            check(std::count_if(notices.begin(), notices.end(), [&](const auto& line) {
                      return line.find(expected) != std::string::npos; }) == 1,
                  "bid notice: missing or duplicate Source result: " + expected);
        }
        bool bought = false;
        const auto squad = squad_range(buyer);
        for (size_t row = 0; row < squad.count; ++row)
            bought |= (g.squad_slots.at(squad.first + row) & 0xfff) == player;
        check(bought == accepted, "bid notice: settlement must match the announced AI outcome");
        check(!g.transfer_decision_observer, "bid notice: console observer survived completed kickoff");
    }
}

void kickoff_reentrant_tests(const std::filesystem::path& data) {
    app::CareerSession session(data, 1u);
    ui::SetupOptions options;
    ui::ManagerChoice manager;
    manager.name = "Modal"; manager.master_club = 0; manager.strength = 2;
    ui::CareerInitializationResult init;
    std::string error;
    check(session.confirm_manager(options, manager, error) && session.finish(init, error), "modal session setup: " + error);
    install_modal_bid(session);
    uint64_t revision = init.desktop.revision;
    const auto before = session.manager_join_checkpoint();
    const auto request = [&] {
        ui::DeskRequest r;
        r.kind = ui::DeskRequestKind::advance_turn; r.manager_slot = session.state().memory.u8(::bmh::layout::ActiveManager); r.revision = revision;
        return r;
    };
    PendingOfferHooks hooks;
    int called = 0;
    hooks.select_transfer = [&](const GameState&, const TransferOfferListView&) {
        ++called;
        ui::LineupRequest r;
        r.kind = ui::LineupRequestKind::prepare_lineup;
        r.manager_slot = session.state().memory.u8(::bmh::layout::ActiveManager);
        r.runtime_club = session.state().manager_teams.at(size_t(r.manager_slot)); r.revision = revision;
        const auto at_pause = session.manager_join_checkpoint();
        std::string refused;
        check(!session.dispatch_lineup(r, revision, refused) && refused == "lineup_unavailable_during_calendar_advance",
              "modal session: reentrant Lineup entry was accepted");
        check_modal_checkpoint(at_pause, session.manager_join_checkpoint(), "modal session Lineup guard");
        // Deliberately bypass the ordinary API to exercise the existing qualified
        // fingerprint and full stored checkpoint restore, not arbitrary memory detection.
        ++session.state().date; ++revision;
        session.game().range(0, 1); session.rand().range(0, 1);
        return PendingOfferTransferChoice{PendingOfferTransferChoiceKind::decline, -1};
    };
    session.set_pending_offer_entry_policy(app::PendingOfferEntryPolicy::normal_interactive);
    session.set_pending_offer_hooks(hooks);
    auto result = session.dispatch_kickoff(request(), revision, error);
    check(called == 1 && result.kind == TurnDeskKind::Failed && error == "pending_offer_reentrant_state",
          "modal session: reentrant commit was not rejected");
    check(revision == init.desktop.revision && result.revision == revision && result.date == before.state.date,
          "modal session: returned revision/date did not restore");
    check_modal_checkpoint(before, session.manager_join_checkpoint(), "modal session reentrant rollback");
    check(!session.preparation_projection(revision).lineup.entry_prepared, "modal session: entry cache drift");
    default_answers(session);
    result = session.dispatch_kickoff(request(), revision, error);
    check(result.kind != TurnDeskKind::Failed && result.kind != TurnDeskKind::WaitingAtDesk,
          "modal session: next kickoff after rollback failed: " + error);
}

void career_tests(const std::filesystem::path& data, int kickoffs) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "openbmh_cli_console_tests";
    std::filesystem::create_directories(dir);
    for (const char* f : {"DIRECT.FBM", "CONSOLE.FBM", "AGAIN.FBM"}) std::filesystem::remove(dir / f);

    // the engine directly (season_sim's start and loop)
    app::CareerSession direct(data, 1u);
    {
        ui::SetupOptions options;
        ui::ManagerChoice choice;
        choice.name = "SIM";
        choice.master_club = 0;
        choice.strength = 2;
        std::string error;
        check(direct.confirm_manager(options, choice, error), "direct: manager " + error);
        ui::CareerInitializationResult init;
        check(direct.finish(init, error), "direct: start " + error);
        default_answers(direct);
        // This deterministic question test intentionally visits every event desk.
        direct.state().memory.set8(::bmh::layout::MatchdaySetup_MatchdayNeedsManagerDesks_RuntimeField,1);
        uint64_t revision = init.desktop.revision;
        for (int i = 0; i < kickoffs; ++i) {
            ui::DeskRequest kickoff;
            kickoff.kind = ui::DeskRequestKind::advance_turn;
            kickoff.manager_slot = int(direct.state().memory.u8(::bmh::layout::ActiveManager));
            kickoff.revision = revision;
            const TurnAdvanceResult r = direct.dispatch_kickoff(kickoff, revision, error);
            check(r.kind != TurnDeskKind::Failed && r.kind != TurnDeskKind::WaitingAtDesk, "direct: kickoff " + error + r.reason);
        }
        check(direct.save_game(dir / "DIRECT.FBM", int(direct.state().memory.u8(::bmh::layout::ActiveManager)), true, error), "direct: save " + error);
    }

    // the same through the console language
    std::vector<std::string> out;
    {
        Console console(options(data, dir), [&](const std::string& line) { out.push_back(line); });
        console.start();
        std::vector<std::string> all = play(console, out, {"NewGame(5, 3, 1, 3, true, true)", "SelectTeam(SIM, club:0, Bundesliga, Upper, 0)"}, false);
        play(console, out, {"Answer(1, Yes)"}, false);
        play(console, out, {"Open(Settings)", "SetSwitch(F6, off)", "GoBack"}, false);
        int questions = 0;
        for (int i = 0; i < kickoffs; ++i) {
            const auto lines = play(console, out, {"Kickoff"});
            questions += int(std::count_if(lines.begin(), lines.end(), [](const std::string& l) { return l.rfind("? Q", 0) == 0; }));
            all.insert(all.end(), lines.begin(), lines.end());
        }
        const auto saved = play(console, out, {"Open(SaveGame)", "Save(CONSOLE)"});
        all.insert(all.end(), saved.begin(), saved.end());
        for (const auto& line : all) check(line.rfind("! ", 0) != 0, "console career: " + line);
        check(questions > 0, "console career: the engine asked no question in " + std::to_string(kickoffs) + " kickoffs");
    }
    check(file_bytes(dir / "DIRECT.FBM") == file_bytes(dir / "CONSOLE.FBM"),
          "the console career differs from the direct CareerSession career after " + std::to_string(kickoffs) + " kickoffs");

    // Save/Load roundtrip: load the console's save, save it again, same bytes
    out = run(options(data, dir), {"LoadGame(CONSOLE)", "Open(SaveGame)", "Save(AGAIN)", "go."});
    for (const auto& line : out) check(line.rfind("! ", 0) != 0, "roundtrip: " + line);
    // the stream is encrypted with a random key (FBM.md), so compare the decrypted content
    const Savegame first = load_savegame(dir / "CONSOLE.FBM"), again = load_savegame(dir / "AGAIN.FBM");
    check(first.plain_header == again.plain_header, "Load + Save changes the header of the saved game");
    if (first.stream != again.stream) {
        size_t differ = 0, at = 0;
        for (size_t i = 0; i < std::min(first.stream.size(), again.stream.size()); ++i)
            if (first.stream[i] != again.stream[i] && differ++ == 0) at = i;
        check(false, "Load + Save changes " + std::to_string(differ) + " bytes of the game state, first at stream offset " + std::to_string(at));
    }
}

// Expired squad contracts use the same synchronous engine-question pause as the
// other kickoff questions. Invalid input leaves the pending question untouched;
// a selected Source demand renews, while the explicit no-offer choice releases.
void contract_expiry_console_tests(const std::filesystem::path& data) {
    const auto dir = std::filesystem::temp_directory_path() / "openbmh_cli_contract_expiry_tests";
    std::filesystem::create_directories(dir);
    std::vector<std::string> out;
    Console console(options(data, dir), [&](const std::string& line) { out.push_back(line); });
    console.start();
    play(console, out, {"NewGame(5, 3, 1, 3, true, true)", "SelectTeam(SIM, club:0, Bundesliga, Upper, 0)"}, false);
    play(console, out, {"Answer(1, Yes)"}, false);

    auto& session = const_cast<app::CareerSession&>(console.session());
    GameState& state = session.state();
    const int manager = state.memory.u8(::bmh::layout::ActiveManager);
    const auto squad = squad_range(state.manager_teams.at(size_t(manager)));
    std::vector<std::pair<int, int>> expiring;
    for (size_t row = 0; row < squad.count; ++row) {
        const size_t slot = squad.first + row;
        const int player = int(state.squad_slots.at(slot) & 0x0fff);
        if (player == 0x0fff || size_t(player) >= state.players.size()) continue;
        const size_t board = size_t(manager) * 0x840 + row * 0x58;
        const int age = int((state.players[size_t(player)].w364 >> 21) & 0x1f) + 1;
        if (age >= 36 || (state.tactics.at(board + 0x0b) & 0x60)) continue;
        expiring.emplace_back(int(row), player);
        if (expiring.size() == 2) break;
    }
    check(expiring.size() == 2, "contract expiry fixture: two negotiable players");
    for (size_t row = 0; row < squad.count; ++row)
        state.tactics.at(size_t(manager) * 0x840 + row * 0x58 + 0x0c) = 0xff;
    for (const auto [row, _player] : expiring)
        state.tactics.at(size_t(manager) * 0x840 + size_t(row) * 0x58 + 0x0c) = 1;

    auto next_question = [&](int max_kickoffs) {
        for (int kickoff = 0; kickoff < max_kickoffs; ++kickoff) {
            auto lines = play(console, out, {"Kickoff"}, false);
            for (const std::string& line : lines)
                if (line.find("Choose an offer at the player's salary demand") != std::string::npos) return line;
            for (int guard = 0; console.question_player() && guard < 16; ++guard) {
                lines = play(console, out, {"GoBack"}, false);
                for (const std::string& line : lines)
                    if (line.find("Choose an offer at the player's salary demand") != std::string::npos) return line;
            }
        }
        return std::string{};
    };
    auto question_id = [](const std::string& line) {
        const size_t begin = line.find("? Q");
        check(begin != std::string::npos, "contract expiry question line");
        const size_t first = begin + 3, end = line.find(' ', first);
        return line.substr(first, end - first);
    };

    const std::string offer_question = next_question(40);
    check(!offer_question.empty() && console.question_player() == manager + 1,
          "contract expiry question was not presented to the owning manager");
    const std::string id = question_id(offer_question);
    out.clear();
    console.feed("Answer(Q" + id + ", invalid)");
    console.feed("go.");
    check(console.question_player() == manager + 1 &&
              std::any_of(out.begin(), out.end(), [](const std::string& line) { return line.find("E_BAD_ARGUMENT") != std::string::npos; }),
          "invalid contract answer consumed or changed the pending question");
    auto resumed = play(console, out, {"Answer(Q" + id + ", 1)"}, false);
    std::string immediate_second_question;
    for (const std::string& line : resumed)
        if (line.find("Choose an offer at the player's salary demand") != std::string::npos)
            immediate_second_question = line;
    const int renewed_player = expiring[0].second;
    auto locate = [&](int player) {
        const auto begin = state.squad_slots.begin() + long(squad.first);
        const auto end = begin + long(squad.count);
        const auto it = std::find_if(begin, end, [player](uint16_t value) { return int(value & 0x0fff) == player; });
        return it == end ? size_t(-1) : size_t(it - state.squad_slots.begin());
    };
    const size_t renewed_slot = locate(renewed_player);
    check(renewed_slot != size_t(-1), "accepted contract unexpectedly released its player");
    const size_t renewed_row = renewed_slot - squad.first;
    check(renewed_row < squad.count && state.tactics.at(size_t(manager) * 0x840 + renewed_row * 0x58 + 0x0c) == 12,
          "selected one-year Source demand did not write a 12-month term");

    const std::string decline_question = immediate_second_question.empty() ? next_question(1) : immediate_second_question;
    check(!decline_question.empty() && console.question_player() == manager + 1,
          "second expired player did not get its own question");
    const std::string decline_id = question_id(decline_question);
    const auto no_offer = decline_question.rfind("No offer (release player)");
    check(no_offer != std::string::npos, "contract question omitted explicit decline");
    size_t option_count = 0;
    for (size_t at = decline_question.find("options:"); at != std::string::npos && at < decline_question.size();) {
        ++option_count;
        at = decline_question.find(" | ", at + 1);
    }
    play(console, out, {"Answer(Q" + decline_id + ", " + std::to_string(option_count) + ")"}, false);
    check(console.question_player() == 0 && locate(expiring[1].second) == size_t(-1),
          "explicit no-offer choice did not follow the Source player-release path");
}

// Exercise the console worker handoff on an unmodified, naturally played
// human-to-human 12-month loan. No calendar/ledger/roster/RNG fixture writes.
void loan_return_console_tests(const std::filesystem::path& data, const std::filesystem::path& csv) {
    const auto input = bmh::test::external_input("loan-return.fbm");
    const auto dir = std::filesystem::temp_directory_path() / "openbmh_cli_loan_return_tests";
    std::filesystem::create_directories(dir);
    // No; Yes then cancel; Yes then rejected zero offer; Yes then a real bid;
    // force quit at question/editor; failed TeamInfo/PlayerInfo presentation.
    for (int branch = 0; branch < 8; ++branch) {
        std::vector<std::string> out;
        auto o = options(data, dir);
        o.seed = 73;
        bool fail_information = false;
        bool purchase_accepted = false;
        Console console(o, [&](const auto& line) {
            if (fail_information && line.rfind('|', 0) == 0) {
                fail_information = false;
                throw std::runtime_error("injected information presentation failure");
            }
            out.push_back(line);
        });
        console.start();
        auto block = [&](const std::vector<std::string>& commands) {
            const auto lines = play(console, out, commands, false);
            for (const auto& line : lines)
                check(line.rfind("! ", 0) != 0, "loan return branch " + std::to_string(branch) + ": " + line);
            return lines;
        };
        block({"LoadGame(\"" + input.generic_string() + "\")"});
        const auto& session = console.session();
        const auto& state = session.state();
        check(state.date == 395 && state.memory.u8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField) == 1 && state.memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField) == 118,
              "loan return: genuine last-month ledger/snapshot");
        const uint32_t loan = state.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField);
        const int owner = int(loan >> 12 & 0x3ff), borrower = int(loan >> 22 & 0x3ff);
        const int borrower_manager = state.manager_of(borrower);
        auto contains = [&](int club) {
            const auto squad = squad_range(club);
            for (size_t row = 0; row < squad.count; ++row)
                if ((state.squad_slots.at(squad.first + row) & 0xfff) == 118) return true;
            return false;
        };
        auto question_id = [](const std::string& line) {
            const size_t begin = line.find("? Q");
            check(begin != std::string::npos, "loan return: expected a numbered question");
            const size_t first = begin + 3, end = line.find(' ', first);
            return line.substr(first, end - first);
        };
        const auto suffix = detail::utf8(message_text(state, ::bmh::layout::Month_ReturnLoans_TextPointer6));
        std::string question;
        std::optional<app::CareerSession::ManagerJoinCheckpoint> before;
        std::map<uint32_t, uint8_t> before_runtime;
        for (int guard = 0; guard < 20 && question.empty(); ++guard) {
            before = session.manager_join_checkpoint();
            before_runtime = session.club_modal_runtime_checkpoint();
            auto lines = block({"Kickoff"});
            for (int questions = 0; questions < 32; ++questions) {
                for (const auto& line : lines)
                    if (line.rfind("? Q", 0) == 0 && line.find(suffix) != std::string::npos) question = line;
                if (!question.empty() || !console.question_player()) break;
                lines = block({"GoBack"});
            }
        }
        check(!question.empty() && console.question_player() == borrower_manager + 1,
              "loan return: exact Source return question belongs to borrower");
        check(contains(borrower) && !contains(owner) && state.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField) == loan,
              "loan return: question must not commit the staged return");
        if (branch == 4) {
            block({"Quit(force=true)"});
        } else {
            block({"@" + std::to_string(borrower_manager + 1) + " Answer(Q" + question_id(question) +
                   (branch == 0 ? ", No)" : ", Yes)")});
            if (branch != 0) {
                check(console.window() == "Transfer.Negotiate" && console.active_player() == borrower_manager + 1 &&
                      !session.transfer_offer_owner(), "loan return: staged owner, not a live market editor");
                check(contains(borrower) && !contains(owner) && state.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField) == loan,
                      "loan return: opening the editor must not commit the staged return");
                if (branch == 5) {
                    block({"Quit(force=true)"});
                } else if (branch >= 6) {
                    fail_information = true;
                    const auto failed = play(console, out, {branch == 6 ? "ShowTeamInfo" : "ShowPlayerInfo"}, false);
                    check(!fail_information && console.window() == "Desktop" && !console.question_player() &&
                          std::any_of(failed.begin(), failed.end(), [](const auto& l) { return l.find("E_INTERNAL") != std::string::npos; }),
                          "loan return: failed information presenter did not abort its month owner");
                } else {
                    const auto negotiation = block({"ShowNegotiation", "ShowTeamInfo", "ShowPlayerInfo"});
                    uint32_t asking_fee = 0;
                    for (const auto& line : negotiation) {
                        if (line.find("| demand") == std::string::npos) continue;
                        const auto column = line.find('|', 1);
                        asking_fee = uint32_t(std::stoul(line.substr(column + 1)));
                    }
                    check(asking_fee > 0, "loan return: Source fee demand missing from negotiation");
                    // A different seat may observe, but cannot edit the borrower's offer.
                    const auto at_editor = session.manager_join_checkpoint();
                    const int other = borrower_manager == 0 ? 2 : 1;
                    const auto refused = play(console, out, {"@" + std::to_string(other) + " SetOfferField(Fee, 1)"}, false);
                    check(std::any_of(refused.begin(), refused.end(), [](const auto& l) { return l.find("E_NOT_YOUR_TURN") != std::string::npos; }),
                          "loan return: another manager changed the staged offer");
                    check_modal_checkpoint(at_editor, session.manager_join_checkpoint(), "loan return seat guard");
                    for (const char* action : {"GoBack", "Quit(force=true)"}) {
                        const auto close_refused = play(console, out, {"@" + std::to_string(other) + " " + action}, false);
                        check(std::any_of(close_refused.begin(), close_refused.end(), [](const auto& l) { return l.find("E_NOT_YOUR_TURN") != std::string::npos; }) &&
                              !console.ended() && console.window() == "Transfer.Negotiate" && console.active_player() == borrower_manager + 1,
                              "loan return: another manager closed the staged offer");
                        check_modal_checkpoint(at_editor, session.manager_join_checkpoint(), "loan return close guard");
                    }
                    if (branch == 1) {
                        block({"SetOfferField(Years, 1)", "SetOfferField(Fee, " + std::to_string(asking_fee) + ")", "GoBack"});
                    } else {
                        if (branch == 3) block({"SetOfferField(Fee, " + std::to_string(asking_fee * 2) + ")", "SetOfferField(Salary, 200000)", "SetOfferField(Years, 1)"});
                        const auto submitted = block({"SubmitOffer"});
                        purchase_accepted = std::any_of(submitted.begin(), submitted.end(), [](const auto& l) { return l.find("offer accepted") != std::string::npos; });
                        check(std::any_of(submitted.begin(), submitted.end(), [](const auto& l) {
                            return l.find("offer rejected") != std::string::npos || l.find("offer pending") != std::string::npos ||
                                   l.find("offer accepted") != std::string::npos;
                        }), "loan return: Source submission result missing");
                    }
                }
            }
        }
        check(!state.loan_return_screen && !state.loan_return_offer_screen && !state.contract_screen,
              "loan return: finished/aborted worker retained a presenter");
        if (branch >= 4) {
            check(console.ended() == (branch < 6) && !console.question_player(), "loan return: abort did not join the modal worker");
            check_modal_checkpoint(*before, session.manager_join_checkpoint(), "loan return abort rollback");
            check(before_runtime == session.club_modal_runtime_checkpoint(), "loan return abort leaked transient modal bytes");
        } else {
            check(!console.question_player() && console.window() == "Desktop" && state.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField) == 0 &&
                  state.memory.u8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField) == 0 && state.memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField) == 0xfff,
                  "loan return: normal return did not finish or clear the real ledger/snapshot");
            if (branch != 3) check(contains(owner) && !contains(borrower), "loan return: declining/cancelling/rejecting purchased the player");
            else check(contains(borrower) == purchase_accepted && contains(owner) != purchase_accepted,
                       "loan return: reported Source purchase differs from actual squad ownership");
            for (int slot = 0; slot < 4; ++slot)
                check(state.memory.u32(::bmh::layout::OfferSlots + uint32_t(slot) * 4) == 0xffffffffu &&
                      (state.squad_slots.at(2928 + 40 + size_t(slot)) & 0xfff) == 0xfff,
                      "loan return: month caller did not clean its temporary offer row");
            block({"Open(SaveGame)", "Save(RETURN" + std::to_string(branch) + ", true)"});
            const auto saved = load_savegame(dir / ("RETURN" + std::to_string(branch) + ".FBM"));
            check(saved.checksum_ok && saved.stream == save_stream(state), "loan return: normal save does not match committed state");
            app::CareerSession reloaded(data, 73, dir / ("RETURN" + std::to_string(branch) + ".FBM"));
            check(save_stream(reloaded.state()) == saved.stream, "loan return: fresh normal load lost the committed return/purchase");
        }
    }
}

// Stage 4 acceptance (10-agenten-und-tests 3: intervention tests with a fixed seed and minute):
// the console's stepped matchday with a substitution at minute 30 in every match of the human
// team equals the direct engine run with the same intervention in TurnDriver::live_minute
// (tests/season_sim.cpp SIM_INTERVENE: field slot 10 out, slot 11 in, then rerate_team).
void intervention_tests(const std::filesystem::path& data, int kickoffs) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "openbmh_cli_console_tests";
    std::filesystem::create_directories(dir);
    for (const char* f : {"IDIRECT.FBM", "ICONSOLE.FBM"}) std::filesystem::remove(dir / f);

    app::CareerSession direct(data, 1u);
    {
        ui::SetupOptions setup;
        ui::ManagerChoice choice;
        choice.name = "SIM";
        choice.master_club = 0;
        choice.strength = 2;
        std::string error;
        check(direct.confirm_manager(setup, choice, error), "direct: manager " + error);
        ui::CareerInitializationResult init;
        check(direct.finish(init, error), "direct: start " + error);
        default_answers(direct);
        direct.set_live_minute([](int minute, MatchEngine& engine, std::vector<MatchGroup>& groups, GameState& gs) {
            if (minute != 30) return;
            const int team = gs.manager_teams.at(0);
            for (const MatchGroup& g : groups)
                for (int m = 0; m < g.matches(); ++m)
                    if (!(g.void_mask() >> m & 1) &&
                        (g.fixtures.at(size_t(m) * 2) == team || g.fixtures.at(size_t(m) * 2 + 1) == team)) {
                        if (manual_substitution(gs, team, 10, 11)) {
                            MinuteReport report;
                            engine.rerate_team(team, report);
                        }
                        return;
                    }
        });
        uint64_t revision = init.desktop.revision;
        for (int i = 0; i < kickoffs; ++i) {
            ui::DeskRequest kickoff;
            kickoff.kind = ui::DeskRequestKind::advance_turn;
            kickoff.manager_slot = int(direct.state().memory.u8(::bmh::layout::ActiveManager));
            kickoff.revision = revision;
            const TurnAdvanceResult r = direct.dispatch_kickoff(kickoff, revision, error);
            check(r.kind != TurnDeskKind::Failed && r.kind != TurnDeskKind::WaitingAtDesk, "direct: kickoff " + error + r.reason);
        }
        check(direct.save_game(dir / "IDIRECT.FBM", int(direct.state().memory.u8(::bmh::layout::ActiveManager)), true, error), "direct: save " + error);
    }

    Options o = options(data, dir);
    o.time = TimeMode::stepped;
    std::vector<std::string> out;
    Console console(o, [&](const std::string& line) { out.push_back(line); });
    console.start();
    auto block = [&](const std::vector<std::string>& lines, bool answer_questions = true) {
        const auto all = play(console, out, lines, answer_questions);  // questions get their default answer
        std::string window;
        for (const auto& l : all)
            if (l.rfind("# Window: ", 0) == 0) window = l.substr(10);
        return window;
    };
    block({"NewGame(5, 3, 1, 3, true, true)", "SelectTeam(SIM, club:0, Bundesliga, Upper, 0)"}, false);
    block({"Answer(1, Yes)"}, false);
    int matches = 0;
    for (int i = 0; i < kickoffs; ++i) {
        if (block({"Kickoff"}) != "Match") continue;
        ++matches;
        block({"Step(29)"});
        block({"Substitute(row:10, row:11)"});  // refused like manual_substitution when not possible
        check(block({"Run"}) != "Match", "console: the matchday did not end with Run");
    }
    check(matches > 5, "console: only " + std::to_string(matches) + " matchdays with the human team");
    block({"Open(SaveGame)", "Save(ICONSOLE)"});
    check(file_bytes(dir / "IDIRECT.FBM") == file_bytes(dir / "ICONSOLE.FBM"),
          "the stepped console career with substitutions differs from the direct engine run");
}

// Stage 5: a listed player can be sold through the console. The bids at the weekly list
// deadline come as a question (legacy routine chooser); choosing the buyer moves the player.
void sale_tests(const std::filesystem::path& data) {
    std::vector<std::string> out;
    Console console(options(data, std::filesystem::temp_directory_path()), [&](const std::string& l) { out.push_back(l); });
    console.start();
    auto feed = [&](const std::vector<std::string>& lines) {
        out.clear();
        for (const auto& l : lines) console.feed(l);
        console.feed("go.");
        return out;
    };
    feed({"NewGame(5, 3, 1, 3, true, false)", "SelectTeam(Tester, \"Schalke\", Bundesliga, Middle, 2)"});
    feed({"Answer(1, Yes)"});
    feed({"Key(M)", "SellPlayer(Gosens)", "GoBack"});
    bool sold = false;
    for (int day = 0; day < 40 && !sold; ++day) {
        auto lines = feed({"Kickoff"});
        for (int guard = 0; guard < 20; ++guard) {
            const auto q = std::find_if(lines.begin(), lines.end(), [](const std::string& l) { return l.rfind("? Q", 0) == 0; });
            if (q == lines.end()) break;
            const std::string id = q->substr(3, q->find(' ', 3) - 3);  // "? Q<n> ..."
            const bool gosens = q->find("Offers for GOSENS") != std::string::npos;
            lines = feed({gosens ? "Answer(" + id + ", Accept1)" : "GoBack"});
            sold = sold || gosens;
        }
    }
    check(sold, "sale: no bid for the listed player in 40 days");
    for (const auto& l : feed({"Key(A)"})) check(l.find("GOSENS") == std::string::npos, "sale: the sold player is still in the squad");
}

// Stage 5 (C4 2.3.22): a home league match moved to a later free day through the Reschedule
// window is played on that day as a replay: the postponed entry is used and freed again and
// the result byte goes 0xCC -> 0xCE (moved) -> 0xCD (original matchday) -> the score.
void reschedule_console_tests(const std::filesystem::path& data) {
    std::vector<std::string> out;
    Console console(options(data, std::filesystem::temp_directory_path()), [&](const std::string& l) { out.push_back(l); });
    console.start();
    auto feed = [&](const std::vector<std::string>& lines) {
        out.clear();
        for (const auto& l : lines) console.feed(l);
        console.feed("go.");
        for (const auto& l : out) check(l.rfind("! ", 0) != 0, "reschedule: " + l);
        return out;
    };
    feed({"NewGame(5, 3, 1, 3, true, false)", "SelectTeam(Tester, \"Schalke\", Bundesliga, Middle, 2)"});
    feed({"Answer(1, Yes)"});
    const auto& g = console.session().state();
    const auto matches = reschedulable_matches(g, 0);
    check(!matches.empty(), "reschedule: no movable home match");
    const LeagueMatchRef m = matches.front();
    std::string game;
    for (const auto& l : feed({"Open(Reschedule)", "ListRescheduleGames"}))
        if (game.empty() && l.rfind("| 20", 0) == 0) game = l.substr(2, 10);
    check(!game.empty(), "reschedule: ListRescheduleGames is empty");
    std::string to;
    int target = -1;
    for (const auto& l : feed({"ListRescheduleDates(" + game + ")"})) {
        if (l.rfind("| 20", 0) != 0 || target >= 0) continue;
        const int day = std::stoi(l.substr(15));  // "| YYYY-MM-DD | <day> |"
        if (day > m.day) to = l.substr(2, 10), target = day;
    }
    check(target > 0, "reschedule: no later day for the match");
    feed({"MoveGame(" + game + ", " + to + ")", "GoBack"});
    const size_t result = size_t(m.match) + size_t(m.round) * 10 + size_t(m.league) * 0x17c;
    check(g.results.at(result) == 0xce, "reschedule: result byte not 0xCE after the move");
    int entry = -1;
    for (int e = 0; e < 40 && entry < 0; e++)
        if (g.memory.u16(::bmh::layout::Postponed + uint32_t(e) * 6) == target) entry = e;
    check(entry >= 0, "reschedule: no postponed entry for the target day");
    bool seen_cd = false;
    for (int guard = 0; guard < 200 && g.season_day() <= target; ++guard) {
        auto lines = feed({"Kickoff"});
        for (int q = 0; q < 20; ++q) {
            if (std::none_of(lines.begin(), lines.end(), [](const std::string& l) { return l.rfind("? Q", 0) == 0; })) break;
            lines = feed({"GoBack"});
        }
        seen_cd = seen_cd || g.results.at(result) == 0xcd;
    }
    check(seen_cd, "reschedule: the original matchday did not mark the match 0xCD");
    const uint8_t score = g.results.at(result);
    check((score & 0xcc) != 0xcc, "reschedule: the moved match was not played on its new day");
    check(g.memory.u16(::bmh::layout::Postponed + uint32_t(entry) * 6) == 0, "reschedule: the postponed entry was not freed (day " + std::to_string(g.memory.u16(::bmh::layout::Postponed + uint32_t(entry) * 6)) + ", target " + std::to_string(target) + ", today " + std::to_string(g.season_day()) + ", from " + std::to_string(m.day) + ", score " + std::to_string(score) + ")");
}

// Stage 5 (C4 2.3.42): poaching. A club keeps its second goalkeeper; an offer at the demand
// for a forward of another club is accepted at once and he joins the squad; a low fee with
// a high salary brings the DFB fee question. Each attempt counts (five per season).
void poach_tests(const std::filesystem::path& data) {
    std::vector<std::string> out;
    Console console(options(data, std::filesystem::temp_directory_path()), [&](const std::string& l) { out.push_back(l); });
    console.start();
    auto feed = [&](const std::vector<std::string>& lines) {
        out.clear();
        for (const auto& l : lines) console.feed(l);
        console.feed("go.");
        return out;
    };
    auto has = [&](const std::string& text) {
        return std::any_of(out.begin(), out.end(), [&](const std::string& l) { return l.find(text) != std::string::npos; });
    };
    feed({"NewGame(5, 3, 1, 3, true, false)", "SelectTeam(Tester, \"Schalke\", Bundesliga, Middle, 2)"});
    feed({"Answer(1, Yes)"});
    // the players of Augsburg: the goalkeeper listed second, the forward with the best strength
    std::vector<std::pair<std::string, std::string>> players;  // id, position
    feed({"Key(M)", "OpenPoach"});
    for (const auto& l : feed({"ListClubPlayers(club:10)"}))
        if (l.rfind("| id:", 0) == 0) {
            const size_t a = l.find(" | ", 2), b = l.find(" | ", a + 3), c = l.find(" | ", b + 3);
            players.push_back({l.substr(2, a - 2), l.substr(b + 3, c - b - 3)});
        }
    check(players.size() > 10, "poach: Augsburg has no squad");
    std::string keeper, forward;
    int keepers = 0;
    for (const auto& [id, pos] : players) {
        if (pos == "TOR" && ++keepers == 2) keeper = id;
        if (pos == "STU" && forward.empty()) forward = id;
    }
    feed({"PoachPlayer(" + keeper + ")"});
    check(has(bmh::test::expected_text("cli_console_tests-external-4").c_str()) && has("E_RULE"), "poach: the second goalkeeper is kept");
    // the offer at the demand
    std::string fee, salary;
    for (const auto& l : feed({"PoachPlayer(" + forward + ")"}))
        if (l.rfind("| demand |", 0) == 0) {
            const size_t a = l.find(" | ", 2), b = l.find(" | ", a + 3);
            fee = l.substr(a + 3, b - a - 3);
            salary = l.substr(b + 3, l.find(" | ", b + 3) - b - 3);
        }
    check(!fee.empty() && has("# Window: Transfer.Negotiate"), "poach: no offer editor for the forward");
    feed({"SetOfferField(Fee, " + fee + ")", "SetOfferField(Salary, " + salary + ")", "SubmitOffer"});
    // the club may still refuse (W92..98 against its rating); then the DFB sets the fee
    const bool dfb = has(bmh::test::expected_text("cli_console_tests-external-3").c_str());
    if (dfb) {
        const auto q = std::find_if(out.begin(), out.end(), [](const std::string& l) { return l.rfind("? Q", 0) == 0; });
        feed({"Answer(" + q->substr(3, q->find(' ', 3) - 3) + ", Yes)"});
    }
    if (!has("offer accepted")) for (const auto& l : out) std::cerr << l << "\n";
    check(has("offer accepted") && (dfb || has(bmh::test::expected_text("cli_console_tests.0").c_str())), "poach: the offer at the demand is not accepted");
    feed({"ListClubPlayers(club:10)"});
    check(!has("| " + forward + " "), "poach: the player is still at his club");
    check(feed({"GoBack", "OpenPoach"}).size() > 0 && has("4 attempts left"), "poach: the attempts are not counted");
    feed({"PoachPlayer(" + forward + ")"});
    check(has("E_BAD_ARGUMENT") || has("E_RULE"), "poach: the new own player can be poached");
}

// Stage 5: the questions of the season change inside a kickoff. The game-end savegame closes
// a season with 5 903 700 EU profit: the tax notice comes, then the question whether to
// adjust the tax return; after "Yes" the outcome (adjusted or caught) is reported. The
// promoted and relegated clubs are listed as well.
void season_question_tests(const std::filesystem::path& data, const std::filesystem::path& saves) {
    std::vector<std::string> out;
    Console console(options(data, saves), [&](const std::string& l) { out.push_back(l); });
    console.start();
    auto feed = [&](const std::vector<std::string>& lines) {
        out.clear();
        for (const auto& l : lines) console.feed(l);
        console.feed("go.");
        return out;
    };
    auto has = [&](const std::string& text) {
        return std::any_of(out.begin(), out.end(), [&](const std::string& l) { return l.find(text) != std::string::npos; });
    };
    feed({"LoadGame(INPUT)"});
    feed({"Kickoff"});
    check(has(bmh::test::expected_text("cli_console_tests-external-1").c_str()), "season: no tax notice");
    const auto q = std::find_if(out.begin(), out.end(), [](const std::string& l) { return l.rfind("? Q", 0) == 0; });
    check(q != out.end() && q->find(bmh::test::expected_text("cli_console_tests-external-2").c_str()) != std::string::npos, "season: no tax question");
    feed({"Answer(" + q->substr(3, q->find(' ', 3) - 3) + ", Yes)"});
    // adjusted: "...statt X nur Y EU..." / caught: a fine
    check(has("EU") && has("relegated") && has("# Window: GameEnd"), "season: no tax outcome, promotion list or game end");
}

// Stage 5 (C4 2.3.30, bank case 5): player 1 borrows 1 000 000 from player 2 at a monthly
// payment of 25 000. Player 1's balance rises, player 2's falls by the same amount, the
// loan row of player 1's matrix (data-field offset + 0 * 0x78 + 2 * 0x18) holds the credit, and no
// fees are charged or asked for.
void manager_credit_tests(const std::filesystem::path& data) {
    std::vector<std::string> out;
    Console console(options(data, std::filesystem::temp_directory_path()), [&](const std::string& l) { out.push_back(l); });
    console.start();
    auto feed = [&](const std::vector<std::string>& lines) {
        out.clear();
        for (const auto& l : lines) console.feed(l);
        console.feed("go.");
        for (const auto& l : out) check(l.rfind("! ", 0) != 0, "manager credit: " + l);
        return out;
    };
    feed({"NewGame(5, 3, 2, 3, true, false)", "SelectTeam(Anna, \"Schalke\", Bundesliga, Middle, 2)"});
    feed({"Answer(1, Yes)"});
    feed({"SelectTeam(Bert, \"Freiburg\", Bundesliga, Middle, 3)"});
    feed({"Answer(2, Yes)"});
    const GameState& g = console.session().state();
    auto balance = [&](int m) {
        const size_t at = size_t(m) * 0xb0c + 0x314;
        return int32_t(g.manager_records.at(at) | g.manager_records.at(at + 1) << 8 | g.manager_records.at(at + 2) << 16 |
                       uint32_t(g.manager_records.at(at + 3)) << 24);
    };
    const int32_t anna = balance(0), bert = balance(1);
    feed({"Key(B)", "OpenCredit(Manager2)", "SetCreditAmount(1000000)", "SetCreditPayment(25000)", "ConfirmCredit"});
    check(std::none_of(out.begin(), out.end(), [](const std::string& l) { return l.rfind("? Q", 0) == 0; }),
          "manager credit: a fee question");
    check(balance(0) == anna + 1000000 && balance(1) == bert - 1000000, "manager credit: the money did not move between the managers");
    const uint32_t row = ::bmh::layout::Loans + 2 * 0x18;
    check(g.memory.u32(row) == 1000000 && g.memory.u32(row + 8) == 25000 && g.memory.u32(row + 12) == 48 &&
              g.memory.u32(row + 4) == 25000 * 48, "manager credit: loan row");
    check(g.memory.u32(::bmh::layout::Loans) == 0, "manager credit: the bank row changed");
}

// Issue42: controlled pitch preparation, through the real Console/Session dispatcher.
// The fixture sets the transient match mode before entering Lineup; it does not
// claim a replay of the historical multiplayer career or an SDL Hall capture.
void pitch_console_mode_tests(const std::filesystem::path& data) {
    for (const bool indoor : {true, false}) {
        std::vector<std::string> out;
        Console console(options(data, std::filesystem::temp_directory_path()),
                        [&](const std::string& line) { out.push_back(line); });
        console.start();
        auto block = [&](const std::vector<std::string>& commands) {
            const auto lines = play(console, out, commands, false);
            for (const auto& line : lines)
                check(line.rfind("! ", 0) != 0, "pitch CLI: " + line + " (" + commands.front() + ")");
        };
        block({"NewGame(5, 3, 1, 3, true, false)", "SelectTeam(SIM, club:0, Bundesliga, Upper, 0)"});
        block({"Answer(1, Yes)"});
        auto& session = const_cast<app::CareerSession&>(console.session());
        session.state().indoor = indoor;
        block({"Key(A)", "OpenPitch"});
        const auto before = session.preparation_projection(0);
        check(before.pitch && before.pitch->indoor == std::optional<bool>(indoor),
              "pitch CLI: authoritative projection must identify the current mode");
        const int manager = before.lineup.manager_slot;
        const size_t option = size_t(manager) * 5;
        const size_t board = size_t(manager) * 0x840;
        const auto squad = squad_range(before.lineup.runtime_club);
        const size_t formation_base = size_t(manager) * 0x160 + (indoor ? 0xb0 : 0);
        const auto rand_seed = session.rand().seed();
        const auto game_seed = session.game().seed();
        const auto rand_calls = session.rand().calls();
        const auto game_calls = session.game().calls();
        const uint8_t old_weights = before.pitch->automatic_weights;
        const int weight = (((old_weights >> 4) & 3) + 1) & 3;
        block({"SetAutoWeights(Fatigue, " + std::to_string(weight) + ")"});
        check(((session.state().manager_records.at(size_t(manager) * 0xb0c + 0x288) >> 4) & 3) == weight,
              "pitch CLI: weight command must change the assistant record");
        block({"SetFormation(1)"});
        check(session.state().manager_options.at(option) == 1,
              "pitch CLI: formation command must enable the selected formation");
        const auto& game = session.state();
        for (size_t row = 0; row < 11; ++row)
            check(game.tactics.at(board + row * 0x58 + 0x41) == game.formations.at(formation_base + row) &&
                  game.tactics.at(board + row * 0x58 + 0x42) == game.formations.at(formation_base + 11 + row),
                  "pitch CLI: selected coordinates must come from the current mode's formation table");
        const uint16_t first = game.squad_slots.at(squad.first + 1), second = game.squad_slots.at(squad.first + 2);
        block({"SwapPitch(row:1, row:2)"});
        check((session.state().squad_slots.at(squad.first + 1) & 0x0fff) == (second & 0x0fff) &&
              (session.state().squad_slots.at(squad.first + 2) & 0x0fff) == (first & 0x0fff) &&
              session.state().manager_options.at(option) == 0,
              "pitch CLI: swap must exchange actual player identities and select manual lineup");
        // Move to another starter's exact place: this is a supported source swap
        // even on the paired Hall grid and avoids inventing free-space coordinates.
        const int depth = session.state().tactics.at(board + 2 * 0x58 + 0x41);
        const int lane = session.state().tactics.at(board + 2 * 0x58 + 0x42);
        block({"MovePlayer(row:1, " + std::to_string(depth) + ", " + std::to_string(lane) + ")"});
        check(session.state().tactics.at(board + 0x58 + 0x41) == depth &&
              session.state().tactics.at(board + 0x58 + 0x42) == lane,
              "pitch CLI: move must commit the requested coordinates");
        block({"MovePlayer(row:1, 19, 8)"});
        check(session.state().tactics.at(board + 0x58 + 0x41) == 19 &&
              session.state().tactics.at(board + 0x58 + 0x42) == 8,
              "pitch CLI: the upper valid grid boundary must be reachable");
        const auto valid_slots = session.state().squad_slots;
        const auto valid_board = session.state().tactics;
        const auto valid_options = session.state().manager_options;
        for (const char* command : {"MovePlayer(row:1, -1, 8)", "MovePlayer(row:1, 20, 8)",
                                    "MovePlayer(row:1, 19, -1)", "MovePlayer(row:1, 19, 9)"}) {
            const auto lines = play(console, out, {command}, false);
            check(std::any_of(lines.begin(), lines.end(), [](const std::string& line) {
                      return line.rfind("! ", 0) == 0;
                  }) && session.state().squad_slots == valid_slots && session.state().tactics == valid_board &&
                  session.state().manager_options == valid_options,
                  "pitch CLI: invalid coordinates must fail without mutating lineup or board");
        }
        if (indoor) {
            // Controlled board, separated targets: two existing pairs are replaced by
            // one physical move. Old reciprocal links must both be removed.
            const int coordinates[11][2] = {{0,4},{3,0},{3,8},{8,4},{15,0},{15,8},
                                           {19,0},{19,8},{0,8},{10,0},{10,8}};
            for (size_t row = 0; row < 24; ++row) {
                session.state().tactics.at(board + row * 0x58 + 0x3f) = 0xff;
                if (row < 11) {
                    session.state().tactics.at(board + row * 0x58 + 0x41) = uint8_t(coordinates[row][0]);
                    session.state().tactics.at(board + row * 0x58 + 0x42) = uint8_t(coordinates[row][1]);
                    const size_t id = session.state().squad_slots.at(squad.first + row) & 0x0fff;
                    session.state().players.at(id).w364 &= ~uint32_t(0x1f0000);
                }
            }
            auto partner = [&](size_t row) -> uint8_t& {
                return session.state().tactics.at(board + row * 0x58 + 0x3f);
            };
            partner(1)=2; partner(2)=1; partner(3)=4; partner(4)=3;
            const auto shown = play(console, out, {"ShowPairs"}, false);
            check(std::find(shown.begin(), shown.end(), "i Hall pairs: 2") != shown.end(),
                  "Hall CLI: ShowPairs must read actual reciprocal board links");
            const auto before_pair_board = session.state().tactics;
            const auto before_pair_slots = session.state().squad_slots;
            const auto before_pair_options = session.state().manager_options;
            for (const char* command : {"PairPlayers(row:1, row:1, 7, 3)",
                                       "PairPlayers(row:1, row:11, 7, 3)",
                                       "PairPlayers(row:1, row:3, 8, 4)",
                                       "PairPlayers(row:1, row:3, 1, 1)",
                                       "PairPlayers(row:1, row:3, 7, 9)"}) {
                const auto lines = play(console, out, {command}, false);
                check(std::any_of(lines.begin(), lines.end(), [](const std::string& line) {
                          return line.rfind("! ", 0) == 0;
                      }) && session.state().tactics == before_pair_board &&
                      session.state().squad_slots == before_pair_slots &&
                      session.state().manager_options == before_pair_options,
                      "Hall CLI: rejected pair must preserve all lineup state");
            }
            block({"PairPlayers(row:1, row:3, 7, 3)"});
            check(partner(1)==3 && partner(3)==1 && partner(2)==0xff && partner(4)==0xff &&
                  session.state().tactics.at(board + 0x58 + 0x41)==7 &&
                  session.state().tactics.at(board + 0x58 + 0x42)==3 &&
                  session.state().manager_options.at(option)==0,
                  "Hall CLI: pair must use the physical placement and clear both former partners");
            check(session.state().squad_slots == before_pair_slots,
                  "Hall CLI: forming a pair must never consume a bench substitution");
            const auto paired_board = session.state().tactics;
            const auto after_shown = play(console, out, {"ShowPitch", "ShowPairs"}, false);
            check(std::count(after_shown.begin(), after_shown.end(), "i Hall pairs: 1")==2 &&
                  session.state().tactics==paired_board,
                  "Hall CLI: both displays must show pairs without changing the board");
            // Another nearby starter wins the Source scan. The requested pair must
            // fail atomically, rather than silently pair the last neighbor.
            session.state().tactics.at(board + 10 * 0x58 + 0x41)=8;
            session.state().tactics.at(board + 10 * 0x58 + 0x42)=4;
            const auto crowded_board = session.state().tactics;
            const auto crowded = play(console, out, {"PairPlayers(row:2, row:3, 7, 4)"}, false);
            check(std::any_of(crowded.begin(), crowded.end(), [](const std::string& line) {
                      return line.rfind("! E_RULE:", 0)==0;
                  }) && session.state().tactics==crowded_board,
                  "Hall CLI: crowded targets must not substitute a different partner");
            session.state().tactics.at(board + 10 * 0x58 + 0x41)=10;
            session.state().tactics.at(board + 10 * 0x58 + 0x42)=8;
        } else {
            const auto outdoor_board = session.state().tactics;
            const auto outdoor_slots = session.state().squad_slots;
            for (const char* command : {"ShowPairs", "PairPlayers(row:1, row:2, 7, 3)"}) {
                const auto lines = play(console, out, {command}, false);
                check(std::any_of(lines.begin(), lines.end(), [](const std::string& line) {
                          return line.rfind("! E_RULE:", 0)==0;
                      }) && session.state().tactics==outdoor_board && session.state().squad_slots==outdoor_slots,
                      "Hall CLI: outdoor pair commands must fail without changes");
            }
        }
        block({"SaveFormation(5)"});
        check(session.state().manager_records.at(size_t(manager) * 0xb0c + 0x218 + size_t(indoor)) & 0x10,
              "pitch CLI: save must enable the current mode's custom formation slot");
        for (size_t row = 0; row < 11; ++row)
            check(session.state().formations.at(formation_base + 4 * 22 + row) ==
                      session.state().tactics.at(board + row * 0x58 + 0x41) &&
                  session.state().formations.at(formation_base + 4 * 22 + 11 + row) ==
                      session.state().tactics.at(board + row * 0x58 + 0x42),
                  "pitch CLI: saved formation must contain both edited coordinates");
        check(session.rand().seed() == rand_seed && session.game().seed() == game_seed &&
              session.rand().calls() == rand_calls && session.game().calls() == game_calls,
              "pitch CLI: preparation commands must preserve both RNG streams");
    }
}

// Stage 2 acceptance (10-agenten-und-tests 3): every "ready" line of commands.csv whose window
// the console has runs at least once without an error, in a real two-manager career.
// `known_gaps` are ready lines the engine cannot serve yet; each names its reason.
void hall_calendar_console_tests(const std::filesystem::path& data, const std::filesystem::path& csv) {
    const auto input = csv.parent_path().parent_path() / "analyses" /
        "2026-10-11-hall-transfer-milestone36-evidence" / "INPUT-WEEK19.FBM";
    const auto dir = std::filesystem::temp_directory_path() / "openbmh_cli_hall_calendar";
    std::filesystem::create_directories(dir);
    std::filesystem::remove(dir / "MIDHALL.FBM");
    std::vector<std::string> out;
    Console console(options(data, dir), [&](const std::string& line) { out.push_back(line); });
    console.start();
    auto block = [&](Console& target, std::vector<std::string>& output, const std::vector<std::string>& commands) {
        for (const auto& line : play(target, output, commands, false))
            check(line.rfind("! ", 0) != 0, "Hall calendar: " + line);
        check(target.question_player() == 0, "Hall calendar fixture unexpectedly asked a question");
    };
    block(console, out, {"LoadGame(\"" + input.generic_string() + "\")"});
    const auto date = console.session().state().date;
    check(console.session().state().memory.u8(::bmh::layout::EventPasses) == 3,
          "Original Saturday input must carry three remaining Hall rounds");
    block(console, out, {"Kickoff"});
    check(console.session().state().date == date &&
          console.session().state().memory.u8(::bmh::layout::EventPasses) == 2 &&
          console.session().state().memory.u8(cup_entry_address(25) + 0x1d) == 1,
          "Hall Saturday first round must stay on the same date and prepare the second round");
    block(console, out, {"Open(SaveGame)", "Save(MIDHALL)", "GoBack"});
    std::vector<std::string> fresh_out;
    Console fresh(options(data, dir), [&](const std::string& line) { fresh_out.push_back(line); });
    fresh.start();
    block(fresh, fresh_out, {"LoadGame(\"" + (dir / "MIDHALL.FBM").generic_string() + "\")"});
    check(fresh.session().state().memory.u8(::bmh::layout::EventPasses) == 2,
          "fresh load must preserve the remaining Hall rounds");
    block(fresh, fresh_out, {"Kickoff"});
    check(fresh.session().state().date == date && fresh.session().state().memory.u8(::bmh::layout::EventPasses) == 1,
          "reloaded Saturday second round must not move the date");
    block(console, out, {"Kickoff", "Kickoff"});
    check(console.session().state().date == date + 1 &&
          console.session().state().memory.u8(::bmh::layout::EventPasses) == 2 &&
          console.session().state().memory.u8(cup_entry_address(27) + 0x1d) == 0,
          "three Saturday group rounds must lead to Sunday's semi-final");
    block(console, out, {"Kickoff"});
    check(console.session().state().date == date + 1 && console.session().state().memory.u8(::bmh::layout::EventPasses) == 1,
          "Sunday semi-final must prepare the final on the same date");
    block(console, out, {"Kickoff"});
    check(console.session().state().date == date + 7 &&
          console.session().state().memory.u8(::bmh::layout::EventPasses) == 3 &&
          console.session().state().memory.u8(::bmh::layout::Cup_AdvanceCup_RuntimeField4) == 2,
          "Sunday final must finish the tournament and prepare next Saturday's three rounds");
    std::filesystem::remove(dir / "MIDHALL.FBM");
}

void coverage_tests(const std::filesystem::path& data, const std::filesystem::path& csv) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "openbmh_cli_console_coverage";
    std::filesystem::create_directories(dir);
    std::filesystem::remove(dir / "COVER.FBM");
    std::vector<std::string> out;
    Console console(options(data, dir), [&](const std::string& line) { out.push_back(line); });
    console.start();
    // one block; returns its output and fails on any error line
    int question = 0;  // the number of the last question asked ("? Q<n> ...")
    auto block = [&](std::vector<std::string> lines, bool errors_allowed = false) {
        out.clear();
        for (const auto& l : lines) console.feed(l);
        console.feed("go.");
        for (const auto& l : out)
            if (l.rfind("? Q", 0) == 0) question = std::stoi(l.substr(3));
        if (!errors_allowed)
            for (const auto& l : out) check(l.rfind("! ", 0) != 0, "coverage: " + l + " (block starting " + lines.front() + ")");
        return out;
    };
    auto answer = [&](const std::string& option) { return "Answer(" + std::to_string(question) + ", " + option + ")"; };
    auto contains = [](const std::vector<std::string>& lines, const std::string& text) {
        return std::any_of(lines.begin(), lines.end(), [&](const std::string& l) { return l.find(text) != std::string::npos; });
    };

    // start: options, load browser, selection for two managers (hot seat)
    block({"SetDuration(5)", "SetManagers(2)", "SetMode(3)", "SetLeagueMode(on)", "SetWorldMode(off)", "ShowOptions"});
    block({"OpenLoadBrowser", "ListFiles", "GoBack"});
    block({"GoBack"});  // = NewGame(...) with the current options
    block({"ListLeagues", "ListTeams(Bundesliga)", "ListPortraits", "ListStrengths", "SetManagerName(\"Anna\")", "SetClub(club:3)",
           "SetStartLeague(Bundesliga)", "SetStrength(Upper)", "SetPortrait(1)", "ShowSelection", "Confirm"});
    block({"ShowSummary", answer("Yes")});
    block({"SelectTeam(\"Bert\", club:0, Bundesliga, Middle, 2)"});
    block({answer("Yes")});

    // Before the first desk, the prepared lineup already accepts pitch edits (#42).
    // Opening row 0 must still show the identity listed after returning to the squad.
    const auto row0_squad = block({"Key(A)", "ShowSquad", "OpenPitch",
                                  "MovePlayer(row:0, 3, 2)", "GoBack", "ShowSquad"});
    const auto initial_lineup = console.session().preparation_projection(0).lineup;
    check(initial_lineup.players.at(0).board[0x41] == 3 &&
          initial_lineup.players.at(0).board[0x42] == 2,
          "coverage: early pitch command did not move row 0");
    check(console.session().state().manager_options.at(size_t(initial_lineup.manager_slot) * 5) == 0,
          "coverage: early manual placement did not disable formation automation");
    const auto row0 = std::find_if(row0_squad.begin(), row0_squad.end(), [](const std::string& line) {
        return line.rfind("| row:0 | id:", 0) == 0;
    });
    check(row0 != row0_squad.end(), "coverage: row 0 missing from squad projection");
    const size_t row0_id_at = row0->find("id:") + 3;
    const size_t row0_id_end = row0->find(" |", row0_id_at);
    check(row0_id_at >= 3 && row0_id_end != std::string::npos, "coverage: malformed row 0 identity");
    const std::string row0_id = row0->substr(row0_id_at, row0_id_end - row0_id_at);
    const auto row0_card = block({"OpenPlayer(row:0)"});
    check(contains(row0_card, "| id | id:" + row0_id + " |"),
          "coverage: OpenPlayer(row:0) showed a different PlayerCard identity");
    block({"GoBack", "GoBack"});

    // desk displays and windows for the player at the desk
    block({"ShowDesk", "ShowDate", "ShowFanStanding", "ShowLamps", "ShowAutomation", "Status", "SetTicketPrice(22)",
           "SetAutomation(Substitution, on)", "SetAutomation(Substitution, off)"});
    block({"Open(Tactics)", "ShowTactics", "SetTactic(7, 4)", "CycleTactic(6)", "GoBack"});
    block({"Key(I)", "InitializeTraining", "ShowTraining", "SetTrainingType(MIT, 2)", "SetTrainingArea(Corner, 1)",
           "SetPlayerIntensity(row:0, 1)", "CyclePlayerFocus(row:1)", "SetTrainingAutomation(1, on)", "GoBack"});
    block({"Open(TrainingCamp)", "ListResorts", "ShowCamp", "SetCampFocus(8, 4, 4)", "BookCamp(0)", "GoBack"});
    block({"Key(B)", "OpenSavings", "ShowSavings", "Deposit(10000)", "Withdraw(5000)", "GoBack", "OpenShares", "ListShares",
           "ShowShare(0)", "ShareAdvice(0)", "BuyShares(0, 5)", "SellShares(0, 2)", "GoBack", "OpenRealEstate", "ListRealEstate",
           "BuyRealEstate(0, 1)", "SellRealEstate(0, 1)", "GoBack", "GoBack"});
    block({"Open(Stadium)"});
    const auto stadium_lines = block({"ShowStadium"});
    check(contains(stadium_lines, "| Block | Places | PlannedPlaces | Building |"),
          "coverage: stadium block capacities have ambiguous column names");
    const auto& stadium_game = console.session().state();
    const size_t stadium_manager = size_t(stadium_game.memory.u8(::bmh::layout::ActiveManager)) * 0xb0c;
    for (size_t b = 0; b < 8; ++b) {
        const int places = int(stadium_game.manager_records.at(stadium_manager + 0x4c + b)) * 1000;
        check(places > 0 && contains(stadium_lines, "| " + std::string(1, char('A' + b)) + " | " +
                                    std::to_string(places) + " |"),
              "coverage: stadium block reports future capacity as current places");
    }
    block({"BuildFacility(Scoreboard)"});
    block({answer("1")});            // the first tier
    block({answer("No")});           // no tip
    block({"Maintain(Pitch)"});
    block({answer("Cancel")});
    block({"OpenPeriphery", "ListPeriphery", "QuotePeriphery(2)"});
    block({answer("No")});
    block({"GoBack", "GoBack"});
    block({"Key(W)", "ShowAdvertising", "ListAdOffers(Board1)", "AcceptAdOffer(Board1, 1)", "DeclineAdOffer",
           "SetSpending(PublicRelations, 1000)", "BuyFanItems(10000)", "GoBack"});
    block({"Key(V)", "ShowContracts", "ShowTeamBonuses", "SetTeamBonus(Goal, 2)", "GoBack"});
    block({"Key(A)", "ShowSquad", "SwapPlayers(row:10, row:15)", "OpenPlayer(row:9)", "ShowPlayer", "OpenPlayer(row:1)",
           "OpenPlayerStatistics", "ShowPlayerStatistics", "SetChart(Form)", "SetMode(Relative)", "BackToPlayerInfo", "GoBack",
           "OpenPitch", "ShowPitch", "SwapPitch(row:1, row:2)", "SetAutoWeights(Fatigue, 2)", "SetFormation(1)",
           "MovePlayer(row:5, 3, 2)", "SaveFormation(5)", "OpenOrder", "ShowOrders", "SetOrder(row:9, Fighting, 35)", "GoBack", "GoBack",
           "GoBack"});
    {
        // a friendly against the first club of the second league on the first free date
        std::string club, date;
        for (const auto& l : block({"Key(F)", "ListFriendlyOpponents(1)"}))
            if (club.empty() && l.rfind("| club:", 0) == 0) club = l.substr(2, l.find(' ', 2) - 2);
        check(!club.empty(), "coverage: no friendly opponent");
        for (const auto& l : block({"ProposeFriendly(" + club + ", Home)", "ListFreeDates"}))
            if (date.empty() && l.rfind("| 20", 0) == 0) date = l.substr(2, 10);
        check(!date.empty(), "coverage: no free date");
        block({"SetFriendlyDate(" + date + ")"});
        block({answer("Yes")});
        block({"GoBack"});
    }
    {
        // move the first movable home match to its first free day
        std::string game, to;
        for (const auto& l : block({"Open(Reschedule)", "ListRescheduleGames"}))
            if (game.empty() && l.rfind("| 20", 0) == 0) game = l.substr(2, 10);
        check(!game.empty(), "coverage: no movable home match");
        for (const auto& l : block({"ListRescheduleDates(" + game + ")"}))
            if (to.empty() && l.rfind("| 20", 0) == 0) to = l.substr(2, 10);
        check(!to.empty(), "coverage: no free day for the home match");
        block({"MoveGame(" + game + ", " + to + ")", "ListMovedGames", "UndoMove(" + to + ")", "GoBack"});
    }
    // the environment: staff levels and the youth academy (on and off, or too late)
    block({"Key(U)", "ShowEnvironment", "SetStaffLevel(Doctor, 2)", "SetYouthAcademy(On)", "SetYouthAcademy(Off)", "GoBack"});
    // the balance plan: all views, a change, the reset; GoBack stores the plan
    block({"Open(Balance)", "ShowBalance", "SetBalanceView(Future)", "SetBalanceItem(IncomeTv, 1000000)", "TakeActuals",
           "SetBalanceView(Real)", "SetBalanceItem(AverageGate, 20000)", "GoBack"});
    {
        // a credit from player 2 (bank case 5): amount, term, a monthly payment from its range,
        // booked; then a full repayment drafted and discarded
        std::string payment;
        for (const auto& l : block({"Key(B)", "OpenCredit(Manager2)", "ShowCredit", "SetCreditAmount(500000)", "SetCreditTerm(24)"}))
            if (l.rfind("| 2 |", 0) == 0) {
                const size_t dots = l.rfind("..");
                if (dots != std::string::npos) payment = l.substr(l.rfind(" | ", dots) + 3, dots - l.rfind(" | ", dots) - 3);
            }
        check(!payment.empty(), "coverage: no monthly payment range");
        payment.erase(std::remove(payment.begin(), payment.end(), '.'), payment.end());
        block({"SetCreditPayment(" + payment + ")", "ConfirmCredit"});
        block({"RepayCredit(All)", "CancelCredit", "GoBack", "GoBack"});
    }
    {
        // poaching: the first forward of Augsburg (the editor is cancelled; the attempt counts)
        std::string forward;
        block({"Key(M)", "OpenPoach", "ListPoachClubs"});
        for (const auto& l : block({"ListClubPlayers(club:10)"}))
            if (forward.empty() && l.rfind("| id:", 0) == 0 && l.find("| STU |") != std::string::npos) forward = l.substr(2, l.find(' ', 2) - 2);
        check(!forward.empty(), "coverage: no forward to poach");
        block({"PoachPlayer(" + forward + ")", "GoBack", "GoBack", "GoBack"});
    }
    {
        // buy the first player of the market (the market depends on the career)
        std::string id;
        for (const auto& l : block({"Key(M)", "ShowMarket", "SetMarketFilter(All)"}))
            if (id.empty() && l.rfind("| id:", 0) == 0) id = l.substr(2, l.find(' ', 2) - 2);
        check(!id.empty(), "coverage: empty transfer market");
        block({"SellPlayer(row:19)", "BuyPlayer(" + id + ")", "ShowNegotiation", "SetOfferField(Fee, 100000)",
               "SetOfferField(Salary, 20000)", "ShowTeamInfo", "ShowPlayerInfo", "SubmitOffer", "GoBack", "GoBack"});
    }
    block({"Key(T)", "ShowTable(0)", "OpenTeamInfo(club:0)", "ShowTeamSquad", "ShowPlayer(row:0)", "CloseTeamInfo", "ShowTable(1)", "GoBack",
           "Open(HighScore)", "ShowHighScore", "GoBack", "Open(TopScorers)", "ShowScorers(Assists, League)", "SetList(Score)", "SetScope(Managers)", "GoBack", "Open(Fixtures)", "NextMatchday", "GoBack", "OpenMailbox", "ListMessages", "GoBack"});
    block({"Open(SaveGame)", "Save(COVER)", "GoBack"});
    // the second manager's turn (hot seat): the first hands over, @1 is no longer allowed
    auto handover = block({"Kickoff"});
    check(contains(handover, "+ Next: Player 2"), "coverage: kickoff hands over to player 2");
    check(contains(block({"@1 Kickoff"}, true), "E_NOT_YOUR_TURN"), "coverage: @1 at player 2's desk");
    // the desk views of a seat not on turn: Status says whose desk it is, Advice/State refuse
    check(contains(block({"@1 Status"}), "you are player 1; player 2 is on turn"), "coverage: @1 Status labels the desk");
    check(contains(block({"@1 Advice"}, true), "E_NOT_YOUR_TURN"), "coverage: @1 Advice at player 2's desk");
    check(contains(block({"@1 State(Desk)"}, true), "E_NOT_YOUR_TURN"), "coverage: @1 State at player 2's desk");
    block({"Chat(1, \"your turn soon\")", "ChatLog"});
    // a periphery offer accepted through the explicit command
    block({"Open(Stadium)", "OpenPeriphery", "QuotePeriphery(3)"});
    block({answer("Yes"), "GoBack", "GoBack"});
    // the Toto coupon exists on some weeks only: play on until there is one
    bool toto = false;
    for (int day = 0; day < 60 && !toto; ++day) {
        auto lines = block({"Key(O)"});
        if (!contains(lines, "no Toto coupon")) {
            block({"ShowToto", "SetTip(1, X)", "SetStake(1000)", "ClearToto", "SubmitToto(20000, [1, X, 2, 1, 1, 1, 1, 1, 1, 1, 1])", "GoBack"});
            toto = true;
        } else {
            block({"GoBack", "Kickoff"});
        }
    }
    check(toto, "coverage: no Toto coupon in 60 days");
    block({"Open(LoadGame)", "Load(COVER)"});
    block({"PullPlug"});
    block({answer("No")});
    block({"Quit(force=true)"});

    // LoadGame of the options screen, in a second session from program start
    std::set<std::string> succeeded = console.succeeded();
    {
        Console second(options(data, dir), [](const std::string&) {});
        second.start();
        for (const char* l : {"LoadGame(COVER)", "go."}) second.feed(l);
        succeeded.insert(second.succeeded().begin(), second.succeeded().end());
    }
    // the Match window, in a stepped session up to the first match of the human team
    {
        Options stepped = options(data, dir);
        stepped.time = TimeMode::stepped;
        std::vector<std::string> lines;
        Console third(stepped, [&](const std::string& l) { lines.push_back(l); });
        third.start();
        for (const char* l : {"NewGame(5, 3, 1, 3, true, false)", "SelectTeam(T, club:3, Bundesliga, Upper, 1)", "go.", "Answer(1, Yes)", "go."})
            third.feed(l);
        for (int i = 0; i < 10 && !third.succeeded().count("Match/Run"); ++i) {
            lines.clear();
            third.feed("Kickoff");
            third.feed("go.");
            if (std::any_of(lines.begin(), lines.end(), [](const std::string& l) { return l == "# Window: Match"; }))
                // the live tactics board and single moves from minute 3 on; each move in a block
                // of its own (a target field the original refuses must not skip the others)
                for (const char* l : {"Step(5)", "go.", "ShowTactics", "SetTactic(1, 1)", "go.", "MovePlayer(row:9, 14, 7)", "go.",
                                      "MovePlayer(row:9, 16, 7)", "go.", "MovePlayer(row:8, 14, 1)", "go.",
                                      "MovePlayer(row:5, 9, 3)", "go.", "ShowMatchStats", "ShowSquad", "Run(Halftime)", "Run", "go."})
                    third.feed(l);
        }
        succeeded.insert(third.succeeded().begin(), third.succeeded().end());
    }

    // Genuine two-manager loan entry: the owner's VER-LEIHEN listing and the
    // borrower's 12/24-month editor must both succeed through console commands.
    // Reload for each term so an earlier editor cannot supply a stale duration.
    const auto loan_input = bmh::test::external_input("human-transfer.fbm");
    for (int months : {12, 24}) {
        Options loan_options = options(data, dir);
        loan_options.seed = 8128;
        std::vector<std::string> lines;
        Console loan(loan_options, [&](const std::string& l) { lines.push_back(l); });
        loan.start();
        auto loan_block = [&](const std::vector<std::string>& commands) {
            const auto output = play(loan, lines, commands, false);
            for (const auto& line : output)
                check(line.rfind("! ", 0) != 0, "loan coverage: " + line);
            check(loan.question_player() == 0, "loan coverage: unexpected question");
            return output;
        };
        loan_block({"LoadGame(\"" + loan_input.generic_string() + "\")", "Key(M)"});
        check(loan.session().state().manager_teams == std::vector<int>({1, 54}) &&
              loan.active_player() == 1, "loan coverage: genuine Frankfurt/Kickers desks");
        loan_block({"LoanOutPlayer(id:118,12)"});
        const auto& listed = loan.session().state();
        int listings = 0;
        for (int entry = 0; entry < 40; ++entry) {
            if ((listed.squad_slots.at(0xb70 + size_t(entry)) & 0xfff) != 118) continue;
            check((listed.memory.u16(::bmh::layout::EditorList + uint32_t(entry) * 0x58 + 8) & 0x7fff) == 1,
                  "loan coverage: listing must retain Frankfurt as seller");
            ++listings;
        }
        check(listings == 1 && loan.succeeded().count("Transfer/LoanOutPlayer"),
              "loan coverage: LoanOutPlayer must create the real BURKARDT listing");
        loan_block({"GoBack", "Kickoff", "Key(M)"});
        check(loan.active_player() == 2 && loan.window() == "Transfer",
              "loan coverage: normal kickoff hands the market to the human borrower");
        const auto negotiation = loan_block({"HirePlayer(id:118," + std::to_string(months) + ")"});
        const auto& editor = loan.session().transfer_offer_owner();
        check(loan.window() == "Transfer.Negotiate" && editor && editor->editable &&
              editor->request.manager == 1 && editor->request.runtime_club == 54 &&
              editor->request.expected_player == 118 && editor->request.loan && editor->quote_loan &&
              editor->mode == 3 && editor->years == months / 12,
              "loan coverage: HirePlayer must set the actual owned loan editor's years");
        check(editor->term == uint8_t(0x80 | months) &&
              loan.session().state().memory.u8(::bmh::layout::EditorList + uint32_t(editor->request.list_entry) * 0x58 +
                  0x34 + uint32_t(editor->bid_slot)) == uint8_t(0x80 | months),
              "loan coverage: the Native bid must contain the requested months and loan flag");
        check(contains(negotiation, "|  | Fee | Monthly salary | Years |") &&
              contains(negotiation, "| offer | 0 | 0 | " + std::to_string(months / 12) + " |"),
              "loan coverage: negotiation must display the requested years without a later field edit");
        check(loan.succeeded().count("Transfer/HirePlayer"),
              "loan coverage: HirePlayer must finish successfully");
        loan_block({"GoBack", "GoBack"});
        succeeded.insert(loan.succeeded().begin(), loan.succeeded().end());
    }

    // compare with the ready lines of commands.csv
    const std::map<std::string, std::string> known_gaps = {
        {"Stadium.Periphery/BuildPeriphery", "covered through QuotePeriphery + Answer(Yes)"},
        {"Stadium.Periphery/DeclinePeriphery", "covered through QuotePeriphery + Answer(No)"},
    };
    std::set<std::string> windows;
    for (const Spec& s : console.specs()) windows.insert(s.window);
    std::ifstream in(csv, std::ios::binary);
    std::string line, missing;
    while (std::getline(in, line)) {
        if (line.find("\"ready\"") == std::string::npos || line[0] != '"') continue;
        const size_t w_end = line.find('"', 1), c_begin = line.find('"', w_end + 1), c_end = line.find('"', c_begin + 1);
        const std::string window_field = line.substr(1, w_end - 1), command = line.substr(c_begin + 1, c_end - c_begin - 1);
        bool relevant = false, done = false;
        size_t start = 0;
        while (true) {
            const size_t bar = window_field.find('|', start);
            const std::string w = window_field.substr(start, bar - start);
            if (windows.count(w)) {
                relevant = true;
                done = done || succeeded.count(w + "/" + command) || known_gaps.count(w + "/" + command);
            }
            if (bar == std::string::npos) break;
            start = bar + 1;
        }
        if (relevant && !done) missing += " " + window_field + "/" + command;
    }
    check(missing.empty(), "coverage: ready commands never run successfully:" + missing);
}

// Stage 3 acceptance (10-agenten-und-tests 4.1, cli_listoptions/cli_goback): every window the
// desk reaches through Open(menu) lists only commands that resolve there (ListOptions(<name>)
// succeeds for each > line), and GoBack leads back to the Desktop. Windows without an engine
// projection answer E_ENGINE_GAP on Open and are counted, not failed.
void window_tree_tests(const std::filesystem::path& data) {
    std::vector<std::string> out;
    Console console(options(data, std::filesystem::temp_directory_path(), true), [&](const std::string& l) { out.push_back(l); });
    console.start();
    auto block = [&](const std::vector<std::string>& lines) {
        out.clear();
        for (const auto& l : lines) console.feed(l);
        console.feed("go.");
        std::vector<Json> parsed;
        for (const auto& l : out) parsed.push_back(parse_json(l));
        return parsed;
    };
    auto field = [](const Json& j, const char* name) { return *j.object()->at(name).string(); };
    auto window_after = [&](const std::vector<Json>& lines) { return lines.empty() ? std::string() : field(lines.back(), "window"); };
    block({"NewGame(5, 3, 1, 3, true, false)", "SelectTeam(T, club:3, Bundesliga, Upper, 1)"});
    block({"Answer(1, Yes)"});
    int opened = 0, gaps = 0;
    for (int menu = 0; menu < 30; ++menu) {
        if (menu == 12) continue;  // Kickoff is no window
        auto lines = block({"Open(" + std::to_string(menu) + ")"});
        bool gap = false;
        for (const auto& l : lines)
            if (field(l, "class") == "error") {
                const std::string t = field(l, "text");
                check(t.rfind("E_ENGINE_GAP", 0) == 0 || t.rfind("E_MODE_LOCKED", 0) == 0, "window tree: Open(" + std::to_string(menu) + "): " + t);
                gap = true;
            }
        if (gap) {
            ++gaps;
            continue;
        }
        ++opened;
        const std::string window = window_after(lines);
        check(window != "Desktop", "window tree: Open(" + std::to_string(menu) + ") stayed on the desk");
        // every listed command resolves in this window
        for (const auto& l : block({"ListOptions"})) {
            if (field(l, "class") != "option") continue;
            std::string name = field(l, "text");
            name.erase(0, name.find_first_not_of(' '));
            name = name.substr(0, name.find('('));
            for (const auto& r : block({"ListOptions(" + name + ")"}))
                check(field(r, "class") != "error", "window tree: " + window + " lists " + name + " but: " + field(r, "text"));
        }
        // back to the desk (an open question is answered by its default)
        std::string now = window;
        for (int step = 0; step < 6 && now != "Desktop"; ++step) {
            const auto back = block({"GoBack"});
            for (const auto& l : back) check(field(l, "class") != "error", "window tree: GoBack in " + now + ": " + field(l, "text"));
            if (!back.empty()) now = window_after(back);
            else break;
        }
        check(now == "Desktop", "window tree: GoBack from " + window + " ends in " + now);
    }
    check(opened >= 12, "window tree: only " + std::to_string(opened) + " windows open (" + std::to_string(gaps) + " gaps)");
}

// The registered commands must be a subset of the concept's command list.
void registry_tests(const std::filesystem::path& data, const std::filesystem::path& saves, const std::filesystem::path& csv) {
    std::ifstream in(csv, std::ios::binary);
    check(bool(in), "cannot read " + csv.string());
    std::set<std::string> known;
    std::string line;
    while (std::getline(in, line)) {
        // "window";"command";...
        if (line.size() < 2 || line[0] != '"') continue;
        const size_t w_end = line.find('"', 1);
        const size_t c_begin = line.find('"', w_end + 1);
        const size_t c_end = line.find('"', c_begin + 1);
        if (w_end == std::string::npos || c_begin == std::string::npos || c_end == std::string::npos) continue;
        // the window field may list several windows: "LoadGame|SaveGame|NewGame.LoadGame"
        const std::string windows = line.substr(1, w_end - 1), command = line.substr(c_begin + 1, c_end - c_begin - 1);
        size_t start = 0;
        while (true) {
            const size_t bar = windows.find('|', start);
            known.insert(windows.substr(start, bar - start) + "/" + command);
            if (bar == std::string::npos) break;
            start = bar + 1;
        }
    }
    Console console(options(data, saves), [](const std::string&) {});
    for (const Spec& s : console.specs()) {
        check(known.count(s.window + "/" + s.name) != 0, "command not in commands.csv: " + s.window + "/" + s.name);
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--parser-only") {
        try {
            parser_tests();
            std::cout << "console parser checks passed without game data\n";
            return 0;
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
    if (argc != 5) {
        std::cerr << "usage: cli_console_tests <BMH data> <golden dir> <save dir> <commands.csv>\n";
        return 2;
    }
    try {
        parser_tests();
        golden_tests(argv[1], argv[2], argv[3]);
        json_tests(argv[1], argv[3]);
        replay_tests(argv[1], argv[3]);
        registry_tests(argv[1], argv[3], argv[4]);
        pitch_console_mode_tests(argv[1]);
        hall_calendar_console_tests(argv[1], argv[4]);
        modal_batch_tests(argv[1]);
        transfer_result_notice_tests(argv[1]);
        kickoff_reentrant_tests(argv[1]);
        career_tests(argv[1], 40);
        contract_expiry_console_tests(argv[1]);
        loan_return_console_tests(argv[1], argv[4]);
        coverage_tests(argv[1], argv[4]);
        window_tree_tests(argv[1]);
        intervention_tests(argv[1], 40);
        sale_tests(argv[1]);
        reschedule_console_tests(argv[1]);
        poach_tests(argv[1]);
        season_question_tests(argv[1], argv[3]);
        manager_credit_tests(argv[1]);
        std::cout << "cli_console_tests ok\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "cli_console_tests: " << e.what() << '\n';
        return 1;
    }
}
