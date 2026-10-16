// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
// textfind <TEXTD.BLK> <substring>: lists the texts containing the substring with their id
// and the address of the pointer variable the original uses for them (format field + 4 * id).
#include <cstdio>
#include <string>

#include "formats/textd.hpp"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: textfind <TEXTD.BLK> <substring>\n");
        return 1;
    }
    const bmh::TextDb db = bmh::load_textd(argv[1]);
    for (size_t i = 0; i < db.strings.size(); i++)
        if (db.strings[i].find(argv[2]) != std::string::npos)
            std::printf("%4zu  offset %08zx  %s\n", i, size_t(::bmh::layout::TextPointerTable) + i * 4, db.strings[i].c_str());
    return 0;
}
