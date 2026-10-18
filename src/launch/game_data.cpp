// SPDX-License-Identifier: GPL-3.0-only
#include "launch/game_data.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <system_error>

#include "formats/binary.hpp"
#include "formats/le.hpp"
#include "formats/m3d.hpp"
#include "formats/textd.hpp"
#include "launch/archive.hpp"
#include "launch/fltd.hpp"

namespace fs = std::filesystem;

namespace bmh::launch {

namespace {

std::string upper(std::string s) {
    for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// The data files every version has (the German editions and the English one share the
// pictures, fonts and sounds; checked against the CD, the nine edition packages and FTL).
constexpr int kPictures[] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
                             21, 22, 23, 24, 25, 26, 27, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42,
                             43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
                             64, 65, 66, 67, 68, 69, 70, 74, 128, 129, 131, 132, 133, 134, 135, 136, 137, 138, 139};
constexpr int kBadges[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 17, 18, 19, 20, 44, 45, 46, 47, 48, 49, 50, 51, 52};

struct Required {
    const char* name;
    uint64_t size;  // 0: any size above zero
};
constexpr Required kCommon[] = {{"M3.DAT", 1408}, {"M3.MEN", 864}, {"M3PC.COL", 768}};

// Game EXEs by family. The German ones all have 602,527 bytes; the hashes differ between
// the editions, so only the size is required.
struct Family {
    GameFamily family;
    const char* exe;
    uint64_t exe_size;
    const char* texts;
    const char* database;
};
constexpr Family kFamilies[] = {
    {GameFamily::german, "BMH.EXE", 602527, "TEXTD.BLK", "M3D.MAN"},
    {GameFamily::english, "FLTD.EXE", 507925, "TEXTE.BLK", "M3E.MAN"},
    {GameFamily::disk1994, "M3.EXE", 0, "", "M3PC.MAN"},
};

std::optional<Family> family_of_exe(const std::string& file_name) {
    const std::string u = upper(file_name);
    for (const Family& f : kFamilies)
        if (u == f.exe) return f;
    return std::nullopt;
}

// Every game EXE directly in `directory`.
std::vector<std::pair<Family, fs::path>> game_exes(const fs::path& directory) {
    std::vector<std::pair<Family, fs::path>> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(directory, ec)) {
        if (!e.is_regular_file(ec)) continue;
        if (auto f = family_of_exe(path_to_utf8(e.path().filename()))) out.emplace_back(*f, e.path());
    }
    return out;
}

void require(GameCheck& c, const fs::path& dir, const std::string& name, uint64_t size, const std::string& shown) {
    const auto p = find_file(dir, name);
    if (!p) {
        c.problems.push_back(shown + " is missing");
        return;
    }
    std::error_code ec;
    const uint64_t actual = fs::file_size(*p, ec);
    if (ec || actual == 0) c.problems.push_back(shown + " is empty or unreadable");
    else if (size != 0 && actual != size)
        c.problems.push_back(shown + " has " + std::to_string(actual) + " bytes instead of " + std::to_string(size));
}

// The shared picture/font/sound files of the sub directories KB, W, F and S.
void require_media(GameCheck& c) {
    const auto sub = [&](const char* name) -> std::optional<fs::path> {
        auto p = find_file(c.directory, name);
        if (!p || !fs::is_directory(*p)) {
            c.problems.push_back(std::string("the directory ") + name + " is missing");
            return std::nullopt;
        }
        return p;
    };
    // (a long list of missing files says less than the count and the first ones)
    const auto files = [&](const fs::path& dir, const char* shown, const std::vector<std::string>& names) {
        std::vector<std::string> missing;
        for (const auto& n : names)
            if (!find_file(dir, n)) missing.push_back(n);
        if (missing.empty()) return;
        std::string text = std::string(shown) + ": " + std::to_string(missing.size()) + " file(s) missing (";
        for (size_t i = 0; i < missing.size() && i < 5; ++i) text += (i ? ", " : "") + missing[i];
        c.problems.push_back(text + (missing.size() > 5 ? ", ...)" : ")"));
    };
    std::vector<std::string> names;
    if (auto kb = sub("KB")) {
        for (int n : kPictures) names.push_back(std::to_string(n) + ".VGA");
        files(*kb, "KB", names);
    }
    names.clear();
    if (auto w = sub("W")) {
        for (int n : kBadges) names.push_back(std::to_string(n) + ".VGA");
        files(*w, "W", names);
    }
    if (auto f = sub("F")) files(*f, "F", {"BMH2.FNT", "BMH3.FNT"});
    if (auto s = sub("S")) files(*s, "S", {"2000", "2001", "2002", "2003"});
}

// A readable title from the text block (ISO 646-DE: the brackets are umlauts).
std::string text_title(const std::string& raw) {
    std::string out;
    for (unsigned char ch : raw) {
        switch (ch) {
            case '[': out += "\xC3\x84"; break;
            case '\\': out += "\xC3\x96"; break;
            case ']': out += "\xC3\x9C"; break;
            case '{': out += "\xC3\xA4"; break;
            case '|': out += "\xC3\xB6"; break;
            case '}': out += "\xC3\xBC"; break;
            case '~': out += "\xC3\x9F"; break;
            default: out += ch >= 0x20 && ch < 0x7F ? char(ch) : '?';
        }
    }
    return out;
}

void check_german(GameCheck& c) {
    // (a translated English game has BMH.exe in the OpenBMH image format, launch/fltd.hpp)
    bool image = false;
    if (const auto exe = find_file(c.directory, "BMH.EXE")) {
        std::ifstream in(*exe, std::ios::binary);
        char head[13] = {};
        in.read(head, sizeof head);
        image = std::string(head, sizeof head) == "OPENBMH-IMAGE";
    }
    require(c, c.directory, "BMH.EXE", image ? 0 : 602527, "BMH.EXE");
    require(c, c.directory, "TEXTD.BLK", 0, "TEXTD.BLK");
    require(c, c.directory, "M3D.MAN", 0, "M3D.MAN");
    for (const Required& r : kCommon) require(c, c.directory, r.name, r.size, r.name);
    require_media(c);
    if (!c.problems.empty()) return;
    // the files must also load: the engine reads tables from the EXE, the texts and the
    // database of clubs and players
    try {
        load_le(*find_file(c.directory, "BMH.EXE"));
    } catch (const std::exception& e) {
        c.problems.push_back(std::string("BMH.EXE cannot be read: ") + e.what());
    }
    try {
        const TextDb texts = load_textd(*find_file(c.directory, "TEXTD.BLK"));
        // the supported rule profile editions greet with their title as the first mailbox message (text 1001);
        // the CD version has another greeting there
        c.title = "Bundesliga Manager Hattrick";
        if (texts.strings.size() > 1001 && texts.strings[1001].rfind("Bundesliga Manager", 0) == 0)
            c.title = text_title(texts.strings[1001]);
    } catch (const std::exception& e) {
        c.problems.push_back(std::string("TEXTD.BLK cannot be read: ") + e.what());
    }
    try {
        load_database(*find_file(c.directory, "M3D.MAN"));
    } catch (const std::exception& e) {
        c.problems.push_back(std::string("M3D.MAN cannot be read: ") + e.what());
    }
}

void check_english(GameCheck& c) {
    require(c, c.directory, "FLTD.EXE", 507925, "FLTD.EXE");
    require(c, c.directory, "TEXTE.BLK", 0, "TEXTE.BLK");
    require(c, c.directory, "M3E.MAN", 0, "M3E.MAN");
    for (const Required& r : kCommon) require(c, c.directory, r.name, r.size, r.name);
    require_media(c);
    c.title = "Football Limited (English)";
}

// A cheap content hash (FNV-1a, 64 bit) that names the cache folder of an archive.
std::string content_hash(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    uint64_t h = 0xcbf29ce484222325ull;
    char buf[1 << 16];
    while (in) {
        in.read(buf, sizeof buf);
        for (std::streamsize i = 0; i < in.gcount(); ++i) h = (h ^ uint8_t(buf[i])) * 0x100000001b3ull;
    }
    char text[17];
    std::snprintf(text, sizeof text, "%016llx", static_cast<unsigned long long>(h));
    return text;
}

// Whether an archive file belongs to the game data: the original file kinds, the sounds
// (S\2000...) and savegames. Pictures, manuals and other fan extras are left out of the
// cache (their long names also overflow the 260 characters of a Windows path).
bool is_game_file(const std::string& relative) {
    const std::string u = upper(relative);
    const std::string base = u.substr(u.find_last_of('/') + 1);
    if (u.rfind("S/", 0) == 0 && base.find('.') == std::string::npos) return true;  // sounds
    const size_t dot = base.find_last_of('.');
    if (dot == std::string::npos) return false;
    const std::string ext = base.substr(dot + 1);
    for (const char* e : {"EXE", "BLK", "MAN", "DAT", "MEN", "COL", "VGA", "FNT", "LBM", "EIN", "CFG", "FBM", "WAP"})
        if (ext == e) return true;
    return false;
}

bool is_archive_name(const std::string& name) {
    const std::string u = upper(name);
    for (const char* ext : {".ZIP", ".7Z", ".ARJ", ".RAR", ".LZH", ".TAR", ".GZ"})
        if (u.size() > std::strlen(ext) && u.compare(u.size() - std::strlen(ext), std::string::npos, ext) == 0) return true;
    return false;
}

// Unpacks the one game of an archive into the cache (or finds it there).
ResolvedGame unpack(const fs::path& file, const fs::path& cache) {
    ResolvedGame r;
    const ArchiveFormat format = archive_format(file);
    if (format == ArchiveFormat::arj) {
        r.error = "This is an ARJ archive (the installer format of the 1994 floppy disks). OpenBMH cannot read it; "
                  "please unpack the installed game.";
        return r;
    }
    if (format == ArchiveFormat::rar) {
        r.error = "RAR archives are not supported; please use ZIP or 7z.";
        return r;
    }
    try {
        Archive archive = Archive::open(file);
        const auto& entries = archive.entries();
        // exactly one game EXE; its directory is the game's root in the archive
        std::vector<size_t> exes;
        bool nested = false, installer = false;
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].directory) continue;
            const std::string& n = entries[i].name;
            const std::string base = n.substr(n.find_last_of('/') + 1);
            if (family_of_exe(base)) exes.push_back(i);
            if (is_archive_name(base)) nested = true;
            if (upper(base) == "INSTALL.EXE") installer = true;
        }
        if (exes.empty()) {
            if (installer) r.error = "The archive holds an installer (INSTALL.EXE), not an installed game.";
            else if (nested) r.error = "The archive contains further archives (a box with several editions). "
                                       "Please choose the archive of one edition.";
            else r.error = "The archive contains no BMH.EXE or FLTD.EXE.";
            return r;
        }
        if (exes.size() > 1) {
            r.error = "The archive contains " + std::to_string(exes.size()) +
                      " games. Please use an archive with exactly one game.";
            return r;
        }
        const std::string& exe_name = entries[exes[0]].name;
        const std::string root = exe_name.substr(0, exe_name.find_last_of('/') + 1);  // "" or "dir/.../"

        const fs::path target = cache / content_hash(file);
        const fs::path marker = target / ".openbmh-unpacked";
        std::error_code ec;
        if (!fs::exists(marker, ec)) {
            const fs::path temp = cache / (content_hash(file) + ".partial");
            fs::remove_all(temp, ec);
            fs::create_directories(temp);
            uint64_t total = 0;
            for (size_t i = 0; i < entries.size(); ++i) {
                const ArchiveEntry& e = entries[i];
                if (e.name.compare(0, root.size(), root) != 0 || e.name.size() == root.size()) continue;
                const std::string rel = e.name.substr(root.size());
                // no absolute paths and no way out of the target directory
                const fs::path relative = path_from_utf8(rel);
                bool safe = !relative.is_absolute() && !relative.has_root_name();
                for (const auto& part : relative) safe = safe && part != "..";
                if (!safe) throw ArchiveError("unsafe path in the archive: " + e.name);
                if (!e.directory && !is_game_file(rel)) continue;
                total += e.size;
                if (total > 256ull * 1024 * 1024) throw ArchiveError("the game in the archive is too large");
                if (e.directory) {
                    fs::create_directories(temp / relative);
                    continue;
                }
                fs::create_directories((temp / relative).parent_path());
                const std::vector<uint8_t> bytes = archive.read(i);
                std::ofstream out(temp / relative, std::ios::binary);
                out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
                if (!out) throw ArchiveError("cannot write " + path_to_utf8(temp / relative));
            }
            std::ofstream(temp / ".openbmh-unpacked") << path_to_utf8(file) << "\n";
            fs::remove_all(target, ec);
            fs::rename(temp, target);
        }
        r.check = check_game_directory(target);
        r.unpacked = true;
    } catch (const std::exception& e) {
        r.error = std::string("The archive cannot be used: ") + e.what();
    }
    return r;
}

