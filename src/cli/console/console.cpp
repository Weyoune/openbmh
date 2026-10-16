// SPDX-License-Identifier: GPL-3.0-only
// The console's core: registry, input blocks, argument binding, output and the global
// commands (docs/CLI_CONCEPT/01-grundlagen.md). The windows live in windows_*.cpp.

#include "cli/console/console.hpp"

#include <algorithm>
#include <stdexcept>

#include "cli/console/detail.hpp"
#include "ui/manager.hpp"

namespace bmh::cli::console {

using namespace detail;

// ---------------------------------------------------------------------------------------
// detail helpers

namespace detail {

Param integer(std::string name, long long min, long long max, bool optional) {
    Param p;
    p.name = std::move(name);
    p.type = Param::Type::integer;
    p.min = min, p.max = max, p.optional = optional;
    return p;
}
Param boolean(std::string name, bool optional) {
    Param p;
    p.name = std::move(name);
    p.type = Param::Type::boolean;
    p.optional = optional;
    return p;
}
Param text(std::string name, long long max_length, bool optional) {
    Param p;
    p.name = std::move(name);
    p.type = Param::Type::text;
    p.max = max_length, p.optional = optional;
    return p;
}
Param choice(std::string name, std::vector<std::string> choices, bool optional) {
    Param p;
    p.name = std::move(name);
    p.type = Param::Type::choice;
    p.choices = std::move(choices);
    p.optional = optional;
    return p;
}

std::string on_off(bool v) { return v ? "on" : "off"; }

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string utf8(std::string_view text) {
    for (char c : text)
        if (static_cast<unsigned char>(c) >= 0x80) return std::string(text);
    return ui::original_to_utf8(text);
}

std::string fold(std::string s) {
    s = lower(std::move(s));
    // UTF-8 Ä Ö Ü (C3 84, C3 96, C3 9C) -> ä ö ü (C3 A4, C3 B6, C3 BC)
    for (size_t i = 0; i + 1 < s.size(); ++i)
        if (static_cast<unsigned char>(s[i]) == 0xC3) {
            unsigned char& c = reinterpret_cast<unsigned char&>(s[i + 1]);
            if (c == 0x84 || c == 0x96 || c == 0x9C) c = static_cast<unsigned char>(c + 0x20);
        }
    return s;
}

// Dates as the concept's `date` type: YYYY-MM-DD (01-grundlagen 1.2).
std::string date_text(int day, int month, int year) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "%04d-%02d-%02d", year, month, day);
    return buffer;
}

std::string money(long long value) { return std::to_string(value); }

size_t resolve_player(const std::string& reference, const std::vector<PlayerRef>& players) {
    const std::string key = lower(reference);
    auto number = [&](size_t prefix) {
        Arg a;
        a.text = reference.substr(prefix);
        const auto n = to_int(a);
        if (!n) fail("E_BAD_ARGUMENT", "\"" + reference + "\" is no player reference (id:<n>, row:<n> or a name)");
        return int(*n);
    };
    if (key.rfind("id:", 0) == 0 || key.rfind("row:", 0) == 0) {
        const bool by_id = key[0] == 'i';
        const int n = number(by_id ? 3 : 4);
        for (size_t i = 0; i < players.size(); ++i)
            if ((by_id ? players[i].id : players[i].row) == n) return i;
        fail("E_BAD_ARGUMENT", "no player " + reference + " in this list");
    }
    const std::string wanted = fold(reference);
    std::vector<size_t> exact, partial;
    for (size_t i = 0; i < players.size(); ++i) {
        const std::string name = fold(players[i].name);
        if (name == wanted) exact.push_back(i);
        else if (name.find(wanted) != std::string::npos) partial.push_back(i);
    }
    const auto& found = exact.empty() ? partial : exact;
    if (found.size() == 1) return found.front();
    if (found.empty()) fail("E_BAD_ARGUMENT", "no player named \"" + reference + "\" in this list");
    std::string candidates;
    for (size_t i = 0; i < found.size() && i < 8; ++i)
        candidates += (i ? ", " : "") + players[found[i]].name + " = id:" + std::to_string(players[found[i]].id);
    fail("E_AMBIGUOUS", "\"" + reference + "\" fits several players: " + candidates);
}

}  // namespace detail

