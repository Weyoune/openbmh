// SPDX-License-Identifier: GPL-3.0-only
#include "import/obw.hpp"
#include "import/classic.hpp"
#include "import/detail.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <string_view>
#include <stdexcept>

#include "formats/executable_layout.hpp"
#include "launch/archive.hpp"
#include "launch/fltd.hpp"
#include "launch/game_data.hpp"
#include "miniz.h"
#include "version.hpp"

namespace fs = std::filesystem;
namespace bmh::obw {
namespace {
using J = cli::Json;
using A = J::Array;
using O = J::Object;

[[noreturn]] void fail(const std::string& message) { throw FormatError("OBW: " + message); }

std::string id(const std::string& type, size_t index) {
    std::ostringstream out;
    out << type << ':' << std::setfill('0') << std::setw(5) << index;
    return out.str();
}

std::string hex(const Bytes& bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        result += digits[byte >> 4];
        result += digits[byte & 15];
    }
    return result;
}

// The documented ISO 646-DE substitutions, with the installation's extended
// Latin-1 bytes represented as Unicode as well. Raw bytes are preserved separately.
std::string text(const std::string& raw) {
    std::string result;
    for (const unsigned char byte : raw) {
        if (byte >= 128) {
            result += char(0xc0 | (byte >> 6));
            result += char(0x80 | (byte & 63));
        } else {
            const char one[] = {char(byte), 0};
            result += iso646de_to_utf8(one, 1);
        }
    }
    return result;
}

bool named(const std::string& value) {
    return std::any_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch >= 128;
    });
}

J localized(const std::string& value) { return O{{"default", value}}; }
J table(const std::string& schema, A items) {
    return O{{"schema", schema + "@1.0"}, {"items", std::move(items)}};
}
void put(Documents& out, const std::string& path, J value) {
    out.emplace(path, cli::dump_json(value) + '\n');
}

int start_year(const std::string& date) {
    if (date.size() != 10 || date.substr(4) != "-07-01" ||
        !std::all_of(date.begin(), date.begin() + 4, [](unsigned char c) { return c >= '0' && c <= '9'; }))
        fail("start_date must be YYYY-07-01 (legacy season anchor)");
    const int year = std::stoi(date.substr(0, 4));
    if (year < 1900 || year > 9998) fail("start_date year must be between 1900 and 9998");
    return year;
}

A bytes_at(const LeImage& exe, uint32_t address, size_t count) {
    A result;
    for (size_t i = 0; i < count; ++i) result.emplace_back(int(exe.u8(address + uint32_t(i))));
    return result;
}

