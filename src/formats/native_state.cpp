// SPDX-License-Identifier: GPL-3.0-only
#include "formats/native_state.hpp"
#include <limits>
#include <type_traits>
namespace bmh {
namespace {
using J = cli::Json;
using O = J::Object;
using A = J::Array;
[[noreturn]] void invalid() { throw FormatError("native state: invalid or missing field"); }
const J &field(const J &obj, const char *name) {
    if (!obj.object())
        invalid();
    const auto it = obj.object()->find(name);
    if (it == obj.object()->end())
        invalid();
    return it->second;
}
template <class T> J number(T v) { return J(int64_t(v)); }
template <class T> void number(const J &j, T &v) {
    if (!j.integer())
        invalid();
    const auto n = *j.integer();
    if constexpr (std::is_unsigned_v<T>) {
        if (n < 0 || uint64_t(n) > uint64_t(std::numeric_limits<T>::max()))
            invalid();
    } else if (n < std::numeric_limits<T>::min() || n > std::numeric_limits<T>::max())
        invalid();
    v = T(n);
}
J scalar(bool b) { return b; }
void scalar(const J &j, bool &b) {
    if (!j.boolean())
        invalid();
    b = *j.boolean();
}
template <class T> J scalar(T n) { return number(n); }
template <class T> void scalar(const J &j, T &n) { number(j, n); }
template <class Range> J sequence(const Range &values) {
    A out;
    for (const auto v : values)
        out.push_back(number(v));
    return out;
}
template <class T> void sequence(const J &j, std::vector<T> &values) {
    if (!j.array() || j.array()->size() > 1000000)
        invalid();
    values.resize(j.array()->size());
    for (size_t i = 0; i < values.size(); ++i)
        number((*j.array())[i], values[i]);
}
template <class T, size_t N> void sequence(const J &j, std::array<T, N> &values) {
    if (!j.array() || j.array()->size() != N)
        invalid();
    for (size_t i = 0; i < N; ++i)
        number((*j.array())[i], values[i]);
}
std::string hex(const Bytes &data) {
    constexpr char d[] = "0123456789abcdef";
    std::string out;
    out.reserve(data.size() * 2);
    for (auto c : data) {
        out += d[c >> 4];
        out += d[c & 15];
    }
    return out;
}
Bytes unhex(const J &value) {
    if (!value.string() || value.string()->size() % 2 || value.string()->size() > 32 * 1024 * 1024)
        invalid();
    const auto &s = *value.string();
    Bytes b;
    b.reserve(s.size() / 2);
    const auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        invalid();
    };
    for (size_t i = 0; i < s.size(); i += 2)
        b.push_back(uint8_t(digit(s[i]) * 16 + digit(s[i + 1])));
    return b;
}
} // namespace
cli::Json encode_native_bytes(const Bytes &bytes) {
    return O{{"schema", "classic-bytes@1.0"}, {"hex", hex(bytes)}};
}
Bytes decode_native_bytes(const J &value, size_t expected) {
    if (!field(value, "schema").string() || *field(value, "schema").string() != "classic-bytes@1.0")
        invalid();
    auto b = unhex(field(value, "hex"));
    if (b.size() != expected)
        invalid();
    return b;
}
#define STATE_SCALARS(X)                                                                                     \
    X(indoor)                                                                                                \
    X(level) X(chance_rain) X(chance_temperature) X(last_shooter) X(last_pick_quality) X(live_scene)         \
        X(sportschau_selection_available) X(sportschau_club_threshold) X(list_toggle) X(lineups_frozen)      \
            X(offer_answer) X(transfer_notice_suppressed) X(pools_open) X(autosave_months) X(autosave_month) \
                X(display_start_year) X(date) X(season_start) X(development_balance) X(trend_rotation)       \
                    X(season)
#define STATE_BYTES(X)                                                                                       \
    X(team_morale)                                                                                           \
    X(player_bonus) X(formations) X(manager_records) X(tactics) X(manager_options) X(club_table)             \
        X(club_heap_bc) X(club_heap_78) X(club_heap_b8) X(master_database_metadata) X(referee_names)         \
            X(stadium_names) X(tournament_name) X(manager_results) X(manager_ratings) X(position_history)    \
                X(league_rounds) X(results) X(stocks) X(foreign_strength) X(mailbox) X(club_names)           \
                    X(player_names)
