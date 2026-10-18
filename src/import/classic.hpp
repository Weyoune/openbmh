// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "import/obw.hpp"
#include "formats/font.hpp"
#include "formats/palette.hpp"

namespace bmh::obw {
// Installation content only. No manager choices, career RNG or save state.
struct ClassicData {
    bool english = false;
    Database database;
    TextDb texts;
    LeImage executable; // DATA regions and four bounded literals; no instructions
    LeImage source_tables;
    Bytes desktops, formations, text_header;
    Palette palette;
    std::map<std::string, Image> images;
    std::map<std::string, Font> fonts;
    std::map<std::string, Bytes> audio;
};

void complete_classic(Documents& out, const std::filesystem::path& root,
                      const Database& database, const LeImage& executable,
                      const LeImage& source_tables, bool english, const Options& options);
void validate_classic(const Documents& out);
// For an editor after deliberate changes. CRC32 detects damage, not authenticity.
void refresh_classic_inventory(Documents& out);
ClassicData load_classic(const Documents& out);
} // namespace bmh::obw