namespace {

const char* class_name(char cls) {
    switch (cls) {
    case '#': return "status";
    case '>': return "option";
    case '|': return "table";
    case '+': return "ok";
    case '!': return "error";
    case '?': return "question";
    case 'i': return "info";
    case '~': return "event";
    case 'c': return "chat";
    default: return "other";
    }
}

// The type part of a parameter: "1..5", "bool", "string", "enum(a,b)".
std::string param_type(const Param& p) {
    switch (p.type) {
    case Param::Type::integer: return std::to_string(p.min) + ".." + std::to_string(p.max);
    case Param::Type::boolean: return "bool";
    case Param::Type::text: return p.max > 0 ? "string(max " + std::to_string(p.max) + ")" : "string";
    case Param::Type::choice: {
        std::string s = "enum(";
        for (size_t i = 0; i < p.choices.size(); ++i) s += (i ? "," : "") + p.choices[i];
        return s + ")";
    }
    }
    return {};
}

// A parameter in a signature: "name:type", optional ones in [brackets].
std::string param_text(const Param& p) {
    if (!p.display.empty()) return p.display;
    const std::string s = p.name + ":" + (p.type == Param::Type::text ? "string" : param_type(p));
    return p.optional ? "[" + s + "]" : s;
}

// The index of an enum argument: a name of the list (canonical comparison) or its number.
int choice_index(const Param& p, const Arg& a) {
    for (size_t i = 0; i < p.choices.size(); ++i)
        if (canonical(p.choices[i]) == canonical(a.text)) return int(i);
    if (const auto n = to_int(a); n && *n >= 0 && size_t(*n) < p.choices.size()) return int(*n);
    return -1;
}

size_t edit_distance(const std::string& a, const std::string& b) {
    std::vector<size_t> row(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) row[j] = j;
    for (size_t i = 1; i <= a.size(); ++i) {
        size_t diagonal = row[0];
        row[0] = i;
        for (size_t j = 1; j <= b.size(); ++j) {
            const size_t up = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0 : 1)});
            diagonal = up;
        }
    }
    return row[b.size()];
}

}  // namespace

long long Console::Values::integer(const std::string& name, long long fallback) const {
    const auto it = args.find(canonical(name));
    return it == args.end() ? fallback : *to_int(it->second);
}
bool Console::Values::boolean(const std::string& name, bool fallback) const {
    const auto it = args.find(canonical(name));
    return it == args.end() ? fallback : *to_bool(it->second);
}
std::string Console::Values::text(const std::string& name, std::string fallback) const {
    const auto it = args.find(canonical(name));
    return it == args.end() ? fallback : it->second.text;
}
int Console::Values::choice(const std::string& name) const {
    const auto it = args.find(canonical(name));
    if (it == args.end() || !spec) return -1;
    for (const Param& p : spec->params)
        if (canonical(p.name) == canonical(name)) return choice_index(p, it->second);
    return -1;
}

// ---------------------------------------------------------------------------------------

Console::Console(Options options, std::function<void(const std::string&)> write)
    : options_(std::move(options)), write_(std::move(write)) {
    texts_ = load_textd(ui::asset_path(options_.data, "TEXTD.BLK"));
    exe_ = load_le(ui::asset_path(options_.data, "BMH.exe"));
    // The program start up to the options dialog (InitializeCareer) - as the SDL app does it.
    session_ = std::make_unique<app::CareerSession>(options_.data, options_.seed);
    install_hooks();
    register_global_commands();
    register_start_commands();
    register_file_commands();
    register_desk_commands();
    register_table_commands();
    register_club_commands();
    register_money_commands();
    register_office_commands();
    register_squad_commands();
    register_transfer_commands();
    register_info_commands();
    register_match_commands();
    register_friendly_commands();
    register_reschedule_commands();
    register_balance_commands();
    register_environment_commands();
    register_state_commands();
    register_advice_commands();
}

