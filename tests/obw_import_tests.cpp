// SPDX-License-Identifier: GPL-3.0-only
#include "import/obw.hpp"
#include "import/classic.hpp"
#include "import/assets.hpp"
#include "formats/executable_layout.hpp"
#include "launch/archive.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace bmh;
using J = bmh::cli::Json;

namespace {
void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F&& action, const std::string& message) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    check(rejected, message);
}
void word(Bytes& data, size_t at, uint32_t value, size_t width = 2) {
    for (size_t i = 0; i < width; ++i) data.at(at + i) = uint8_t(value >> (i * 8));
}
void append(Bytes& data, uint32_t value, size_t width = 2) {
    for (size_t i = 0; i < width; ++i) data.push_back(uint8_t(value >> (i * 8)));
}
void save(const fs::path& path, const Bytes& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    check(bool(out), "write synthetic fixture");
}
const J& f(const J& object, const char* name) { return object.object()->at(name); }
J document(const obw::Documents& documents, const char* path) { return cli::parse_json(documents.at(path)); }

Database database(bool english) {
    Database db;
    db.club_names.resize(419);
    for (size_t i = 0; i < db.club_names.size(); ++i) db.club_names[i] = "Synthetic team " + std::to_string(i);
    db.club_names[0] = "Test M}nchen";
    db.club_names[357] = english ? "ENGLAND" : "DEUTSCHLAND";
    db.club_names[358] = english ? "GERMANY" : "SPANIEN";
    db.club_names[359] = english ? "ITALY" : "ENGLAND";
    db.club_names[360] = "FRANCE";
    db.club_names[361] = english ? "SPAIN" : "ITALIEN";
    if (english) db.club_names[150] = "   ";
    db.players.resize(2574);
    for (size_t i = 0; i < db.players.size(); ++i)
        db.players[i] = {"Synthetic Player " + std::to_string(i), 60u | (90u << 7) | (80u << 14), 0x321, (6u << 21) | uint32_t(i % 4)};
    db.players[0].name = "T{st \"Name\"";
    db.players[1].w368 |= 1u << 27;
    db.raw_player_bonus_values.resize(3146);
    db.raw_player_bonus_values[1] = 128 | 17; // nationality 33, plus a bonus of 17
    db.raw_database_metadata.resize(100);
    db.raw_club_auxiliary_words.resize(168 * 2);
    db.raw_club_badge_selectors.resize(419 * 2);
    db.raw_foreign_club_strength.resize(251, 60);
    db.raw_league_auxiliary_bytes.resize(189);
    db.raw_referee_properties.resize(50);
    db.raw_club_kit_selectors.resize(419);
    db.raw_club_short_name_offsets.resize(168);
    db.squad_list.resize((3096 - 4) * 2 + 108);
    for (size_t i = 0; i < db.squad_list.size(); i += 2) word(db.squad_list, i, 4095);
    word(db.squad_list, 0, 0x8000); word(db.squad_list, 2, 1);
    word(db.squad_list, 90 * 24 * 2, 2); // first foreign squad, 16-slot stride
    word(db.squad_list, (2928 - 1) * 2, 3); // last squad
    db.referees = {"Synthetic Referee"};
    db.stadiums = {"Synthetic Stadium"};
    const auto pool = [](const std::vector<std::string>& names, Bytes& offsets) {
        Bytes result;
        for (const auto& name : names) {
            append(offsets, uint32_t(result.size()));
            result.insert(result.end(), name.begin(), name.end()); result.push_back(0);
        }
        return result;
    };
    db.pools[0] = pool(db.club_names, db.raw_club_name_offsets);
    std::vector<std::string> names;
    for (const auto& player : db.players) names.push_back(player.name);
    db.pools[1] = pool(names, db.raw_player_name_offsets);
    Bytes unused;
    db.pools[2] = pool(db.referees, unused); db.pools[3] = pool(db.stadiums, unused);
    return db;
}

