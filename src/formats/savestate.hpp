// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Structured view of the decrypted savegame stream.
//
// The original writes its whole game state by dumping global variables one after
// another (NationalCoachAfterLoad, 125 calls of the stream writer FbmDecrypt). There are no
// headers or tags: the position of every block follows only from the fixed order and
// the size of the preceding blocks. This file reproduces that order so a block can be
// looked up by the address of the global it was dumped from, e.g. block(LeagueResults) for
// the match results.
//
// Block sizes are either constants or depend on table sizes of the master database
// (DbCounts). Blocks that were heap allocations in the original ("*PlayerConditionTechniqueForm") store the
// pointed-to data, not the pointer.
//
// After the state blocks follow four trailer blocks, each a u16 length plus data. They
// are the string pools of the master database (club names, player names, referees,
// stadiums), saved so that renamed clubs/players survive.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "formats/binary.hpp"
#include "formats/m3d.hpp"

namespace bmh {

struct SaveBlock {
    uint32_t address = 0;  // address of the global variable in BMH.exe V2.24 (for reference)
    bool heap = false;     // true if the global was a pointer to heap data
    size_t offset = 0;     // byte offset inside the decrypted stream
    size_t size = 0;
};

// The blocks of a savegame in the order NationalCoachAfterLoad writes them (offsets counted from 0).
// The heap copy of the club records (ClubSeasonRecords) is present only if `club_records` is set.
std::vector<SaveBlock> save_layout(const DbCounts& counts = {}, bool club_records = true);

class SaveState {
public:
    // Splits a decrypted stream (Savegame::stream). Throws FormatError if the block
    // sizes do not add up exactly to the stream length.
    SaveState(const Bytes& stream, const DbCounts& counts = {});

    const std::vector<SaveBlock>& blocks() const { return blocks_; }
    // First block dumped from the given global (some globals are dumped twice).
    const SaveBlock& block(uint32_t address) const;
    // Raw bytes of a block; valid as long as the stream passed to the constructor lives.
    const uint8_t* data(uint32_t address) const { return stream_.data() + block(address).offset; }
    const Bytes& stream() const { return stream_; }

    // The four string pools from the trailer (0 = club names, 1 = player names,
    // 2 = referees, 3 = stadiums), same layout as in M3D.MAN.
    const Bytes& pool(int index) const { return pools_.at(index); }

    // OpenBMH extension behind the trailer (only in OpenBMH saves of games that need it, such
    // as the English cup memory): "OBMHEXT1", u16 count, then per block u32 address,
    // u32 size, the bytes. Original saves have none.
    struct ExtensionBlock {
        uint32_t address;
        Bytes bytes;
    };
    const std::vector<ExtensionBlock>& extension() const { return extension_; }

private:
    const Bytes& stream_;
    std::vector<SaveBlock> blocks_;
    std::map<uint32_t, size_t> index_;
    std::vector<Bytes> pools_;
    std::vector<ExtensionBlock> extension_;
};

// Appends the OpenBMH extension (SaveState::extension) to a save stream.
void append_save_extension(Bytes& stream, const std::vector<SaveState::ExtensionBlock>& blocks);

}  // namespace bmh