// ~Console is in windows_match.cpp, where the engine thread (EngineRun) is complete.

std::string Console::text(int index) const {
    if (index < 0 || size_t(index) >= texts_.strings.size()) return {};
    std::string s = utf8(texts_.strings[size_t(index)]);
    // dialog markers at the start steer the original's dialog mode (09-texte 1.6)
    while (!s.empty() && (s.rfind("\xC2\xA7", 0) == 0 || s.rfind("\xC2\xBA", 0) == 0)) s.erase(0, 2);
    // padding for the screen layout is no content
    while (!s.empty() && s.back() == ' ') s.pop_back();
    while (!s.empty() && s.front() == ' ') s.erase(0, 1);
    return s;
}

// ---------------------------------------------------------------------------------------
// registry

void Console::add(Spec spec, Handler handler) {
    handlers_[spec.window + "/" + canonical(spec.name)] = std::move(handler);
    specs_.push_back(std::move(spec));
}

const Spec* Console::find(std::string_view window, std::string_view key) const {
    for (const Spec& s : specs_)
        if (s.window == window && canonical(s.name) == key) return &s;
    return nullptr;
}

void Console::register_global_commands() {
    // --- global commands, valid in every window (01-grundlagen 9) ----------------------
    Param list_param = detail::text("command", 0, true);
    list_param.display = "[command:string|all]";
    add({"*", "ListOptions", {list_param}, "commands possible here; ListOptions(all) also shows locked ones", false},
        [this](const Values& v) {
            const std::string which = v.text("command");
            if (!which.empty() && canonical(which) != "all") {
                const Spec* spec = find(window_, canonical(which));
                if (!spec) spec = find("*", canonical(which));
                if (!spec) fail("E_UNKNOWN_COMMAND", which + " is not a command of " + window_);
                emit('>', option_line(*spec));
                std::vector<std::vector<std::string>> rows;
                for (const Param& p : spec->params) rows.push_back({p.name, param_type(p), p.optional ? "optional" : "required"});
                if (!rows.empty()) emit_table({"Parameter", "Type", "Use"}, rows);
                return;
            }
            const bool all = !which.empty();
            for (const Spec* spec : window_specs(all)) {
                std::string line = option_line(*spec);
                if (canonical(spec->name) == "answer" && !question_) line += " [locked: no open question]";
                emit('>', line);
                // the desk's icons: one line per window Open() reaches (02-fenster 4 and 6)
                if (window_ == "Desktop" && canonical(spec->name) == "open" && career_) {
                    ui::DeskState d = desk();
                    std::sort(d.items.begin(), d.items.end(), [](const auto& a, const auto& b) { return a.kind < b.kind; });
                    for (const auto& item : d.items) {
                        if (item.kind >= 30 || item.kind == 12) continue;  // widgets; 12 = Kickoff
                        if (item.disabled && !all) continue;
                        std::string target = "    Open(" + menu_window(item.kind) + ")";
                        if (target.size() < 48) target.resize(48, ' ');
                        target += " - menu " + std::to_string(item.kind);
                        if (item.disabled) target += " [locked: mode]";
                        emit('>', target);
                    }
                }
            }
        });
    add({"*", "GoBack", {}, "right mouse button: one level back, otherwise nothing", false},
        [this](const Values& v) {
            if (question_) {
                // a question's right mouse button: its default answer (01-grundlagen 2.4) - only
                // from the player it belongs to, and said aloud (it may decline a sale)
                if (v.player && v.player != question_->player)
                    fail("E_NOT_YOUR_TURN", "the question belongs to player " + std::to_string(question_->player));
                Question q = std::move(*question_);
                question_.reset();
                emit('+', "Q" + std::to_string(q.id) + " answered: " + q.default_option + " (GoBack = default answer)");
                q.on_answer(q.default_option);
                return;
            }
            if (window_ == "NewGame") new_game_continue();  // exception: right mouse = next (H1 S. 13)
            else if (const auto it = closers_.find(window_); it != closers_.end()) it->second();
            else if (!close_window()) emit('+', "nothing to go back to");
        });
    Param chat_to = integer("to", 0, 4, true);
    add({"*", "Chat", {chat_to, detail::text("text", 200)}, "message to all, or to one player (0 = only the log)", false},
        [this](const Values& v) {
            const std::string message = v.text("text");
            if (message.empty()) fail("E_BAD_ARGUMENT", "text must not be empty");
            const int from = v.player ? v.player : (player_ ? player_ : 1);
            const int to = v.has("to") ? int(v.integer("to")) : -1;
            chat_.push_back({from, to, message});
            const std::string target = to < 0 ? "All" : to == 0 ? "Log" : "P" + std::to_string(to);
            emit('c', "[P" + std::to_string(from) + " -> " + target + "] " + message);
        });
    add({"*", "ChatLog", {integer("count", 1, 1000, true)}, "the last chat messages", false}, [this](const Values& v) {
        const size_t count = size_t(v.integer("count", 20));
        std::vector<std::vector<std::string>> rows;
        const size_t first = chat_.size() > count ? chat_.size() - count : 0;
        for (size_t i = first; i < chat_.size(); ++i) {
            const ChatEntry& e = chat_[i];
            rows.push_back({std::to_string(i + 1), "P" + std::to_string(e.from),
                            e.to < 0 ? "All" : e.to == 0 ? "Log" : "P" + std::to_string(e.to), e.text});
        }
        emit_table({"#", "From", "To", "Text"}, rows);
    });
    Param answer_option = detail::text("option");
    answer_option.display = "option:enum";
    Param answer_id = detail::text("id");
    answer_id.display = "id:int";  // 2 or Q2, as the question line shows it
    add({"*", "Answer", {answer_id, answer_option}, "answer the open question", false},
        [this](const Values& v) {
            if (!question_) fail("E_BAD_ARGUMENT", "there is no open question");
            std::string id_text = v.text("id");
            if (!id_text.empty() && (id_text[0] == 'Q' || id_text[0] == 'q')) id_text.erase(0, 1);
            Arg id_arg;
            id_arg.text = id_text;
            const auto id = to_int(id_arg);
            if (!id) fail("E_BAD_ARGUMENT", "id must be the question number, e.g. 2 or Q2");
            if (*id != question_->id) fail("E_BAD_ARGUMENT", "the open question is Q" + std::to_string(question_->id));
            if (v.player && v.player != question_->player)
                fail("E_NOT_YOUR_TURN", "the question belongs to player " + std::to_string(question_->player));
            Arg arg;
            arg.text = v.text("option");
            std::string chosen;
            for (const std::string& o : question_->options)
                if (canonical(o) == canonical(arg.text)) chosen = o;
            if (chosen.empty() && question_->options == std::vector<std::string>{"Yes", "No"})
                if (const auto b = to_bool(arg)) chosen = *b ? "Yes" : "No";
            if (chosen.empty())
                if (const auto n = to_int(arg); n && *n >= 1 && size_t(*n) <= question_->options.size())
                    chosen = question_->options[size_t(*n - 1)];
            if (chosen.empty()) fail("E_BAD_ARGUMENT", "option must be one of the question's options");
            Question q = std::move(*question_);
            question_.reset();
            emit('+', "Q" + std::to_string(q.id) + " answered: " + chosen);  // (some answers print nothing else)
            q.on_answer(chosen);
        });
    add({"*", "Status", {}, "window, player and a short overview", false}, [this](const Values& v) {
        // a seat not on turn sees the desk of the manager on turn: say so, so it is not read
        // as its own club
        if (v.player && player_ && v.player != player_)
            emit('+', "you are player " + std::to_string(v.player) + "; player " + std::to_string(player_) +
                          " is on turn - the overview below is his");
        if (run_) {
            emit('+', match_status());
            return;
        }
        if (!career_) {
            emit('+', "options: " + options_text());
        } else {
            const ui::DeskState d = desk();
            emit('+', utf8(d.manager_name) + " | " + utf8(d.club_name) + " | " +
                          date_text(d.date.day, d.date.month, d.date.year) + " | balance " + money(d.balance));
        }
        if (question_) emit('+', "open question: Q" + std::to_string(question_->id));
    });
    add({"*", "Help", {detail::text("command", 0, true)}, "command reference (all windows)", false}, [this](const Values& v) {
        const std::string which = v.text("command");
        for (const Spec& spec : specs_) {
            if (!which.empty() && canonical(spec.name) != canonical(which)) continue;
            emit('>', "[" + spec.window + "] " + option_line(spec));
        }
        if (!which.empty() && std::none_of(specs_.begin(), specs_.end(),
                                           [&](const Spec& s) { return canonical(s.name) == canonical(which); }))
            fail("E_UNKNOWN_COMMAND", which + " is no command");
    });
    add({"*", "Quit", {boolean("force", true)}, "end the session (asks first during a game)", false},
        [this](const Values& v) {
            auto end = [this] {
                if (run_) stop_matchday();
                ended_ = true;
                emit('+', "session ended");
            };
            if (question_ && !v.boolean("force"))
                fail("E_PENDING_QUESTION", "answer Q" + std::to_string(question_->id) + " first (or Quit(force=true))");
            if (!career_ || v.boolean("force")) {
                end();
                return;
            }
            // the plug on the desk asks first (H2 S. 24); the question is the console's own
            // text: the original asks with a picture, not with a TEXTD text
            ask("Quit the game? Unsaved progress is lost.", {"Yes", "No"}, "No", [this, end](const std::string& answer) {
                if (answer == "Yes") end();
                else emit('+', "quit cancelled");
            });
        });
}

