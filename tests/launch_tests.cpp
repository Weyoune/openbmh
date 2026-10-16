// SPDX-License-Identifier: GPL-3.0-only
// The game data launcher (src/launch): directory, EXE and archive input, the archive rules
// (exactly one game, no nested boxes, no paths leaving the target) and the data check.
// The archives are written here with miniz from the installed game data.
//   launch_tests <BMH data>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "launch/archive.hpp"
#include "launch/game_data.hpp"
#include "miniz.h"

namespace fs = std::filesystem;
using namespace bmh::launch;

namespace {

int failures = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FAIL: " << what << "\n";
    }
}

bool contains(const std::vector<std::string>& lines, const std::string& part) {
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& l) { return l.find(part) != std::string::npos; });
}

// The original data files of `data` (the kinds the game uses; fan pictures left out), as
// paths relative to it with '/'.
std::vector<std::string> data_files(const fs::path& data) {
    std::vector<std::string> out;
    for (const auto& e : fs::recursive_directory_iterator(data)) {
        if (!e.is_regular_file()) continue;
        std::string rel = path_to_utf8(fs::relative(e.path(), data));
        std::replace(rel.begin(), rel.end(), '\\', '/');
        std::string ext = path_to_utf8(e.path().extension());
        for (char& c : ext) c = char(std::toupper(static_cast<unsigned char>(c)));
        const bool sound = rel.rfind("S/", 0) == 0 && ext.empty();
        if (sound || ext == ".EXE" || ext == ".BLK" || ext == ".MAN" || ext == ".DAT" || ext == ".MEN" || ext == ".COL" ||
            ext == ".VGA" || ext == ".FNT" || ext == ".LBM")
            out.push_back(rel);
    }
    return out;
}

struct ZipWriter {
    mz_zip_archive zip{};
    explicit ZipWriter(const fs::path& file) {
        if (!mz_zip_writer_init_file(&zip, path_to_utf8(file).c_str(), 0)) throw std::runtime_error("cannot create zip");
    }
    void add(const std::string& name, const std::vector<char>& bytes) {
        if (!mz_zip_writer_add_mem(&zip, name.c_str(), bytes.data(), bytes.size(), MZ_DEFAULT_LEVEL))
            throw std::runtime_error("cannot add " + name);
    }
    void add_file(const std::string& name, const fs::path& file) {
        std::ifstream in(file, std::ios::binary);
        add(name, std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()));
    }
    ~ZipWriter() {
        mz_zip_writer_finalize_archive(&zip);
        mz_zip_writer_end(&zip);
    }
};

