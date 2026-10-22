// SPDX-License-Identifier: GPL-3.0-only
#include "content/save.hpp"
#include "import/detail.hpp"
#include "game/save.hpp"
#include <limits>
#include <algorithm>
#include <bit>
namespace bmh::content {
using namespace obw::detail;
namespace {
J rng(uint32_t seed, uint64_t calls) { return O{{"seed", int64_t(seed)}, {"calls", std::to_string(calls)}}; }
uint64_t calls(const J &value) {
    const auto s = str(get(value, "calls"));
    size_t end = 0;
    uint64_t n;
    try {
        n = std::stoull(s, &end);
    } catch (...) {
        fail("invalid random draw counter");
    }
    if (end != s.size() || s.empty() ||
        !std::all_of(s.begin(), s.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }))
        fail("invalid random draw counter");
    return n;
}
uint32_t seed(const J &value) {
    const auto n = num(get(value, "seed"));
    if (n < 0 || n > UINT32_MAX)
        fail("invalid random seed");
    return uint32_t(n);
}
Documents world_of(const Documents &docs) {
    Documents world;
    for (const auto &[name, value] : docs)
        if (!name.starts_with("state/"))
            world[name] = value;
    world["manifest.json"] = docs.at("state/world-manifest.json");
    return world;
}
} // namespace
namespace {
template <class T> J vector_json(const std::vector<T> &values) {
    A rows;
    for (auto value : values)
        rows.emplace_back(int64_t(value));
    return rows;
}
template <class T> void vector_json(const J &j, std::vector<T> &out) {
    if (!j.array() || j.array()->size() > 1000000)
        fail("invalid checkpoint array");
    for (const auto &row : *j.array()) {
        const auto n = num(row);
        if (n < int64_t(std::numeric_limits<T>::min()) || n > int64_t(std::numeric_limits<T>::max()))
            fail("checkpoint array value out of range");
        out.push_back(T(n));
    }
}
J group_json(const MatchGroup &g) {
    O out;
#define FIELD(name) out[#name] = vector_json(g.name);
    FIELD(desc)
    FIELD(fixtures) FIELD(results) FIELD(cup_results) FIELD(cup_other_leg) FIELD(records) FIELD(end_minute)
        FIELD(spectators) FIELD(log)
#undef FIELD
            out["cup_addresses"] = A{int64_t(g.cup_result_address[0]), int64_t(g.cup_result_address[1])};
    out["phase"] = g.phase;
    out["ended"] = int64_t(g.ended);
    out["kickoff_void_mask"] = int64_t(g.kickoff_void_mask);
    return out;
}
MatchGroup group_json(const J &in) {
    MatchGroup g;
#define FIELD(name) vector_json(get(in, #name), g.name);
    FIELD(desc)
    FIELD(fixtures) FIELD(results) FIELD(cup_results) FIELD(cup_other_leg) FIELD(records) FIELD(end_minute)
        FIELD(spectators) FIELD(log)
#undef FIELD
            const auto &at = arr(get(in, "cup_addresses"));
    if (at.size() != 2)
        fail("invalid cup addresses");
    for (size_t i = 0; i < 2; ++i) {
        const auto n = num(at[i]);
        if (n < 0 || n > UINT32_MAX)
            fail("invalid cup address");
        g.cup_result_address[i] = uint32_t(n);
    }
    const auto phase = num(get(in, "phase")), ended = num(get(in, "ended")),
               mask = num(get(in, "kickoff_void_mask"));
    if (phase < 0 || phase > 1 || ended < 0 || ended > UINT32_MAX || mask < 0 || mask > UINT32_MAX)
        fail("invalid match checkpoint");
    g.phase = int(phase);
    g.ended = uint32_t(ended);
    g.kickoff_void_mask = uint32_t(mask);
    if ((g.desc.size() != 0xc0 && g.desc.size() != 0xd0) || g.fixtures.size() % 2 || g.matches() > 32 ||
        (!g.records.empty() && g.records.size() != size_t(g.matches()) * 18) ||
        (!g.end_minute.empty() && g.end_minute.size() != size_t(g.matches())) ||
        (!g.spectators.empty() && g.spectators.size() != size_t(g.matches())))
        fail("invalid match dimensions");
    return g;
}
J groups_json(const std::vector<MatchGroup> &groups) {
    A rows;
    for (const auto &group : groups)
        rows.push_back(group_json(group));
    return rows;
}
std::vector<MatchGroup> groups_json(const J &j) {
    std::vector<MatchGroup> groups;
    const auto &rows = arr(j);
    if (rows.size() > 6)
        fail("too many match groups");
    for (const auto &row : rows)
        groups.push_back(group_json(row));
    return groups;
}
} // namespace
J driver_checkpoint(const TurnDriver &d) {
    return O{{"schema", "classic-driver@1.0"},
             {"stage", int(d.stage)},
             {"calendar_entry", d.calendar_entry},
             {"manager_position", d.manager_position},
             {"next_event_id", std::to_string(d.next_event_id)},
             {"loaded_matchday_resume", d.loaded_matchday_resume},
             {"suppress_remaining_pre_match_offers", d.suppress_remaining_pre_match_offers},
             {"national_nomination_desk", d.national_nomination_desk},
             {"enable_national_nomination_desks", d.enable_national_nomination_desks},
             {"groups", groups_json(d.groups)},
             {"referee_source", vector_json(d.referee_source)},
             {"referees", vector_json(d.referees)},
             {"last_results", O{{"valid", d.last_results.valid},
                                {"date", int64_t(d.last_results.date)},
                                {"calendar_entry", d.last_results.calendar_entry},
                                {"matchday", d.last_results.matchday},
                                {"groups", groups_json(d.last_results.groups)},
                                {"tournament_name", vector_json(d.last_results.tournament_name)}}}};
}
void restore_driver(TurnDriver &d, const J &j) {
    if (str(get(j, "schema")) != "classic-driver@1.0")
        fail("unsupported driver checkpoint");
    const auto stage = num(get(j, "stage"));
    const auto position = num(get(j, "manager_position"));
    const auto entry = num(get(j, "calendar_entry"));
    if (stage < 0 || stage > 2 || position < 0 || position > 4 || entry < 0 || entry > UINT16_MAX)
        fail("invalid saved desk cursor");
    d.stage = TurnDriver::Stage(stage);
    d.manager_position = int(position);
    d.calendar_entry = uint16_t(entry);
    const auto flag = [](const J &value, const char *name) {
        const auto &b = get(value, name);
        if (!b.boolean())
            fail("invalid driver flag");
        return *b.boolean();
    };
    d.loaded_matchday_resume = flag(j, "loaded_matchday_resume");
    d.suppress_remaining_pre_match_offers = flag(j, "suppress_remaining_pre_match_offers");
    d.national_nomination_desk = flag(j, "national_nomination_desk");
    d.enable_national_nomination_desks = flag(j, "enable_national_nomination_desks");
    J counter = O{{"calls", get(j, "next_event_id")}};
    d.next_event_id = calls(counter);
    d.groups = groups_json(get(j, "groups"));
    d.referee_source.clear();
    d.referees.clear();
    vector_json(get(j, "referee_source"), d.referee_source);
    vector_json(get(j, "referees"), d.referees);
    const auto &last = get(j, "last_results");
    d.last_results.valid = flag(last, "valid");
    const auto date = num(get(last, "date")), calendar = num(get(last, "calendar_entry")),
               matchday = num(get(last, "matchday"));
    if (date < 0 || date > UINT32_MAX || calendar < 0 || calendar > UINT16_MAX || matchday < 0 ||
        matchday > UINT16_MAX)
        fail("invalid result snapshot");
    d.last_results.date = uint32_t(date);
    d.last_results.calendar_entry = uint16_t(calendar);
    d.last_results.matchday = uint16_t(matchday);
    d.last_results.groups = groups_json(get(last, "groups"));
    d.last_results.tournament_name.clear();
    vector_json(get(last, "tournament_name"), d.last_results.tournament_name);
}
void save_career(const std::filesystem::path &path, const Classic &content, const GameState &state,
                 const WatcomRandom &random, const GameRandom &game, const TurnDriver &driver, bool overwrite,
                 const J &session) {
    if (driver.stage != TurnDriver::Stage::Empty && driver.stage != TurnDriver::Stage::MatchdayDesks &&
        driver.stage != TurnDriver::Stage::SeasonEndDesk)
        fail("save unavailable outside a manager desk");
    Documents docs = content.documents;
    if (verify(docs) != content.content_id)
        fail("invalid content identity");
    docs["state/world-manifest.json"] = docs.at("manifest.json");
    auto manifest = parse(docs, "manifest.json");
    (*manifest.object())["kind"] = "save";
    (*manifest.object())["save_profile"] = "classic-career-v1";
    (*manifest.object())["content_id"] = content.content_id;
    put(docs, "manifest.json", std::move(manifest));
    put(docs, "state/career.json", encode_native_state(state));
    put(docs, "state/rng.json",
        O{{"schema", "classic-rng@1.0"},
          {"watcom", rng(random.seed(), random.calls())},
          {"game", rng(uint32_t(game.seed()), game.calls())}});
    put(docs, "state/session.json", session);
    put(docs, "state/driver.json", driver_checkpoint(driver));
    put(docs, "state/information.json", encode_native_bytes(save_header(state)));
    O checks;
    for (const auto &[name, value] : docs)
        if (name.starts_with("state/"))
            checks[name] = sha256(value);
    put(docs, "state/integrity.json", O{{"schema", "save-integrity@1.0"}, {"entries", std::move(checks)}});
    write_archive(docs, path, overwrite);
}
SavedCareer load_career(const std::filesystem::path &path, const Classic &content) {
    const auto docs = read_archive(path);
    const auto manifest = parse(docs, "manifest.json");
    if (str(get(manifest, "kind")) != "save" || str(get(manifest, "save_profile")) != "classic-career-v1" ||
        str(get(manifest, "content_id")) != content.content_id)
        fail("save belongs to another mod or format");
    const auto integrity = parse(docs, "state/integrity.json");
    if (str(get(integrity, "schema")) != "save-integrity@1.0")
        fail("unsupported save integrity");
    O checks;
    for (const auto &[name, value] : docs)
        if (name.starts_with("state/") && name != "state/integrity.json")
            checks[name] = sha256(value);
    if (cli::dump_json(get(integrity, "entries")) != cli::dump_json(checks))
        fail("damaged native save");
    const auto world = world_of(docs);
    if (verify(world) != content.content_id)
        fail("save world contents changed");
    SavedCareer out;
    out.state = decode_native_state(parse(docs, "state/career.json"));
    if (out.state.players.size() != content.database.players.size() ||
        out.state.club_ids.size() != content.database.club_names.size() ||
        out.state.manager_records.size() != 5 * 0xb0c)
        fail("native state does not match Classic dimensions");
    const auto generators = parse(docs, "state/rng.json");
    if (str(get(generators, "schema")) != "classic-rng@1.0")
        fail("unsupported RNG checkpoint");
    const auto &w = get(generators, "watcom");
    const auto &g = get(generators, "game");
    out.random.restore(seed(w), calls(w));
    out.game.restore(std::bit_cast<int32_t>(seed(g)), calls(g));
    out.session = parse(docs, "state/session.json");
    out.driver = parse(docs, "state/driver.json");
    out.header = decode_native_bytes(parse(docs, "state/information.json"), 172);
    return out;
}
Bytes save_information(const std::filesystem::path &path) {
    const auto docs = read_archive(path);
    return decode_native_bytes(parse(docs, "state/information.json"), 172);
}
Bytes load_preferences(const std::filesystem::path &path, const char *kind, size_t size) {
    const auto b = read_file(path);
    const auto root = cli::parse_json(std::string(b.begin(), b.end()));
    if (str(get(root, "schema")) != std::string("classic-") + kind + "@1.0")
        fail("unsupported preferences");
    return decode_native_bytes(get(root, "payload"), size);
}
void save_preferences(const std::filesystem::path &path, const char *kind, const Bytes &b, bool overwrite) {
    write_atomic(path,
                 cli::dump_json(O{{"schema", std::string("classic-") + kind + "@1.0"},
                                  {"payload", encode_native_bytes(b)}}) +
                     "\n",
                 overwrite);
}
} // namespace bmh::content
