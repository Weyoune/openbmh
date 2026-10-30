// SPDX-License-Identifier: GPL-3.0-only
#include "formats/textd.hpp"

#include "formats/binary.hpp"

namespace bmh {

TextDb load_textd(const std::filesystem::path& path) {
    Bytes d = read_file(path);
    if (d.size() < 7) throw FormatError("TEXTD: file too short");
    // header: u8 id, u16 buffer size, u32 unknown; the buffer may be larger than the file rest
    TextDb db;
    size_t p = 7;
    db.blob.assign(d.begin() + 7, d.end());
    while (db.strings.size() < TEXT_COUNT && p < d.size()) {
        size_t start = p;
        db.offsets.push_back(start - 7);
        while (p < d.size() && d[p] != 0) p++;
        db.strings.emplace_back(reinterpret_cast<const char*>(d.data() + start), p - start);
        p++;
    }
    if (db.strings.size() != TEXT_COUNT) throw FormatError("TEXTD: expected 1382 strings");
    return db;
}

}  // namespace bmh
