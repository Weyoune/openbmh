// SPDX-License-Identifier: GPL-3.0-only
#include "formats/fbm.hpp"

#include <algorithm>
#include <cstring>

namespace bmh {

void fbm_decrypt(uint8_t* data, size_t n, uint8_t key) {
    for (size_t i = 0; i < n; i++) {
        data[i] = uint8_t((data[i] - key) ^ key);
        key = uint8_t(key - 0x29);
    }
}

void fbm_encrypt(uint8_t* data, size_t n, uint8_t key) {
    for (size_t i = 0; i < n; i++) {
        data[i] = uint8_t((data[i] ^ key) + key);
        key = uint8_t(key - 0x29);
    }
}

Savegame load_savegame(const std::filesystem::path& path) {
    Bytes d = read_file(path);
    static const char magic[] = "KRON_FBM";
    auto it = std::search(d.begin(), d.end(), magic, magic + 8);
    if (it == d.end()) throw FormatError("FBM: magic KRON_FBM not found");
    size_t m = size_t(it - d.begin());
    if (m + 26 > d.size()) throw FormatError("FBM: truncated header");

    Savegame s;
    s.plain_header.assign(d.begin(), it);
    size_t p = m + 10;  // "KRON_FBM\0" + 1 byte
    s.id1 = le32(d, p);
    p += 4;
    s.version.assign(reinterpret_cast<const char*>(&d[p]), strnlen(reinterpret_cast<const char*>(&d[p]), 6));
    p += 6;
    s.id2 = le32(d, p);
    p += 4;
    s.random = d[p++];
    s.key = d[p++];

    s.stream.assign(d.begin() + p, d.end() - 1);
    s.checksum = d.back();
    fbm_decrypt(s.stream.data(), s.stream.size(), s.key);

    uint8_t sum = s.key;
    for (uint8_t b : s.stream) sum = uint8_t(sum + b);
    s.checksum_ok = sum == s.checksum;
    return s;
}

}  // namespace bmh
