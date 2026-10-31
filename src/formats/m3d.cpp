// SPDX-License-Identifier: GPL-3.0-only
#include "formats/m3d.hpp"
#include "formats/data_profile.hpp"

#include <cstring>

namespace bmh {

namespace {

struct Reader {
    const Bytes& d;
    size_t pos = 0;
    Bytes take(size_t n) {
        if (pos + n > d.size()) throw FormatError("M3D: unexpected end of file");
        Bytes b(d.begin() + pos, d.begin() + pos + n);
        pos += n;
        return b;
    }
};

std::vector<std::string> split_pool(const Bytes& pool) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i < pool.size(); i++)
        if (pool[i] == 0) { out.emplace_back(reinterpret_cast<const char*>(pool.data() + start), i - start); start = i + 1; }
    return out;
}

std::string pool_string(const Bytes& pool, size_t offset) {
    if (offset >= pool.size()) throw FormatError("M3D: string offset out of range");
    const char* s = reinterpret_cast<const char*>(pool.data() + offset);
    return std::string(s, strnlen(s, pool.size() - offset));
}

}  // namespace

Database load_database(const std::filesystem::path& path) {
    return load_database(path, load_data_profile(path.parent_path()).counts);
}

Database load_database(const std::filesystem::path& path, const DbCounts& c) {
    Bytes d = read_file(path);
    Reader r{d};
    Bytes lens = r.take(8);
    size_t len_clubs = le16(lens, 0), len_players = le16(lens, 2), len_refs = le16(lens, 4), len_stadiums = le16(lens, 6);

    Database db; db.counts=c;
    db.raw_player_bonus_values = r.take(c.n66 + 0x32);
    Bytes club_pool = r.take(len_clubs);
    Bytes player_pool = r.take(len_players);
    Bytes ref_pool = r.take(len_refs), stadium_pool = r.take(len_stadiums);
    db.referees = split_pool(ref_pool);
    db.stadiums = split_pool(stadium_pool);
    Bytes club_ofs = r.take(c.clubs * 2);
    Bytes player_ofs = r.take(c.players * 2);
    db.pools[0] = club_pool, db.pools[1] = player_pool, db.pools[2] = ref_pool, db.pools[3] = stadium_pool;
    db.raw_club_name_offsets = club_ofs, db.raw_player_name_offsets = player_ofs;
    db.raw_database_metadata = r.take(100);
    db.raw_club_auxiliary_words = r.take(c.n42 * 2);
    db.raw_club_badge_selectors = r.take(c.clubs * 2);
    db.raw_foreign_club_strength = r.take(0xFB);
    db.raw_league_auxiliary_bytes = r.take(0xBD);
    db.raw_referee_properties = r.take(0x32);
    db.raw_club_kit_selectors = r.take(c.clubs);
    db.raw_club_short_name_offsets = r.take(c.n42);
    db.squad_list = r.take((c.n66 - 4) * 2 + 0x6C);
    Bytes w35c = r.take(c.players * 4), w368 = r.take(c.players * 4), w364 = r.take(c.players * 4);
    if (r.pos != d.size()) throw FormatError("M3D: trailing data, table sizes do not match");

    for (uint32_t i = 0; i < c.clubs; i++) db.club_names.push_back(pool_string(club_pool, le16(club_ofs, i * 2)));
    for (uint32_t i = 0; i < c.players; i++)
        db.players.push_back({pool_string(player_pool, le16(player_ofs, i * 2)), le32(w35c, i * 4), le32(w368, i * 4), le32(w364, i * 4)});
    return db;
}

}  // namespace bmh
