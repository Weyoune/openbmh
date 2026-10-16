// SPDX-License-Identifier: GPL-3.0-only
#include "cli/json.hpp"

#include <charconv>
#include <limits>

namespace bmh::cli {
namespace {

void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp <= 0x7f) out += char(cp);
    else if (cp <= 0x7ff) {
        out += char(0xc0 | (cp >> 6)); out += char(0x80 | (cp & 0x3f));
    } else if (cp <= 0xffff) {
        out += char(0xe0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3f)); out += char(0x80 | (cp & 0x3f));
    } else {
        out += char(0xf0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3f));
        out += char(0x80 | ((cp >> 6) & 0x3f)); out += char(0x80 | (cp & 0x3f));
    }
}

class Parser {
public:
    explicit Parser(std::string_view source) : source_(source) {}
    Json parse() {
        skip_space();
        Json result = parse_value();
        skip_space();
        if (at_ != source_.size()) fail("trailing data");
        return result;
    }
private:
    std::string_view source_;
    std::size_t at_ = 0;
    [[noreturn]] void fail(std::string_view message) const {
        throw JsonError(std::string(message) + " at byte " + std::to_string(at_));
    }
    void skip_space() {
        while (at_ < source_.size() && (source_[at_] == ' ' || source_[at_] == '\t' || source_[at_] == '\r' || source_[at_] == '\n')) ++at_;
    }
    bool take(char c) {
        if (at_ < source_.size() && source_[at_] == c) { ++at_; return true; }
        return false;
    }
    Json parse_value() {
        if (at_ >= source_.size()) fail("expected value");
        switch (source_[at_]) {
        case '{': return parse_object();
        case '[': return parse_array();
        case '"': return Json(parse_string());
        case 't': literal("true"); return Json(true);
        case 'f': literal("false"); return Json(false);
        case 'n': literal("null"); return Json(nullptr);
        default: return Json(parse_integer());
        }
    }
    void literal(std::string_view word) {
        if (source_.substr(at_, word.size()) != word) fail("invalid literal");
        at_ += word.size();
    }
    int hex_digit(char c) const {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }
    std::uint32_t hex4() {
        if (source_.size() - at_ < 4) fail("short unicode escape");
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const int digit = hex_digit(source_[at_++]);
            if (digit < 0) fail("invalid unicode escape");
            value = (value << 4) | std::uint32_t(digit);
        }
        return value;
    }
    std::string parse_string() {
        if (!take('"')) fail("expected string");
        std::string out;
        while (at_ < source_.size()) {
            const unsigned char c = static_cast<unsigned char>(source_[at_++]);
            if (c == '"') return out;
            if (c < 0x20) fail("control character in string");
            if (c != '\\') { out += char(c); continue; }
            if (at_ == source_.size()) fail("short escape");
            const char escaped = source_[at_++];
            switch (escaped) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                std::uint32_t cp = hex4();
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    if (source_.size() - at_ < 6 || source_[at_] != '\\' || source_[at_ + 1] != 'u') fail("missing low surrogate");
                    at_ += 2;
                    const std::uint32_t low = hex4();
                    if (low < 0xdc00 || low > 0xdfff) fail("invalid low surrogate");
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                } else if (cp >= 0xdc00 && cp <= 0xdfff) fail("unexpected low surrogate");
                append_utf8(out, cp);
                break;
            }
            default: fail("invalid string escape");
            }
        }
        fail("unterminated string");
    }
    std::int64_t parse_integer() {
        const std::size_t start = at_;
        take('-');
        if (at_ == source_.size()) fail("invalid integer");
        if (source_[at_] == '0') ++at_;
        else {
            if (source_[at_] < '1' || source_[at_] > '9') fail("expected integer");
            while (at_ < source_.size() && source_[at_] >= '0' && source_[at_] <= '9') ++at_;
        }
        if (at_ < source_.size() && (source_[at_] == '.' || source_[at_] == 'e' || source_[at_] == 'E')) fail("only integer numbers are supported");
        std::int64_t value{};
        const auto result = std::from_chars(source_.data() + start, source_.data() + at_, value);
        if (result.ec != std::errc{} || result.ptr != source_.data() + at_) fail("integer out of range");
        return value;
    }
    Json parse_array() {
        take('['); skip_space(); Json::Array values;
        if (take(']')) return values;
        for (;;) {
            skip_space(); values.push_back(parse_value()); skip_space();
            if (take(']')) return values;
            if (!take(',')) fail("expected comma in array");
        }
    }
    Json parse_object() {
        take('{'); skip_space(); Json::Object values;
        if (take('}')) return values;
        for (;;) {
            skip_space();
            if (at_ >= source_.size() || source_[at_] != '"') fail("expected object key");
            std::string key = parse_string(); skip_space();
            if (!take(':')) fail("expected colon");
            skip_space();
            if (!values.emplace(std::move(key), parse_value()).second) fail("duplicate object key");
            skip_space();
            if (take('}')) return values;
            if (!take(',')) fail("expected comma in object");
        }
    }
};

void quote(std::string& out, std::string_view value) {
    static constexpr char hex[] = "0123456789abcdef";
    out += '"';
    for (unsigned char c : value) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
            else out += char(c);
        }
    }
    out += '"';
}

void dump(std::string& out, const Json& json) {
    if (std::holds_alternative<std::nullptr_t>(json.value)) out += "null";
    else if (const auto* b = json.boolean()) out += *b ? "true" : "false";
    else if (const auto* n = json.integer()) out += std::to_string(*n);
    else if (const auto* s = json.string()) quote(out, *s);
    else if (const auto* a = json.array()) {
        out += '[';
        bool first = true;
        for (const auto& item : *a) { if (!first) out += ','; first = false; dump(out, item); }
        out += ']';
    } else {
        out += '{'; bool first = true;
        for (const auto& [key, item] : *json.object()) {
            if (!first) out += ','; first = false; quote(out, key); out += ':'; dump(out, item);
        }
        out += '}';
    }
}

} // namespace

Json parse_json(std::string_view source) { return Parser(source).parse(); }
std::string dump_json(const Json& value) { std::string out; dump(out, value); return out; }

} // namespace bmh::cli