struct NationKey { std::string key, confederation; };
NationKey nation_key(const std::string& label, size_t index) {
    // Generic geographic aliases, never an edition-dependent positional inventory.
    // Unknown or renamed countries retain a private, documented source key.
    std::string upper = label;
    for (char& ch : upper) if (ch >= 'a' && ch <= 'z') ch = char(ch - 'a' + 'A');
    static const std::map<std::string, std::string> aliases = {
        {"DEUTSCHLAND", "de"}, {"GERMANY", "de"}, {"ENGLAND", "x-eng"},
        {"SPANIEN", "es"}, {"SPAIN", "es"}, {"FRANKREICH", "fr"}, {"FRANCE", "fr"},
        {"ITALIEN", "it"}, {"ITALY", "it"}, {"PORTUGAL", "pt"},
        {"NIEDERLANDE", "nl"}, {"HOLLAND", "nl"}, {"NETHERLANDS", "nl"},
        {"BELGIEN", "be"}, {"BELGIUM", "be"}, {"SCHOTTLAND", "x-sco"}, {"SCOTLAND", "x-sco"},
        {"WALES", "x-wal"}, {"N.IRELAND", "x-nir"}, {"NORTHERN IRELAND", "x-nir"},
        {"IRLAND", "ie"}, {"IRELAND", "ie"}, {"REP. OF IRELAND", "ie"},
        {"ÖSTERREICH", "at"}, {"AUSTRIA", "at"}, {"SCHWEIZ", "ch"}, {"SWITZERLAND", "ch"},
        {"DÄNEMARK", "dk"}, {"DENMARK", "dk"}, {"SCHWEDEN", "se"}, {"SWEDEN", "se"},
        {"NORWEGEN", "no"}, {"NORWAY", "no"}, {"FINNLAND", "fi"}, {"FINLAND", "fi"},
        {"POLEN", "pl"}, {"POLAND", "pl"}, {"TÜRKEI", "tr"}, {"TURKEY", "tr"},
        {"RUSSLAND", "ru"}, {"RUSSIA", "ru"}, {"UKRAINE", "ua"},
        {"TSCHECHIEN", "cz"}, {"TSCHECHEI", "cz"}, {"SLOWAKEI", "sk"}, {"SLOVAKIA", "sk"},
        {"UNGARN", "hu"}, {"HUNGARY", "hu"}, {"RUMÄNIEN", "ro"}, {"ROMANIA", "ro"},
        {"KROATIEN", "hr"}, {"CROATIA", "hr"}, {"BULGARIEN", "bg"}, {"BULGARIA", "bg"},
        {"GRIECHENLAND", "gr"}, {"GREECE", "gr"}, {"SERBIEN", "rs"}, {"SERBIA", "rs"},
        {"SLOWENIEN", "si"}, {"SLOVENIA", "si"}, {"BOSNIEN", "ba"}, {"BOSNIA", "ba"},
        {"KOSOVO", "x-kos"}, {"LITHUANIA", "lt"}, {"LUXEMBURG", "lu"},
        {"CYPRUS", "cy"}, {"MALTA", "mt"}, {"ISRAEL", "il"},
        {"BRASILIEN", "br"}, {"BRASIL", "br"}, {"BRAZIL", "br"},
        {"ARGENTINIEN", "ar"}, {"ARGENTINA", "ar"}, {"URUGUAY", "uy"}, {"PERU", "pe"},
        {"CHILE", "cl"}, {"KOLUMBIEN", "co"}, {"COLUMBIA", "co"}, {"COLOMBIA", "co"},
        {"BOLIVIEN", "bo"}, {"BOLIVIA", "bo"}, {"PARAGUAY", "py"},
        {"VENEZUELA", "ve"}, {"ECUADOR", "ec"},
        {"USA", "us"}, {"KANADA", "ca"}, {"CANADA", "ca"},
        {"MEXIKO", "mx"}, {"MEXICO", "mx"}, {"PANAMA", "pa"},
        {"AUSTRALIEN", "au"}, {"AUSTRALIA", "au"}, {"NEW ZEALAND", "nz"},
        {"MAROKKO", "ma"}, {"MOROCCO", "ma"}, {"KAMERUN", "cm"}, {"CAMEROON", "cm"},
        {"SENEGAL", "sn"}, {"NIGERIA", "ng"}, {"ALGERIEN", "dz"}, {"ALGERIA", "dz"},
        {"TUNESIEN", "tn"}, {"TUNISIA", "tn"}, {"ÄGYPTEN", "eg"}, {"EGYPT", "eg"},
        {"TOGO", "tg"}, {"GHANA", "gh"}, {"SÜDAFRIKA", "za"}, {"SÜDARFIKA", "za"},
        {"SOUTH AFRICA", "za"}, {"ELFENBEINKÜSTE", "ci"},
        {"IRAN", "ir"}, {"JAPAN", "jp"}, {"CHINA", "cn"},
        {"SÜDKOREA", "kr"}, {"SOUTH KOREA", "kr"}, {"SAUDI ARABIEN", "sa"}, {"SAUDI ARABIA", "sa"},
        {"INDONESIA", "id"}, {"LEBANON", "lb"}, {"IRAQ", "iq"}, {"KUWAIT", "kw"},
        {"JORDAN", "jo"}, {"JORADAN", "jo"}, {"SYRIA", "sy"}
    };
    const auto found = aliases.find(upper);
    if (found == aliases.end()) return {"x-legacy-" + std::to_string(index), "other"};
    const std::string& key = found->second;
    const std::set<std::string> south = {"br", "ar", "uy", "pe", "cl", "co", "bo", "py", "ve", "ec"};
    const std::set<std::string> north = {"us", "ca", "mx", "pa"};
    const std::set<std::string> africa = {"ma", "cm", "sn", "ng", "dz", "tn", "eg", "tg", "gh", "za", "ci"};
    const std::set<std::string> asia = {"au", "ir", "jp", "cn", "kr", "sa", "id", "lb", "iq", "kw", "jo", "sy"};
    return {key, south.contains(key) ? "conmebol" : north.contains(key) ? "concacaf" :
        africa.contains(key) ? "caf" : asia.contains(key) ? "afc" : key == "nz" ? "ofc" : "uefa"};
}

std::string po_quote(const std::string& value) {
    std::string result = "\"";
    for (unsigned char ch : value) {
        switch (ch) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (ch < 32 || ch == 127) {
                    result += '\\';
                    result += char('0' + (ch >> 6));
                    result += char('0' + ((ch >> 3) & 7));
                    result += char('0' + (ch & 7));
                } else result += char(ch);
        }
    }
    return result + '"';
}

const J& field(const J& value, std::string_view key, std::string_view path) {
    const auto* object = value.object();
    if (!object || !object->contains(key)) fail(std::string(path) + ": missing " + std::string(key));
    return object->find(key)->second;
}
std::string string_field(const J& value, const std::string& key, const std::string& path) {
    const auto* result = field(value, key, path).string();
    if (!result) fail(path + ": " + key + " must be a string");
    return *result;
}

} // namespace

