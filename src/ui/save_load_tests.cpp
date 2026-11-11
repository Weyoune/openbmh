// SPDX-License-Identifier: GPL-3.0-only
// Tests for SaveLoadScreen: no writes from picker input, overwrite confirmation, cancel paths, and filename editing.
#include "ui/save_load.hpp"
#include "ui/flow.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include "testing/check.hpp"

using namespace bmh;
using namespace bmh::ui;

namespace {
using bmh::test_support::check;
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
            const auto existing = root / (std::string("Existing") + extension);
            const std::string fixture = "authored filename fixture";
            std::ofstream(existing) << fixture;
            const auto unchanged = [&] {
                std::ifstream input(existing);
                check(std::string(std::istreambuf_iterator<char>(input), {}) == fixture,
                      "picker interactions never write the selected file");
            };
            SaveLoadScreen overwrite(argv[1], root, SaveLoadMode::save, 2, 17, backdrop, true, kind, 9);
            check(overwrite.click(70, 45) && overwrite.click(70, 45) && !overwrite.pending() &&
                  overwrite.state_json().find("\"overwrite_confirmation_open\":false") != std::string::npos,
                  "separate single clicks only select an existing save");
            check(overwrite.click(70, 45, false, true) && !overwrite.pending() &&
                  overwrite.state_json().find("\"overwrite_confirmation_open\":true") != std::string::npos,
                  "a true double-click asks for overwrite confirmation without submitting a write");
            check(!overwrite.click(70, 45, false, true) && !overwrite.pending(),
                  "repeated double-clicks cannot confirm an overwrite");
            unchanged();
            check(overwrite.key("ENTER") && overwrite.pending() && overwrite.pending()->overwrite &&
                  overwrite.pending()->path == existing && overwrite.pending()->manager_slot == 2 &&
                  overwrite.pending()->revision == 17 && overwrite.pending()->file_kind == kind &&
                  overwrite.pending()->origin_item_id == 9,
                  "Enter confirms the exact selected file with its manager, revision and file kind");
            const auto request = *overwrite.pending();
            check(!overwrite.click(70, 45, false, true) && !overwrite.key("ENTER") &&
                  *overwrite.pending() == request,
                  "queued mouse releases and Enter cannot submit a second pending overwrite");
            overwrite.acknowledge();
            check(overwrite.close_requested() && !overwrite.pending(),
                  "acknowledgement closes the picker and clears its one request");
            unchanged();
            SaveLoadScreen mouse(argv[1], root, SaveLoadMode::save, 0, 7, backdrop, true, kind);
            const auto list = mouse.render();
            mouse.click(70, 45, false, true);
            check(mouse.render().pixels != list.pixels && !mouse.text("IGNORED") &&
                  !mouse.key("BACKSPACE") && !mouse.info_press(70, 45),
                  "the original overwrite question owns input and renders over the file list");
            check(!mouse.click(100, 171, false, true) && !mouse.pending(),
                  "double-click releases cannot accept even over the question's Yes button");
            check(mouse.click(210, 171) && !mouse.pending() && !mouse.close_requested() &&
                  mouse.render().pixels == list.pixels,
                  "No dismisses the question and restores the unchanged picker");
            mouse.click(70, 45, false, true);
            check(mouse.click(100, 171) && mouse.pending() && mouse.pending()->overwrite &&
                  mouse.pending()->path == existing,
                  "double-click followed by a separate Yes click submits an entirely mouse-operated overwrite");
            const auto mouse_request = *mouse.pending();
            check(!mouse.click(100, 171) && *mouse.pending() == mouse_request,
                  "additional Yes releases preserve one pending write");
            unchanged();
            for (bool right : {false, true}) {
                SaveLoadScreen cancel(argv[1], root, SaveLoadMode::save, 0, 7, backdrop, true, kind);
                cancel.click(70, 45, false, true);
                check((right ? cancel.click(70, 45, true, true) : cancel.key("ESC")) &&
                      cancel.close_requested() && !cancel.pending(),
                      "right-click and Escape cancel an armed overwrite without a write");
            }
            const auto second = root / (std::string("Second") + extension);
            std::ofstream(second) << fixture;
            SaveLoadScreen change_row(argv[1], root, SaveLoadMode::save, 0, 7, backdrop, true, kind);
            change_row.click(70, 45, false, true);
            check(!change_row.click(70, 55) && change_row.selected() == 0,
                  "the question prevents the file list from changing its target");
            change_row.click(210, 171); // Decline before selecting another file.
            check(change_row.click(70, 55) && change_row.key("ENTER") && !change_row.pending() &&
                  change_row.key("ENTER") && change_row.pending() && change_row.pending()->path == second,
                  "selecting another row cannot reuse the first file's overwrite confirmation");
            SaveLoadScreen reset(argv[1], root, SaveLoadMode::save, 0, 7, backdrop, true, kind);
            reset.click(70, 45, false, true);
            reset.click(210, 171);
            reset.click(70, 45);
            check(reset.key("ENTER") && !reset.pending() && reset.key("ENTER") && reset.pending(),
                  "declining and selecting again preserves the existing two-Enter save path");
            SaveLoadScreen rename(argv[1], root, SaveLoadMode::save, 0, 7, backdrop, true, kind);
            rename.click(70, 45, false, true);
            rename.click(210, 171);
            check(rename.text("Other") && rename.key("ENTER") && rename.pending() &&
                  !rename.pending()->overwrite && rename.pending()->path.filename() == std::string("Other") + extension,
                  "editing an armed overwrite starts an independent new-file save");
            SaveLoadScreen activate_load(argv[1], root, SaveLoadMode::load, 0, 7, backdrop, true, kind);
            check(activate_load.click(70, 45, false, true) && activate_load.pending() &&
                  activate_load.pending()->action == SaveLoadAction::load && activate_load.pending()->path == existing,
                  "an explicit double-click also activates an existing load entry");
            unchanged();
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
        StartupFlow flow(argv[1], root);
        flow.click(214, 215);
        check(flow.screen_name() == "save_load", "setup opens its save-list picker");
        const auto load = flow.click(70, 45, false, true);
        check(load.request == FlowRequest::file_action && load.file_action.action == SaveLoadAction::load &&
              load.file_action.path.filename() == "Existing.FBM",
              "startup flow forwards a real double-click to its active file picker");
        std::cout << "save/load input checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