// ---------------------------------------------------------------------------------------
// input

void Console::start() {
    last_block_.clear();
    last_output_.clear();
    flush_status();
}

bool Console::feed(std::string_view line) {
    if (ended_) return false;
    switch (classify_line(line)) {
    case LineKind::skip: break;
    case LineKind::clear: pending_.clear(); break;
    case LineKind::command: pending_.emplace_back(line); break;
    case LineKind::go: execute_block(); break;
    }
    return !ended_;
}

void Console::execute_block() {
    last_block_ = std::move(pending_);
    pending_.clear();
    last_output_.clear();
    if (last_block_.empty()) {
        emit('+', "nothing to do");
        return;
    }
    for (size_t i = 0; i < last_block_.size() && !ended_; ++i) {
        try {
            Command cmd;
            try {
                cmd = parse_command(last_block_[i]);
            } catch (const ParseError& e) {
                fail("E_BAD_ARGUMENT", std::string("syntax: ") + e.what());
            }
            try {
                execute(cmd);
            } catch (const CliError&) {
                throw;
            } catch (const std::exception& e) {
                // an engine exception is a defect of the program, never of the input
                fail("E_INTERNAL", e.what());
            }
        } catch (const CliError& e) {
            emit('!', e.code + ": " + e.message + (e.text_key.empty() ? "" : " [textKey: " + e.text_key + "]"));
            const size_t skipped = last_block_.size() - i - 1;
            if (skipped) emit('!', "E_BLOCK_ABORTED: " + std::to_string(skipped) + " command" + (skipped == 1 ? "" : "s") + " skipped");
            break;
        }
    }
    flush_status();
}

