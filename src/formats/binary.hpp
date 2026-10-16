// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Little helpers for reading the original little-endian data files.
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace bmh {

using Bytes = std::vector<uint8_t>;

struct FormatError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

Bytes read_file(const std::filesystem::path& path);

inline uint16_t le16(const Bytes& b, size_t o) { return uint16_t(b.at(o) | b.at(o + 1) << 8); }
inline uint32_t le32(const Bytes& b, size_t o) { return uint32_t(le16(b, o)) | uint32_t(le16(b, o + 2)) << 16; }

// Strings in the game use ISO 646-DE: [ \ ] { | } ~ are Ä Ö Ü ä ö ü ß
std::string iso646de_to_utf8(const char* s, size_t max_len = std::string::npos);

}  // namespace bmh
