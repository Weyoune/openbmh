// SPDX-License-Identifier: GPL-3.0-only
// The game without BMH.exe and FLTD.EXE: OpenBMH carries the numbers and texts of the Diamond 3.7 executable (tables,
// rates, positions, prices, live commentary, messages; the generated formats/diamond_constants.inc) and of the English
// Football Limited (formats/fltd_constants.inc) and needs only the asset files of the game.
//
// load_le() returns diamond_constants_image() for a BMH.exe that does not exist, so every reader of the executable image
// works unchanged. A BMH.exe that is there is used as before (it carries its own edition's numbers and texts). For an
// English installation the launcher (launch/fltd.cpp) writes english_constants_image() as the image of the game
// directory when FLTD.EXE is missing.
#pragma once

#include <filesystem>

#include "formats/binary.hpp"
#include "formats/le.hpp"

namespace bmh {

// The German (Diamond 3.7) image built from the embedded data.
LeImage diamond_constants_image();

// The English Football Limited image in the German address space, built from the embedded data (what
// launch::translate_fltd() makes from FLTD.EXE).
LeImage english_constants_image();

// Whether `file_name` is the name of the executable (BMH.exe in any case).
bool is_executable_name(const std::filesystem::path& file_name);

}  // namespace bmh
