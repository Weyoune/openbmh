// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// .FBM savegame container (docs/formats/FBM.md): plain header, magic, encrypted stream, checksum.
// Writer FbmDecrypt2 + NationalCoachAfterLoad, stream cipher in FbmDecrypt.
#include <cstdint>
#include <filesystem>
#include <string>

#include "formats/binary.hpp"

namespace bmh {

struct Savegame {
    Bytes plain_header;      // FbmDecrypt2 output (date, names for the load list)
    uint32_t id1 = 0, id2 = 0;
    std::string version;     // "V2.24"
    uint8_t random = 0, key = 0;
    Bytes stream;            // decrypted game state + trailer blocks
    uint8_t checksum = 0;
    bool checksum_ok = false;
};

// Stream cipher: encrypt c = (b ^ k) + k, decrypt b = (c - k) ^ k, then k -= 0x29
void fbm_decrypt(uint8_t* data, size_t n, uint8_t key);
void fbm_encrypt(uint8_t* data, size_t n, uint8_t key);

Savegame load_savegame(const std::filesystem::path& path);

}  // namespace bmh
