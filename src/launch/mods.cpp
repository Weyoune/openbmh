// SPDX-License-Identifier: GPL-3.0-only
#include "launch/mods.hpp"
#include "content/save.hpp"
#include "launch/archive.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <set>
namespace bmh::launch {
namespace fs = std::filesystem;
namespace {
std::string fold(std::string s) {
    for (auto &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string filename(const fs::path &p) { return path_to_utf8(p.filename()); }
void safe_directory(const fs::path &path) {
    const auto status = fs::symlink_status(path);
    if (fs::is_symlink(status) || (fs::exists(status) && !fs::is_directory(status)))
        throw FormatError("Unsafe portable directory: " + path_to_utf8(path));
    fs::create_directories(path);
}
} // namespace
ModLibrary::ModLibrary(fs::path root) : root_(fs::absolute(std::move(root)).lexically_normal()) {}
void ModLibrary::validate_name(const std::string &name) {
    if (name.empty() || name.size() > 100 || name == "." || name == ".." || name.front() == '.' ||
        name.back() == '.' || name.back() == ' ')
        throw FormatError("Invalid mod name");
    for (unsigned char c : name)
        if (c < 32 || c == 127 || std::string("/\\:*?\"<>|").find(char(c)) != std::string::npos)
            throw FormatError("Invalid character in mod name");
    auto base = fold(name.substr(0, name.find('.')));
    const std::set<std::string> reserved{"con",  "prn",  "aux",  "nul",  "com1", "com2", "com3", "com4",
                                         "com5", "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3",
                                         "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    if (reserved.contains(base))
        throw FormatError("Reserved mod name");
}
std::vector<ModEntry> ModLibrary::list() const {
    std::vector<ModEntry> out;
    const auto folder = root_ / "mods";
    if (!fs::exists(fs::symlink_status(folder)))
        return out;
    if (fs::is_symlink(fs::symlink_status(folder)) || !fs::is_directory(folder))
        throw FormatError("Unsafe mods directory");
    for (const auto &entry : fs::directory_iterator(folder)) {
        const auto name = filename(entry.path());
        if (name.starts_with('.'))
            continue;
        if (!entry.is_directory() && !entry.is_symlink())
            continue;
        ModEntry mod;
        mod.name = name;
        mod.directory = entry.path();
        try {
            validate_name(name);
            if (entry.is_symlink())
                throw FormatError("Linked mod directories are not supported");
            for (const auto &child : fs::directory_iterator(entry.path()))
                if (child.path().filename() != "content.obw")
                    throw FormatError("Unexpected files in sealed Classic mod");
            const auto content = content::open(entry.path() / "content.obw");
            mod.content_id = content->content_id;
        } catch (const std::exception &e) {
            mod.error = e.what();
        }
        out.push_back(std::move(mod));
    }
    std::sort(out.begin(), out.end(),
              [](const auto &a, const auto &b) { return fold(a.name) < fold(b.name); });
    return out;
}
void ModLibrary::available(const std::string &name) const {
    validate_name(name);
    const auto folder = root_ / "mods";
    if (fs::exists(fs::symlink_status(folder))) {
        if (fs::is_symlink(fs::symlink_status(folder)) || !fs::is_directory(folder))
            throw FormatError("Unsafe mods directory");
        for (const auto &entry : fs::directory_iterator(folder))
            if (fold(filename(entry.path())) == fold(name))
                throw FormatError("A mod with this name already exists");
    }
}
void ModLibrary::owned(const ModEntry &mod) const {
    if (mod.directory.parent_path() != root_ / "mods" || filename(mod.directory) != mod.name ||
        fs::is_symlink(fs::symlink_status(mod.directory)))
        throw FormatError("Mod path is not owned by this library");
}
fs::path ModLibrary::user_directory(const ModEntry &mod) const {
    if (mod.content_id.size() != 64 ||
        mod.content_id.find_first_not_of("0123456789abcdef") != std::string::npos)
        throw FormatError("Invalid mod identity");
    safe_directory(root_ / "userdata");
    const auto directory = root_ / "userdata" / mod.content_id;
    safe_directory(directory);
    return directory;
}
ImportReport ModLibrary::add(const AddMod &request, const SaveMigrator &migrate,
                             const ImportProgress &progress) {
    available(request.name);
    if (request.start_year < 1900 || request.start_year > 9998)
        throw FormatError("Start year must be 1900–9998");
    safe_directory(root_ / "mods");
    const auto update = [&](const std::string &s) {
        if (progress)
            progress(s);
    };
    update("Installation wird konvertiert …");
    obw::Options options;
    options.start_date = std::to_string(request.start_year) + "-07-01";
    options.title = "Classic installation";
    auto docs = obw::import_installation(request.source, options);
    const auto identity = content::seal(docs);
    update("Konvertierte Inhalte werden geprüft …");
    const auto staging =
        root_ / "mods" /
        (".import-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!fs::create_directory(staging))
        throw FormatError("Cannot reserve mod staging directory");
    struct Cleanup {
        fs::path p;
        ~Cleanup() {
            std::error_code ec;
            fs::remove_all(p, ec);
        }
    } cleanup{staging};
    content::write_archive(docs, staging / "content.obw");
    (void)content::open(staging / "content.obw");
    ImportReport report;
    report.mod = {request.name, "Classic", identity, {}, root_ / "mods" / path_from_utf8(request.name)};
    const auto userdata = user_directory(report.mod);
    // User data is retained separately, and never overwrites a previous migration.
    if (request.migrate) {
        const auto source = request.saves.empty() ? request.source : request.saves;
        if (!fs::is_directory(source))
            report.errors.push_back("Spielstandsordner ist nicht verfügbar");
        else {
            std::vector<fs::path> files;
            for (const auto &entry : fs::directory_iterator(source))
                if (entry.is_regular_file() && !entry.is_symlink())
                    files.push_back(entry.path());
            std::sort(files.begin(), files.end());
            for (const auto &input : files) {
                const auto ext = fold(path_to_utf8(input.extension()));
                if (ext != ".fbm" && ext != ".ein" && ext != ".men")
                    continue;
                update("Übernahme: " + filename(input));
                try {
                    auto destination = userdata / input.stem();
                    destination += ext == ".fbm" ? ".obs" : ext == ".ein" ? ".obsettings" : ".oblayout";
                    if (fs::exists(fs::symlink_status(destination)))
                        throw FormatError("Destination already exists; retained existing file");
                    if (ext == ".fbm") {
                        if (!migrate)
                            throw FormatError("Save migrator unavailable");
                        migrate(staging / "content.obw", input, destination);
                    } else {
                        auto bytes = read_file(input);
                        if (ext == ".ein" && bytes.size() != 180)
                            throw FormatError("Invalid settings length");
                        if (ext == ".men" && bytes.size() != 0x360)
                            throw FormatError("Invalid desktop layout length");
                        content::save_preferences(destination, ext == ".ein" ? "settings" : "layout", bytes,
                                                  false);
                    }
                    ++report.migrated;
                } catch (const std::exception &e) {
                    report.errors.push_back(filename(input) + ": " + e.what());
                }
            }
        }
    }
    update("Mod wird eingetragen …");
    available(request.name);
    fs::rename(staging, report.mod.directory);
    return report;
}
void ModLibrary::rename(const ModEntry &mod, const std::string &name) {
    owned(mod);
    if (name == mod.name)
        return;
    available(name);
    fs::rename(mod.directory, root_ / "mods" / path_from_utf8(name));
}
void ModLibrary::remove(const ModEntry &mod) {
    owned(mod);
    fs::remove_all(mod.directory);
}
} // namespace bmh::launch