void Console::execute(const Command& cmd) {
    const Spec* spec = find(window_, cmd.key);
    if (!spec) spec = find("*", cmd.key);
    if (!spec) {
        // Run/Step outside a match: with several managers the match day starts only after the
        // last one's Kickoff (each Kickoff passes the turn) - agents sent Run here again and
        // again in the learning session, so the error says what to do instead
        if ((cmd.key == "run" || cmd.key == "step") && window_ != "Match")
            fail("E_NOT_IN_WINDOW", "no match is running - the match day starts after every manager's Kickoff; "
                                    "WaitTurn (or Status) shows when the window is Match");
        for (const Spec& s : specs_)
            if (canonical(s.name) == cmd.key) fail("E_NOT_IN_WINDOW", s.name + " is only available in " + s.window);
        std::string best;
        size_t best_distance = 1000;
        for (const Spec& s : specs_) {
            const size_t d = edit_distance(cmd.key, canonical(s.name));
            if (d < best_distance) best_distance = d, best = s.name;
        }
        fail("E_UNKNOWN_COMMAND", cmd.name + " is unknown" +
                                      (best_distance <= std::max<size_t>(2, cmd.key.size() / 3) ? " (did you mean " + best + "?)" : ""));
    }
    // An engine question owns the paused kickoff. Some navigation commands are
    // changes=false yet prepare Native state on entry (notably Open(Lineup)).
    // Only explicit observers and the current question's answer may run here.
    const bool engine_question_observer =
        spec->window == "*" &&
        (cmd.key == "answer" || cmd.key == "goback" || cmd.key == "status" ||
         cmd.key == "help" || cmd.key == "listoptions" || cmd.key == "chat" || cmd.key == "chatlog" ||
         (cmd.key == "quit" && loan_return_modal_active()));
    const bool engine_question_desk_observer =
        spec->window == "Desktop" && (cmd.key == "showdate" || cmd.key == "showopponent");
    if (question_ && (spec->changes ||
        (run_ && !engine_question_observer && !engine_question_desk_observer)))
        fail("E_PENDING_QUESTION", "answer Q" + std::to_string(question_->id) + " first");
    if (loan_return_offer_active() && !engine_question_observer && spec->window != "Transfer.Negotiate")
        fail("E_MODE_LOCKED", "finish the loan-return negotiation first");
    if (spec->changes && cmd.player && player_ && cmd.player != player_)
        fail("E_NOT_YOUR_TURN", "current player is " + std::to_string(player_));
    if (loan_return_modal_active() && cmd.player && cmd.player != player_ &&
        (cmd.key == "goback" || cmd.key == "quit"))
        fail("E_NOT_YOUR_TURN", "the loan-return modal belongs to player " + std::to_string(player_));
    // A player who is not on turn shares the screen with the active one: he may use the
    // commands of every window (Status, Help, Chat, Answer ...) but not those of the open
    // window, which would page or open things under the active player's hands.
    if (cmd.player && player_ && cmd.player != player_ && spec->window != "*")
        fail("E_NOT_YOUR_TURN", "current player is " + std::to_string(player_) + " (WaitTurn waits for your turn)");
    Values values = bind(*spec, cmd);
    handlers_.at(spec->window + "/" + canonical(spec->name))(values);
    succeeded_.insert(spec->window + "/" + spec->name);
}