Documents convert_database(const Database& db, const TextDb& texts, const LeImage& exe,
                           bool english, const Options& options) {
    start_year(options.start_date);
    if (db.club_names.size() != 419 || db.players.size() != 2574 || db.squad_list.size() < 2928 * 2)
        fail("unsupported database counts (expected 419 clubs, 2574 players and 2928 squad slots)");
    if (texts.strings.size() != TEXT_COUNT) fail("incomplete text database");
    const int expected[9] = {english ? 20 : 18, english ? 20 : 18, english ? 20 : 18,
        english ? 20 : 18, english ? 10 : 18, 12, 12, 12, 12};
    for (size_t i = 0; i < 9; ++i)
        if (exe.u8(layout::LeagueSizes + uint32_t(i)) != expected[i])
            fail("unsupported edition league sizes");

    Documents out;
    A warnings, nations, clubs, players, stadiums, competitions, rules, calendar;
    std::array<std::string, 62> nation_ids;
    std::set<std::string> used_nation_ids;
    const std::string language = english ? "en" : "de";
    for (size_t i = 0; i < nation_ids.size(); ++i) {
        const auto label = text(db.club_names.at(357 + i));
        auto key = nation_key(label, i);
        if (!used_nation_ids.insert(key.key).second) {
            key = {"x-legacy-" + std::to_string(i), "other"};
            used_nation_ids.insert(key.key);
        }
        if (key.key.starts_with("x-legacy-"))
            warnings.emplace_back("unresolved nation identity at legacy index " + std::to_string(i));
        nation_ids[i] = "nation:" + key.key;
        nations.emplace_back(O{{"id", nation_ids[i]}, {"name", localized(label)},
            {"confederation", key.confederation}, {"playable", i < 5},
            {"national_team", "nt:" + key.key}, {"x-legacy", O{{"nationality_index", int(i)}, {"team_index", int(357 + i)}}}});
    }
    nations.emplace_back(O{{"id", "nation:x-legacy-unassigned"},
        {"name", localized("Unassigned legacy nation")}, {"confederation", "other"}, {"playable", false}});

    std::vector<std::string> club_ids(419);
    int league_first = 0;
    std::array<std::string, 9> comp_ids;
    for (int league = 0; league < 9; ++league) {
        const int nation = league < 5 ? 0 : league - 4;
        const int tier = league < 5 ? (english ? league + 1 : (league < 3 ? league + 1 : league)) : 1;
        comp_ids[size_t(league)] = "comp:" + nation_ids[size_t(nation)].substr(7) + '-' + std::to_string(tier);
        if (!english && league == 2) comp_ids[2] += "-a";
        if (!english && league == 3) comp_ids[3] += "-b";
        for (int team = league_first; team < league_first + expected[league]; ++team)
            club_ids[size_t(team)] = id("club", size_t(team));
        league_first += expected[league];
    }
    for (size_t team = 138; team < 357; ++team)
        if (named(text(db.club_names[team]))) club_ids[team] = id("club", team);
    for (size_t i = 0; i < nation_ids.size(); ++i) club_ids[357 + i] = "nt:" + nation_ids[i].substr(7);

    for (size_t team = 0; team < club_ids.size(); ++team) {
        if (club_ids[team].empty()) continue;
        int league = -1, first = 0;
        for (int l = 0; l < 9; ++l) {
            if (int(team) >= first && int(team) < first + expected[l]) league = l;
            first += expected[l];
        }
        const std::string type = team >= 357 ? "national" : team >= 168 ? "foreign" : team >= 138 ? "amateur" : "club";
        const std::string nation = team >= 357 ? nation_ids[team - 357] : team >= 168 ?
            "nation:x-legacy-unassigned" : nation_ids[league >= 5 ? size_t(league - 4) : 0];
        O row{{"id", club_ids[team]}, {"type", type}, {"name", localized(text(db.club_names[team]))}, {"nation", nation},
            {"x-legacy", O{{"team_index", int(team)}, {"badge_word", int(le16(db.raw_club_badge_selectors, team * 2))},
                {"kit_selector", int(db.raw_club_kit_selectors.at(team))}}}};
        // This byte encodes a strength band and style flags, not a 0..99 rating.
        if (team >= 168) row.at("x-legacy").object()->emplace("strength_code", int(db.raw_foreign_club_strength.at(team - 168)));
        clubs.emplace_back(std::move(row));
    }

    std::vector<int> player_team(db.players.size(), -1);
    std::vector<int> player_row(db.players.size(), -1);
    for (int team = 0; team < 138; ++team) {
        const size_t first = team < 90 ? size_t(team) * 24 : size_t(team) * 16 + 720;
        const size_t count = team < 90 ? 24 : 16;
        for (size_t row = 0; row < count; ++row) {
            const auto word = le16(db.squad_list, (first + row) * 2);
            const size_t player = word & 4095;
            if (player == 4095) continue;
            if (player >= db.players.size()) fail("squad slot references an unknown player");
            if (player_team[player] >= 0) fail("player occurs in more than one squad slot");
            player_team[player] = team;
            player_row[player] = int(row);
        }
    }
    constexpr const char* positions[] = {"gk", "def", "mid", "fwd"};
    constexpr const char* skills[] = {"teamplay", "speed", "shooting", "passing", "tackling", "free_kick", "corner"};
    for (size_t i = 0; i < db.players.size(); ++i) {
        const auto& player = db.players[i];
        int nationality = player.nationality() + ((db.raw_player_bonus_values.at(i) & 128) ? 32 : 0);
        // Import the source record, not career initialization's nationality
        // rewrites and random ratings. The original words remain in x-legacy.
        if (nationality < 0 || size_t(nationality) >= nation_ids.size())
            fail("player " + std::to_string(i) + " nationality " + std::to_string(nationality) + " is outside the nation table");
        O attributes{{"form", player.form()}, {"technique", player.technique()}, {"condition", player.condition()},
            {"strength", player.strength()}, {"bonus", int(db.raw_player_bonus_values.at(i) & 127)}};
        for (int skill = 0; skill < 7; ++skill) attributes[skills[skill]] = player.skill(Player::Skill(skill));
        O row{{"id", id("player", i)}, {"name", O{{"first", ""}, {"last", text(player.name)}}},
            {"age_at_start", O{{"years", player.age()}, {"on", options.start_date}}},
            {"nationalities", A{nation_ids[size_t(nationality)]}}, {"positions", A{positions[player.position()]}},
            {"attribute_set", "bmh-classic@1"}, {"attributes", std::move(attributes)},
            {"x-legacy", O{{"player_index", int(i)}, {"condition_technique_form_word", J(int64_t(player.w35c))},
                {"skill_word", J(int64_t(player.w368))}, {"identity_word", J(int64_t(player.w364))},
                {"squad_row", player_row[i]}}}};
        if (player_team[i] >= 0) row["team"] = club_ids[size_t(player_team[i])];
        players.emplace_back(std::move(row));
    }
    for (size_t i = 0; i < db.stadiums.size(); ++i)
        stadiums.emplace_back(O{{"id", id("stadium", i)}, {"name", localized(text(db.stadiums[i]))},
            {"x-legacy", O{{"name_pool_index", int(i)}}}});

    A fixture_tables;
    const std::vector<std::pair<int, uint32_t>> bindings = english ?
        std::vector<std::pair<int, uint32_t>>{{4, layout::EnglishFourTeamFixtureTable}, {6, layout::EnglishSixTeamFixtureTable},
            {12, layout::EnglishTwelveTeamFixtureTable}, {20, layout::EnglishTwentyTeamFixtureTable}} :
        std::vector<std::pair<int, uint32_t>>{{4, layout::CareerSession_RuntimeError_RuntimeField},
            {6, layout::CareerSession_RuntimeError_RuntimeField2}, {12, layout::CareerSession_RuntimeError_RuntimeField3},
            {16, layout::CareerSession_RuntimeError_ExecutableField3}, {18, layout::CareerSession_RuntimeError_ExecutableField},
            {20, layout::CareerSession_RuntimeError_ExecutableField2}};
    for (const auto& [teams, address] : bindings) {
        A rounds;
        for (int round = 1; round < teams; ++round) {
            const auto source = exe.cstring(exe.u32(address + uint32_t(round * 4)));
            if (source.size() != size_t(teams)) fail("fixture table has an invalid round length");
            std::set<char> seen;
            A pairs;
            for (const char ch : source)
                if (ch < 'a' || ch >= 'a' + teams || !seen.insert(ch).second) fail("fixture round has an invalid team");
            for (size_t i = 0; i < source.size(); i += 2) pairs.emplace_back(A{int(source[i] - 'a'), int(source[i + 1] - 'a')});
            rounds.emplace_back(std::move(pairs));
        }
        fixture_tables.emplace_back(O{{"id", "legacy:" + std::to_string(teams)}, {"teams", teams}, {"rounds", std::move(rounds)}});
    }

    A series;
    int first = 0, label_index = 0;
    for (size_t league = 0; league < 9; ++league) {
        const int teams = expected[league];
        A members;
        for (int t = first; t < first + teams; ++t) members.emplace_back(club_ids[size_t(t)]);
        const auto bit = exe.u8(layout::LeagueCalendarBits + uint32_t(league));
        const bool played = !(english && league == 4);
        const auto series_id = "legacy-league-" + std::to_string(league);
        std::string label;
        if (league < 5) {
            const auto label_slot = (layout::Achievements_FormatError_TextPointer3 - layout::TextPointerTable) / 4 + uint32_t(label_index);
            label = text(texts.strings.at(label_slot));
            const char suffix = char(exe.u8(layout::CareerSession_InstallLeagueSizes_RuntimeField3 + uint32_t(league * 2)));
            if (suffix) label += std::string(" ") + suffix;
            if (league == 4 || exe.u8(layout::CareerSession_InstallLeagueSizes_RuntimeField3 + uint32_t((league + 1) * 2)) < 'B') ++label_index;
        } else label = text(db.club_names[357 + league - 4]);
        O stage{{"id", "regular"}, {"format", "league"}, {"teams", teams}, {"legs", 2},
            {"generator", "legacy:" + std::to_string(teams)}, {"slots", A{O{{"teams", std::move(members)}}}}};
        // The unplayed English pool has no ten-team legacy fixture generator.
        if (played) stage["matchdays"] = "calendar:" + series_id;
        else stage.erase("generator");
        const auto nation = league < 5 ? nation_ids[0] : nation_ids[league - 4];
        const int tier = league < 5 ? (english ? int(league + 1) : (league < 3 ? int(league + 1) : int(league))) : 1;
        competitions.emplace_back(O{{"id", comp_ids[league]}, {"name", localized(label)}, {"type", "league"},
            {"trophy", "asset:comp/" + comp_ids[league].substr(5) + "/trophy"},
            {"nation", nation}, {"tier", tier}, {"played", played}, {"stages", A{std::move(stage)}},
            {"x-legacy", O{{"league_index", int(league)}, {"calendar_bit", int(bit)},
                {"relegation_places", int(exe.u8(layout::CareerSession_InstallLeagueSizes_RuntimeField2 + uint32_t(league)))},
                {"europe_places", int(exe.u8(layout::CareerSession_InstallLeagueSizes_RuntimeField + uint32_t(league)))}}}});
        if (played) series.emplace_back(O{{"id", series_id}, {"legacy_calendar_bit", int(bit)}});
        first += teams;
    }

    O attribute_fields;
    for (const char* attribute : {"form", "technique", "condition"})
        attribute_fields[attribute] = O{{"min", 0}, {"max", 127}};
    attribute_fields["strength"] = O{{"min", 0}, {"max", 99}, {"derived", true}};
    attribute_fields["bonus"] = O{{"min", 0}, {"max", 127}};
    for (const char* skill : skills) attribute_fields[skill] = O{{"min", 0}, {"max", 3}};
    rules.emplace_back(O{{"id", "bmh-classic@1"}, {"type", "attribute_set"}, {"fields", std::move(attribute_fields)}});
    rules.emplace_back(O{{"id", "rule:legacy-fixtures"}, {"type", "fixture_tables"}, {"tables", std::move(fixture_tables)}});
    rules.emplace_back(O{{"id", "rule:legacy-ladder"}, {"type", "legacy_ladder"},
        {"first_league", bytes_at(exe, layout::CalendarFirstLeague, 8)},
        {"league_count", bytes_at(exe, layout::CalendarLeagueCounts, 8)},
        {"below", bytes_at(exe, layout::CareerSession_InstallLeagueSizes_RuntimeField5, 8)},
        {"below_count", bytes_at(exe, layout::CareerSession_InstallLeagueSizes_RuntimeField4, 8)},
        {"newcomer_first", int(exe.u16(layout::CareerSession_DispatchLineup_RuntimeField))},
        {"newcomer_count", int(exe.u16(layout::CareerSession_InstallLeagueSizes_RuntimeField6))}});
    A resorts;
    for (uint32_t resort = 0; resort < 8; ++resort)
        resorts.emplace_back(O{{"id", "resort:" + std::to_string(resort)},
            {"base_price", J(int64_t(int32_t(exe.u32(layout::DeskActions_ConfigureTrainingCampData_RuntimeField2 + resort * 4))))},
            {"quality", bytes_at(exe, layout::DeskActions_ConfigureTrainingCampData_RuntimeField + resort * 4, 4)}});
    rules.emplace_back(O{{"id", "rule:training-camps"}, {"type", "training_camps"}, {"resorts", std::move(resorts)}});
    // Retain the floating-point divisor as IEEE-754 bytes, not a rounded money value.
    rules.emplace_back(O{{"id", "rule:credit-rates"}, {"type", "credit_rates"},
        {"factors", bytes_at(exe, layout::BankCredit_BankCreditContext_RuntimeField3, 49)},
        {"x-legacy-divisor-low", J(int64_t(exe.u32(layout::BankCredit_BankCreditContext_RuntimeField)))},
        {"x-legacy-divisor-high", J(int64_t(exe.u32(layout::BankCredit_BankCreditContext_RuntimeField2)))}});
    A day_words;
    for (uint32_t day = 0; day < 366; ++day) day_words.emplace_back(int(exe.u16(layout::CompetitionCalendar + day * 2)));
    calendar.emplace_back(O{{"id", "calendar:legacy-season"}, {"season_start", "07-01"},
        {"series", std::move(series)}, {"x-legacy", O{{"day_words", std::move(day_words)},
        {"note", "Source day offsets, not final dated fixtures; career setup applies competition and weekday options."}}}});

    const auto currency_suffix = exe.cstring(layout::CurrencySuffixText);
    const std::string currency = english ? "GBP" : currency_suffix == " DM" ? "DEM" : currency_suffix == " EU" ? "EUR" : "X-LEGACY";
    put(out, "manifest.json", O{{"format", "openbmh-world"}, {"format_version", "1.0"}, {"kind", "world"},
        {"min_reader", "1.0"}, {"generator", std::string("OpenBMH importer ") + kOpenBmhVersion},
        {"title", localized(options.title)}, {"provenance", english ? "import:fltd" : "import:bmh-de"},
        {"content_license", "local-import-only"}, {"currency", O{{"code", currency}, {"minor_units", 0}, {"x-legacy-suffix", text(currency_suffix)}}},
        {"start_date", options.start_date}, {"languages", A{language}}, {"limits", O{{"leagues", 32}}},
        {"requires", A{"import:legacy-database-v1"}}, {"extensions", A{O{{"id", "x-legacy"}, {"version", "1.0"}}}},
        {"x-import", O{{"profile", "legacy-database-v1"}, {"playable", false},
            {"rule_profile", "openbmh-supported"}, {"default_attribute_set", "bmh-classic@1"}}}});
    put(out, "world/nations.json", table("nations", std::move(nations)));
    put(out, "world/clubs.json", table("clubs", std::move(clubs)));
    put(out, "world/players.json", table("players", std::move(players)));
    put(out, "world/stadiums.json", table("stadiums", std::move(stadiums)));
    for (const char* empty : {"staff", "contracts", "finance"})
        put(out, std::string("world/") + empty + ".json", table(empty, {}));
    put(out, "world/competitions.json", table("competitions", std::move(competitions)));
    put(out, "world/rules.json", table("rules", std::move(rules)));
    put(out, "world/calendar.json", table("calendar", std::move(calendar)));

    O raw_tables;
    const std::pair<const char*, const Bytes*> raw[] = {
        {"player_bonus_values", &db.raw_player_bonus_values}, {"database_metadata", &db.raw_database_metadata},
        {"club_auxiliary_words", &db.raw_club_auxiliary_words}, {"club_badge_selectors", &db.raw_club_badge_selectors},
        {"foreign_club_strength", &db.raw_foreign_club_strength}, {"league_auxiliary_bytes", &db.raw_league_auxiliary_bytes},
        {"referee_properties", &db.raw_referee_properties}, {"club_kit_selectors", &db.raw_club_kit_selectors},
        {"club_short_name_offsets", &db.raw_club_short_name_offsets}, {"club_name_offsets", &db.raw_club_name_offsets},
        {"player_name_offsets", &db.raw_player_name_offsets}, {"squad_list", &db.squad_list}};
    for (const auto& [name, data] : raw) raw_tables[name] = hex(*data);
    A pools;
    for (const auto& pool : db.pools) pools.emplace_back(hex(pool));
    put(out, "x-legacy/database.json", O{{"schema", "legacy-database@1.0"}, {"encoding", "hex"},
        {"counts", O{{"clubs", 419}, {"players", 2574}, {"n42", 168}, {"n66", 3096}}},
        {"pools", std::move(pools)}, {"tables", std::move(raw_tables)}});
    A original_texts;
    std::string po = "# Local installation import; not licensed for redistribution.\nmsgid \"\"\nmsgstr \"\"\n"
        "\"Content-Type: text/plain; charset=UTF-8\\n\"\n\"Language: " + language + "\\n\"\n";
    for (size_t i = 0; i < texts.strings.size(); ++i) {
        original_texts.emplace_back(O{{"index", int(i)}, {"text", text(texts.strings[i])},
            {"raw_hex", hex(Bytes(texts.strings[i].begin(), texts.strings[i].end()))}});
        po += "\nmsgctxt \"legacy-text\"\nmsgid \"legacy.text." + std::to_string(i) + "\"\nmsgstr " + po_quote(text(texts.strings[i])) + '\n';
    }
    put(out, "x-legacy/texts.json", table("legacy-texts", std::move(original_texts)));
    out["locale/" + language + "-original/openbmh.po"] = std::move(po);
    warnings.emplace_back("foreign club nation assignments remain unresolved outside the four simulated foreign leagues");
    put(out, "x-legacy/import-report.json", O{{"schema", "legacy-import-report@1.0"},
        {"profile", "legacy-database-v1"}, {"warnings", std::move(warnings)},
        {"unmapped", A{"staff and referee roles", "stadium capacities/owners", "contracts and finances",
            "cup definitions and qualification", "dated calendar/competition options", "complete executable rule tables",
            "foreign strength-band interpretation", "career initialization transforms", "graphics/assets"}},
        {"notes", A{"Database attributes are seeds: legacy career initialization varies player ratings.",
            "No career was started and no random numbers were consumed.",
            "Executable tables use the existing OpenBMH loader and its supported rule profile, not an unmodified executable dump.",
            "Source ages are retained without inventing exact birthdays; reference date is supplied by the user."}}});
    validate(out);
    return out;
}