ResolvedGame resolve_game_unchecked(const fs::path& input, const fs::path& cache);

}  // namespace

std::optional<fs::path> find_file(const fs::path& directory, const std::string& name) {
    std::error_code ec;
    const fs::path exact = directory / path_from_utf8(name);
    if (fs::exists(exact, ec)) return exact;
    const std::string wanted = upper(name);
    for (const auto& e : fs::directory_iterator(directory, ec))
        if (upper(path_to_utf8(e.path().filename())) == wanted) return e.path();
    return std::nullopt;
}

GameCheck check_game_directory(const fs::path& path) {
    GameCheck c;
    std::error_code ec;
    fs::path dir = fs::is_directory(path, ec) ? path : path.parent_path();
    auto exes = game_exes(dir);
    if (exes.empty()) {
        // an edition folder with the game one level below (e.g. "BMH 2026-27 supported rule profile\BMH")
        std::vector<fs::path> candidates;
        for (const auto& e : fs::directory_iterator(dir, ec))
            if (e.is_directory(ec) && !game_exes(e.path()).empty()) candidates.push_back(e.path());
        if (candidates.size() == 1) {
            dir = candidates[0];
            exes = game_exes(dir);
        }
    }
    c.directory = dir;
    if (exes.empty()) {
        c.problems.push_back("no BMH.EXE or FLTD.EXE in " + path_to_utf8(dir));
        return c;
    }
    // a named EXE decides; otherwise the German game before the English one
    std::sort(exes.begin(), exes.end(), [](const auto& a, const auto& b) { return int(a.first.family) < int(b.first.family); });
    auto chosen = exes.front();
    if (!fs::is_directory(path, ec))
        for (const auto& e : exes)
            if (upper(path_to_utf8(e.second.filename())) == upper(path_to_utf8(path.filename()))) chosen = e;
    c.family = chosen.first.family;
    c.exe = chosen.second;
    switch (c.family) {
        case GameFamily::german:
            check_german(c);
            c.playable = c.problems.empty();
            break;
        case GameFamily::english:
            check_english(c);
            // complete English data is translated for the engine when the game is resolved
            // (resolve_game -> prepare_english_game); the check itself stays on the original
            break;
        case GameFamily::disk1994:
            c.title = "Bundesliga Manager Hattrick (1994 floppy version)";
            c.unsupported = "The 1994 floppy version (M3.EXE) is not supported; please use the CD version or a later edition.";
            break;
        case GameFamily::unknown: break;
    }
    return c;
}

