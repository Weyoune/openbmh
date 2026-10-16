// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// SeatHub: one console game shared by several clients (agents, a graphical front end), each
// client one human manager ("seat"). The transports - the mailbox directories of --serve and
// the TCP connections of --listen (src/cli/main.cpp) - only move blocks of lines; the hub
// decides what each seat may do and sees:
// - A seat's command lines run as "@k <line>": a client acts only as its own manager (a line
//   with another @n is refused). Whoever is not on turn may only use the commands of every
//   window (Status, Help, Chat, Answer, State ...); the console says E_NOT_YOUR_TURN.
// - An answer starts with what happened in the other seats' blocks since this seat's last
//   answer and concerns it - lines of player All or k, chat to all or to Pk - between two
//   "--- meanwhile ---" info lines. Errors and listings stay with their sender; another
//   manager's desk lines go to that manager only.
// - A block that holds only "WaitTurn" is held until manager k is on turn, a question for
//   him is open, a live match runs (player All) or the session ends; its answer is the
//   meanwhile part, the window and player status and an open question for him again.
// Lines are routed by Console::LineInfo (class, player, text), so the text and the JSON
// format (--json) behave the same. With one seat the hub is a plain pass-through.
// - During a live match (stepped time, window Match, player All) a Run or Step is held until
//   every seat has sent one; the earliest stop is played and every seat gets the output.
//   A block that ends with Run/Step (e.g. "Substitute(..)" then "Run") runs its other lines
//   at once and holds the Run. Should the match day end while runs are held (it cannot, as
//   every Run goes through the barrier, but a held seat must never wait for ever), the held
//   seats are answered with what their lines did and a note.

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cli/console/console.hpp"

namespace bmh::cli {

class SeatHub {
public:
    // `seats`: number of clients (1-4). `on_block`: called after every executed block (the
    // console's last_block / last_output), for the session log.
    SeatHub(console::Options options, int seats, std::function<void(const console::Console&)> on_block);

    // The start output (Window/Player lines), for each newly connected seat.
    const std::vector<std::string>& start_output() const { return start_; }
    int seats() const { return int(seats_.size()); }
    bool ended() const { return console_.ended(); }

    // One block from seat `seat` (1-based). Returns its answer, or nothing when the block is a
    // WaitTurn that is held; release() hands such answers out later.
    std::optional<std::vector<std::string>> submit(int seat, std::vector<std::string> lines);
    // Held WaitTurns that can be answered now (after a block of another seat, or at the end).
    std::vector<std::pair<int, std::vector<std::string>>> release();
    // A held WaitTurn of a seat whose client went away is dropped.
    void forget(int seat);
    // Feeds one executed block of an earlier session log again (--resume): the console draws
    // no random numbers, so the game reaches the same state. Returns false when the output
    // differs from `expected`.
    bool replay(const std::vector<std::string>& block, const std::vector<std::string>& expected);

private:
    struct Seat {
        bool waiting = false;            // a WaitTurn is held
        std::vector<std::string> feed;   // other seats' lines that concern this seat
        std::string run;                 // its Run/Step during a live match, held until all have sent one
        std::vector<std::string> held;   // the output of the lines before that Run in its block
    };
    void drop_stale_runs();  // held runs of a match day that is over are answered
    std::string format(char cls, const std::string& text) const;  // a line of the hub itself
    bool concerns(const console::Console::LineInfo& line, int seat) const;
    bool on_turn(int seat) const;
    std::vector<std::string> answer(Seat& seat, std::vector<std::string> own);
    std::vector<std::string> run_block(int seat, std::vector<std::string> lines);

    console::Options options_;
    console::Console console_;
    std::vector<Seat> seats_;
    std::vector<std::string> start_;
    std::function<void(const console::Console&)> on_block_;
    // the lines of the block being executed, with their meaning
    std::vector<console::Console::LineInfo> lines_;
    std::vector<std::string> asked_;     // the last question line per player (index player-1)
    std::vector<std::pair<int, std::vector<std::string>>> ready_;  // answers of an agreed live-match Run
};

}  // namespace bmh::cli
