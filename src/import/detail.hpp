// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "import/obw.hpp"
#include <iomanip>
#include <sstream>
#include <string_view>
#include "miniz.h"

namespace bmh::obw::detail {
using J = cli::Json;
using A = J::Array;
using O = J::Object;
[[noreturn]] inline void fail(const std::string& message) { throw FormatError("OBW: " + message); }
inline const J& get(const J& value, std::string_view key) {
    if (!value.object()) fail("expected object for field: " + std::string(key));
    const auto found = value.object()->find(key);
    if (found == value.object()->end()) fail("missing field: " + std::string(key));
    return found->second;
}
inline std::string str(const J& value) {
    if (!value.string()) fail("expected string");
    return *value.string();
}
inline int64_t num(const J& value) {
    if (!value.integer()) fail("expected integer");
    return *value.integer();
}
inline const A& arr(const J& value) {
    if (!value.array()) fail("expected array");
    return *value.array();
}
inline J parse(const Documents& out, const std::string& path) {
    if (!out.contains(path)) fail("missing entry: " + path);
    return cli::parse_json(out.at(path));
}
inline void put(Documents& out, const std::string& path, J value) {
    out[path] = cli::dump_json(value) + '\n';
}
inline A items(const Documents& out, const std::string& table) {
    return arr(get(parse(out, "world/" + table + ".json"), "items"));
}
inline void table(Documents& out, const std::string& name, A values) {
    put(out, "world/" + name + ".json", O{{"schema", name + "@1.0"}, {"items", std::move(values)}});
}
inline std::string id(const std::string& prefix, size_t index) {
    std::ostringstream out;
    out << prefix << ':' << std::setfill('0') << std::setw(5) << index;
    return out.str();
}
inline std::string binary(const Bytes& bytes) { return std::string(bytes.begin(), bytes.end()); }
inline Bytes bytes(const std::string& value) { return Bytes(value.begin(), value.end()); }
inline std::string legacy_text(const std::string& raw) {
    std::string result;
    for (const unsigned char byte : raw) {
        if (byte >= 128) { result += char(0xc0 | (byte >> 6)); result += char(0x80 | (byte & 63)); }
        else { const char one[] = {char(byte), 0}; result += iso646de_to_utf8(one, 1); }
    }
    return result;
}
inline Bytes unhex(const std::string& hex) {
    if (hex.size() % 2) fail("odd hex length");
    const auto digit = [](char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        fail("invalid hex digit");
    };
    Bytes result;
    result.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) result.push_back(uint8_t(digit(hex[i]) * 16 + digit(hex[i + 1])));
    return result;
}
inline uint32_t crc(const std::string& data) {
    return uint32_t(mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>(data.data()), data.size()));
}
inline bool safe_path(const std::string& path) {
    if (path.empty() || path.front() == '/' || path.back() == '/' || path.find('\\') != std::string::npos ||
        path.find(':') != std::string::npos || path.find('\0') != std::string::npos) return false;
    std::istringstream parts(path);
    std::string part;
    while (std::getline(parts, part, '/')) if (part.empty() || part == "." || part == "..") return false;
    return true;
}
} // namespace bmh::obw::detail