LeImage executable(bool english) {
    constexpr uint32_t base = 0x10000;
    Bytes bytes(layout::GermanImageEnd - base);
    const auto put = [&](uint32_t address, uint32_t value, size_t width = 1) { word(bytes, address - base, value, width); };
    const int sizes[] = {english ? 20 : 18, english ? 20 : 18, english ? 20 : 18, english ? 20 : 18, english ? 10 : 18, 12, 12, 12, 12};
    for (uint32_t i = 0; i < 9; ++i) {
        put(layout::LeagueSizes + i, uint32_t(sizes[i]));
        put(layout::LeagueCalendarBits + i, english && i == 4 ? 31 : i);
    }
    if (!english) { put(layout::CareerSession_InstallLeagueSizes_RuntimeField3 + 4, 'A'); put(layout::CareerSession_InstallLeagueSizes_RuntimeField3 + 6, 'B'); }
    const std::vector<std::pair<int, uint32_t>> bindings = english ?
        std::vector<std::pair<int, uint32_t>>{{4, layout::EnglishFourTeamFixtureTable}, {6, layout::EnglishSixTeamFixtureTable},
            {12, layout::EnglishTwelveTeamFixtureTable}, {20, layout::EnglishTwentyTeamFixtureTable}} :
        std::vector<std::pair<int, uint32_t>>{{4, layout::CareerSession_RuntimeError_RuntimeField}, {6, layout::CareerSession_RuntimeError_RuntimeField2},
            {12, layout::CareerSession_RuntimeError_RuntimeField3}, {16, layout::CareerSession_RuntimeError_ExecutableField3},
            {18, layout::CareerSession_RuntimeError_ExecutableField}, {20, layout::CareerSession_RuntimeError_ExecutableField2}};
    uint32_t cursor = layout::GermanImageEnd - 4096; // synthetic data storage, outside pointer tables
    for (const auto& [teams, address] : bindings) {
        put(address, cursor, 4); ++cursor;
        std::vector<int> order;
        for (int i = 0; i < teams; ++i) order.push_back(i);
        for (int round = 1; round < teams; ++round) {
            put(address + uint32_t(round * 4), cursor, 4);
            for (int i = 0; i < teams / 2; ++i) {
                put(cursor++, uint32_t('a' + order[size_t(i)]));
                put(cursor++, uint32_t('a' + order[size_t(teams - 1 - i)]));
            }
            ++cursor;
            std::rotate(order.begin() + 1, order.end() - 1, order.end());
        }
    }
    put(layout::Career_BuildStadium_RuntimeField4 - 1, 2);
    put(base, 0xdeadbeef, 4); // an executable instruction region must never be exported
    for (uint32_t i = 0; i < 5; ++i) {
        put(layout::ClubPlayerDetail_ClubPlayerDetailRedraw_RuntimeField2 + i, 4);
        put(layout::ClubPlayerDetail_DrawPortrait_RuntimeField + i, 2 + i);
        put(layout::ClubPlayerDetail_DrawPortrait_RuntimeField2 + i, 3 + i);
        put(layout::ClubPlayerDetail_DrawPortrait_RuntimeField3 + i, 3);
        put(layout::ClubPlayerDetail_DrawPortrait_RuntimeField4 + i, 2);
        put(layout::ClubPlayerDetail_DrawPortrait_RuntimeField7 + i * 2, 20, 2);
    }
    put(layout::ClubPlayerDetail_DrawPortrait_RuntimeField, 253);
    put(layout::ClubPlayerDetail_DrawPortrait_RuntimeField3, 70);
    put(layout::ClubPlayerDetail_DrawPortrait_RuntimeField7, 319, 2);
    const std::string suffix = english ? " &" : " DM";
    for (size_t i = 0; i < suffix.size(); ++i) put(layout::CurrencySuffixText + uint32_t(i), uint8_t(suffix[i]));
    return LeImage::from_bytes(base, std::move(bytes));
}

