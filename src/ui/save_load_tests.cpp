// SPDX-License-Identifier: GPL-3.0-only
#include "ui/save_load.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace bmh;
using namespace bmh::ui;

namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("save/load tests require externally supplied game assets");
        const auto root = std::filesystem::temp_directory_path() /
            ("openbmh-file-input-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() { std::error_code error; std::filesystem::remove_all(path, error); }
        } cleanup{root};
        const Image backdrop(320, 240, 0x42);
        for (const auto [kind, extension] : {
                std::pair{SaveLoadFileKind::game, ".FBM"},
                std::pair{SaveLoadFileKind::settings, ".EIN"},
                std::pair{SaveLoadFileKind::menu_layout, ".MEN"}}) {
            std::ofstream(root / (std::string("Existing") + extension)) << "authored filename fixture";
            SaveLoadScreen load(argv[1], root, SaveLoadMode::load, 0, 7, backdrop, true, kind);
            check(load.click(0, 0, true) && load.close_requested() && !load.pending(),
                  "right-click closes every file picker without submitting a load");
            SaveLoadScreen save(argv[1], root, SaveLoadMode::save, 0, 7, backdrop, true, kind);
            check(save.text("Custom"), "a save picker accepts a new name immediately after opening");
            check(save.text(" Name"), "subsequent text appends to the edited name");
            save.key("ENTER");
            check(save.pending() && save.pending()->path.filename() == std::string("Custom Name") + extension &&
                  !save.pending()->overwrite, "new typing replaces the prefilled filename and keeps its file kind");
            check(!save.click(0, 0, true) && !save.close_requested(),
                  "a submitted file operation retains ownership until acknowledged or rejected");
            save.reject("authored refusal");
            check(save.click(0, 0, true) && save.close_requested(),
                  "a rejected save can be cancelled with the right mouse button");
            SaveLoadScreen edit(argv[1], root, SaveLoadMode::save, 0, 7, backdrop, true, kind);
            edit.click(120, 214); edit.key("BACKSPACE");
            check(edit.text("Long Name") && edit.key("SELECT_ALL") && edit.text("Short"),
                  "clicking and select-all permit replacing a whole filename");
            check(edit.text("\xc3\x84") && edit.key("BACKSPACE"), "backspace removes a complete UTF-8 character");
            edit.key("ENTER");
            check(edit.pending() && edit.pending()->path.filename() == std::string("Short") + extension,
                  "editing and UTF-8 deletion preserve the resulting host filename");
        }
        std::cout << "save/load input checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
