// SPDX-License-Identifier: GPL-3.0-only
// The game without BMH.exe and FLTD.EXE: OpenBMH carries the numbers of the Diamond 3.7 executable (tables, rates,
// positions, prices; the generated formats/diamond_constants.inc) and of the English Football Limited
// (formats/fltd_constants.inc) and needs only the asset files of the game. The game texts that live in the executables
// (live commentary, messages) are not part of the source: they are in the data file openbmh.dat, which ships with the
// release next to the programs (tools/diamond_constants/generate.py makes it). Without that file those texts are empty.
//
// load_le() returns diamond_constants_image() for a BMH.exe that does not exist, so every reader of the executable image
// works unchanged. A BMH.exe that is there is used as before (it carries its own edition's numbers and texts). For an
// English installation the launcher (launch/fltd.cpp) writes english_constants_image() as the image of the game
// directory when FLTD.EXE is missing.
#pragma once

#include <filesystem>
#include <string>

#include "formats/binary.hpp"
#include "formats/le.hpp"

namespace bmh {

// openbmh.dat: "OPENBMH-DAT" NUL, u32 set count, then per set u32 edition (1 German, 2 English), u32 range count, count
// times (u32 address, u32 length), then the bytes of all ranges. A range must be one of the edition's text ranges, so
// the file can never change a number of the image.
//
// The file is looked for in: the environment variable OPENBMH_DAT ("-" = none at all), the folder of the game data, the folder of the
// running program, the current folder. Returns an empty path when there is none.
std::filesystem::path find_texts_file(const std::filesystem::path& game_directory);

// A short text that changes when the texts file changes (empty without a file): used to rebuild cached English games.
std::string texts_signature(const std::filesystem::path& game_directory);

// The German (Diamond 3.7) image built from the embedded numbers plus the texts of openbmh.dat (found for
// `game_directory`).
LeImage diamond_constants_image(const std::filesystem::path& game_directory = {});

// The English Football Limited image in the German address space (what launch::translate_fltd() makes from
// FLTD.EXE), built from the embedded numbers plus the texts of openbmh.dat.
LeImage english_constants_image(const std::filesystem::path& game_directory = {});

// Puts the texts of an openbmh.dat file (edition 1 or 2) into `image`. Throws FormatError for malformed data or for a
// range that is not a text range of that edition. Returns false when the file has no set of that edition.
bool apply_texts(LeImage& image, int edition, const Bytes& file);

// Whether `file_name` is the name of the executable (BMH.exe in any case).
bool is_executable_name(const std::filesystem::path& file_name);

}  // namespace bmh
