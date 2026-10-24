// SPDX-License-Identifier: GPL-3.0-only
#include "modern/content/world_save.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "content/digest.hpp"
#include "content/package.hpp"
#include "content/save.hpp"
#include "formats/native_state.hpp"
#include "import/detail.hpp"
#include "modern/game/save.hpp"

namespace bmh::modern {

namespace {

using namespace obw::detail;

J generator(uint32_t seed, uint64_t calls) {
    return O{{"seed", int64_t(seed)}, {"calls", std::to_string(calls)}};
}
uint64_t draw_count(const J& value) {
    const auto s = str(get(value, "calls"));
    if (s.empty() || s.size() > 20 || !std::all_of(s.begin(), s.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }))
        fail("invalid random draw counter");
    return std::stoull(s);
}
uint32_t seed_of(const J& value) {
    const auto n = num(get(value, "seed"));
    if (n < 0 || n > int64_t(UINT32_MAX)) fail("invalid random seed");
    return uint32_t(n);
}
J strings(const std::vector<std::string>& values) {
    A out;
    for (const auto& v : values) out.push_back(v);
    return out;
}
std::vector<std::string> strings(const J& value) {
    std::vector<std::string> out;
    for (const J& v : arr(value)) out.push_back(str(v));
    return out;
}
std::string world_identity(const World& world) { return world.id + "@" + world.data_revision; }

// The Toto columns 2-6 and the last settlement per manager (docs/new_engine/toto.md).
J pools_json(const std::vector<ModernState::PoolsColumns>& pools) {
    A out;
    for (const auto& p : pools) {
        A extra, hits;
        for (const auto& column : p.extra) {
            A tips;
            for (uint8_t t : column) tips.push_back(int(t));
            extra.push_back(std::move(tips));
        }
        for (uint8_t h : p.hits) hits.push_back(int(h));
        out.push_back(O{{"extra", std::move(extra)}, {"hits", std::move(hits)}, {"paid", int64_t(p.paid)}, {"won", int64_t(p.won)}});
    }
    return out;
}
std::vector<ModernState::PoolsColumns> pools_json(const J& value) {
    std::vector<ModernState::PoolsColumns> out;
    for (const J& row : arr(value)) {
        ModernState::PoolsColumns p;
        for (const J& column : arr(get(row, "extra"))) {
            const A& tips = arr(column);
            if (tips.size() != 11) fail("invalid Toto column");
            std::array<uint8_t, 11> c{};
            for (size_t i = 0; i < 11; ++i) {
                const int64_t t = num(tips[i]);
                if (t < 0 || t > 2) fail("invalid Toto tip");
                c[i] = uint8_t(t);
            }
            p.extra.push_back(c);
        }
        for (const J& h : arr(get(row, "hits"))) p.hits.push_back(uint8_t(num(h)));
        p.paid = uint32_t(num(get(row, "paid"))), p.won = uint32_t(num(get(row, "won")));
        out.push_back(std::move(p));
    }
    return out;
}

}  // namespace

void save_world_career(const std::filesystem::path& path, const World& world, const ModernState& state,
                       const WatcomRandom& random, const GameRandom& game, const TurnDriver& driver, bool overwrite,
                       const cli::Json& session) {
    if (driver.stage != TurnDriver::Stage::Empty && driver.stage != TurnDriver::Stage::MatchdayDesks &&
        driver.stage != TurnDriver::Stage::SeasonEndDesk)
        fail("save unavailable outside a manager desk");
    obw::Documents docs;
    put(docs, "manifest.json",
        O{{"format", "openbmh-world"}, {"format_version", "1.1"}, {"kind", "save"},
          {"save_profile", "modern-career-v1"}, {"requires", A{J("runtime:modern-v1")}},
          {"world", O{{"id", world.id}, {"data_revision", world.data_revision}, {"title", world.title}}}});
    put(docs, "state/career.json", encode_native_state(state));
    put(docs, "state/modern.json",
        O{{"schema", "modern-state@1.1"},
          {"club_ids", strings(state.world_club_id)},
          {"club_names", strings(state.world_club_name)},
          {"club_short_names", strings(state.world_club_short)},
          {"player_ids", strings(state.world_player_id)},
          {"player_first", strings(state.player_first)},
          {"player_last", strings(state.player_last)},
          {"player_nations", strings(state.player_nation)},
          {"league_ids", strings(state.world_league_id)},
          {"league_names", strings(state.world_league_name)},
          {"generated_players", int64_t(state.almanac.generated_players)},
          {"pools", pools_json(state.pools)}});
    // The almanac: finished seasons unchanged, the running one encoded now.
    for (const auto& [name, bytes] : state.almanac.finished) docs[name] = bytes;
    if (state.almanac.current.season >= 0) docs["history/current.bin"] = encode_almanac_season(state.almanac.current);
    put(docs, "state/rng.json",
        O{{"schema", "classic-rng@1.0"},
          {"watcom", generator(random.seed(), random.calls())},
          {"game", generator(uint32_t(game.seed()), game.calls())}});
    put(docs, "state/session.json", session);
    put(docs, "state/driver.json", content::driver_checkpoint(driver));
    put(docs, "state/information.json", encode_native_bytes(modern::save_header(state)));
    O checks;
    for (const auto& [name, value] : docs)
        if (name.starts_with("state/") || name.starts_with("history/")) checks[name] = content::sha256(value);
    put(docs, "state/integrity.json", O{{"schema", "save-integrity@1.0"}, {"entries", std::move(checks)}});
    content::write_archive(docs, path, overwrite);
}

