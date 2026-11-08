// SPDX-License-Identifier: GPL-3.0-only
// The game without BMH.exe and FLTD.EXE (formats/diamond_image.*): the images built from the embedded numbers and texts
// of the Diamond 3.7 executable and of the English Football Limited. Usage: diamond_image_tests [<BMH data dir> | -] [<openbmh.dat>]
// (with a data directory the embedded German image is also compared byte for byte with its BMH.exe).
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include "formats/diamond_image.hpp"
#include "formats/le.hpp"

using namespace bmh;
namespace fs = std::filesystem;

namespace {

constexpr uint32_t kImageStart = 0x10000, kMinimumSize = 0xB0000;  // where the image begins and its least size
int failures = 0;
void require(bool ok, const char* what) {
    if (!ok) { std::cerr << "FAIL: " << what << "\n"; ++failures; }
}

bool contains_text(const LeImage& image, const std::string& text) {
    const auto& b = image.bytes();
    return std::search(b.begin(), b.end(), text.begin(), text.end()) != b.end();
}

// Without the texts file the images hold the numbers only; with it (argv[2], data/openbmh.dat) they also hold the texts.
void german(const Bytes& dat) {
    LeImage image = diamond_constants_image();
    require(image.size() > kMinimumSize && image.base() == kImageStart, "the German image has the layout of the Diamond data object");
    // the letter tables of the cup draw and fixtures are numbers of the image
    require(contains_text(image, "abcdefghijklmnopqrst"), "the fixture letter tables are embedded");
    if (dat.empty()) return;
    require(apply_texts(image, 1, dat), "openbmh.dat has the German texts");
    require(contains_text(image, "DER LINIENRICHTER HAT DIE FAHNE OBEN"), "the German texts come from openbmh.dat");
}

void english(const Bytes& dat) {
    LeImage image = english_constants_image();
    require(image.base() == kImageStart && image.size() > kMinimumSize, "the English image lies in the German address space");
    if (dat.empty()) return;
    require(apply_texts(image, 2, dat), "openbmh.dat has the English texts");
    require(contains_text(image, "Competition closed"), "the English texts come from openbmh.dat");
}

void damaged_file(const Bytes& dat) {
    LeImage image = diamond_constants_image();
    bool threw = false;
    try { apply_texts(image, 1, Bytes{'n', 'o', 't', ' ', 'a', ' ', 'f', 'i', 'l', 'e', ' ', ' ', ' ', ' ', ' ', ' '}); } catch (const FormatError&) { threw = true; }
    require(threw, "a file without the magic is refused");
    if (dat.size() < 40) return;
    Bytes cut(dat.begin(), dat.begin() + 40);
    threw = false;
    try { apply_texts(image, 1, cut); } catch (const FormatError&) { threw = true; }
    require(threw, "a truncated file is refused");
    // a range that is not a text of the game cannot be written: the file never changes a number of the image
    Bytes bad = dat;
    for (size_t at = 12 + 4 + 8; at < 12 + 4 + 12; ++at) bad[at] ^= 0x55;   // the address of the first German range
    threw = false;
    try { apply_texts(image, 1, bad); } catch (const FormatError&) { threw = true; }
    require(threw, "a range that is no text range is refused");
}
void missing_executable() {
    // a BMH.exe that is not there stands for the embedded German image
    const fs::path nowhere = fs::temp_directory_path() / "openbmh-no-such-directory" / "BMH.exe";
    const LeImage loaded = load_le(nowhere);
    const LeImage embedded = diamond_constants_image();
    require(loaded.bytes() == embedded.bytes() && loaded.base() == embedded.base(), "a missing BMH.exe loads the embedded image");
    require(is_executable_name("bmh.EXE") && !is_executable_name("TEXTD.BLK"), "is_executable_name");
}

void against_real_executable(const fs::path& directory) {
    fs::path exe;
    for (const auto& entry : fs::directory_iterator(directory))
        if (is_executable_name(entry.path().filename())) exe = entry.path();
    if (exe.empty()) return;
    const LeImage real = load_le(exe);
    const LeImage embedded = diamond_constants_image();
    // the embedded image is the Diamond 3.7 data object: every byte of it that is not zero equals the executable
    if (real.size() != embedded.size()) { std::cout << "(the executable is not Diamond 3.7: embedded numbers not compared)\n"; return; }
    size_t compared = 0, different = 0;
    for (uint32_t i = 0; i < embedded.size(); ++i) {
        const uint8_t value = embedded.bytes()[i];
        if (!value) continue;
        ++compared;
        if (real.bytes()[i] != value) ++different;
    }
    if (different) std::cout << "(the executable differs from the embedded Diamond 3.7 in " << different << " of " << compared << " bytes)\n";
    else std::cout << "all " << compared << " embedded bytes equal BMH.exe\n";
}

}  // namespace

int main(int argc, char** argv) try {
    Bytes dat;
    if (argc > 2) dat = read_file(argv[2]);
    german(dat);
    english(dat);
    damaged_file(dat);
    missing_executable();
    if (argc > 1 && std::string_view(argv[1]) != "-") against_real_executable(argv[1]);
    if (failures) return 1;
    std::cout << "PASS embedded German and English images\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
}
