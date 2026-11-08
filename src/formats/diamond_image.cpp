// SPDX-License-Identifier: GPL-3.0-only
// Implementation of diamond_image.hpp: the images built from the embedded data of the generated .inc files.
#include "formats/diamond_image.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace bmh {

namespace {

struct Chunk { uint32_t address; uint32_t size; const uint8_t* bytes; };

namespace german {
#include "formats/diamond_constants.inc"
}  // namespace german
namespace english {
#include "formats/fltd_constants.inc"
}  // namespace english

LeImage build(uint32_t base, uint32_t size, const Chunk* first, const Chunk* last) {
    Bytes bytes(size, 0);
    for (const Chunk* chunk = first; chunk != last; ++chunk)
        std::copy(chunk->bytes, chunk->bytes + chunk->size, bytes.begin() + (chunk->address - base));
    return LeImage::from_bytes(base, std::move(bytes));
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

LeImage diamond_constants_image() {
    return build(german::kImageBase, german::kImageSize, std::begin(german::kChunks), std::end(german::kChunks));
}

LeImage english_constants_image() {
    return build(english::kImageBase, english::kImageSize, std::begin(english::kChunks), std::end(english::kChunks));
}

bool is_executable_name(const std::filesystem::path& file_name) { return lower(file_name.string()) == "bmh.exe"; }

}  // namespace bmh