Documents import_installation(const fs::path& source, const Options& options) {
    if (!fs::exists(source)) fail("source does not exist");
    if (!fs::is_directory(source)) {
        auto filename = launch::path_to_utf8(source.filename());
        for (char& ch : filename) ch = char(std::toupper(static_cast<unsigned char>(ch)));
        if (filename != "BMH.EXE" && filename != "FLTD.EXE") fail("source must be a directory, BMH.EXE or FLTD.EXE; FBM conversion is not implemented");
    }
    fs::path root = fs::is_directory(source) ? source : source.parent_path();
    auto german = launch::find_file(root, "BMH.EXE"), english_exe = launch::find_file(root, "FLTD.EXE");
    if (!german && !english_exe && fs::is_directory(root)) {
        std::vector<fs::path> candidates;
        for (const auto& entry : fs::directory_iterator(root))
            if (entry.is_directory() && (launch::find_file(entry.path(), "BMH.EXE") || launch::find_file(entry.path(), "FLTD.EXE")))
                candidates.push_back(entry.path());
        if (candidates.size() != 1) fail("expected exactly one BMH.EXE or FLTD.EXE installation");
        root = candidates[0];
        german = launch::find_file(root, "BMH.EXE");
        english_exe = launch::find_file(root, "FLTD.EXE");
    }
    if (!german && !english_exe) fail("no BMH.EXE or FLTD.EXE found");
    bool english = bool(english_exe) && !german;
    if (german && english_exe) {
        if (fs::is_directory(source)) fail("ambiguous installation: name BMH.EXE or FLTD.EXE explicitly");
        if (fs::equivalent(source, *english_exe)) english = true;
        else if (!fs::equivalent(source, *german)) fail("name BMH.EXE or FLTD.EXE explicitly");
    }
    const auto required = [&](const std::string& name) {
        const auto file = launch::find_file(root, name);
        if (!file || !fs::is_regular_file(*file)) fail("missing " + name);
        return *file;
    };
    const auto exe = english ? launch::translate_fltd(*english_exe) : load_le(*german);
    const std::string expected_mod = english ? "FLTD-classic" : "BMH-classic";
    if (!options.mod.empty() && options.mod != expected_mod) fail("--mod must be " + expected_mod + " for this installation");
    if (!options.database_only) {
        const auto checked = launch::check_game_directory(english ? *english_exe : *german);
        if (!checked.problems.empty()) {
            std::string problem = "incomplete installation";
            for (const auto& message : checked.problems) problem += "\n- " + message;
            fail(problem + "\nUse --database-only only for an explicitly partial development export.");
        }
    }
    const auto db_path = required(english ? "M3E.MAN" : "M3D.MAN");
    const auto text_path = required(english ? "TEXTE.BLK" : "TEXTD.BLK");
    const auto database = load_database(db_path);
    auto out = convert_database(database, load_textd(text_path), exe, english, options);
    // Exact source database for lossless future re-mapping, not an executable or a
    // runnable legacy installation. No absolute developer paths enter the archive.
    put(out, "x-legacy/source-database.json", O{{"schema", "legacy-source@1.0"},
        {"name", english ? "M3E.MAN" : "M3D.MAN"}, {"encoding", "hex"}, {"bytes", hex(read_file(db_path))}});
    if (!options.database_only) {
        const auto source_tables = english ? launch::translate_fltd(*english_exe, false) : load_le(*german, false);
        complete_classic(out, root, database, exe, source_tables, english, options);
    }
    validate(out);
    return out;
}

