// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <filesystem>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace bmh::editor {
using Json = nlohmann::ordered_json;
namespace fs = std::filesystem;
struct Error : std::runtime_error {
    std::string code;
    Json details;
    explicit Error(std::string key, Json info = Json::object())
        : std::runtime_error(key), code(std::move(key)), details(std::move(info)) {}
};
inline std::string text(const Json &value) {
    if (value.is_string())
        return value.get<std::string>();
    if (value.is_null())
        return {};
    if (value.is_object()) {
        for (auto key : {"default", "known_as", "full"})
            if (value.contains(key) && value[key].is_string() && !value[key].get<std::string>().empty())
                return value[key].get<std::string>();
        if (value.contains("first") || value.contains("last")) {
            auto first = value.value("first", Json());
            auto last = value.value("last", Json());
            return text(first) + (text(first).empty() || text(last).empty() ? "" : " ") + text(last);
        }
    }
    return value.dump();
}
inline std::string str(const Json &object, std::string_view key, const std::string &fallback = {}) {
    auto it = object.find(std::string(key));
    return it == object.end() || !it->is_string() ? fallback : it->get<std::string>();
}
inline std::string escaped(std::string_view value) {
    std::string out;
    for (auto c : value)
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        case '\'':
            out += "&#39;";
            break;
        default:
            out += c;
        }
    return out;
}
std::string utf8(const fs::path &);
fs::path path(std::string_view);
std::string read_file(const fs::path &, uint64_t maximum = 64ull * 1024 * 1024);
void write_atomic(const fs::path &, std::string_view, bool overwrite = true);
std::string new_id(const std::string &prefix);
std::string folded(std::string value);
bool safe_name(std::string_view);
} // namespace bmh::editor
