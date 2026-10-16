// SPDX-License-Identifier: GPL-3.0-only
// Development helper: resolves and checks each argument like the program start does
// (directory, BMH.EXE/FLTD.EXE or ZIP/7z archive) and prints the result.
//   launch_check [--cache <dir>] <input>...
#include <iostream>

#include "launch/archive.hpp"
#include "launch/game_data.hpp"

int main(int argc, char** argv) {
    using namespace bmh::launch;
    std::filesystem::path cache = default_cache_directory();
    int bad = 0;
    const std::vector<std::string> args = utf8_arguments(argc, argv);
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--cache" && i + 1 < args.size()) {
            cache = path_from_utf8(args[++i]);
            continue;
        }
        const ResolvedGame game = resolve_game(path_from_utf8(arg), cache);
        std::cout << "== " << arg << (game.unpacked ? " (unpacked)" : "") << "\n" << describe(game);
        if (!game.error.empty() || !game.check.playable) ++bad;
    }
    return bad == 0 ? 0 : 1;
}
