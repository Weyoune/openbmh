// SPDX-License-Identifier: GPL-3.0-only
#include "cli/seat_hub.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "cli/console/parser.hpp"
#include "cli/json.hpp"

namespace bmh::cli {

namespace {

std::string trimmed(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t") - a + 1);
}

bool is_wait_turn(const std::string& command) {
    const std::string key = console::canonical(command);
    return key == "waitturn" || key == "waitturn()";
}

const char* json_class(char cls) {  // as Console::emit names them
    switch (cls) {
    case '#': return "status";
    case '!': return "error";
    default: return "info";
    }
}

}  // namespace

SeatHub::SeatHub(console::Options options, int seats, std::function<void(const console::Console&)> on_block)
    : options_(options),
      console_(options, [](const std::string&) {}),
      seats_(size_t(seats < 1 ? 1 : seats)),
      on_block_(std::move(on_block)),
      asked_(4) {
    console_.set_line_observer([this](const console::Console::LineInfo& line) { lines_.push_back(line); });
    console_.start();
    for (const auto& l : lines_) start_.push_back(l.line);
    lines_.clear();
    if (on_block_) on_block_(console_);
}

std::string SeatHub::format(char cls, const std::string& text) const {
    if (!options_.json) return std::string(1, cls) + " " + text;
    return dump_json(Json(Json::Object{{"class", json_class(cls)}, {"text", text}}));
}

// A line concerns a seat when it belongs to its manager or to all; chat lines name their
// addressee ("[P1 -> P2] ...", "[P1 -> All] ...").
bool SeatHub::concerns(const console::Console::LineInfo& line, int seat) const {
    if (line.cls == 'c') {
        const size_t arrow = line.text.find(" -> ");
        const size_t end = line.text.find(']', arrow == std::string::npos ? 0 : arrow);
        if (arrow == std::string::npos || end == std::string::npos) return true;
        const std::string target = line.text.substr(arrow + 4, end - arrow - 4);
        return target == "All" || target == "P" + std::to_string(seat);
    }
    return line.player == 0 || line.player == seat;
}

bool SeatHub::on_turn(int seat) const {
    if (console_.ended()) return true;
    if (const int q = console_.question_player()) return q == seat;
    return console_.active_player() == seat || console_.active_player() == 0;
}

std::vector<std::string> SeatHub::answer(Seat& seat, std::vector<std::string> own) {
    std::vector<std::string> response;
    if (!seat.feed.empty()) {
        response.push_back(format('i', "--- meanwhile ---"));
        response.insert(response.end(), seat.feed.begin(), seat.feed.end());
        response.push_back(format('i', "--- end of meanwhile ---"));
    }
    response.insert(response.end(), own.begin(), own.end());
    seat.feed.clear();
    return response;
}

std::optional<std::vector<std::string>> SeatHub::submit(int seat, std::vector<std::string> lines) {
    using console::classify_line;
    using console::LineKind;
    if (seat < 1 || seat > int(seats_.size())) return std::vector<std::string>{format('!', "E_BAD_ARGUMENT: no seat " + std::to_string(seat))};
    Seat& me = seats_[size_t(seat - 1)];
    while (!lines.empty() && trimmed(lines.back()).empty()) lines.pop_back();
    std::vector<std::string> commands;
    for (const auto& l : lines)
        if (classify_line(l) == LineKind::command) commands.push_back(trimmed(l));
    const bool multi = seats_.size() > 1;

    if (commands.size() == 1 && is_wait_turn(commands[0])) {
        me.waiting = true;
        if (!multi) {  // one client: always its turn
            auto now = release();
            if (!now.empty()) return now.front().second;
        }
        return std::nullopt;
    }
    for (const auto& c : commands)
        if (is_wait_turn(c))
            return answer(me, {format('!', "E_BAD_ARGUMENT: WaitTurn must be a block of its own (nothing else in it)")});

    // A live match runs for everybody (player All): the clock moves only when every seat has
    // sent its Run or Step - after its own interventions (tactics, substitutions, moves). The
    // earliest stop asked for is taken (Step(n) before Run(Halftime) before Run); every seat
    // then gets the output. Without this the first Run would play the day to the end and the
    // other managers would miss their half time.
    // A block that ends with the Run (interventions first, then "Run") runs its other lines
    // now and holds only the Run - otherwise its Run would play past the barrier (learning
    // session run 1: the other seats then waited for a match day that was over).
    if (multi && console_.window() == "Match" && !commands.empty()) {
        // Only the last command of a block may move the clock: a Run/Step in the middle
        // ("Step(3)", "SetTactic(..)", "Run(Halftime)", "ShowMatchStats") would run past the
        // barrier for everybody (learning session run 2, block 715).
        for (size_t i = 0; i + 1 < commands.size(); ++i) {
            std::string m = commands[i];
            if (!m.empty() && m[0] == '@') m = trimmed(m.substr(m.find_first_not_of("@0123456789")));
            const std::string key = console::canonical(m.substr(0, m.find('(')));
            if (key == "run" || key == "step")
                return answer(me, {format('!', "E_BAD_ARGUMENT: in a live match only the last command of a block may be "
                                                "Run or Step (the clock waits for every manager) - nothing was executed")});
        }
        std::string c = commands.back();
        if (!c.empty() && c[0] == '@') c = trimmed(c.substr(c.find_first_not_of("@0123456789")));
        const std::string name = console::canonical(c.substr(0, c.find('(')));
        if ((name == "run" || name == "step") && !me.run.empty())
            return answer(me, {format('!', "E_BAD_ARGUMENT: your " + me.run + " is already waiting for the other managers")});
        if ((name == "run" || name == "step") && commands.size() > 1) {
            std::vector<std::string> before;
            for (const auto& l : lines) {
                if (classify_line(l) == LineKind::command && trimmed(l) == commands.back()) break;
                before.push_back(l);
            }
            before.push_back("go.");
            // (the earlier lines are checked as any block: an @n of another seat is refused)
            auto first = submit(seat, before);
            if (first) me.held = std::move(*first);
            if (console_.window() != "Match") {  // (cannot happen: interventions do not end the day)
                std::vector<std::string> out = std::move(me.held);
                me.held.clear();
                out.push_back(format('i', "the match day is over - " + c + " was not needed"));
                return out;
            }
            commands = {commands.back()};
        }
        if (name == "run" || name == "step") {
            me.run = c;
            for (const auto& s : seats_)
                if (s.run.empty()) return std::nullopt;  // wait for the others
            int step = 0;
            bool halftime = false;
            for (const auto& s : seats_) {
                const std::string key = console::canonical(s.run);
                if (key.rfind("step", 0) == 0) {
                    const size_t open = s.run.find('(');
                    const int n = open == std::string::npos ? 1 : std::max(1, std::atoi(s.run.c_str() + open + 1));
                    step = step == 0 ? n : std::min(step, n);
                }
                if (key.find("halftime") != std::string::npos) halftime = true;
            }
            const std::string chosen = step ? "Step(" + std::to_string(step) + ")" : halftime ? "Run(Halftime)" : "Run";
            std::vector<std::string> own = run_block(seat, {"@" + std::to_string(seat) + " " + chosen, "go."});
            for (size_t j = 0; j < seats_.size(); ++j) {
                Seat& s = seats_[j];
                s.run.clear();
                std::vector<std::string> mine = std::move(s.held);
                s.held.clear();
                if (int(j) + 1 == seat) mine.insert(mine.end(), own.begin(), own.end());
                ready_.emplace_back(int(j) + 1, answer(s, std::move(mine)));
            }
            return std::nullopt;  // every seat's answer comes out of release()
        }
    }
    // every command line acts for this seat's manager (one seat: the active player, as at the
    // console)
    if (multi) {
        std::string refused;
        for (auto& l : lines) {
            if (classify_line(l) != LineKind::command) continue;
            const std::string t = trimmed(l);
            if (t.front() == '@') {
                size_t i = 1;
                while (i < t.size() && std::isdigit(static_cast<unsigned char>(t[i]))) ++i;
                if (t.substr(1, i - 1) != std::to_string(seat)) refused = t;
                l = t;
            } else {
                l = "@" + std::to_string(seat) + " " + t;
            }
        }
        if (!refused.empty())
            return answer(me, {format('!', "E_NOT_YOUR_TURN: you are player " + std::to_string(seat) + " (" + refused + ")")});
    }
    if (lines.empty() || classify_line(lines.back()) != LineKind::go) lines.push_back("go.");

    return answer(me, run_block(seat, std::move(lines)));
}

// Executes one block for a seat and splits its output (see submit).
std::vector<std::string> SeatHub::run_block(int seat, std::vector<std::string> lines) {
    using console::classify_line;
    using console::LineKind;
    const bool multi = seats_.size() > 1;
    std::vector<std::string> own;
    const bool sender_on_turn = !multi || on_turn(seat);  // (also true for the answer to his question)
    for (const auto& l : lines) {
        lines_.clear();
        const bool executes = classify_line(l) == LineKind::go;
        console_.feed(l);
        if (executes && on_block_) on_block_(console_);
        // Errors and listings ('!', '>') and the sender's own chat answer the sender only;
        // another manager's lines (his desk after the turn passed) go to that seat only. A seat
        // that was not on turn can change nothing for the others: it gets all of its output,
        // the others only its chat.
        for (const auto& line : lines_) {
            if (line.cls == '?' && line.player >= 1 && line.player <= 4) asked_[size_t(line.player - 1)] = line.line;
            if (!multi) {
                own.push_back(line.line);
                continue;
            }
            const bool chat = line.cls == 'c';
            const bool reply = line.cls == '!' || line.cls == '>' ||
                               (chat && line.text.rfind("[P" + std::to_string(seat) + " ", 0) == 0);
            if (!sender_on_turn || reply || line.cls == '#' || concerns(line, seat)) own.push_back(line.line);
            if (reply && !chat) continue;
            if (!sender_on_turn && !chat) continue;
            for (size_t j = 0; j < seats_.size(); ++j)
                if (int(j) + 1 != seat && concerns(line, int(j) + 1)) seats_[j].feed.push_back(line.line);
        }
        if (console_.ended()) break;
    }
    lines_.clear();
    return own;
}

void SeatHub::drop_stale_runs() {
    if (console_.window() == "Match" && !console_.ended()) return;
    for (size_t j = 0; j < seats_.size(); ++j) {
        Seat& s = seats_[j];
        if (s.run.empty()) continue;
        std::vector<std::string> mine = std::move(s.held);
        s.held.clear();
        mine.push_back(format('i', "the match day is over - your " + s.run + " was not needed"));
        s.run.clear();
        ready_.emplace_back(int(j) + 1, answer(s, std::move(mine)));
    }
}

std::vector<std::pair<int, std::vector<std::string>>> SeatHub::release() {
    drop_stale_runs();
    std::vector<std::pair<int, std::vector<std::string>>> out;
    // the answers of a live-match Run every seat agreed on come first
    out.swap(ready_);
    for (size_t k = 0; k < seats_.size(); ++k) {
        Seat& seat = seats_[k];
        const int number = int(k) + 1;
        if (!seat.waiting || !(seats_.size() == 1 || on_turn(number))) continue;
        const int active = console_.active_player();
        std::vector<std::string> status{format('#', "Window: " + console_.window()),
                                        format('#', "Player: " + (active ? std::to_string(active) : std::string("All")))};
        if (console_.ended()) status.push_back(format('#', "Session ended"));
        // a question for this seat may have been asked in its own last answer: repeat it, so a
        // client does not mistake the turn for a free one (GoBack would decline it)
        if (console_.question_player() == number && !asked_[k].empty()) status.push_back(asked_[k]);
        out.emplace_back(number, answer(seat, std::move(status)));
        seat.waiting = false;
    }
    return out;
}

bool SeatHub::replay(const std::vector<std::string>& block, const std::vector<std::string>& expected) {
    for (const auto& l : block) console_.feed(l);
    console_.feed("go.");
    if (on_block_) on_block_(console_);
    lines_.clear();
    return console_.last_output() == expected;
}

void SeatHub::forget(int seat) {
    if (seat >= 1 && seat <= int(seats_.size())) seats_[size_t(seat - 1)].waiting = false;
}

}  // namespace bmh::cli