ResolvedGame resolve_game(const fs::path& input, const fs::path& cache) {
    try {
        ResolvedGame game = resolve_game_unchecked(input, cache);
        // The English Football Limited runs on the same engine through a German-layout data
        // directory, built once in the cache (launch/fltd.hpp); its name follows FLTD.EXE.
        if (game.error.empty() && game.check.family == GameFamily::english && game.check.problems.empty()) {
            const fs::path prepared = cache / ("en-" + content_hash(game.check.exe));
            const fs::path marker = prepared / ".openbmh-english";
            // the version of the translation (launch/fltd.cpp, fltd_map.inc): a cache built
            // by another version is built again
            constexpr const char *kTranslation =
                "english translation 14"; // 14: bind native English OPL slots and note frequencies
            std::string built;
            {
                std::ifstream in(marker);
                std::getline(in, built);
            }
            std::error_code ec;
            if (built != kTranslation) {
                fs::remove_all(prepared, ec);
                std::string error;
                if (!prepare_english_game(game.check.directory, prepared, error)) {
                    game.error = "The English version cannot be prepared: " + error;
                    return game;
                }
                std::ofstream(marker) << kTranslation << "\n" << path_to_utf8(game.check.directory) << "\n";
            }
            game.check.directory = prepared;
            game.check.playable = true;
            game.unpacked = true;
        }
        return game;
    } catch (const std::exception& e) {
        // (file system errors, names that cannot be converted)
        ResolvedGame r;
        r.error = std::string("The game cannot be opened: ") + e.what();
        return r;
    }
}