Console::Values Console::bind(const Spec& spec, const Command& cmd) const {
    Values values;
    values.spec = &spec;
    values.player = cmd.player;
    std::vector<const Arg*> positional;
    for (const Arg& a : cmd.args)
        if (a.name.empty()) positional.push_back(&a);
    const size_t required = size_t(std::count_if(spec.params.begin(), spec.params.end(), [](const Param& p) { return !p.optional; }));
    if (positional.size() > spec.params.size())
        fail("E_BAD_ARGUMENT", spec.name + " takes at most " + std::to_string(spec.params.size()) + " arguments");
    // positional arguments fill the required parameters; the surplus fills optional ones
    // from the left (Chat("x") = text, Chat(2, "x") = to + text)
    // "fee:1500000" looks like the name:type of ListOptions but is a positional text; say how
    // named arguments are written (references like id:7, club:3 go to text parameters)
    for (const Arg* a : positional) {
        const size_t colon = a->text.find(':');
        if (colon == std::string::npos || colon == 0) continue;
        const std::string head = canonical(a->text.substr(0, colon));
        if (head == canonical("id") || head == canonical("row")) continue;
        for (const Param& p : spec.params)
            if (canonical(p.name) == head && p.type != Param::Type::text)  // texts take references like club:3
                fail("E_BAD_ARGUMENT", "named arguments are written name=value, e.g. " + p.name + "=" + a->text.substr(colon + 1));
    }
    size_t optional_slots = positional.size() > required ? positional.size() - required : 0;
    size_t next = 0;
    for (const Param& p : spec.params) {
        if (next >= positional.size()) break;
        if (p.optional) {
            if (optional_slots == 0) continue;
            --optional_slots;
        }
        values.args[canonical(p.name)] = *positional[next++];
    }
    for (const Arg& a : cmd.args) {
        if (a.name.empty()) continue;
        const auto it = std::find_if(spec.params.begin(), spec.params.end(), [&](const Param& p) { return canonical(p.name) == a.name; });
        if (it == spec.params.end()) fail("E_BAD_ARGUMENT", spec.name + " has no parameter " + a.name);
        if (values.args.count(a.name)) fail("E_BAD_ARGUMENT", "parameter " + it->name + " given twice");
        values.args[a.name] = a;
    }
    for (const Param& p : spec.params) {
        const auto it = values.args.find(canonical(p.name));
        if (it == values.args.end()) {
            if (!p.optional) fail("E_BAD_ARGUMENT", spec.name + " needs " + param_text(p));
            continue;
        }
        const Arg& a = it->second;
        switch (p.type) {
        case Param::Type::integer: {
            const auto n = to_int(a);
            if (!n || *n < p.min || *n > p.max)
                fail("E_BAD_ARGUMENT", p.name + " must be " + std::to_string(p.min) + ".." + std::to_string(p.max));
            break;
        }
        case Param::Type::boolean:
            if (!to_bool(a)) fail("E_BAD_ARGUMENT", p.name + " must be true or false");
            break;
        case Param::Type::text:
            if (p.max > 0 && a.text.size() > size_t(p.max))
                fail("E_BAD_ARGUMENT", p.name + " is longer than " + std::to_string(p.max) + " characters");
            break;
        case Param::Type::choice:
            if (choice_index(p, a) < 0) fail("E_BAD_ARGUMENT", p.name + " must be one of " + param_type(p));
            break;
        }
    }
    return values;
}

