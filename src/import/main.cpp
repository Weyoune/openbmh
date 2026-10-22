// SPDX-License-Identifier: GPL-3.0-only
#include "import/obw.hpp"
#include "content/package.hpp"
#include "launch/archive.hpp"
#include "version.hpp"

#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
        const auto args = bmh::launch::utf8_arguments(argc, argv);
        std::filesystem::path source, destination, validate_path;
        bmh::obw::Options options;
        bool directory = false;
        for (size_t i = 1; i < args.size(); ++i) {
            const auto& arg = args[i];
            if (arg == "--help" || arg == "-h") {
                std::cout << "OpenBMH installation-to-OBW converter\n"
                    "Usage: openbmh_import --data DIR_OR_EXE --output WORLD.obw\n"
                    "       --start-date YYYY-07-01 [--mod BMH-classic|FLTD-classic]\n"
                    "       [--title TEXT] [--directory] [--database-only]\n"
                    "       openbmh_import --validate WORLD.obw_OR_DIRECTORY\n\n"
                    "Imports a complete BMH or Football Limited installation, including media.\n"
                    "--directory writes an unpacked world instead of a ZIP archive.\n"
                    "Existing destinations are never overwritten. Imported content stays local.\n"
                    "--database-only explicitly selects the old partial development export.\n"
                    "Classic content targets the 0.2 adapter; 0.1 cannot load OBW. No FBM conversion.\n";
                return 0;
            }
            if (arg == "--version") {
                std::cout << "OpenBMH importer " << bmh::kOpenBmhVersion << " (OBW 1.0)\n";
                return 0;
            }
            if (arg == "--directory") { directory = true; continue; }
            if (arg == "--database-only") { options.database_only = true; continue; }
            if (arg != "--data" && arg != "--output" && arg != "--start-date" && arg != "--title" &&
                arg != "--mod" && arg != "--validate")
                throw std::runtime_error("unknown option: " + arg);
            if (++i == args.size()) throw std::runtime_error("missing value for " + arg);
            if (arg == "--data") source = bmh::launch::path_from_utf8(args[i]);
            if (arg == "--output") destination = bmh::launch::path_from_utf8(args[i]);
            if (arg == "--start-date") options.start_date = args[i];
            if (arg == "--title") options.title = args[i];
            if (arg == "--mod") {
                options.mod = args[i];
                if (options.mod == "FTDL-classic" || options.mod == "FTLD-classic") options.mod = "FLTD-classic";
                if (options.mod != "BMH-classic" && options.mod != "FLTD-classic")
                    throw std::runtime_error("--mod must be BMH-classic or FLTD-classic");
            }
            if (arg == "--validate") validate_path = bmh::launch::path_from_utf8(args[i]);
        }
        if (!validate_path.empty()) {
            if (!source.empty() || !destination.empty() || !options.start_date.empty() || !options.mod.empty() || directory || options.database_only)
                throw std::runtime_error("--validate cannot be combined with conversion options");
            const auto documents = bmh::obw::read(validate_path);
            if (documents.contains("x-classic/seal.json"))
                bmh::content::verify(documents);
            std::cout << "Valid OBW content: " << documents.size() << " entries; no engine runtime test implied.\n";
            return 0;
        }
        if (source.empty() || destination.empty() || options.start_date.empty())
            throw std::runtime_error("--data, --output and --start-date are required; see --help");
        if (!directory && destination.extension() != ".obw")
            throw std::runtime_error("archive output must use the .obw extension");
        auto documents = bmh::obw::import_installation(source, options);
        if (!options.database_only)
            bmh::content::seal(documents);
        bmh::obw::write(documents, destination, directory);
        std::cout << "Converted " << documents.size() << " documents to "
                  << bmh::launch::path_to_utf8(destination) << "\n"
                  << "Profile: "
                  << (options.database_only ? "legacy-database-v1 (partial)"
                                            : "classic-installation-v1 (complete Classic content)")
                  << "; content_license: local-import-only.\n"
                  << "See manifest.json and x-legacy/import-report.json. Classic packages can be added to "
                     "the 0.2 portable mods directory.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "openbmh_import: " << error.what() << '\n';
        return 1;
    }
}