namespace {
ResolvedGame resolve_game_unchecked(const fs::path& input, const fs::path& cache) {
    std::error_code ec;
    if (!fs::exists(input, ec)) {
        ResolvedGame r;
        r.error = "not found: " + path_to_utf8(input);
        return r;
    }
    if (fs::is_directory(input, ec)) return {check_game_directory(input), {}, false};
    if (family_of_exe(path_to_utf8(input.filename()))) return {check_game_directory(input), {}, false};
    if (archive_format(input) != ArchiveFormat::none) return unpack(input, cache);
    ResolvedGame r;
    r.error = path_to_utf8(input.filename()) + " is neither a game directory, BMH.EXE/FLTD.EXE nor a ZIP/7z archive.";
    return r;
}
}  // namespace

fs::path default_cache_directory() {
    const auto env = [](const char* name) -> std::optional<fs::path> {
#ifdef _WIN32
        // (the wide environment: the profile path may hold characters outside the code page)
        std::wstring wname(name, name + std::strlen(name));
        wchar_t* v = nullptr;
        size_t length = 0;
        if (_wdupenv_s(&v, &length, wname.c_str()) == 0 && v) {
            const fs::path value = *v ? fs::path(v) : fs::path();
            std::free(v);
            if (!value.empty()) return value;
        }
#else
        if (const char* v = std::getenv(name); v && *v) return fs::path(v);
#endif
        return std::nullopt;
    };
#if defined(_WIN32)
    if (auto base = env("LOCALAPPDATA")) return *base / "OpenBMH" / "games";
    return fs::temp_directory_path() / "OpenBMH" / "games";
#elif defined(__APPLE__)
    if (auto home = env("HOME")) return *home / "Library" / "Application Support" / "OpenBMH" / "games";
    return fs::temp_directory_path() / "OpenBMH" / "games";
#else
    if (auto data = env("XDG_DATA_HOME")) return *data / "openbmh" / "games";
    if (auto home = env("HOME")) return *home / ".local" / "share" / "openbmh" / "games";
    return fs::temp_directory_path() / "openbmh" / "games";
#endif
}

std::string describe(const ResolvedGame& game) {
    if (!game.error.empty()) return game.error;
    const GameCheck& c = game.check;
    std::string text;
    if (!c.title.empty()) text += c.title + "\n";
    text += path_to_utf8(c.directory) + "\n";
    for (const auto& p : c.problems) text += "- " + p + "\n";
    if (!c.unsupported.empty()) text += c.unsupported + "\n";
    if (c.playable) text += "The game data is complete.\n";
    return text;
}

}  // namespace bmh::launch
