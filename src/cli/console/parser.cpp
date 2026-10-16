// SPDX-License-Identifier: GPL-3.0-only
#include "cli/console/parser.hpp"

#include <cctype>
#include <charconv>

namespace bmh::cli::console {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

bool name_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_'; }

// Splits the text between the parentheses at top-level commas: commas inside "quotes" or
// [brackets] do not split.
std::vector<std::string> split_args(std::string_view inner) {
    std::vector<std::string> parts;
    std::string current;
    bool quoted = false;
    int depth = 0;
    for (size_t i = 0; i < inner.size(); ++i) {
        const char c = inner[i];
        if (quoted) {
            current += c;
            if (c == '\\' && i + 1 < inner.size()) current += inner[++i];
            else if (c == '"') quoted = false;
            continue;
        }
        if (c == '"') quoted = true;
        else if (c == '[') ++depth;
        else if (c == ']') {
            if (--depth < 0) throw ParseError("unbalanced ']'");
        } else if (c == ',' && depth == 0) {
            parts.push_back(current);
            current.clear();
            continue;
        }
        current += c;
    }
    if (quoted) throw ParseError("unterminated string");
    if (depth != 0) throw ParseError("unbalanced '['");
    parts.push_back(current);
    return parts;
}

Arg parse_arg(std::string_view text) {
    Arg arg;
    text = trim(text);
    if (text.empty()) throw ParseError("empty argument");
    // name=value: a plain name before '=' (no quotes or brackets in front of it)
    if (text.front() != '"' && text.front() != '[') {
        const size_t eq = text.find('=');
        if (eq != std::string_view::npos) {
            const std::string_view name = trim(text.substr(0, eq));
            bool plain = !name.empty();
            for (char c : name) plain = plain && name_char(c);
            if (plain) {
                arg.name = canonical(name);
                text = trim(text.substr(eq + 1));
                if (text.empty()) throw ParseError("missing value for " + std::string(name));
            }
        }
    }
    if (text.front() == '"') {
        if (text.size() < 2 || text.back() != '"') throw ParseError("unterminated string");
        arg.quoted = true;
        const std::string_view body = text.substr(1, text.size() - 2);
        for (size_t i = 0; i < body.size(); ++i) {
            if (body[i] == '\\' && i + 1 < body.size()) {
                arg.text += body[++i];
            } else if (body[i] == '"') {
                throw ParseError("unescaped quote in string");
            } else {
                arg.text += body[i];
            }
        }
    } else {
        arg.text = std::string(text);
    }
    return arg;
}

}  // namespace

std::string canonical(std::string_view name) {
    std::string key;
    for (char c : name)
        if (c != '.' && c != '_') key += char(std::tolower(static_cast<unsigned char>(c)));
    return key;
}

LineKind classify_line(std::string_view line) {
    line = trim(line);
    if (line.empty() || line.substr(0, 2) == "//") return LineKind::skip;
    const std::string key = [&] {
        std::string k;
        for (char c : line) k += char(std::tolower(static_cast<unsigned char>(c)));
        return k;
    }();
    if (key == "go.") return LineKind::go;
    if (key == "clear.") return LineKind::clear;
    return LineKind::command;
}

Command parse_command(std::string_view line) {
    Command cmd;
    line = trim(line);
    if (!line.empty() && line.front() == '@') {
        size_t i = 1;
        while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) ++i;
        if (i == 1) throw ParseError("'@' needs a player number");
        int player = 0;
        std::from_chars(line.data() + 1, line.data() + i, player);
        if (player < 1 || player > 4) throw ParseError("player must be 1-4");
        cmd.player = player;
        line = trim(line.substr(i));
    }
    size_t n = 0;
    while (n < line.size() && name_char(line[n])) ++n;
    if (n == 0) throw ParseError("expected a command name");
    cmd.name = std::string(line.substr(0, n));
    cmd.key = canonical(cmd.name);
    if (cmd.key.empty()) throw ParseError("expected a command name");
    std::string_view rest = trim(line.substr(n));
    if (rest.empty()) return cmd;
    if (rest.front() != '(' || rest.back() != ')') throw ParseError("arguments must be written as Name(a, b)");
    const std::string_view inner = trim(rest.substr(1, rest.size() - 2));
    if (inner.empty()) return cmd;
    bool named_seen = false;
    for (const std::string& part : split_args(inner)) {
        Arg arg = parse_arg(part);
        if (arg.name.empty() && named_seen) throw ParseError("positional argument after a named one");
        named_seen = named_seen || !arg.name.empty();
        cmd.args.push_back(std::move(arg));
    }
    return cmd;
}

std::optional<long long> to_int(const Arg& arg) {
    if (arg.quoted) return std::nullopt;
    std::string digits;
    for (char c : arg.text)
        if (c != '_') digits += c;
    if (digits.empty()) return std::nullopt;
    long long value = 0;
    const char* begin = digits.data();
    if (*begin == '+') ++begin;
    const auto [ptr, ec] = std::from_chars(begin, digits.data() + digits.size(), value);
    if (ec != std::errc{} || ptr != digits.data() + digits.size()) return std::nullopt;
    return value;
}

std::optional<bool> to_bool(const Arg& arg) {
    if (arg.quoted) return std::nullopt;
    const std::string v = canonical(arg.text);
    if (v == "true" || v == "yes" || v == "on" || v == "1") return true;
    if (v == "false" || v == "no" || v == "off" || v == "0") return false;
    return std::nullopt;
}

}  // namespace bmh::cli::console
