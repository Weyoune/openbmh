// SPDX-License-Identifier: GPL-3.0-only
// Implementation of fltd.hpp. translate_fltd() builds the German-layout image of the English
// FLTD.EXE: a table of entries (generated include launch/fltd_map.inc) says for every German
// data object whether to copy it from its English address, keep the German values, fill
// pointers with an empty string, or use one of OpenBMH's own English texts (kOwnTexts, which
// must fit the German text's room); the English data segment is also placed at its own
// addresses. prepare_english_game() writes that image and copies the text, database and
// other data files into the target directory. Addresses come from formats/executable_layout.hpp.
#include "formats/executable_layout.hpp"
#include "launch/fltd.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "formats/binary.hpp"
#include "formats/diamond_image.hpp"
#include "launch/archive.hpp"
#include "launch/game_data.hpp"

namespace fs = std::filesystem;

namespace bmh::launch {

namespace {

// The English image: address = file offset - 0x496d (157 of 165 data strings are referenced
// with that offset), initialised data up to Career_CompetitionPointersEnglish_RuntimeField.
constexpr uint32_t kEnFileOffset = 0x496d;
constexpr uint32_t kEnDataStart = ::bmh::layout::EnDataStart, kEnDataEnd = ::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField;
constexpr uint32_t kFltdSize = 507925;
constexpr uint32_t kEnglishCalendarDateSetup = 0x684;
constexpr uint32_t kEnglishOverwriteQuestionPointer = 0x6c43c;
// The German image the engine expects: LE objects from layout field, data object GermanDataSegmentBegin-layout field.
constexpr uint32_t kDeBase = 0x10000, kDeEnd = ::bmh::layout::GermanImageEnd;
// A zero byte in the German BSS: the empty string of empty_pointers entries.
constexpr uint32_t kEmptyString = ::bmh::layout::EmptyString;

enum class Kind { copy, keep, empty_pointers, own_text };

struct Entry {
    uint32_t de;      // German address
    uint32_t length;  // bytes of the German object (texts: with the closing NUL)
    Kind kind;
    uint32_t en;      // English address (copy)
    std::vector<uint8_t> bytes;  // the German values (keep)
};

const std::vector<Entry> kMap = {
#include "launch/fltd_map.inc"
};

// OpenBMH's English texts for V2.24 texts the English V2.20 does not have. Each must fit the
// German text's room (its length including the NUL).
struct OwnText {
    uint32_t de;
    const char* text;
};
const OwnText kOwnTexts[] = {
    {::bmh::layout::Fltd_Layout_TextPointer, ", "},
    {::bmh::layout::Fltd_Layout_TextPointer2, ": "},
    {::bmh::layout::Fltd_Layout_TextPointer3, "s"},   // plural ending (German "e")
    {::bmh::layout::Fltd_Layout_TextPointer4, " #"},
    {::bmh::layout::Fltd_Layout_TextPointer5, "Competition closed"},
    {::bmh::layout::Fltd_Layout_ExecutableField, "As not all season ticket holders fit into the stadium, you pay "},
    {::bmh::layout::Fltd_Layout_ExecutableField2, " compensation."},
    {::bmh::layout::CareerSession_FormatNumber_TextPointer, "("},
    {::bmh::layout::CareerSession_FormatNumber_ExecutableField2, "# disc.)"},
    {::bmh::layout::Fltd_Layout_ExecutableField3, "The sponsor feels exploited. He pays only "},
    {::bmh::layout::Fltd_Layout_ExecutableField4, " percent of the price."},
    {::bmh::layout::CurrencySuffixText, " &"},  // the pound sign of the English font
    {::bmh::layout::Fltd_Layout_ExecutableField5, "AUTO"},
    {::bmh::layout::BalancePlan_DrawRows_TextPointer, "+)"},
    {::bmh::layout::Fltd_Layout_ExecutableField6, "-"},
};

}  // namespace

LeImage translate_fltd(const fs::path& fltd_exe) {
    const Bytes raw = read_file(fltd_exe);
    if (raw.size() != kFltdSize || raw[0] != 'M' || raw[1] != 'Z')
        throw std::runtime_error(path_to_utf8(fltd_exe) + " is not the Football Limited FLTD.EXE");
    const auto en = [&](uint32_t address) -> uint8_t {
        const size_t at = size_t(address) + kEnFileOffset;
        return at < raw.size() ? raw[at] : 0;
    };
    Bytes image(kDeEnd - kDeBase, 0);
    const auto put = [&](uint32_t address, uint8_t value) {
        if (address < kDeBase || address >= kDeEnd) throw std::runtime_error("FLTD map outside the image");
        image[address - kDeBase] = value;
    };
    // the English data segment at its own addresses (German code addresses: never read as data)
    for (uint32_t a = kEnDataStart; a < kEnDataEnd; ++a) put(a, en(a));
    // (the map is sorted by German address; a copy reaches up to the next entry, because the
    // object extents of the decompiler are sometimes finer than the tables - LeagueSizes/a2bce)
    // (the German values are written last: a keep-de range such as the competition table
    // CupTable covers entries inside it)
    for (int pass = 0; pass < 2; ++pass)
    for (size_t n = 0; n < kMap.size(); ++n) {
        const Entry& e = kMap[n];
        if ((e.kind == Kind::keep) != (pass == 1)) continue;
        switch (e.kind) {
            case Kind::copy: {
                uint32_t length = e.length;
                if (n + 1 < kMap.size() && kMap[n + 1].de > e.de) length = std::max(length, std::min(kMap[n + 1].de - e.de, 0x800u));
                for (uint32_t i = 0; i < length; ++i) put(e.de + i, en(e.en + i));
                break;
            }
            case Kind::keep:
                for (uint32_t i = 0; i < e.length && i < e.bytes.size(); ++i) put(e.de + i, e.bytes[i]);
                break;
            case Kind::empty_pointers:
                for (uint32_t i = 0; i + 4 <= e.length; i += 4)
                    for (uint32_t b = 0; b < 4; ++b) put(e.de + i + b, uint8_t(kEmptyString >> (8 * b)));
                break;
            case Kind::own_text: {
                const OwnText* t = nullptr;
                for (const OwnText& o : kOwnTexts)
                    if (o.de == e.de) t = &o;
                const std::string text = t ? t->text : "";
                if (text.size() + 1 > e.length) throw std::runtime_error("English text does not fit at " + std::to_string(e.de));
                for (uint32_t i = 0; i < e.length; ++i) put(e.de + i, i < text.size() ? uint8_t(text[i]) : 0);
                break;
            }
        }
    }
    // Preserve the English date-setup operands in the shared epoch slot. The
    // English routine uses a different stack layout; the calendar reader
    // verifies that layout before reading its year and weekday.
    for (uint32_t i = 0; i < 14; ++i)
        put(layout::CalendarDateSetup + i, en(kEnglishCalendarDateSetup + i));
    // The native 11x11 pointer is an edition-owned indexed bitmap.
    for (uint32_t i = 0; i < 11 * 11; ++i)
        put(::bmh::layout::OriginalPointerPixels + i,
            en(::bmh::layout::EnglishOriginalPointerPixels + i));
    // Copy the actual English six-budget geometry, numeric steps and suffixes
    // for the common monthly environment model.
    for(const auto [to,from,count]:{std::array<uint32_t,3>{layout::Environment_FormatError_RuntimeField,layout::EnglishEnvironmentTileX,6},
        {layout::Environment_FormatError_RuntimeField2,layout::EnglishEnvironmentTileY,6},{layout::EnvironmentYouthColors,layout::EnglishEnvironmentYouthColors,3},
        {layout::EnvironmentBudgetSteps,layout::EnglishEnvironmentBudgetSteps,6},{layout::EnvironmentYouthPositionSuffix,layout::EnglishEnvironmentYouthPositionSuffix,5},
        {layout::EnvironmentCostSuffix,layout::EnglishEnvironmentCostSuffix,2}})for(uint32_t i=0;i<count;++i)put(to+i,en(from+i));
    LeImage out = LeImage::from_bytes(kDeBase, std::move(image));
    return out;
}

bool prepare_english_game(const fs::path& source, const fs::path& target, std::string& error) {
    try {
        std::error_code ec;
        fs::create_directories(target);
        const auto copy = [&](const fs::path& from, const fs::path& to) {
            fs::create_directories(to.parent_path());
            fs::copy_file(from, to, fs::copy_options::overwrite_existing);
        };
        // the shared data files and directories (names as the German game has them)
        for (const char* name : {"M3.DAT", "M3.MEN", "M3PC.COL"})
            if (auto f = find_file(source, name)) copy(*f, target / name);
        for (const char* dir : {"KB", "W", "F", "S"}) {
            auto d = find_file(source, dir);
            if (!d) continue;
            for (const auto& e : fs::directory_iterator(*d)) {
                std::string name = path_to_utf8(e.path().filename());
                for (char& c : name) c = char(std::toupper(static_cast<unsigned char>(c)));
                if (e.is_regular_file()) copy(e.path(), target / dir / path_from_utf8(name));
            }
        }
        if (auto f = find_file(source, "TEXTE.BLK")) copy(*f, target / "TEXTD.BLK");
        if (auto f = find_file(source, "M3E.MAN")) copy(*f, target / "M3D.MAN");
        // FLTD.EXE is optional: without it the numbers built into OpenBMH and the texts of openbmh.dat stand in (formats/diamond_image.hpp)
        const auto exe = find_file(source, "FLTD.EXE");
        save_image(exe ? translate_fltd(*exe) : english_constants_image(source), target / "BMH.exe");
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

}  // namespace bmh::launch