// ---------------------------------------------------------------------------------------
// output

void Console::emit(char cls, const std::string& text, Json data) {
    if (cls != '#') flush_status();
    std::string line;
    if (options_.json) {
        Json::Object o;
        o["class"] = class_name(cls);
        o["window"] = window_;
        o["player"] = player_ ? Json(player_) : Json("All");
        o["text"] = text;
        if (!std::holds_alternative<std::nullptr_t>(data.value)) o["data"] = std::move(data);
        line = dump_json(Json(std::move(o)));
    } else {
        line = std::string(1, cls) + " " + text;
    }
    last_output_.push_back(line);
    if (observer_) observer_({cls, player_, text, line});
    write_(line);
}

void Console::emit_table(const std::vector<std::string>& header, const std::vector<std::vector<std::string>>& rows) {
    auto row_text = [](const std::vector<std::string>& cells) {
        std::string s = "|";
        for (const std::string& c : cells) s += " " + c + " |";
        return s.substr(2);  // emit adds "| "
    };
    auto cells_json = [](const std::vector<std::string>& cells, bool head) {
        Json::Array a;
        for (const std::string& c : cells) a.emplace_back(c);
        return Json(Json::Object{{"header", head}, {"cells", Json(std::move(a))}});
    };
    // With --json each row also carries a record keyed by the column names, the cells typed:
    // whole numbers and the references id:/row:/club: as numbers, "-" and "" as null, the
    // rest as text - what a graphical client needs without parsing the table (stage 2.5).
    auto typed = [](const std::string& cell) -> Json {
        if (cell.empty() || cell == "-") return Json(nullptr);
        std::string digits = cell;
        for (const char* prefix : {"id:", "row:", "club:"})
            if (digits.rfind(prefix, 0) == 0) digits = digits.substr(std::string(prefix).size());
        const bool negative = !digits.empty() && digits[0] == '-';
        const size_t from = negative ? 1 : 0;
        if (digits.size() > from && digits.size() - from <= 18 &&
            digits.find_first_not_of("0123456789", from) == std::string::npos)
            return Json(std::int64_t(std::stoll(digits)));
        return Json(cell);
    };
    emit('|', row_text(header), cells_json(header, true));
    for (const auto& r : rows) {
        Json data = cells_json(r, false);
        if (options_.json) {
            Json::Object record;
            for (size_t i = 0; i < r.size(); ++i)
                record[i < header.size() && !header[i].empty() ? header[i] : "c" + std::to_string(i + 1)] = typed(r[i]);
            data.object()->insert_or_assign("record", Json(std::move(record)));
        }
        emit('|', row_text(r), std::move(data));
    }
}

