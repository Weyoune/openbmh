// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "launch/fltd.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main() {
    using namespace bmh;
    std::filesystem::path root;
    try {
        root = std::filesystem::temp_directory_path() / ("openbmh-fltd-map-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(root);
        // Authored X-32-shaped data: only tested source slots have nonzero bytes.
        Bytes source(507925);
        source[0] = 'M'; source[1] = 'Z';
        const auto put = [&](uint32_t address, uint8_t value) { source.at(address + 0x496d) = value; };
        const auto check = [](bool value, const char* message) { if (!value) throw std::runtime_error(message); };
        const uint8_t toggle_rows[] = {3, 7, 11, 13, 17, 19};
        put(layout::EnglishLayoutTranslation16 - 1, 99); // alignment padding is not a toggle row
        for (uint32_t i = 0; i < 6; ++i) put(layout::EnglishLayoutTranslation16 + i, toggle_rows[i]);
        put(layout::EnglishLayoutTranslation73 - 1, 0xfb); // negative colour precedes positive colour
        put(layout::EnglishLayoutTranslation73, 0x47);
        for (uint32_t i = 0; i < 2; ++i) {
            put(layout::EnglishDesktopBinSourceX + i, uint8_t(10 + i));
            put(layout::EnglishDesktopBinSourceY + i, uint8_t(20 + i));
            put(layout::EnglishDesktopSocketSourceX + i, uint8_t(30 + i));
        }
        const auto path = root / "FLTD.EXE";
        { std::ofstream out(path, std::ios::binary); out.write(reinterpret_cast<const char*>(source.data()), std::streamsize(source.size())); }
        const auto image = launch::translate_fltd(path);
        for (uint32_t i = 0; i < 6; ++i)
            check(image.u8(layout::Setup_FormatError_RuntimeField8 + i) == toggle_rows[i], "toggle mapping includes alignment padding");
        check(image.u8(layout::Desktop_Copy_TextPointer) == 0xfb, "negative balance colour");
        check(image.u8(layout::Desktop_Copy_TextPointer + 1) == 0x47, "positive balance must use the next source byte");
        for (uint32_t i = 0; i < 2; ++i) {
            check(image.u8(layout::Desktop_Render_RuntimeField + i) == 10 + i, "bin source x");
            check(image.u8(layout::Desktop_Render_RuntimeField2 + i) == 20 + i, "bin source y");
            check(image.u8(layout::Desktop_Render_RuntimeField3 + i) == 30 + i, "socket source x");
        }
        std::filesystem::remove_all(root);
        std::cout << "FLTD setup, colour and desktop-control mappings passed\n";
        return 0;
    } catch (const std::exception& error) {
        if (!root.empty()) { std::error_code ec; std::filesystem::remove_all(root, ec); }
        std::cerr << error.what() << '\n'; return 1;
    }
}