SavedWorldCareer load_world_career(const std::filesystem::path& path, const World& world) {
    const auto docs = content::read_archive(path);
    const auto manifest = parse(docs, "manifest.json");
    if (str(get(manifest, "kind")) != "save" || str(get(manifest, "save_profile")) != "modern-career-v1")
        fail("not a Modern career save");
    const auto& saved_world = get(manifest, "world");
    if (str(get(saved_world, "id")) != world.id || str(get(saved_world, "data_revision")) != world.data_revision)
        fail("the save belongs to another world: " + str(get(saved_world, "id")) + "@" +
             str(get(saved_world, "data_revision")) + ", loaded " + world_identity(world));
    const auto integrity = parse(docs, "state/integrity.json");
    if (str(get(integrity, "schema")) != "save-integrity@1.0") fail("unsupported save integrity");
    O checks;
    for (const auto& [name, value] : docs)
        if ((name.starts_with("state/") || name.starts_with("history/")) && name != "state/integrity.json") checks[name] = content::sha256(value);
    if (cli::dump_json(get(integrity, "entries")) != cli::dump_json(checks)) fail("damaged Modern save");

    SavedWorldCareer out;
    out.state = ModernState(decode_native_state(parse(docs, "state/career.json")));
    const auto modern = parse(docs, "state/modern.json");
    if (str(get(modern, "schema")) != "modern-state@1.1") fail("unsupported Modern state");
    out.state.world_club_id = strings(get(modern, "club_ids"));
    out.state.world_club_name = strings(get(modern, "club_names"));
    out.state.world_club_short = strings(get(modern, "club_short_names"));
    out.state.world_player_id = strings(get(modern, "player_ids"));
    out.state.player_first = strings(get(modern, "player_first"));
    out.state.player_last = strings(get(modern, "player_last"));
    out.state.player_nation = strings(get(modern, "player_nations"));
    out.state.world_league_id = strings(get(modern, "league_ids"));
    out.state.world_league_name = strings(get(modern, "league_names"));
    out.state.almanac.generated_players = uint32_t(num(get(modern, "generated_players")));
    out.state.pools = pools_json(get(modern, "pools"));
    for (const auto& [name, bytes] : docs) {
        if (!name.starts_with("history/season-")) continue;
        decode_almanac_season(bytes);  // a damaged season fails here, not in the almanac screen
        out.state.almanac.finished.emplace_back(name, bytes);
    }
    if (const auto it = docs.find("history/current.bin"); it != docs.end())
        out.state.almanac.current = decode_almanac_season(it->second);
    if (out.state.world_player_id.size() != out.state.players.size() ||
        out.state.world_club_id.size() != out.state.club_ids.size())
        fail("Modern names do not match the saved state");
    const auto generators = parse(docs, "state/rng.json");
    if (str(get(generators, "schema")) != "classic-rng@1.0") fail("unsupported RNG checkpoint");
    out.random.restore(seed_of(get(generators, "watcom")), draw_count(get(generators, "watcom")));
    out.game.restore(std::bit_cast<int32_t>(seed_of(get(generators, "game"))), draw_count(get(generators, "game")));
    out.session = parse(docs, "state/session.json");
    out.driver = parse(docs, "state/driver.json");
    out.header = decode_native_bytes(parse(docs, "state/information.json"), 172);
    return out;
}

}  // namespace bmh::modern