void Console::flush_status() {
    if (window_ == printed_window_ && player_ == printed_player_) return;
    printed_window_ = window_;
    printed_player_ = player_;
    emit('#', "Window: " + window_);
    emit('#', "Player: " + (player_ ? std::to_string(player_) : std::string("All")));
}

std::string Console::option_line(const Spec& spec) const {
    std::string sig = spec.name + "(";
    for (size_t i = 0; i < spec.params.size(); ++i) sig += (i ? ", " : "") + param_text(spec.params[i]);
    sig += ")";
    if (spec.description.empty()) return sig;
    if (sig.size() < 48) sig.resize(48, ' ');
    return sig + " - " + spec.description;
}

std::vector<const Spec*> Console::window_specs(bool include_locked) const {
    std::vector<const Spec*> result;
    for (const Spec& s : specs_)
        if (s.window == window_) result.push_back(&s);
    for (const Spec& s : specs_) {
        if (s.window != "*") continue;
        if (canonical(s.name) == "answer" && !question_ && !include_locked) continue;
        result.push_back(&s);
    }
    return result;
}

// ---------------------------------------------------------------------------------------
// windows and questions

void Console::set_window(std::string window, int player) {
    window_ = std::move(window);
    player_ = player;
    back_.clear();
}

void Console::open_window(std::string window) {
    back_.push_back(window_);
    window_ = std::move(window);
}

bool Console::close_window() {
    if (back_.empty()) {
        // the start windows have a fixed way back (02-fenster 3)
        if (window_ == "NewGame.LoadGame" || (window_ == "NewGame.Select" && confirmed_.empty())) {
            set_window("NewGame", 1);
            return true;
        }
        return false;
    }
    window_ = back_.back();
    back_.pop_back();
    return true;
}

void Console::ask(std::string text, std::vector<std::string> options, std::string default_option,
                  std::function<void(const std::string&)> on_answer, std::string text_key) {
    Question q;
    q.id = next_question_++;
    q.player = player_ ? player_ : 1;
    q.text = std::move(text);
    q.options = std::move(options);
    q.default_option = std::move(default_option);
    q.on_answer = std::move(on_answer);
    std::string opts;
    for (size_t i = 0; i < q.options.size(); ++i) opts += (i ? " | " : "") + q.options[i];
    // --json: the question as data (id, the player it belongs to, the options and the answer
    // GoBack gives), so a client can show a dialog without parsing the line
    Json data = nullptr;
    if (options_.json) {
        Json::Array options;
        for (const auto& o : q.options) options.emplace_back(o);
        data = Json(Json::Object{{"id", q.id}, {"player", q.player}, {"text", q.text}, {"options", Json(std::move(options))},
                                 {"default", q.default_option}, {"text_key", text_key}});
    }
    emit('?', "Q" + std::to_string(q.id) + " \"" + q.text + "\"  options: " + opts +
                  (text_key.empty() ? "" : "  [textKey: " + text_key + "]"),
         std::move(data));
    question_ = std::move(q);
}

}  // namespace bmh::cli::console
