// SPDX-License-Identifier: GPL-3.0-only
// Implementation of diamond_image.hpp: the images built from the embedded numbers of the generated .inc files plus the
// texts of openbmh.dat.
#include "formats/diamond_image.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace bmh {

namespace {

struct Chunk { uint32_t address; uint32_t size; const uint8_t* bytes; };
struct Range { uint32_t address; uint32_t length; };

namespace german {
#include "formats/diamond_constants.inc"
}  // namespace german
namespace english {
#include "formats/fltd_constants.inc"
}  // namespace english

constexpr char kMagic[12] = "OPENBMH-DAT";  // 11 characters and the NUL
constexpr char kFileName[] = "openbmh.dat";

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

uint32_t word(const Bytes& b, size_t at) {
    if (at + 4 > b.size()) throw FormatError("openbmh.dat: truncated");
    return uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 | uint32_t(b[at + 2]) << 16 | uint32_t(b[at + 3]) << 24;
}

LeImage build(uint32_t base, uint32_t size, const Chunk* first, const Chunk* last) {
    Bytes bytes(size, 0);
    for (const Chunk* chunk = first; chunk != last; ++chunk)
        std::copy(chunk->bytes, chunk->bytes + chunk->size, bytes.begin() + (chunk->address - base));
    return LeImage::from_bytes(base, std::move(bytes));
}

std::filesystem::path executable_directory() {
    std::error_code ec;
#ifdef _WIN32
    wchar_t buffer[32768];
    const DWORD n = GetModuleFileNameW(nullptr, buffer, DWORD(std::size(buffer)));
    if (n == 0 || n >= std::size(buffer)) return {};
    return std::filesystem::path(buffer).parent_path();
#elif defined(__APPLE__)
    char buffer[4096];
    uint32_t size = sizeof buffer;
    if (_NSGetExecutablePath(buffer, &size) != 0) return {};
    return std::filesystem::weakly_canonical(buffer, ec).parent_path();
#else
    char buffer[4096];
    const ssize_t n = readlink("/proc/self/exe", buffer, sizeof buffer - 1);
    if (n <= 0) return {};
    buffer[n] = 0;
    return std::filesystem::path(buffer).parent_path();
#endif
}

std::filesystem::path file_in(const std::filesystem::path& directory) {
    if (directory.empty()) return {};
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec))
        if (entry.is_regular_file(ec) && lower(entry.path().filename().string()) == kFileName) return entry.path();
    return {};
}

void add_texts(LeImage& image, int edition, const std::filesystem::path& game_directory) {
    const auto file = find_texts_file(game_directory);
    if (file.empty()) return;
    try {
        apply_texts(image, edition, read_file(file));
    } catch (const std::exception&) {
        // a damaged texts file leaves the texts empty; the game itself still runs
    }
}

}  // namespace

std::filesystem::path find_texts_file(const std::filesystem::path& game_directory) {
    if (const char* env = std::getenv("OPENBMH_DAT"); env && *env) {
        if (std::string_view(env) == "-") return {};  // "no texts file at all" (tests of the launcher)
        std::error_code ec;
        if (std::filesystem::is_regular_file(env, ec)) return env;
    }
    for (const auto& directory : {game_directory, executable_directory(), std::filesystem::path(".")})
        if (auto file = file_in(directory); !file.empty()) return file;
    return {};
}

std::string texts_signature(const std::filesystem::path& game_directory) {
    const auto file = find_texts_file(game_directory);
    if (file.empty()) return {};
    try {
        const Bytes data = read_file(file);
        uint64_t hash = 1469598103934665603ull;  // FNV-1a
        for (uint8_t b : data) hash = (hash ^ b) * 1099511628211ull;
        return std::to_string(data.size()) + "-" + std::to_string(hash);
    } catch (const std::exception&) {
        return {};
    }
}

bool apply_texts(LeImage& image, int edition, const Bytes& file) {
    if (file.size() < sizeof kMagic + 4 || !std::equal(kMagic, kMagic + sizeof kMagic, file.begin()))
        throw FormatError("openbmh.dat: not an OpenBMH data file");
    const Range* known = edition == 1 ? std::begin(german::kTextRanges) : std::begin(english::kTextRanges);
    const Range* known_end = edition == 1 ? std::end(german::kTextRanges) : std::end(english::kTextRanges);
    const uint32_t sets = word(file, sizeof kMagic);
    if (sets > 16) throw FormatError("openbmh.dat: too many sets");
    size_t at = sizeof kMagic + 4;
    bool found = false;
    for (uint32_t s = 0; s < sets; ++s) {
        const uint32_t id = word(file, at), count = word(file, at + 4);
        if (count > 65536) throw FormatError("openbmh.dat: too many ranges");
        const size_t table = at + 8;
        size_t data = table + size_t(count) * 8;
        for (uint32_t n = 0; n < count; ++n) {
            const uint32_t address = word(file, table + n * 8ull), length = word(file, table + n * 8ull + 4);
            if (data + length > file.size()) throw FormatError("openbmh.dat: truncated");
            if (int(id) == edition) {
                const bool allowed = std::any_of(known, known_end,
                    [&](const Range& r) { return r.address == address && length <= r.length; });
                if (!allowed) throw FormatError("openbmh.dat: a range is not a text of the game");
                image.patch(address, file.data() + data, length);
                found = true;
            }
            data += length;
        }
        at = data;
    }
    return found;
}

LeImage diamond_constants_image(const std::filesystem::path& game_directory) {
    LeImage image = build(german::kImageBase, german::kImageSize, std::begin(german::kChunks), std::end(german::kChunks));
    add_texts(image, 1, game_directory);
    return image;
}

LeImage english_constants_image(const std::filesystem::path& game_directory) {
    LeImage image = build(english::kImageBase, english::kImageSize, std::begin(english::kChunks), std::end(english::kChunks));
    add_texts(image, 2, game_directory);
    return image;
}

bool is_executable_name(const std::filesystem::path& file_name) { return lower(file_name.string()) == "bmh.exe"; }

}  // namespace bmh
