// SPDX-License-Identifier: GPL-3.0-only
#include "ui/host_path.hpp"

#include <iostream>
#include <stdexcept>

int main() {
    try {
        // make_preferred produces actual backslashes on Windows. No installed
        // game assets or existing directory are needed for this native test.
        auto native = std::filesystem::path(u8"C:/Example/Profile/Save Games").make_preferred();
        const auto label = bmh::ui::save_directory_label(native);
        if (label != "C:/Example/Profile/Save Games" || label.find('\\') != std::string::npos)
            throw std::runtime_error("native path separators leak into the ISO 646-DE label");
        if (std::filesystem::path(std::u8string(label.begin(), label.end())) != native)
            throw std::runtime_error("displayed path does not identify the original host path");
        native = std::filesystem::path(u8"C:/Example/Büro/Spielstände").make_preferred();
        if (bmh::ui::save_directory_label(native) != "C:/Example/Büro/Spielstände")
            throw std::runtime_error("Unicode host path changed during separator normalization");
        std::cout << "Host path labels preserve Unicode and portable separators\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
