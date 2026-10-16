// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// TEXTD.BLK: UI strings (docs/formats/TEXTD.md), loaded in CareerFirstSteps2
#include <filesystem>
#include <string>
#include <vector>

namespace bmh {

constexpr size_t TEXT_COUNT = 0x566;  // pointer table TextPointerTable[1382]

struct TextDb {
    std::vector<std::string> strings;  // raw ISO 646-DE, index = text id
    // The texts as the original keeps them in memory: one block, NUL separated. Some
    // routines copy a fixed number of bytes from a text and so also take the following
    // ones (see raw()).
    std::vector<uint8_t> blob;
    std::vector<size_t> offsets;
    std::vector<uint8_t> raw(size_t id, size_t n) const {
        std::vector<uint8_t> r(n, 0);
        for (size_t i = 0; i < n && offsets.at(id) + i < blob.size(); i++) r[i] = blob[offsets.at(id) + i];
        return r;
    }
    const std::string& operator[](size_t id) const { return strings.at(id); }
};

TextDb load_textd(const std::filesystem::path& path);

}  // namespace bmh
