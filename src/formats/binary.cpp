// SPDX-License-Identifier: GPL-3.0-only
#include "formats/binary.hpp"
#include "formats/resource_bundle.hpp"

#include <fstream>
#include <iterator>

namespace bmh {

Bytes read_file(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path))
        if (auto bytes=bundled_resource(path)) return std::move(*bytes);
    std::ifstream in(path, std::ios::binary);
    if (!in) throw FormatError("cannot open " + path.string());
    return Bytes(std::istreambuf_iterator<char>(in), {});
}

std::string iso646de_to_utf8(const char* s, size_t max_len) {
    std::string out;
    for (size_t i = 0; i < max_len && s[i] != '\0'; i++) {
        switch (s[i]) {
            case '[': out += "Ä"; break;
            case '\\': out += "Ö"; break;
            case ']': out += "Ü"; break;
            case '{': out += "ä"; break;
            case '|': out += "ö"; break;
            case '}': out += "ü"; break;
            case '~': out += "ß"; break;
            default: out += s[i];
        }
    }
    return out;
}

}  // namespace bmh
