// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Helpers shared by the console's command files (console.cpp and windows_*.cpp). Not part of
// the console's public interface.

#include <map>
#include <string>
#include <vector>

#include "cli/console/console.hpp"

namespace bmh::cli::console::detail {

// A rejected command: "! <code>: <message> [textKey: <key>]" (01-grundlagen 6). A failed
// command changes neither the game nor the random sequence: handlers check everything they
// can before they call the engine, and engine calls that fail leave the session unchanged.
struct CliError {
    std::string code, message, text_key;
};

// The eight tactic settings (manager record +0xd2..+0xd9) by their place on the original
// board KB/11.VGA (hitboxes Tactics_Click_RuntimeField..Tactics_Click_RuntimeField6, src/ui/TACTICS.md) and the manual H2 p. 71.
// Item 0 is the attitude the match engine uses for a human side (match_engine.cpp
// update_attitude): fixed for the whole match unless the assistant controls it
// (SetAutomation(Tactics)); computer sides play 2 at home and 1 away and go up when behind.
// Items 1-3 and 5-6: the exact meaning of each diagram is not proven, the names say where.
inline constexpr const char* kTacticNames[8] = {
    "attitude 0 defensive .. 3 offensive",
    "pass/shot diagram, lower field (lower middle)",
    "pass/shot diagram, lower field (upper middle)",
    "pass/shot diagram, lower field (left)",
    "flat or high balls (stick figures)",
    "top right diagram, left (wingers hang back / build-up from behind)",
    "top right diagram, right (wingers hang back / build-up from behind)",
    "emphasis (EINSATZ!!!) 0..11",
};

[[noreturn]] inline void fail(std::string code, std::string message, std::string text_key = {}) {
    throw CliError{std::move(code), std::move(message), std::move(text_key)};
}

// Parameter constructors for the registry.
Param integer(std::string name, long long min, long long max, bool optional = false);
Param boolean(std::string name, bool optional = false);
Param text(std::string name, long long max_length = 0, bool optional = false);
// enum: the names (case/'.'/'_' ignored) or their index
Param choice(std::string name, std::vector<std::string> choices, bool optional = false);

std::string on_off(bool v);
std::string lower(std::string s);
// For name comparisons: lower case, also the German capitals Ä Ö Ü in UTF-8.
std::string fold(std::string s);
// Text from the engine as UTF-8. Most projections keep the original ISO 646-DE bytes
// ("M]NCHEN"), some already convert (cup draw names, results). The original encoding has no
// bytes above 0x7f, so a string with such bytes is taken as UTF-8 already.
std::string utf8(std::string_view text);
// "2027-07-01" (the concept's date type)
std::string date_text(int day, int month, int year);
// Money as whole euros without separators (01-grundlagen 2.1)
std::string money(long long value);

// A player of a list a command can refer to (01-grundlagen 1.3).
struct PlayerRef {
    int row = -1;        // row in the window's list (squad slot)
    int id = -1;         // player id of the game state (stable)
    std::string name;    // UTF-8
};
// Resolves id:<n>, row:<n> or a (unique part of a) name against `players`; returns the
// index into `players`. E_BAD_ARGUMENT if nothing fits, E_AMBIGUOUS with candidates.
size_t resolve_player(const std::string& reference, const std::vector<PlayerRef>& players);

// The window a desk menu (menu_kind 0-29) opens, named as in 02-fenster 6; 63 = Mailbox.
// Empty for other numbers.
std::string menu_window(int menu);
// The menu number of a window name (canonical comparison), -1 if none.
int menu_of_window(std::string_view name);

}  // namespace bmh::cli::console::detail

namespace bmh::cli::console {

// The arguments of one call, bound to the parameter names and already type-checked.
struct Console::Values {
    std::map<std::string, Arg> args;  // canonical parameter name -> argument
    const Spec* spec = nullptr;
    int player = 0;  // the @n of the call (0 = none)

    bool has(const std::string& name) const { return args.count(canonical(name)) != 0; }
    long long integer(const std::string& name, long long fallback = 0) const;
    bool boolean(const std::string& name, bool fallback = false) const;
    std::string text(const std::string& name, std::string fallback = {}) const;
    // the index of an enum argument in its parameter's choices (-1 = absent)
    int choice(const std::string& name) const;
};

}  // namespace bmh::cli::console
