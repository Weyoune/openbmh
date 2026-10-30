// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/save.hpp"
#include "game/calendar.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

#include "formats/savestate.hpp"
#include "game/career.hpp"
#include "game/national.hpp"

namespace bmh {

namespace {

void put(Bytes& d, const uint8_t* src, size_t n) {
    for (size_t i = 0; i < n && i < d.size(); i++) d[i] = src[i];
}
void put(Bytes& d, const std::vector<uint8_t>& v) { put(d, v.data(), v.size()); }
void put16(Bytes& d, const std::vector<uint16_t>& v) {
    for (size_t i = 0; i < v.size() && 2 * i + 1 < d.size(); i++) d[2 * i] = uint8_t(v[i]), d[2 * i + 1] = uint8_t(v[i] >> 8);
}
void put32(Bytes& d, size_t at, uint32_t x) {
    for (int i = 0; i < 4 && at + size_t(i) < d.size(); i++) d[at + size_t(i)] = uint8_t(x >> (8 * i));
}

// DeskDate2(date, buffer, 0): "TT.MM.JJJJ" of a day counter (day 0 = 1 July of the supplied edition; a
// four-year leap cycle that starts with the third year).
std::string date_text(uint32_t counter) {
    static const int kDays[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int cycle = 3, years = 0, month = 7;
    while (counter > uint32_t(364 + (cycle == 4))) {
        if (cycle == 4) cycle = 0, --counter;
        ++years, counter -= 365, ++cycle;
    }
    auto days = [&] { return month == 2 ? 28 + (cycle == 4) : kDays[month]; };
    while (counter > uint32_t(days())) {
        counter -= uint32_t(days());
        if (++month == 13) month = 1, ++years;
    }
    int day = int(counter) + 1;
    if (day > days()) {
        day -= days();
        if (++month == 13) month = 1, ++years;
    }
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d.%02d.%04d", day, month, calendar_epoch().year + years);
    return buf;
}

}  // namespace

Bytes save_stream(const GameState& live) {
    // NationalCoachAfterLoad writes a Bundestrainer's game with the slots swapped back (national.hpp)
    const bool coach = live.memory.has(::bmh::layout::CoachActive) && live.memory.u8(::bmh::layout::CoachActive) != 0;
    const GameState swapped = coach ? national_coach_save_form(live) : GameState{};
    const GameState& g = coach ? swapped : live;
    const bool records = g.memory.has(::bmh::layout::ClubSeasonRecords) && g.memory.u32(::bmh::layout::ClubSeasonRecords) != 0;
    Bytes stream;
    for (const SaveBlock& b : save_layout(DbCounts{}, records)) {
        Bytes d(b.size, 0);
        if (b.heap) {
            switch (b.address) {
                case ::bmh::layout::PlayerBonuses: put(d, g.player_bonus); break;
                case ::bmh::layout::TeamMorale: put(d, g.team_morale); break;
                case ::bmh::layout::ClubBadgeSelectors: put(d, g.club_heap_78); break;
                case ::bmh::layout::TeamFlags: put16(d, g.team_flags); break;
                case ::bmh::layout::ClubIdentities: put16(d, g.club_ids); break;
                case ::bmh::layout::PlayerNameOffsets: put16(d, g.player_name_offsets); break;
                case ::bmh::layout::MasterDatabaseMetadata: put(d, g.master_database_metadata); break;
                case ::bmh::layout::ClubAuxiliaryWords: put(d, g.club_heap_bc); break;
                case ::bmh::layout::LeagueRankHistory: put(d, g.position_history); break;
                case ::bmh::layout::ClubAuxiliaryBytes: put(d, g.club_heap_b8); break;
                case ::bmh::layout::SquadSlots: put16(d, g.squad_slots); break;
                case ::bmh::layout::PlayerCards: put16(d, g.player_cards); break;
                case ::bmh::layout::ManagerMailbox: put(d, g.mailbox); break;
                case ::bmh::layout::TournamentName: put(d, g.tournament_name); break;
                case ::bmh::layout::ClubSeasonRecords: put(d, g.club_table); break;
                case ::bmh::layout::PlayerConditionTechniqueForm:
                case ::bmh::layout::PlayerSkillWords:
                case ::bmh::layout::PlayerIdentityWords:
                    for (size_t i = 0; i < g.players.size(); i++) {
                        const Player& p = g.players[i];
                        put32(d, i * 4, b.address == ::bmh::layout::PlayerConditionTechniqueForm ? p.w35c : b.address == ::bmh::layout::PlayerSkillWords ? p.w368 : p.w364);
                    }
                    break;
                default: break;
            }
        } else {
            for (size_t i = 0; i < b.size; i++)
                if (g.memory.has(b.address + uint32_t(i))) d[i] = g.memory.u8(b.address + uint32_t(i));
            // the blocks GameState keeps in typed fields
            switch (b.address) {
                case ::bmh::layout::ManagerRecords: put(d, g.manager_records); break;
                case ::bmh::layout::ManagerMatchHistory: put(d, g.manager_results); break;
                case ::bmh::layout::ManagerRatings: put(d, g.manager_ratings); break;
                case ::bmh::layout::LeagueResults: put(d, g.results); break;
                case ::bmh::layout::LeagueRoundCounters: put(d, g.league_rounds); break;
                case ::bmh::layout::TacticsBoards: put(d, g.tactics); break;
                case ::bmh::layout::StockExchange: put(d, g.stocks); break;
                case ::bmh::layout::ManagerAutomation: put(d, g.manager_options); break;
                case ::bmh::layout::Formations: put(d, g.formations); break;
                case ::bmh::layout::ForeignClubStrength: put(d, g.foreign_strength); break;
                case ::bmh::layout::Difficulty: d[0] = uint8_t(g.level); break;
                case ::bmh::layout::CalendarDay: put32(d, 0, g.date); break;
                case ::bmh::layout::DaysIntoSeason: d[0] = uint8_t(g.date - g.season_start), d[1] = uint8_t((g.date - g.season_start) >> 8); break;
                case ::bmh::layout::PlayerDevelopmentBalance: put32(d, 0, uint32_t(g.development_balance)); break;
                case ::bmh::layout::SeasonIndex: d[0] = uint8_t(g.season), d[1] = uint8_t(uint16_t(g.season) >> 8); break;
                default: break;
            }
        }
        stream.insert(stream.end(), d.begin(), d.end());
    }
    for (const std::vector<uint8_t>* pool : {&g.club_names, &g.player_names, &g.referee_names, &g.stadium_names}) {
        stream.push_back(uint8_t(pool->size()));
        stream.push_back(uint8_t(pool->size() >> 8));
        stream.insert(stream.end(), pool->begin(), pool->end());
    }
    // the English cup memory has no place in the original layout: OpenBMH extension
    std::vector<SaveState::ExtensionBlock> extension;
    for (const auto& [at, size] : kEnglishCompetitionBlocks)
        if (g.memory.has(at) && g.memory.has(at + size - 1)) {
            Bytes bytes(size);
            for (uint32_t i = 0; i < size; ++i) bytes[i] = g.memory.u8(at + i);
            extension.push_back({at, std::move(bytes)});
        }
    append_save_extension(stream, extension);
    return stream;
}

Bytes save_header(const GameState& g) {
    const int managers = g.memory.u8(::bmh::layout::ManagerCount);
    Bytes h;
    h.push_back(uint8_t(managers));
    const std::string date = date_text(g.date);
    for (size_t i = 0; i < 11; i++) h.push_back(i < date.size() ? uint8_t(date[i]) : 0);
    Bytes list(0xa0, 0);
    for (int m = 0; m < managers && m < 4; m++) {
        const size_t rec = size_t(m) * 0xb0c;
        const int team = int16_t(g.manager_records.at(rec + 0x290) | g.manager_records.at(rec + 0x291) << 8);
        const bool national = team == 0x165;
        const size_t at = size_t(m) * 0x28;
        list[at] = national ? 0x1e : g.manager_records.at(rec + 0x210);  // (0x1e: high byte of the loop word)
        list[at + 1] = national ? 1 : 0;
        for (size_t i = 0; i < 0x13; i++) list[at + 2 + i] = g.manager_records.at(rec + 0xa8 + i);
        const size_t name = g.club_ids.at(size_t(team));
        for (size_t i = 0; i < 0x13 && name + i < g.club_names.size(); i++) list[at + 0x15 + i] = g.club_names[name + i];
    }
    h.insert(h.end(), list.begin(), list.end());
    return h;
}

Savegame make_savegame(const GameState& g, WatcomRandom& rand) {
    Savegame s;
    s.plain_header = save_header(g);
    s.id1 = 0;
    s.version = "V2.24";
    const uint32_t hi = uint32_t(rand.range(0, 0x8fff)), lo = uint32_t(rand.range(0, 0x8fff));
    s.id2 = lo + hi * 0x10000 + 1;
    s.key = uint8_t(rand.range(0, 0xff) + 'n');
    s.random = uint8_t(rand.range(0, 0xff));
    s.stream = save_stream(g);
    uint8_t sum = s.key;
    for (uint8_t b : s.stream) sum = uint8_t(sum + b);
    s.checksum = sum;
    s.checksum_ok = true;
    return s;
}

Bytes savegame_bytes(const Savegame& s) {
    Bytes out = s.plain_header;
    static const char magic[10] = {'K', 'R', 'O', 'N', '_', 'F', 'B', 'M', 0, 'k'};
    out.insert(out.end(), magic, magic + 10);
    for (int i = 0; i < 4; i++) out.push_back(uint8_t(s.id1 >> (8 * i)));
    for (size_t i = 0; i < 6; i++) out.push_back(i < s.version.size() ? uint8_t(s.version[i]) : 0);
    for (int i = 0; i < 4; i++) out.push_back(uint8_t(s.id2 >> (8 * i)));
    out.push_back(s.random);
    out.push_back(s.key);
    Bytes enc = s.stream;
    fbm_encrypt(enc.data(), enc.size(), s.key);
    out.insert(out.end(), enc.begin(), enc.end());
    out.push_back(s.checksum);
    return out;
}

void write_savegame(const std::filesystem::path& path, const Savegame& s) {
    const Bytes b = savegame_bytes(s);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) throw FormatError("cannot write " + path.string());
    f.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
    const bool written = bool(f);
    f.close();
    // Buffered final bytes can fail only at close, after write reported the
    // complete size. The autosave caller must not publish that temporary file.
    if (!written || !f) throw FormatError("short write " + path.string());
}

}  // namespace bmh
