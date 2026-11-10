// SPDX-License-Identifier: GPL-3.0-only
// Text encoding helpers: see text_encoding.hpp.
#include "formats/text_encoding.hpp"

#include <array>
#include <iomanip>
#include <sstream>

#include "formats/binary.hpp"

namespace bmh {
namespace {

// The seven German letters and the ISO 646-DE bytes that stand for them, in the same order.
constexpr std::array<std::string_view, 7> german = {"Ä", "Ö", "Ü", "ä", "ö", "ü", "ß"};
constexpr std::string_view original_german = "[\\]{|}~";

}  // namespace

std::string json_quote(std::string_view value) {
    std::ostringstream s; s << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') s << '\\' << c;
        else if (c < 32) s << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c) << std::dec;
        else s << c;
    }
    s << '"'; return s.str();
}

std::string original_to_utf8(std::string_view value) {
    std::string out;
    for (char c : value) {
        auto i = original_german.find(c);
        if (i != std::string_view::npos) out += german[i];
        else if (static_cast<unsigned char>(c) < 128) out += c;
        else out += "�"; // Unsupported original byte remains visibly unknown.
    }
    return out;
}

std::string utf8_to_original(std::string_view value) {
    std::string out;
    for (size_t i = 0; i < value.size();) {
        bool found = false;
        for (size_t j = 0; j < german.size(); ++j)
            if (value.substr(i, german[j].size()) == german[j]) {
                out += original_german[j]; i += german[j].size(); found = true; break;
            }
        if (found) continue;
        const unsigned char c = value[i++];
        if (c >= 128) throw FormatError("manager name: unsupported UTF-8 character");
        out += char(c);
    }
    return out;
}

}  // namespace bmh