// A zip of the game under `prefix`, without the files in `skip`, plus `extra` text entries.
fs::path make_zip(const fs::path& dir, const std::string& name, const fs::path& data, const std::string& prefix,
                  const std::vector<std::string>& skip = {}, const std::vector<std::string>& extra = {}) {
    const fs::path file = dir / name;
    ZipWriter zip(file);
    for (const auto& rel : data_files(data))
        if (std::find(skip.begin(), skip.end(), rel) == skip.end()) zip.add_file(prefix + rel, data / path_from_utf8(rel));
    for (const auto& e : extra) zip.add(e, {'x'});
    return file;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: launch_tests <BMH data>\n";
        return 2;
    }
    const fs::path data = path_from_utf8(utf8_arguments(argc, argv)[1]);
    const fs::path work = fs::temp_directory_path() / "openbmh_launch_tests";
    fs::remove_all(work);
    fs::create_directories(work);
    const fs::path cache = work / "cache";

    // an installed directory and its EXE
    {
        const ResolvedGame dir = resolve_game(data, cache);
        check(dir.error.empty() && dir.check.playable && dir.check.family == GameFamily::german, "directory: " + describe(dir));
        const auto exe = find_file(data, "bmh.exe");  // (any case)
        check(exe.has_value(), "find_file ignores the case");
        if (exe) {
            const ResolvedGame by_exe = resolve_game(*exe, cache);
            check(by_exe.check.playable && by_exe.check.directory == data, "EXE: " + describe(by_exe));
        }
        check(!resolve_game(work / "nothing", cache).error.empty(), "a missing path is an error");
    }

    // an edition folder with the game one level below
    {
        const fs::path edition = work / "BMH 2026-27 Edition";
        for (const auto& rel : data_files(data)) {
            const fs::path target = edition / "BMH" / path_from_utf8(rel);
            fs::create_directories(target.parent_path());
            fs::copy_file(data / path_from_utf8(rel), target);
        }
        fs::create_directories(edition / "Anleitung");
        const ResolvedGame r = resolve_game(edition, cache);
        check(r.check.playable && r.check.directory == edition / "BMH", "edition folder: " + describe(r));
    }

    // a ZIP with the game in a sub directory and fan extras: unpacked once, extras left out
    {
        const fs::path zip = make_zip(work, "edition.zip", data, "BMH 2026-27/BMH/", {},
                                      {"BMH 2026-27/Anleitung/Handbuch.pdf", "BMH 2026-27/BMH/KB/12.VGA Fanbild (lang).jpg"});
        const ResolvedGame first = resolve_game(zip, cache);
        check(first.unpacked && first.check.playable, "zip: " + describe(first));
        check(!fs::exists(first.check.directory / "KB" / "12.VGA Fanbild (lang).jpg"), "zip: a fan picture was unpacked");
        check(!fs::exists(first.check.directory.parent_path() / "Anleitung"), "zip: files outside the game were unpacked");
        // the second start finds the cache (a file placed there survives)
        std::ofstream(first.check.directory / "probe.txt") << "x";
        const ResolvedGame second = resolve_game(zip, cache);
        check(second.check.directory == first.check.directory && fs::exists(second.check.directory / "probe.txt"),
              "zip: the second start unpacked again");
    }

    // a ZIP with a missing picture: complete except that file
    {
        const fs::path zip = make_zip(work, "missing.zip", data, "", {"KB/5.VGA"});
        const ResolvedGame r = resolve_game(zip, cache);
        check(r.error.empty() && !r.check.playable && contains(r.check.problems, "KB: 1 file(s) missing (5.VGA)"),
              "missing picture: " + describe(r));
    }

    // a box with several editions, two games, an installer, a path leaving the target
    {
        const fs::path box = work / "box.zip";
        {
            ZipWriter zip(box);
            zip.add("Box/Edition 1.zip", {'P', 'K'});
            zip.add("Box/Edition 2.zip", {'P', 'K'});
        }
        check(resolve_game(box, cache).error.find("further archives") != std::string::npos, "box: " + describe(resolve_game(box, cache)));

        const fs::path two = work / "two.zip";
        {
            ZipWriter zip(two);
            zip.add_file("A/BMH.EXE", *find_file(data, "BMH.EXE"));
            zip.add_file("B/BMH.EXE", *find_file(data, "BMH.EXE"));
        }
        check(resolve_game(two, cache).error.find("2 games") != std::string::npos, "two games: " + describe(resolve_game(two, cache)));

        const fs::path installer = work / "disk.zip";
        {
            ZipWriter zip(installer);
            zip.add("INSTALL.EXE", {'M', 'Z'});
            zip.add("BMH.ARJ", {'x'});
        }
        check(resolve_game(installer, cache).error.find("installer") != std::string::npos, "installer: " + describe(resolve_game(installer, cache)));

        const fs::path evil = make_zip(work, "evil.zip", data, "BMH/", {}, {"BMH/../../evil.DAT"});
        const ResolvedGame r = resolve_game(evil, cache);
        check(r.error.find("unsafe path") != std::string::npos, "path traversal: " + describe(r));
        check(!fs::exists(cache / "evil.DAT") && !fs::exists(work / "evil.DAT"), "path traversal: a file was written outside");
    }

    // neither directory, EXE nor archive
    {
        std::ofstream(work / "notes.txt") << "hello";
        check(!resolve_game(work / "notes.txt", cache).error.empty(), "a text file is no game");
    }

    fs::remove_all(work);
    std::cout << (failures ? "launch_tests FAILED: " + std::to_string(failures) : std::string("launch_tests passed")) << "\n";
    return failures ? 1 : 0;
}
