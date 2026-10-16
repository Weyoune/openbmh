// SPDX-License-Identifier: GPL-3.0-only
#include "launch/file_dialog.hpp"

#include <stdexcept>

#include <nfd.hpp>

#include "launch/archive.hpp"

namespace bmh::launch {

std::optional<std::filesystem::path> choose_game_file(const std::string& title) {
    (void)title;  // (nfd 1.2 has no dialog title parameter; the window title is the system's)
    NFD::Guard guard;
    NFD::UniquePathU8 path;
    // one filter with all accepted kinds; the check after the choice decides
    const nfdu8filteritem_t filters[] = {{"BMH game (ZIP, 7z, BMH.EXE, FLTD.EXE)", "zip,7z,exe"}};
    const nfdresult_t result = NFD::OpenDialog(path, filters, 1);
    if (result == NFD_CANCEL) return std::nullopt;
    if (result != NFD_OKAY) throw std::runtime_error(std::string("no file dialog: ") + NFD::GetError());
    return path_from_utf8(path.get());
}

}  // namespace bmh::launch
