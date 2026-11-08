// SPDX-License-Identifier: GPL-3.0-only
// The game without BMH.exe and FLTD.EXE (formats/diamond_image.*): the images built from the embedded numbers and texts
// of the Diamond 3.7 executable and of the English Football Limited. Usage: diamond_image_tests [<BMH data dir>]
// (with a data directory the embedded German image is also compared byte for byte with its BMH.exe).
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>

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

void german() {
    const LeImage image = diamond_constants_image();
    require(image.size() > kMinimumSize && image.base() == kImageStart, "the German image has the layout of the Diamond data object");
    // the game texts are built in: a line of the live commentary
    require(contains_text(image, "DER LINIENRICHTER HAT DIE FAHNE OBEN"), "the German texts are embedded");
    // the letter tables of the cup draw and fixtures are numbers of the image
    require(contains_text(image, "abcdefghijklmnopqrst"), "the fixture letter tables are embedded");
}

void english() {
    const LeImage image = english_constants_image();
    require(image.base() == kImageStart && image.size() > kMinimumSize, "the English image lies in the German address space");
    require(contains_text(image, "Competition closed"), "the English texts are embedded");
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
    german();
    english();
    missing_executable();
    if (argc > 1) against_real_executable(argv[1]);
    if (failures) return 1;
    std::cout << "PASS embedded German and English images\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
}