#define STATE_NUMBERS(X)                                                                                     \
    X(squad_slots)                                                                                           \
    X(team_flags) X(manager_teams) X(live_matches) X(sportschau_homes) X(sportschau_priorities) X(club_ids)  \
        X(player_cards) X(player_name_offsets)
J encode_native_state(const GameState &g) {
    O out{{"schema", "classic-state@1.0"}};
#define PUT_SCALAR(name) out[#name] = scalar(g.name);
    STATE_SCALARS(PUT_SCALAR)
#undef PUT_SCALAR
#define PUT_BYTES(name) out[#name] = hex(g.name);
    STATE_BYTES(PUT_BYTES)
#undef PUT_BYTES
#define PUT_NUMBERS(name) out[#name] = sequence(g.name);
    STATE_NUMBERS(PUT_NUMBERS)
#undef PUT_NUMBERS
    A players;
    for (const auto &p : g.players)
        players.push_back(O{{"name_hex", hex(Bytes(p.name.begin(), p.name.end()))},
                            {"form", number(p.w35c)},
                            {"skills", number(p.w368)},
                            {"identity", number(p.w364)}});
    out["players"] = std::move(players);
    A blocks;
    for (const auto &[at, bytes] : g.memory.blocks())
        blocks.push_back(O{{"address", number(at)}, {"hex", hex(bytes)}});
    out["memory"] = std::move(blocks);
    // Reality scene residues influence later matches and must survive a checkpoint.
    out["reality"] = O{{"initialized", g.reality.initialized},     {"deflected", g.reality.deflected},
                       {"card_kind", number(g.reality.card_kind)}, {"distance", g.reality.distance},
                       {"actors", sequence(g.reality.cue_actors)}, {"grid", g.reality.grid.checkpoint()}};
    return out;
}
GameState decode_native_state(const J &in) {
    if (!field(in, "schema").string() || *field(in, "schema").string() != "classic-state@1.0")
        invalid();
    GameState g;
#define GET_SCALAR(name) scalar(field(in, #name), g.name);
    STATE_SCALARS(GET_SCALAR)
#undef GET_SCALAR
#define GET_BYTES(name) g.name = unhex(field(in, #name));
    STATE_BYTES(GET_BYTES)
#undef GET_BYTES
#define GET_NUMBERS(name) sequence(field(in, #name), g.name);
    STATE_NUMBERS(GET_NUMBERS)
#undef GET_NUMBERS
    const auto &players = field(in, "players");
    if (!players.array() || players.array()->size() > 4095)
        invalid();
    for (const auto &row : *players.array()) {
        Player p;
        auto bytes = unhex(field(row, "name_hex"));
        p.name.assign(bytes.begin(), bytes.end());
        number(field(row, "form"), p.w35c);
        number(field(row, "skills"), p.w368);
        number(field(row, "identity"), p.w364);
        g.players.push_back(std::move(p));
    }
    const auto &memory = field(in, "memory");
    if (!memory.array() || memory.array()->size() > 8192)
        invalid();
    std::map<uint32_t, Bytes> blocks;
    for (const auto &row : *memory.array()) {
        uint32_t at;
        number(field(row, "address"), at);
        auto b = unhex(field(row, "hex"));
        if (uint64_t(at) + b.size() > UINT32_MAX || !blocks.emplace(at, std::move(b)).second)
            invalid();
    }
    g.memory = StaticMemory::from_blocks(std::move(blocks));
    const auto &reality = field(in, "reality");
    scalar(field(reality, "initialized"), g.reality.initialized);
    scalar(field(reality, "deflected"), g.reality.deflected);
    number(field(reality, "card_kind"), g.reality.card_kind);
    number(field(reality, "distance"), g.reality.distance);
    sequence(field(reality, "actors"), g.reality.cue_actors);
    g.reality.grid.restore_checkpoint(field(reality, "grid"));
    if (g.display_start_year < 1900 || g.display_start_year > 9998 || g.manager_teams.size() > 4 ||
        g.level < 0 || g.level > 9 || g.date < g.season_start || g.date > 3000000)
        invalid();
    return g;
}
#undef STATE_SCALARS
#undef STATE_BYTES
#undef STATE_NUMBERS
} // namespace bmh