Bytes serialize(const Database& db) {
    Bytes bytes;
    for (const auto& pool : db.pools) append(bytes, uint32_t(pool.size()));
    const auto add = [&](const Bytes& data) { bytes.insert(bytes.end(), data.begin(), data.end()); };
    add(db.raw_player_bonus_values);
    for (const auto& pool : db.pools) add(pool);
    for (const auto* table : {&db.raw_club_name_offsets, &db.raw_player_name_offsets, &db.raw_database_metadata,
        &db.raw_club_auxiliary_words, &db.raw_club_badge_selectors, &db.raw_foreign_club_strength,
        &db.raw_league_auxiliary_bytes, &db.raw_referee_properties, &db.raw_club_kit_selectors, &db.raw_club_short_name_offsets, &db.squad_list}) add(*table);
    for (int table = 0; table < 3; ++table)
        for (const auto& player : db.players) append(bytes, table == 0 ? player.w35c : table == 1 ? player.w368 : player.w364, 4);
    return bytes;
}
} // namespace

int main() {
    fs::path root;
    try {
        root = fs::temp_directory_path() / ("openbmh-obw-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directory(root);
        const obw::Options options{"2026-07-01", "Synthetic world"};
        TextDb texts;
        texts.strings.resize(TEXT_COUNT, "Synthetic text\n\"quote\"\\slash");
        texts.strings[0] += char(0xa7);
        const auto db = database(false);
        const auto exe = executable(false);
        const auto documents = obw::convert_database(db, texts, exe, false, options);
        check(documents == obw::convert_database(db, texts, exe, false, options), "conversion must be deterministic");
        const auto clubs = document(documents, "world/clubs.json");
        check(*f(f(f(clubs, "items").array()->at(0), "name"), "default").string() == "Test München", "decode German names");
        const auto players = document(documents, "world/players.json");
        const auto& rows = *f(players, "items").array();
        check(rows.size() == 2574, "all player identities retained");
        check(*f(rows[0], "team").string() == "club:00000", "lineup flag must not change player id");
        check(*f(rows[2], "team").string() == "club:00090", "foreign squad stride");
        check(*f(rows[3], "team").string() == "club:00137", "last squad assignment");
        check(!rows[4].object()->contains("team"), "unassigned player remains free");
        check(!rows[0].object()->contains("born"), "do not invent birthdays");
        check(*f(f(rows[0], "name"), "last").string() == "Täst \"Name\"", "escaped player name survives JSON");
        check(*f(f(rows[1], "attributes"), "bonus").integer() == 17, "bonus high bit is nationality, not rating");
        check(*f(rows[1], "nationalities").array()->at(0).string() == "nation:x-legacy-33", "nationality extension bit");
        check(*f(rows.back(), "nationalities").array()->at(0).string() == "nation:de", "database nationalities are preserved before career rewrites");
        check(documents.at("locale/de-original/openbmh.po").find("\\\"quote\\\"") != std::string::npos, "gettext quoting");
        const auto english = obw::convert_database(database(true), texts, executable(true), true, options);
        const auto manifest = document(english, "manifest.json");
        check(*f(manifest, "provenance").string() == "import:fltd", "English provenance");
        check(*f(f(manifest, "currency"), "code").string() == "GBP", "English currency");
        const auto comps = document(english, "world/competitions.json");
        const auto& leagues = *f(comps, "items").array();
        check(*f(leagues[3], "tier").integer() == 4, "English fourth tier");
        check(!*f(leagues[4], "played").boolean(), "English fifth league is not simulated");
        check(!f(leagues[4], "stages").array()->at(0).object()->contains("generator"), "no fabricated ten-team fixture table");
        check(f(document(english, "world/clubs.json"), "items").array()->size() == 418, "ignore unnamed English slot");

        const auto zip = root / "test.obw";
        obw::write(documents, zip, false);
        auto archive = launch::Archive::open(zip);
        check(archive.entries().size() == documents.size(), "complete ZIP container");
        for (size_t i = 0; i < archive.entries().size(); ++i) {
            const auto& entry = archive.entries()[i];
            const auto bytes = archive.read(i);
            check(std::string(bytes.begin(), bytes.end()) == documents.at(entry.name), "ZIP document round trip");
        }
        const auto folder = root / "unpacked";
        obw::write(documents, folder, true);
        for (const auto& [name, contents] : documents) {
            const auto bytes = read_file(folder / name);
            check(std::string(bytes.begin(), bytes.end()) == contents, "unpacked document round trip");
        }
        const auto saved_zip = read_file(zip);
        rejects([&] { obw::write(english, zip, false); }, "refuse to overwrite archive");
        check(read_file(zip) == saved_zip, "existing output untouched");
        rejects([&] { obw::write(documents, folder, true); }, "refuse to overwrite directory");
        rejects([&] { obw::convert_database(db, texts, exe, false, {"2026-02-30", "bad"}); }, "reject invalid date");
        auto bad_db = db;
        word(bad_db.squad_list, 2, 0);
        rejects([&] { obw::convert_database(bad_db, texts, exe, false, options); }, "reject duplicate squad ownership");
        word(bad_db.squad_list, 2, 4000);
        rejects([&] { obw::convert_database(bad_db, texts, exe, false, options); }, "reject unknown player");
        auto bad = documents;
        bad.erase("world/players.json");
        rejects([&] { obw::write(bad, root / "bad.obw", false); }, "reject incomplete archive");
        check(!fs::exists(root / "bad.obw"), "failure leaves no published output");
        bad = documents;
        auto changed = players;
        std::get<J::Array>(changed.object()->at("items").value)[0].object()->at("team") = "club:99999";
        bad["world/players.json"] = cli::dump_json(changed);
        rejects([&] { obw::validate(bad); }, "reject dangling player team");
        bad = documents;
        bad["../escape.json"] = "{}";
        rejects([&] { obw::write(bad, root / "escape.obw", false); }, "reject path traversal");
        check(!fs::exists(root / "escape.obw"), "bad archive path leaves no output");

        // Exercise the actual on-disk legacy readers and case-insensitive discovery.
        const auto input = root / "synthetic-installation";
        fs::create_directory(input);
        const auto raw_database = serialize(db);
        save(input / "m3d.man", raw_database);
        save_image(exe, input / "bmh.EXE");
        Bytes raw_texts(7);
        for (const auto& value : texts.strings) { raw_texts.insert(raw_texts.end(), value.begin(), value.end()); raw_texts.push_back(0); }
        save(input / "textd.blk", raw_texts);
        auto partial_options = options;
        partial_options.database_only = true;
        const auto converted = obw::import_installation(input, partial_options);
        check(converted.contains("x-legacy/source-database.json"), "retain exact database for future mappings");
        check(read_file(input / "m3d.man") == raw_database, "source database unchanged");
        check(obw::import_installation(root, partial_options) == converted, "follow one nested installation");
        rejects([&] { obw::import_installation(input, options); }, "full mod requires its media and startup data");

        // Complete synthetic content profile, including embedded NULs and media.
        for (const auto* dir : {"KB", "W", "F", "S"}) fs::create_directory(input / dir);
        save(input / "M3.MEN", Bytes(864, 7)); save(input / "M3.DAT", Bytes(1408, 9));
        save(input / "M3PC.COL", Bytes(768, 31));
        Bytes font(37, 0);
        word(font, 0, 1); font[3] = 2; word(font, 4, 1); font[8] = font[9] = 'A'; word(font, 16, 1);
        font[28] = 0x80; word(font, 31, 1); word(font, 35, 1);
        save(input / "F/BMH2.FNT", font); save(input / "F/BMH3.FNT", font);
        Bytes bank{'F','O','R','M',8,0,0,0,'A','L','P','T','R','A','U','M'};
        for (const char* name : {"2000", "2001", "2002", "2003"}) save(input / "S" / name, bank);
        const auto image = [&](const fs::path& path) {
            Bytes vga(8, 0), rle;
            constexpr int width = 320, height = 240;
            for (int i = 0; i < width * height; i += 128) {
                rle.push_back(127);
                for (int k = 0; k < 128; ++k) rle.push_back(uint8_t((i + k) % 251));
            }
            rle.push_back(0); word(vga, 0, width); word(vga, 2, height); word(vga, 4, uint32_t(rle.size()), 4);
            vga.insert(vga.end(), rle.begin(), rle.end()); save(path, vga);
        };
        image(input / "KB/0.VGA"); image(input / "KB/29.VGA"); image(input / "W/0.VGA");
        auto classic = converted;
        obw::complete_classic(classic, input, db, exe, exe, false, options);
        obw::validate(classic);
        const auto loaded = obw::load_classic(classic);
        check(loaded.desktops == Bytes(864, 7) && loaded.formations == Bytes(1408, 9), "all startup seed bytes survive");
        check(loaded.database.players[0].name == db.players[0].name && loaded.texts.blob == Bytes(raw_texts.begin() + 7, raw_texts.end()), "source records and text blob survive");
        check(loaded.images.at("kb/29").pixels.at(12 * 320 + 17) == uint8_t((12 * 320 + 17) % 251), "indexed media round trip");
        check(loaded.images.at("portraits/layer-0-0").pixels.at(67) == uint8_t((3 * 320 + 320) % 251), "portrait feature preserves packed row wrap");
        check(loaded.fonts.at("bmh2.fnt").glyph('A').bit_width == 1 && loaded.audio.at("2000") == bank, "font and audio round trip");
        check(loaded.executable.u32(0x10000) == 0, "no executable code in compatibility image");
        const auto portrait = obw::portrait_recipe("ABC", exe);
        check(*f(portrait, "background").integer() >= 0 && f(portrait, "layers").array()->size() == 5, "frozen part recipe");
        const auto classic_zip = root / "classic.obw";
        obw::write(classic, classic_zip, false);
        check(obw::read(classic_zip) == classic, "binary Classic ZIP round trip");
        auto damaged = classic;
        damaged.at("x-classic/desktop-seed.bin")[0] ^= 1;
        rejects([&] { obw::validate(damaged); }, "inventory catches binary corruption");
        damaged = classic; damaged["state/rng.json"] = "{}";
        rejects([&] { obw::validate(damaged); }, "reject injected save state");
        damaged = classic;
        auto renamed = document(damaged, "world/players.json");
        auto& renamed_rows = std::get<J::Array>(renamed.object()->at("items").value);
        const auto appearance = f(renamed_rows[0], "appearance");
        renamed_rows[0].object()->at("name").object()->at("last") = "Renamed";
        damaged["world/players.json"] = cli::dump_json(renamed);
        obw::refresh_classic_inventory(damaged);
        obw::validate(damaged);
        check(cli::dump_json(f(renamed_rows[0], "appearance")) == cli::dump_json(appearance), "rename keeps frozen face");
        renamed_rows[0].object()->at("appearance").object()->at("background") = 4;
        damaged["world/players.json"] = cli::dump_json(renamed);
        obw::refresh_classic_inventory(damaged);
        rejects([&] { obw::validate(damaged); }, "reject invalid appearance even with updated inventory");
        auto wrong_options = options; wrong_options.mod = "FLTD-classic";
        rejects([&] { obw::complete_classic(damaged, input, db, exe, exe, false, wrong_options); }, "mod/edition mismatch rejected");
        save(input / "m3d.man", Bytes{0, 1});
        rejects([&] { obw::import_installation(input, options); }, "reject truncated legacy database");
        fs::remove_all(root);
        std::cout << "OBW importer: synthetic DE/EN mappings, containers and rejection paths passed\n";
        return 0;
    } catch (const std::exception& error) {
        if (!root.empty()) { std::error_code ec; fs::remove_all(root, ec); }
        std::cerr << error.what() << '\n';
        return 1;
    }
}
