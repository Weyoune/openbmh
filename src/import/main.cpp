// SPDX-License-Identifier: GPL-3.0-only
#include "import/obw.hpp"
#include "launch/archive.hpp"
#include "version.hpp"

#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
        const auto args = bmh::launch::utf8_arguments(argc, argv);
        std::filesystem::path source, destination;
        bmh::obw::Options options;
        bool directory = false;
        for (size_t i = 1; i < args.size(); ++i) {
            const auto& arg = args[i];
            if (arg == "--help" || arg == "-h") {
                std::cout << "OpenBMH installation-to-OBW converter\n"
                    "Usage: openbmh_import --data DIR_OR_EXE --output WORLD.obw\n"
                    "       --start-date YYYY-07-01 [--title TEXT] [--directory]\n\n"
                    "Reads BMH.EXE/M3D.MAN/TEXTD.BLK or FLTD.EXE/M3E.MAN/TEXTE.BLK.\n"
                    "--directory writes an unpacked world instead of a ZIP archive.\n"
                    "Existing destinations are never overwritten. Imported content stays local.\n"
                    "Conversion profile only: no game loading, FBM saves or graphics yet.\n";
                return 0;
            }
            if (arg == "--version") {
                std::cout << "OpenBMH importer " << bmh::kOpenBmhVersion << " (OBW 1.0)\n";
                return 0;
            }
            if (arg == "--directory") { directory = true; continue; }
            if (arg != "--data" && arg != "--output" && arg != "--start-date" && arg != "--title")
                throw std::runtime_error("unknown option: " + arg);
            if (++i == args.size()) throw std::runtime_error("missing value for " + arg);
            if (arg == "--data") source = bmh::launch::path_from_utf8(args[i]);
            if (arg == "--output") destination = bmh::launch::path_from_utf8(args[i]);
            if (arg == "--start-date") options.start_date = args[i];
            if (arg == "--title") options.title = args[i];
        }
        if (source.empty() || destination.empty() || options.start_date.empty())
            throw std::runtime_error("--data, --output and --start-date are required; see --help");
        if (!directory && destination.extension() != ".obw")
            throw std::runtime_error("archive output must use the .obw extension");
        const auto documents = bmh::obw::import_installation(source, options);
        bmh::obw::write(documents, destination, directory);
        std::cout << "Converted " << documents.size() << " documents to "
                  << bmh::launch::path_to_utf8(destination) << "\n"
                  << "Profile: legacy-database-v1; content_license: local-import-only.\n"
                  << "See x-legacy/import-report.json for coverage and remaining mappings.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "openbmh_import: " << error.what() << '\n';
        return 1;
    }
}
