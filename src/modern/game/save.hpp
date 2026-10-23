// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/save.hpp"
// Writing savegames (MakeSavegame with FbmDecrypt2 and NationalCoachAfterLoad).
//
// A ModernState keeps part of the savegame in typed fields (players, squads, records, ...)
// and the rest in StaticMemory; save_stream puts both back into the block order of the
// original, so that loading a savegame and saving it again gives the same stream.

#include <cstdint>
#include <filesystem>

#include "formats/binary.hpp"
#include "formats/fbm.hpp"
#include "game/random.hpp"
#include "modern/game/modern_state.hpp"

namespace bmh::modern {

// The decrypted stream of a savegame: the 125 state blocks (NationalCoachAfterLoad, with the club
// records when their pointer ClubSeasonRecords is set) and the four name pools.
Bytes save_stream(const ModernState& g);

// FbmDecrypt2: the plain header read by the load list - the number of managers, the date
// ("TT.MM.JJJJ") and per manager (0x28 bytes): start league, national team flag, his name
// and his club's name (0x13 bytes each). The original leaves stack bytes in the unused
// parts; they are zero here.
Bytes save_header(const ModernState& g);

// MakeSavegame from the header on: "KRON_FBM", the previous id (here 0), "V2.24", the new
// id W(0, 0x8fff) << 16 + W(0, 0x8fff) + 1, the key byte W(0, 0xff) + 'n' and a random byte
// W(0, 0xff) (the four Watcom draws of saving), the encrypted stream and its checksum.
Savegame make_savegame(const ModernState& g, WatcomRandom& rand);

// The file bytes of a savegame (inverse of load_savegame).
Bytes savegame_bytes(const Savegame& s);
void write_savegame(const std::filesystem::path& path, const Savegame& s);

}  // namespace bmh::modern
