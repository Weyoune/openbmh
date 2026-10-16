// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The command line syntax of the console language (docs/CLI_CONCEPT/01-grundlagen.md, 1):
//
//   [@<player>] Name [ ( Argument {, Argument} ) ]
//
// Names ignore case and the characters '.' and '_' ("ListOptions" = "List.Options" =
// "list_options"). Arguments are positional or named (name=value; named ones only at the
// end), separated by commas at the top level; strings are "quoted" (\" and \\ escaped) or a
// single word, lists use [a, b]. The parser only splits a line; the types are checked against
// the command's parameter list by the console (E_BAD_ARGUMENT before anything happens).

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace bmh::cli::console {

// One argument as written: `text` without the surrounding quotes, `quoted` if it was a
// "string", `name` empty for a positional argument.
struct Arg {
    std::string name;
    std::string text;
    bool quoted = false;
};

struct Command {
    int player = 0;       // the @n prefix (1-4), 0 = the active player
    std::string name;     // as written
    std::string key;      // canonical(name)
    std::vector<Arg> args;
};

class ParseError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Lower case without '.' and '_': the key commands are compared with.
std::string canonical(std::string_view name);

// Splits one command line. Throws ParseError with a short reason on a syntax error.
Command parse_command(std::string_view line);

// What a line of input is, before parsing: a command, the block confirmation "go.", the
// block reset "clear.", or nothing (empty line or // comment).
enum class LineKind { command, go, clear, skip };
LineKind classify_line(std::string_view line);

// Value conversion for the parameter check. Integers allow '_' as thousands separator and a
// sign; booleans accept true/false/yes/no/on/off/1/0. Quoted text is never a number/bool.
std::optional<long long> to_int(const Arg& arg);
std::optional<bool> to_bool(const Arg& arg);

}  // namespace bmh::cli::console
