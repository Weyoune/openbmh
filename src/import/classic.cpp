// SPDX-License-Identifier: GPL-3.0-only
#include "import/classic.hpp"
#include "import/assets.hpp"
#include "import/detail.hpp"
#include "formats/executable_layout.hpp"
#include "launch/game_data.hpp"
#include "launch/archive.hpp"
#include <algorithm>
#include <cctype>
#include <iterator>
#include <set>

namespace bmh::obw {
namespace {
using namespace detail;
namespace fs = std::filesystem;
constexpr const char* profile = "classic-installation-v1";
constexpr const char* feature = "runtime:classic-v1";
constexpr const char* layout_id = "openbmh-classic-layout@1";
const A initializers{"classic-load-seeds@1", "classic-player-ratings@1", "classic-pending-offers@1",
    "classic-markets-and-moods@1", "classic-free-agents@1", "classic-setup-options@1",
    "classic-confirm-managers@1", "classic-season-setup@1"};
const A runtime_inputs{"game_rng_seed", "watcom_rng_seed", "managers", "game_level", "mode", "duration", "competition_options"};
struct Literal { const char* name; uint32_t address, size; };
// These four read-only labels share an LE code object. Extract the bounded
// strings individually, never the containing executable object/instructions.
constexpr Literal literals[] = {
    {"free_kick_taker", layout::FreeKickTakerLabel, 9}, {"penalty_taker", layout::PenaltyTakerLabel, 9},
    {"corner_taker", layout::CornerTakerLabel, 8}, {"automatic_takers", layout::AutomaticTakersLabel, 5}};

A literal_data(const LeImage& exe) {
    constexpr char digits[] = "0123456789abcdef";
    A values;
    for (const auto& literal : literals) {
        std::string hex;
        bool terminated = false;
        for (uint32_t i = 0; i < literal.size; ++i) {
            const auto c = exe.u8(literal.address + i);
            hex += digits[c >> 4]; hex += digits[c & 15];
            if (c == 0) { terminated = true; break; }
            if (c < 32 || c > 126) fail("invalid bounded embedded text literal");
        }
        if (!terminated) fail("unterminated bounded embedded text literal");
        values.emplace_back(O{{"name", literal.name}, {"address", int(literal.address)}, {"bytes_hex", hex}});
    }
    return values;
}

Bytes required(const fs::path& root, const std::string& name, size_t size) {
    const auto file = launch::find_file(root, name);
    if (!file || !fs::is_regular_file(*file) || fs::is_symlink(fs::symlink_status(*file)) || fs::file_size(*file) != size)
        fail(name + ": missing or incorrect size (expected " + std::to_string(size) + ')');
    return read_file(*file);
}
A data_regions(Documents& out, const LeImage& exe, bool english, const std::string& variant) {
    A regions;
    const auto add = [&](const char* name, uint32_t begin, uint32_t end) {
        if (!exe.contains(begin) || !exe.contains(end - 1)) fail("incomplete executable data region");
        const std::string path = "x-classic/" + variant + '/' + name + ".bin";
        out[path] = std::string(reinterpret_cast<const char*>(exe.data(begin)), end - begin);
        regions.emplace_back(O{{"name", name}, {"base", int(begin)}, {"size", int(end - begin)}, {"path", path}});
    };
    add("common-data", layout::GermanDataSegmentBegin, layout::GermanImageEnd);
    if (english) add("english-data", layout::EnDataStart, layout::Career_CompetitionPointersEnglish_RuntimeField);
    return regions;
}
LeImage load_regions(const Documents& out, const J& value, const J& embedded, bool english) {
    const auto& regions = arr(value);
    if (regions.size() != size_t(english ? 2 : 1)) fail("incorrect data-region count");
    Bytes image(layout::GermanImageEnd - 0x10000, 0);
    std::set<uint32_t> seen;
    for (const auto& region : regions) {
        const auto base = num(get(region, "base")), size = num(get(region, "size"));
        const bool german = base == layout::GermanDataSegmentBegin && size == layout::GermanImageEnd - layout::GermanDataSegmentBegin;
        const bool en = english && base == layout::EnDataStart && size == layout::Career_CompetitionPointersEnglish_RuntimeField - layout::EnDataStart;
        if ((!german && !en) || !seen.insert(uint32_t(base)).second) fail("unsupported/duplicate data region (code is forbidden)");
        const auto& bytes = out.at(str(get(region, "path")));
        if (int64_t(bytes.size()) != size) fail("truncated data region");
        std::copy(bytes.begin(), bytes.end(), image.begin() + (base - 0x10000));
    }
    const auto& texts = arr(embedded);
    if (texts.size() != std::size(literals)) fail("incomplete embedded text literals");
    for (size_t i = 0; i < std::size(literals); ++i) {
        const auto& literal = literals[i];
        const auto data = unhex(str(get(texts[i], "bytes_hex")));
        if (num(get(texts[i], "address")) != literal.address || str(get(texts[i], "name")) != literal.name ||
            data.empty() || data.size() > literal.size || data.back() != 0) fail("invalid embedded text literal binding");
        for (size_t ch = 0; ch + 1 < data.size(); ++ch)
            if (data[ch] < 32 || data[ch] > 126) fail("invalid embedded text character");
        std::copy(data.begin(), data.end(), image.begin() + (literal.address - 0x10000));
    }
    return LeImage::from_bytes(0x10000, std::move(image));
}
J localized(const std::string& value) { return O{{"default", legacy_text(value)}}; }

void competitions(Documents& out, const LeImage& exe) {
    auto comps = items(out, "competitions");
    const auto texts = parse(out, "x-legacy/texts.json");
    const auto& strings = arr(get(texts, "items"));
    std::map<int, A> cups;
    for (uint32_t entry = 0; entry < 36; ++entry) {
        const uint32_t at = layout::CupTable + entry * 0x28;
        const int cup = int(int16_t(exe.u16(at + 0x18)));
        if (cup < 0 || cup > 26) continue; // reserved sentinel entries stay in data regions
        const int flags = exe.u8(at + 0x14), next = int(int8_t(exe.u8(at + 0x16)));
        const int parallel = int(int8_t(exe.u8(at + 0x17)));
        const int count = exe.u8(at + 0x1a);
        O stage{{"id", "entry-" + std::to_string(entry)}, {"format", (flags & 4) ? "knockout" : "groups"},
            {"teams", count}, {"legs", 1 + (flags & 1)}, {"matchdays", "calendar:classic-cup-" + std::to_string(cup)},
            {"slots", A{O{{"resolver", "classic-entry@1"}, {"entry", int(entry)}, {"count", count}}}},
            {"classic", O{{"entry", int(entry)}, {"flags", flags}, {"next_entry", next}, {"parallel_entry", parallel},
                {"rounds", int(exe.u8(at + 0x1b))}, {"qualifying_rounds", int(exe.u8(at + 0x1c))},
                {"groups", int(exe.u8(at + 0x1e) & 15)}, {"group_index", int(exe.u8(at + 0x1e) >> 4)},
                {"qualification_quota", int(int8_t(exe.u8(at + 0x1f)))},
                {"draw_each_round", bool(flags & 0x20)}, {"decide_on_day", bool(flags & 8)}}}};
        cups[cup].emplace_back(std::move(stage));
    }
    const auto english = str(get(parse(out, "manifest.json"), "provenance")) == "import:fltd";
    for (auto& [cup, stages] : cups) {
        const auto text_index = (layout::DeskState_FormatError_ExecutableField3 - layout::TextPointerTable) / 4 + uint32_t(cup + 1);
        const auto label = text_index < strings.size() ? get(strings[text_index], "text") : J("Classic competition " + std::to_string(cup));
        std::string type = cup == 0 ? "cup" : cup >= 1 && cup <= 3 ? "club_international" :
            cup >= 4 && cup <= 8 ? "national_team" : cup == 10 ? "friendly" : cup == 11 && !english ? "indoor" : "cup";
        comps.emplace_back(O{{"id", "comp:classic-cup-" + std::to_string(cup)}, {"name", O{{"default", label}}},
            {"trophy", "asset:comp/classic-cup-" + std::to_string(cup) + "/trophy"},
            {"type", type}, {"played", true}, {"stages", std::move(stages)},
            {"classic", O{{"competition_id", cup}, {"calendar_bit", cup + 5},
                {"qualification", "classic-season-entrants@1"}, {"runtime_participants", true}}}});
    }
    table(out, "competitions", std::move(comps));
    auto calendars = items(out, "calendar");
    auto& calendar = *calendars.at(0).object();
    auto series = arr(calendar.at("series"));
    for (const auto& [cup, unused] : cups) {
        (void)unused;
        series.emplace_back(O{{"id", "classic-cup-" + std::to_string(cup)}, {"legacy_calendar_bit", cup + 5}});
    }
    calendar["series"] = std::move(series);
    calendar["initialization"] = O{{"policy", "classic-calendar@1"},
        {"inputs", A{"start_date", "season_index", "competition_options"}},
        {"source_anchor", "07-01"}, {"weekday_alignment", "classic-season-calendar@1"}};
    table(out, "calendar", std::move(calendars));
}
} // namespace

void complete_classic(Documents& out, const fs::path& root, const Database& db,
                      const LeImage& exe, const LeImage& source_tables, bool english, const Options& options) {
    using namespace detail;
    const auto mod = english ? "FLTD-classic" : "BMH-classic";
    if (!options.mod.empty() && options.mod != mod) fail("--mod does not match the source edition (expected " + std::string(mod) + ')');
    const auto desktop = required(root, "M3.MEN", 864);
    const auto formations = required(root, "M3.DAT", 1408);
    out["x-classic/desktop-seed.bin"] = binary(desktop);
    out["x-classic/formation-seed.bin"] = binary(formations);
    const auto source_text = launch::find_file(root, english ? "TEXTE.BLK" : "TEXTD.BLK");
    if (!source_text) fail("missing source text block");
    const auto raw_texts = read_file(*source_text);
    if (raw_texts.size() < 7) fail("truncated source text block");
    out["x-classic/text-blob.bin"] = std::string(raw_texts.begin() + 7, raw_texts.end());
    A text_header;
    for (size_t i = 0; i < 7; ++i) text_header.emplace_back(int(raw_texts[i]));

    const auto effective_regions = data_regions(out, exe, english, "effective");
    const auto original_regions = data_regions(out, source_tables, english, "source");
    put(out, "x-classic/startup.json", O{{"schema", "classic-startup@1.0"}, {"layout", layout_id},
        {"mod", mod}, {"english", english}, {"rule_profile", "openbmh-supported@1"},
        {"data_regions", effective_regions}, {"source_regions", original_regions},
        {"embedded_literals", literal_data(exe)}, {"source_literals", literal_data(source_tables)},
        {"desktop_seed", "x-classic/desktop-seed.bin"}, {"formation_seed", "x-classic/formation-seed.bin"},
        {"text_blob", "x-classic/text-blob.bin"},
        {"text_header", std::move(text_header)},
        {"initializers", initializers}, {"runtime_inputs", runtime_inputs},
        {"rng", O{{"import_consumes_draws", false}, {"game", "bmh-game@1"}, {"secondary", "watcom@1"}}}});

    auto manifest = parse(out, "manifest.json");
    auto& m = *manifest.object();
    m["id"] = std::string("world:") + mod;
    m["mod"] = mod;
    m["requires"] = A{feature};
    m["extensions"] = A{O{{"id", "x-legacy"}, {"version", "1.0"}}, O{{"id", "x-classic"}, {"version", "1.0"}}};
    m["x-import"] = O{{"profile", profile}, {"content_complete", true}, {"runtime", feature},
        {"rule_profile", "openbmh-supported@1"}, {"source_rules_preserved", true},
        {"default_attribute_set", "bmh-classic@1"}, {"default_portrait_set", "bmh-classic@1"}};
    put(out, "manifest.json", std::move(manifest));

    auto players = items(out, "players");
    A contracts;
    for (size_t i = 0; i < players.size(); ++i) {
        auto& player = *players[i].object();
        player["appearance"] = portrait_recipe(db.players.at(i).name, exe);
        player["photo"] = "asset:player/" + str(player.at("id")).substr(7) + "/photo";
        if (player.contains("team")) contracts.emplace_back(O{{"id", id("contract", i)}, {"person", player.at("id")},
            {"team", player.at("team")}, {"initialization", O{{"policy", "classic-player-contract@1"},
                {"source_identity_word", int64_t(db.players.at(i).w364)}, {"source_skill_word", int64_t(db.players.at(i).w368)}}}});
    }
    table(out, "players", std::move(players));
    table(out, "contracts", std::move(contracts));

    auto nations = items(out, "nations");
    auto clubs = items(out, "clubs");
    A finance;
    for (auto& club : clubs) {
        auto& row = *club.object();
        const auto team = size_t(num(get(row.at("x-legacy"), "team_index")));
        if (team >= 168 && team < 357) row["nation"] = get(nations.at(5 + (team - 168) / 7), "id");
        if (team >= 168) {
            constexpr int levels[] = {55, 61, 65, 67, 70, 73, 76, 80};
            const auto code = db.raw_foreign_club_strength.at(team - 168);
            row["classic_strength"] = O{{"band", int(code >> 5)}, {"base", levels[code >> 5]},
                {"style_flags", int(code & 31)}, {"policy", "classic-strength-only-team@1"}};
        }
        row["kit"] = O{{"set", "bmh-classic@1"}, {"selector", int(db.raw_club_kit_selectors.at(team))}};
        if (team < 138) {
            row["stadium_initialization"] = O{{"policy", "classic-stadium@1"}, {"name_pool", "stadiums"}};
            finance.emplace_back(O{{"id", id("finance", team)}, {"team", row.at("id")},
                {"initialization", O{{"policy", "classic-manager-finance@1"},
                    {"when", "human_manager_assigned"}, {"inputs", A{"game_level", "mode", "start_league"}}}}});
        }
    }
    table(out, "clubs", std::move(clubs));
    nations.erase(std::remove_if(nations.begin(), nations.end(), [](const J& nation) {
        return str(get(nation, "id")) == "nation:x-legacy-unassigned";
    }), nations.end());
    table(out, "nations", std::move(nations));
    table(out, "finance", std::move(finance));
    A staff;
    for (size_t i = 0; i < db.referees.size(); ++i) {
        if (i >= db.raw_referee_properties.size()) fail("referee property count mismatch");
        const auto property = db.raw_referee_properties[i];
        staff.emplace_back(O{{"id", id("staff", i)}, {"name", localized(db.referees[i])}, {"role", "referee"},
            {"attributes", O{{"strictness", int((property >> 3) & 3)}}},
            {"x-legacy", O{{"referee_index", int(i)}, {"property", int(property)}}}});
    }
    table(out, "staff", std::move(staff));
    auto rules = items(out, "rules");
    rules.emplace_back(O{{"id", "rule:classic-runtime"}, {"type", "classic_runtime"}, {"version", 1},
        {"startup", "x-classic/startup.json"}, {"layout", layout_id},
        {"calendar", "classic-calendar@1"}, {"qualifications", "classic-season-entrants@1"},
        {"financial_initialization", "classic-manager-finance@1"}, {"stadium_initialization", "classic-stadium@1"},
        {"contract_initialization", "classic-player-contract@1"}, {"staff_initialization", "classic-confirm-managers@1"},
        {"default_portrait_set", "bmh-classic@1"}});
    rules.emplace_back(O{{"id", "rule:classic-data-tables"}, {"type", "classic_data_regions"},
        {"layout", layout_id}, {"effective", effective_regions}, {"source", original_regions},
        {"coverage", A{"competition_configuration", "qualification_and_movement", "calendar", "player_generation",
            "contracts", "finance", "sponsors", "training", "stadiums", "transfer_market", "national_teams",
            "match_parameters", "embedded_texts", "interface_tables", "audio_tables"}}});
    table(out, "rules", std::move(rules));
    competitions(out, exe);
    import_assets(out, root, db, exe);

    auto report = parse(out, "x-legacy/import-report.json");
    auto& r = *report.object();
    r["profile"] = profile;
    r["unmapped"] = A{};
    auto warnings = arr(r.at("warnings"));
    warnings.erase(std::remove_if(warnings.begin(), warnings.end(), [](const J& warning) {
        return warning.string() && warning.string()->starts_with("foreign club nation assignments");
    }), warnings.end());
    r["warnings"] = std::move(warnings);
    r["coverage"] = O{{"core_entities", true}, {"all_classic_data_regions", true}, {"media", true},
        {"new_career_initialization", "runtime:classic-v1"}, {"savegame_conversion", false}};
    r["notes"] = A{"Complete installation content for the versioned Classic adapter, not a running career.",
        "Runtime-generated values are initialization policies, not fabricated initial contracts or stadiums.",
        "Generic competitions/rules are projections. Exact classic semantics use x-classic data and runtime:classic-v1.",
        "Both the supported effective tables and unpatched source data tables are retained; no executable code is exported.",
        "Player appearances are frozen from the original byte names; only portrait parts, never composed faces, are stored.",
        "No FBM, personal EIN, SOUND.CFG, high scores, or executable is copied.",
        "No 0.1 frontend reads OBW. The 0.2 runtime adapter is separate work."};
    put(out, "x-legacy/import-report.json", std::move(report));

    // All payload entries, including binary assets, are checked before publication/load.
    refresh_classic_inventory(out);
}

void refresh_classic_inventory(Documents& out) {
    using namespace detail;
    A inventory;
    for (const auto& [path, contents] : out)
        if (path != "x-classic/inventory.json")
            inventory.emplace_back(O{{"path", path}, {"bytes", int64_t(contents.size())}, {"crc32", int64_t(crc(contents))}});
    put(out, "x-classic/inventory.json", O{{"schema", "classic-inventory@1.0"}, {"entries", std::move(inventory)}});
}

static ClassicData decode_classic(const Documents& out) {
    using namespace detail;
    const auto startup = parse(out, "x-classic/startup.json");
    if (str(get(startup, "schema")) != "classic-startup@1.0" || str(get(startup, "layout")) != layout_id)
        fail("unsupported classic startup/layout version");
    ClassicData data;
    const auto* en = get(startup, "english").boolean();
    if (!en) fail("invalid edition flag");
    data.english = *en;
    data.executable = load_regions(out, get(startup, "data_regions"), get(startup, "embedded_literals"), data.english);
    data.source_tables = load_regions(out, get(startup, "source_regions"), get(startup, "source_literals"), data.english);
    const auto database = parse(out, "x-legacy/source-database.json");
    data.database = decode_database(unhex(str(get(database, "bytes"))));
    data.desktops = bytes(out.at(str(get(startup, "desktop_seed"))));
    data.formations = bytes(out.at(str(get(startup, "formation_seed"))));
    if (data.desktops.size() != 864 || data.formations.size() != 1408) fail("invalid startup seed sizes");
    data.texts.blob = bytes(out.at(str(get(startup, "text_blob"))));
    const auto& header = arr(get(startup, "text_header"));
    if (header.size() != 7) fail("invalid source text header");
    for (const auto& byte : header) {
        if (num(byte) < 0 || num(byte) > 255) fail("invalid source text header byte");
        data.text_header.push_back(uint8_t(num(byte)));
    }
    size_t position = 0;
    while (data.texts.strings.size() < TEXT_COUNT && position < data.texts.blob.size()) {
        const auto begin = position;
        while (position < data.texts.blob.size() && data.texts.blob[position]) ++position;
        if (position == data.texts.blob.size()) fail("unterminated classic text");
        data.texts.offsets.push_back(begin);
        data.texts.strings.emplace_back(data.texts.blob.begin() + begin, data.texts.blob.begin() + position);
        ++position;
    }
    if (data.texts.strings.size() != TEXT_COUNT) fail("incomplete classic texts");
    load_assets(out, data);
    return data;
}

void validate_classic(const Documents& out) {
    using namespace detail;
    const auto manifest = parse(out, "manifest.json");
    const auto& imported = get(manifest, "x-import");
    if (str(get(imported, "profile")) != profile || !get(imported, "content_complete").boolean() ||
        !*get(imported, "content_complete").boolean()) fail("incomplete classic profile");
    const auto& required_features = arr(get(manifest, "requires"));
    if (required_features.size() != 1 || str(required_features[0]) != feature) fail("missing classic runtime requirement");
    const auto startup = parse(out, "x-classic/startup.json");
    const auto expected = str(get(manifest, "provenance")) == "import:fltd" ? "FLTD-classic" : "BMH-classic";
    if (str(get(manifest, "mod")) != expected || str(get(startup, "mod")) != expected) fail("mod/edition mismatch");
    if (str(get(imported, "rule_profile")) != "openbmh-supported@1" ||
        str(get(startup, "rule_profile")) != "openbmh-supported@1" ||
        cli::dump_json(get(startup, "initializers")) != cli::dump_json(initializers) ||
        cli::dump_json(get(startup, "runtime_inputs")) != cli::dump_json(runtime_inputs)) fail("unsupported classic initialization contract");
    std::set<std::string> seen;
    const auto inventory = parse(out, "x-classic/inventory.json");
    for (const auto& entry : arr(get(inventory, "entries"))) {
        const auto path = str(get(entry, "path"));
        if (!safe_path(path) || path == "x-classic/inventory.json" || !seen.insert(path).second || !out.contains(path)) fail("invalid inventory entry: " + path);
        const auto& bytes = out.at(path);
        if (int64_t(bytes.size()) != num(get(entry, "bytes")) || int64_t(crc(bytes)) != num(get(entry, "crc32"))) fail("damaged entry: " + path);
        if (path.starts_with("state/")) fail("save state is forbidden in an installation mod");
        auto suffix = fs::path(path).extension().string();
        for (char& ch : suffix) ch = char(std::tolower(static_cast<unsigned char>(ch)));
        if (suffix == ".exe" || suffix == ".fbm" || suffix == ".obs" || suffix == ".ein" || suffix == ".cfg") fail("non-content file in classic world");
    }
    if (seen.size() + 1 != out.size()) fail("unlisted archive entries");
    auto data = decode_classic(out);
    if (data.english != (std::string(expected) == "FLTD-classic")) fail("startup edition mismatch");
    // Every fixture pointer must remain readable from the exported DATA view.
    const auto fixture_rules = items(out, "rules");
    const auto fixture = std::find_if(fixture_rules.begin(), fixture_rules.end(), [](const J& row) {
        return str(get(row, "id")) == "rule:legacy-fixtures";
    });
    if (fixture == fixture_rules.end()) fail("missing fixture generators");
    const std::map<int, uint32_t> bindings = data.english ?
        std::map<int, uint32_t>{{4, layout::EnglishFourTeamFixtureTable}, {6, layout::EnglishSixTeamFixtureTable},
            {12, layout::EnglishTwelveTeamFixtureTable}, {20, layout::EnglishTwentyTeamFixtureTable}} :
        std::map<int, uint32_t>{{4, layout::CareerSession_RuntimeError_RuntimeField}, {6, layout::CareerSession_RuntimeError_RuntimeField2},
            {12, layout::CareerSession_RuntimeError_RuntimeField3}, {16, layout::CareerSession_RuntimeError_ExecutableField3},
            {18, layout::CareerSession_RuntimeError_ExecutableField}, {20, layout::CareerSession_RuntimeError_ExecutableField2}};
    std::set<int> fixture_sizes;
    for (const auto& entry : arr(get(*fixture, "tables"))) {
        const int teams = int(num(get(entry, "teams")));
        if (!bindings.contains(teams) || !fixture_sizes.insert(teams).second) fail("invalid fixture generator size");
        if (str(get(entry, "id")) != "legacy:" + std::to_string(teams)) fail("invalid fixture generator identity");
        const auto& rounds = arr(get(entry, "rounds"));
        if (rounds.size() != size_t(teams - 1)) fail("invalid fixture round count");
        for (size_t round = 0; round < rounds.size(); ++round) {
            const auto row = data.executable.cstring(data.executable.u32(bindings.at(teams) + uint32_t(round + 1) * 4));
            if (row.size() != size_t(teams) || arr(rounds[round]).size() != size_t(teams / 2)) fail("incomplete fixture round data");
            std::set<char> seen;
            for (const char team : row)
                if (team < 'a' || team >= 'a' + teams || !seen.insert(team).second) fail("invalid fixture round participants");
            for (size_t match = 0; match < size_t(teams / 2); ++match) {
                const auto& pair = arr(arr(rounds[round])[match]);
                if (pair.size() != 2 || num(pair[0]) != row[match * 2] - 'a' || num(pair[1]) != row[match * 2 + 1] - 'a') fail("fixture projection disagrees with classic data");
            }
        }
    }
    if (fixture_sizes.size() != bindings.size()) fail("incomplete fixture generators");
    const auto portrait_set = parse(out, "assets/portrait-sets/bmh-classic@1/set.json");
    const auto& layers = arr(get(portrait_set, "layers"));
    if (layers.size() != 5 || arr(get(portrait_set, "backgrounds")).size() != 4) fail("invalid portrait set");
    std::set<std::string> assets;
    const auto index = parse(out, "assets/index.json");
    for (const auto& row : arr(get(index, "items"))) {
        const auto key = str(get(row, "key"));
        if (!key.starts_with("asset:") || !assets.insert(key).second || str(get(row, "license")) != "local-import-only") fail("invalid asset metadata");
        const auto* files = get(row, "files").object();
        if (!files || files->empty()) fail("missing asset files");
        for (const auto& [size, path] : *files) {
            (void)size;
            if (!out.contains(str(path))) fail("missing asset file: " + str(path));
        }
    }
    for (const auto& row : arr(get(portrait_set, "backgrounds")))
        if (!assets.contains(str(get(row, "asset")))) fail("missing portrait background asset");
    for (const auto& layer : layers) for (const auto& part : arr(get(layer, "parts")))
        if (!assets.contains(str(part))) fail("missing portrait part asset");
    const auto players = items(out, "players");
    if (players.size() != data.database.players.size()) fail("incomplete player projections");
    for (const auto& player : players) {
        const auto& appearance = get(player, "appearance");
        const auto background = num(get(appearance, "background"));
        const auto& parts = arr(get(appearance, "layers"));
        if (str(get(appearance, "set")) != "bmh-classic@1" || background < 0 || background > 3 || parts.size() != 5) fail("invalid frozen player appearance");
        for (size_t i = 0; i < 5; ++i)
            if (num(parts[i]) < 0 || num(parts[i]) >= int64_t(arr(get(layers[i], "parts")).size())) fail("portrait part index out of range");
    }
    const auto clubs = items(out, "clubs");
    for (const auto& club : clubs) if (!assets.contains(str(get(club, "crest")))) fail("missing club crest");
    if (items(out, "finance").size() != 138 || items(out, "staff").size() != data.database.referees.size()) fail("incomplete finance/staff seeds");
    std::map<std::string, std::string> owned;
    for (const auto& player : players)
        if (player.object()->contains("team")) owned.emplace(str(get(player, "id")), str(get(player, "team")));
    const auto contracts = items(out, "contracts");
    if (contracts.size() != owned.size()) fail("incomplete contract seeds");
    for (const auto& contract : contracts) {
        const auto who = str(get(contract, "person"));
        if (!owned.contains(who) || owned.at(who) != str(get(contract, "team"))) fail("contract does not match its player/team");
        owned.erase(who);
    }
    std::set<int> entries;
    size_t leagues = 0;
    for (const auto& competition : items(out, "competitions")) {
        if (str(get(competition, "type")) == "league") { ++leagues; continue; }
        const auto cup = num(get(get(competition, "classic"), "competition_id"));
        for (const auto& stage : arr(get(competition, "stages"))) {
            const auto entry = num(get(get(stage, "classic"), "entry"));
            if (entry < 0 || entry >= 36 || !entries.insert(int(entry)).second ||
                data.executable.u16(layout::CupTable + uint32_t(entry) * 0x28 + 0x18) != cup) fail("invalid classic cup stage binding");
        }
    }
    if (leagues != 9) fail("classic world requires nine league definitions");
    for (uint32_t entry = 0; entry < 36; ++entry) {
        const int cup = int(int16_t(data.executable.u16(layout::CupTable + entry * 0x28 + 0x18)));
        if (cup >= 0 && cup <= 26 && !entries.contains(int(entry))) fail("missing classic cup stage");
    }
}

ClassicData load_classic(const Documents& out) {
    validate(out);
    return decode_classic(out);
}
} // namespace bmh::obw
