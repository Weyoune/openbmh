// SPDX-License-Identifier: GPL-3.0-only
// blockdump - prints one state block of a savegame (.FBM) as hex, optionally only a range.
//
// Usage: blockdump <savegame.FBM> <hex address of the global> [<offset> <length>] [u16|u32]
//   The address is the original global the block was saved from (see savestate.cpp), e.g.
//   CompetitionCalendar for the calendar. With u16/u32 the values are printed as little-endian numbers.
#include <cstdio>
#include <string>

#include "formats/fbm.hpp"
#include "formats/savestate.hpp"

using namespace bmh;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: blockdump <savegame.FBM> <hex address> [offset length] [u16|u32]\n");
        return 1;
    }
    Savegame sg = load_savegame(argv[1]);
    SaveState st(sg.stream);
    uint32_t address = uint32_t(std::stoul(argv[2], nullptr, 16));
    const SaveBlock& b = st.block(address);
    size_t from = argc > 4 ? std::stoul(argv[3], nullptr, 0) : 0;
    size_t len = argc > 4 ? std::stoul(argv[4], nullptr, 0) : b.size;
    std::string unit = argc > 5 ? argv[5] : (argc == 4 ? argv[3] : "u8");
    if (from > b.size) from = b.size;
    if (from + len > b.size) len = b.size - from;
    const uint8_t* p = st.data(address) + from;
    std::printf("block %05x: %zu bytes (heap %d), showing %zu from offset %zu\n", address, b.size, b.heap, len, from);
    size_t step = unit == "u32" ? 4 : unit == "u16" ? 2 : 1;
    for (size_t i = 0; i + step <= len; i += step) {
        if (i % (16 * step / step * (step == 1 ? 1 : 1)) == 0 && i % 16 == 0) std::printf("\n%6zu:", from + i);
        uint32_t v = 0;
        for (size_t k = 0; k < step; k++) v |= uint32_t(p[i + k]) << (8 * k);
        std::printf(step == 1 ? " %02x" : step == 2 ? " %04x" : " %08x", v);
    }
    std::printf("\n");
    return 0;
}
