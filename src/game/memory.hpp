// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The static data of the original (its data object, LE addresses GermanDataSegmentBegin-layout field) as far
// as the savegame stores it.
//
// Many parts of the original work on small global tables that point at each other, e.g.
// the cup table at CupTable whose entries hold pointers to team lists and result arrays in
// other globals. Porting every one of them into a typed field would lose that structure,
// so they are kept here as raw memory addressed like in the original. The pointers stored
// in such tables are guest linear addresses: data address + kGuestDataBase.
//
// Blocks that GameState also decodes into typed fields (squads, players, results, ...)
// must be read and written through those fields; their copies here are not kept current.

#include <cstdint>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "formats/savestate.hpp"

namespace bmh {

// Offset between the LE addresses of the data object and the guest linear addresses the
// original stores in pointers (DOS/4GW loads the data object at relocated data-segment base).
constexpr uint32_t kGuestDataBase = 0x1a2000;

class StaticMemory {
public:
    StaticMemory() = default;
    // All non-heap blocks of a savegame.
    explicit StaticMemory(const SaveState& save) {
        for (const SaveBlock& b : save.blocks())
            if (!b.heap && b.size != 0 && !blocks_.count(b.address))
                blocks_[b.address].assign(save.stream().begin() + b.offset, save.stream().begin() + b.offset + b.size);
    }

    // A global the savegame does not store (e.g. a work list the original rebuilds before
    // use): created zeroed unless it is there already.
    void add_block(uint32_t address, size_t size) {
        if (!has(address)) blocks_[address].assign(size, 0);
    }

    bool has(uint32_t address) const {
        auto it = blocks_.upper_bound(address);
        if (it == blocks_.begin()) return false;
        --it;
        return address - it->first < it->second.size();
    }
    uint8_t& at(uint32_t address) {
        auto it = blocks_.upper_bound(address);
        if (it != blocks_.begin()) {
            --it;
            if (address - it->first < it->second.size()) return it->second[address - it->first];
        }
        throw std::out_of_range("static memory: address " + hex(address) + " is not in the savegame");
    }
    uint8_t u8(uint32_t a) const { return const_cast<StaticMemory*>(this)->at(a); }
    uint16_t u16(uint32_t a) const { return uint16_t(u8(a) | u8(a + 1) << 8); }
    uint32_t u32(uint32_t a) const { return uint32_t(u16(a) | uint32_t(u16(a + 2)) << 16); }
    void set8(uint32_t a, uint8_t v) { at(a) = v; }
    void set16(uint32_t a, uint16_t v) { at(a) = uint8_t(v), at(a + 1) = uint8_t(v >> 8); }
    void set32(uint32_t a, uint32_t v) { set16(a, uint16_t(v)), set16(a + 2, uint16_t(v >> 16)); }
    // A pointer stored in the original's memory, as an LE data address (0 stays 0).
    uint32_t pointer(uint32_t a) const {
        uint32_t p = u32(a);
        return p == 0 ? 0 : p - kGuestDataBase;
    }
    void set_pointer(uint32_t a, uint32_t address) { set32(a, address == 0 ? 0 : address + kGuestDataBase); }

    const std::map<uint32_t, Bytes> &blocks() const { return blocks_; }
    static StaticMemory from_blocks(std::map<uint32_t, Bytes> blocks) {
        StaticMemory memory;
        memory.blocks_ = std::move(blocks);
        return memory;
    }

private:
    static std::string hex(uint32_t v) {
        char b[16];
        std::snprintf(b, sizeof b, "0x%x", v);
        return b;
    }
    std::map<uint32_t, std::vector<uint8_t>> blocks_;
};

}  // namespace bmh