void validate(const Documents& documents) {
    const auto parse = [&](const std::string& path) {
        const auto found = documents.find(path);
        if (found == documents.end()) fail(path + ": missing document");
        return cli::parse_json(found->second);
    };
    const auto manifest = parse("manifest.json");
    for (const auto& [key, expected] : std::vector<std::pair<std::string, std::string>>{
        {"format", "openbmh-world"}, {"format_version", "1.0"}, {"kind", "world"}, {"content_license", "local-import-only"}})
        if (string_field(manifest, key, "manifest.json") != expected) fail("manifest.json: unsupported " + key);
    start_year(string_field(manifest, "start_date", "manifest.json"));
    uint64_t total = 0;
    if (documents.size() > 8192) fail("world has too many entries");
    for (const auto& [path, contents] : documents) {
        total += contents.size();
        if (contents.size() > 32 * 1024 * 1024 || total > 128ull * 1024 * 1024) fail("world exceeds import size limits");
        if (!detail::safe_path(path)) fail("unsafe archive path: " + path);
    }
    const auto& features = field(manifest, "requires", "manifest.json");
    const auto& profile = field(manifest, "x-import", "manifest.json");
    const bool classic = string_field(profile, "profile", "manifest.json:x-import") == "classic-installation-v1";
    if (classic) validate_classic(documents);
    else {
        if (!features.array() || features.array()->size() != 1 || !features.array()->front().string() ||
            *features.array()->front().string() != "import:legacy-database-v1") fail("manifest.json: missing conversion-only feature guard");
        const auto* playable = field(profile, "playable", "manifest.json:x-import").boolean();
        if (!playable || *playable || string_field(profile, "profile", "manifest.json:x-import") != "legacy-database-v1")
            fail("manifest.json: conversion profile must remain non-playable");
    }

    std::map<std::string, std::string> ids;
    struct Reference { std::string value, prefix, path; };
    std::vector<Reference> references;
    for (const char* name : {"nations", "clubs", "players", "staff", "stadiums", "contracts", "finance", "competitions", "rules", "calendar"}) {
        const std::string path = std::string("world/") + name + ".json";
        const auto data = parse(path);
        if (string_field(data, "schema", path) != std::string(name) + "@1.0") fail(path + ": unsupported schema");
        const auto* items = field(data, "items", path).array();
        if (!items) fail(path + ": items must be an array");
        for (size_t i = 0; i < items->size(); ++i) {
            const auto& item = items->at(i);
            const auto at = path + ":items[" + std::to_string(i) + ']';
            const auto entity_id = string_field(item, "id", at);
            if (!ids.emplace(entity_id, name).second) fail(at + ": duplicate id " + entity_id);
            const std::map<std::string, std::string> prefixes{{"nations", "nation:"}, {"players", "player:"},
                {"staff", "staff:"}, {"stadiums", "stadium:"}, {"contracts", "contract:"},
                {"finance", "finance:"}, {"competitions", "comp:"}, {"calendar", "calendar:"}};
            if (const auto prefix = prefixes.find(name); prefix != prefixes.end() && !entity_id.starts_with(prefix->second))
                fail(at + ": wrong id type " + entity_id);
            if (std::string(name) == "clubs" && !entity_id.starts_with("club:") && !entity_id.starts_with("nt:"))
                fail(at + ": wrong team id type " + entity_id);
            const auto ref = [&](const std::string& key, const std::string& prefix) {
                if (item.object()->contains(key)) references.push_back({string_field(item, key, at), prefix, at + '.' + key});
            };
            ref("nation", "nation:"); ref("team", "club:"); ref("stadium", "stadium:"); ref("national_team", "nt:");
            if (std::string(name) == "contracts") ref("person", "player:");
            if (std::string(name) == "players") {
                const auto* nats = field(item, "nationalities", at).array();
                if (!nats || nats->empty()) fail(at + ": nationalities must not be empty");
                for (const auto& nat : *nats) {
                    if (!nat.string()) fail(at + ": invalid nationality");
                    references.push_back({*nat.string(), "nation:", at + ".nationalities"});
                }
                if (string_field(item, "attribute_set", at) != "bmh-classic@1") fail(at + ": invalid attribute set");
                const auto& attributes = field(item, "attributes", at);
                if (!attributes.object()) fail(at + ": attributes must be an object");
                for (const auto& [attribute, value] : *attributes.object()) {
                    const auto* number = value.integer();
                    const int maximum = attribute == "strength" ? 99 :
                        (attribute == "form" || attribute == "technique" || attribute == "condition" || attribute == "bonus") ? 127 : 3;
                    if (!number || *number < 0 || *number > maximum) fail(at + ".attributes." + attribute + ": outside classic scale");
                }
            }
            if (std::string(name) == "competitions") {
                // Presentation references may be supplied by a later content pack.
                // Missing assets use the documented generic trophy fallback.
                if (item.object()->contains("trophy")) {
                    const auto key = string_field(item, "trophy", at);
                    constexpr std::string_view prefix = "asset:comp/", suffix = "/trophy";
                    if (!key.starts_with(prefix) || !key.ends_with(suffix) ||
                        key.size() <= prefix.size() + suffix.size()) fail(at + ": invalid trophy asset key");
                    const auto name = key.substr(prefix.size(), key.size() - prefix.size() - suffix.size());
                    if (!std::all_of(name.begin(), name.end(), [](unsigned char c) {
                            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                   (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
                        }) || name == "." || name == "..") fail(at + ": invalid trophy asset key");
                }
                const auto* stages = field(item, "stages", at).array();
                if (!stages) fail(at + ": stages must be an array");
                for (const auto& stage : *stages) {
                    const auto* slots = field(stage, "slots", at).array();
                    if (!slots) fail(at + ": slots must be an array");
                    size_t participants = 0;
                    std::set<std::string> unique_teams;
                    for (const auto& slot : *slots) {
                        if (classic && slot.object() && slot.object()->contains("resolver")) {
                            if (string_field(slot, "resolver", at) != "classic-entry@1") fail(at + ": unknown slot resolver");
                            const auto* count = field(slot, "count", at).integer();
                            if (!count || *count < 0 || *count > 255) fail(at + ": invalid classic slot count");
                            participants += size_t(*count);
                            continue;
                        }
                        const auto* teams = field(slot, "teams", at).array();
                        if (!teams) fail(at + ": slot teams must be an array");
                        for (const auto& team : *teams) {
                            if (!team.string()) fail(at + ": invalid slot team");
                            if (!unique_teams.insert(*team.string()).second) fail(at + ": duplicate stage participant");
                            references.push_back({*team.string(), "club:", at + ".stages.slots"});
                        }
                        participants += teams->size();
                    }
                    const auto* count = field(stage, "teams", at).integer();
                    if (!count || *count != int64_t(participants)) fail(at + ": stage team count disagrees with slots");
                }
            }
        }
    }
    for (const auto& ref : references)
        if (!ids.contains(ref.value) || !ref.value.starts_with(ref.prefix)) fail(ref.path + ": unknown or wrong-type id " + ref.value);
}

Documents read(const fs::path& source) {
    Documents result;
    uint64_t total = 0;
    const auto add = [&](const std::string& name, const Bytes& bytes) {
        if (!detail::safe_path(name)) fail("unsafe archive entry: " + name);
        total += bytes.size();
        if (total > 128ull * 1024 * 1024 || result.size() >= 8192) fail("world exceeds import size limits");
        if (!result.emplace(name, detail::binary(bytes)).second) fail("duplicate archive entry: " + name);
    };
    if (fs::is_directory(source)) {
        for (const auto& entry : fs::recursive_directory_iterator(source)) {
            if (entry.is_symlink()) fail("symlinks are forbidden in an unpacked world");
            if (!entry.is_regular_file()) continue;
            if (entry.file_size() > 32 * 1024 * 1024 || total + entry.file_size() > 128ull * 1024 * 1024) fail("world exceeds import size limits");
            const auto name = entry.path().lexically_relative(source).generic_u8string();
            add(std::string(name.begin(), name.end()), read_file(entry.path()));
        }
    } else {
        if (fs::file_size(source) > 128ull * 1024 * 1024) fail("world archive is too large");
        auto archive = launch::Archive::open(source);
        for (size_t i = 0; i < archive.entries().size(); ++i) {
            const auto& entry = archive.entries()[i];
            if (entry.directory) continue;
            if (entry.size > 32 * 1024 * 1024 || total + entry.size > 128ull * 1024 * 1024) fail("world exceeds import size limits");
            add(entry.name, archive.read(i));
        }
    }
    validate(result);
    return result;
}

void write(const Documents& documents, const fs::path& destination, bool directory) {
    validate(documents);
    if (fs::symlink_status(destination).type() != fs::file_type::not_found) fail("destination already exists");
    const auto parent = destination.has_parent_path() ? destination.parent_path() : fs::path(".");
    if (!fs::is_directory(parent)) fail("output parent directory does not exist");
    fs::path staging;
    for (int attempt = 0; attempt < 100; ++attempt) {
        staging = parent / (".openbmh-import-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + '-' + std::to_string(attempt));
        if (fs::create_directory(staging)) break;
        staging.clear();
    }
    if (staging.empty()) fail("cannot reserve output staging directory");
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); }
    } cleanup{staging};
    mz_zip_archive zip{};
    if (!directory && !mz_zip_writer_init_heap(&zip, 0, 0)) fail("cannot initialize ZIP writer");
    struct ZipCleanup { mz_zip_archive* zip; ~ZipCleanup() { if (zip) mz_zip_writer_end(zip); } } zip_cleanup{directory ? nullptr : &zip};
    for (const auto& [name, contents] : documents) {
        const fs::path relative = launch::path_from_utf8(name);
        if (relative.empty() || relative.is_absolute() || relative.has_root_name() || name.find('\\') != std::string::npos)
            fail("unsafe document path");
        for (const auto& part : relative) if (part == ".." || part == ".") fail("unsafe document path");
        if (directory) {
            const auto file = staging / relative;
            fs::create_directories(file.parent_path());
            std::ofstream stream(file, std::ios::binary);
            stream.write(contents.data(), std::streamsize(contents.size()));
            stream.close();
            if (!stream) fail("cannot write document " + name);
        } else if (!mz_zip_writer_add_mem(&zip, name.c_str(), contents.data(), contents.size(), MZ_BEST_COMPRESSION))
            fail("cannot add ZIP document " + name);
    }
    if (!directory) {
        void* buffer = nullptr;
        size_t size = 0;
        if (!mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size)) fail("cannot finish ZIP archive");
        struct Free { void* value; ~Free() { mz_free(value); } } free{buffer};
        std::ofstream stream(staging / "world.obw", std::ios::binary);
        stream.write(static_cast<const char*>(buffer), std::streamsize(size));
        stream.close();
        if (!stream) fail("cannot write ZIP archive");
    }
    if (fs::symlink_status(destination).type() != fs::file_type::not_found) fail("destination appeared during conversion");
    fs::rename(directory ? staging : staging / "world.obw", destination);
}

} // namespace bmh::obw
