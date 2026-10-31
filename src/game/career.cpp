// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/career.hpp"
#include "game/lineup.hpp"

#include <algorithm>

#include "game/messages.hpp"
#include "game/schedule.hpp"

namespace bmh {

namespace {

const int& kPoolSlots = runtime_dimensions.pool_first;   // CareerRuntimeField: the clubs' squads end here
const int& kTailFrom = runtime_dimensions.market_special_first;
const int& kTailTo = runtime_dimensions.free_first;      // CareerRuntimeField2
const int& kExtraSlots = runtime_dimensions.extra_slots_first;  // CareerSession_DataDirectory_RuntimeField4: 54 extra players from here
const int& kSquadSlots = runtime_dimensions.total_slots;
const int& kLeagueTeams = runtime_dimensions.league_teams;     // CareerSession_DispatchLineup_RuntimeField

// LeagueSizes: clubs per league; LeagueStrengthRanges: (minimum, maximum) strength per league.
// (the clubs per league are kLeagueSizes, schedule.hpp)
int kLeagueStrength[9][2] = {{71, 93}, {49, 71}, {28, 49}, {28, 49}, {10, 28},
                                       {85, 95}, {90, 95}, {75, 95}, {80, 95}};

}  // namespace

std::pair<int,int> career_strength_range(int league) {
    if (league<0 || league>=9) throw FormatError("career: invalid league range");
    return {kLeagueStrength[league][0],kLeagueStrength[league][1]};
}

void install_career_rules(const LeImage& exe) {
    int next[9][2]{};
    for (int league = 0; league < 9; ++league) {
        for (int bound = 0; bound < 2; ++bound)
            next[league][bound] = exe.u8(layout::LeagueStrengthRanges + uint32_t(league * 2 + bound));
        if (next[league][0] > next[league][1] || next[league][1] > 99)
            throw FormatError("career: unsupported original league strength range");
    }
    install_lineup_rules(exe);
    for (int league = 0; league < 9; ++league)
        for (int bound = 0; bound < 2; ++bound) kLeagueStrength[league][bound] = next[league][bound];
}

void generate_player(GameState& g, int id, int level_flags, GameRandom& game, WatcomRandom& rand) {
    Player& p = g.players.at(size_t(id));
    const bool keep_age = (level_flags & 0x1000) != 0, exact = (level_flags & 0x2000) != 0;
    const int level = int16_t(level_flags & 0x0fff);
    uint32_t w = 0x3000000;  // goalkeepers
    if ((p.w364 & 3) != 0) {
        w = uint32_t(game.range(0, 7)) << 24;
        w += uint32_t(game.range(0, 7)) << 21;
    }
    const int lo = exact ? level : level - level / 10, hi = exact ? level : level + level / 10;
    const int condition = std::clamp(int(int16_t(game.range(lo, hi))), 10, 0x63);  // ClampInteger
    w += uint32_t(condition) << 14;
    const int technique = std::clamp(int(int16_t(game.range(lo, hi))), 10, 0x63);
    w += uint32_t(technique) << 7;
    p.w35c = w + uint32_t(game.range(0x23, 0x41));
    p.w368 = (p.w368 & 0xff03c000u) + uint32_t(rand.range(0, 0x3fff));
    p.w364 &= ~(0x1fu << 21);
    if (!keep_age) p.w364 |= uint32_t(game.range(2, 5)) << 21;
}

std::vector<uint16_t> startup_squad_list(const Database& db) {
    std::vector<uint16_t> slots(kSquadSlots, 0x0fff);
    for (size_t i = 0; i + 1 < db.squad_list.size() && i / 2 < slots.size(); i += 2)
        slots[i / 2] = uint16_t(db.squad_list[i] | db.squad_list[i + 1] << 8);
    std::copy_backward(slots.begin() + long(kTailFrom), slots.begin() + long(kTailFrom + (kSquadSlots - kTailTo)), slots.end());
    return slots;
}

void startup_players(GameState& g, GameRandom& game, WatcomRandom& rand) {
    for (size_t i = 0; i < 0x36; i++) {
        const int level = rand.range(i < 0x16 ? 0x14 : 0x46, 0x5a);
        generate_player(g, int16_t(g.squad_slots.at(kExtraSlots + i)), int16_t(level), game, rand);
    }
    int previous = 0, index = -1;  // local_18, local_1c
    for (int team = 0; team < kLeagueTeams; team++) {
        const int league = team_league(team);
        index = league == previous ? index + 1 : 0;
        previous = league;
        const uint32_t count = uint32_t(kLeagueSizes[league]);
        const uint32_t range = uint32_t(kLeagueStrength[league][1] - kLeagueStrength[league][0]);
        const int level = int16_t(int((count - uint32_t(index)) * range * 100) / int(count) / 100) + kLeagueStrength[league][0];
        const SquadRange r = squad_range(team);
        for (size_t s = r.first; s < r.first + r.count; s++) {
            const int id = g.squad_slots.at(s) & 0x0fff;
            if (id != 0x0fff) generate_player(g, id, level | 0x1000, game, rand);
        }
    }
}

}  // namespace bmh

#include "formats/le.hpp"
#include "formats/savestate.hpp"

namespace bmh {

GameState startup_state(const LeImage& exe, const Database& db, const Bytes& m3_men, const Bytes& m3_dat) {
    const DbCounts counts=db.counts;
    const std::vector<SaveBlock> layout = save_layout(counts, true);
    Bytes stream;
    for (const SaveBlock& b : layout) {
        Bytes data(b.size, 0);
        auto put = [&](const Bytes& src, size_t at = 0) {
            for (size_t i = 0; i < src.size() && at + i < data.size(); i++) data[at + i] = src[i];
        };
        if (!b.heap) {
            for (size_t i = 0; i < b.size; i++)
                if (exe.contains(b.address + uint32_t(i))) data[i] = exe.u8(b.address + uint32_t(i));
        }
        switch (b.address) {
            case ::bmh::layout::PlayerBonuses: if (b.heap) put(db.raw_player_bonus_values); break;
            case ::bmh::layout::ClubBadgeSelectors: if (b.heap) put(db.raw_club_badge_selectors); break;
            case ::bmh::layout::ClubIdentities: if (b.heap) put(db.raw_club_name_offsets); break;
            case ::bmh::layout::PlayerNameOffsets: if (b.heap) put(db.raw_player_name_offsets); break;
            case ::bmh::layout::MasterDatabaseMetadata: if (b.heap) put(db.raw_database_metadata); break;
            case ::bmh::layout::ClubAuxiliaryWords: if (b.heap) put(db.raw_club_auxiliary_words); break;
            case ::bmh::layout::ForeignClubStrength: put(db.raw_foreign_club_strength); break;
            case ::bmh::layout::Career_Put_ExecutableField3: put(db.raw_league_auxiliary_bytes); break;
            case ::bmh::layout::RefereeProperties: put(db.raw_referee_properties); break;
            case ::bmh::layout::ClubKitSelectors: put(db.raw_club_kit_selectors); break;
            case ::bmh::layout::ClubShortNameOffsets: put(db.raw_club_short_name_offsets); break;
            case ::bmh::layout::ClubSeasonRecords: if (!b.heap) data[0] = 1; break;  // the pointer: records allocated
            case ::bmh::layout::NextDeskManager:
                for (size_t m = 0; m < 4; m++) data[m * 2] = uint8_t(m), data[m * 2 + 1] = 0;
                break;
            case ::bmh::layout::ManagerAutomation:
                for (size_t m = 0; m < 4; m++) data[m * 5] = 1;
                break;
            case ::bmh::layout::ActiveManager: data[0] = 4; break;
            case ::bmh::layout::Desktops:
                for (size_t k = 0; k < 5; k++) put(m3_men, k * 0x360);
                break;
            case ::bmh::layout::Formations:
                put(m3_dat);
                for (size_t k = 1; k < 4; k++)
                    for (size_t i = 0; i < 0x160; i++) data[k * 0x160 + i] = data[i];
                break;
            case ::bmh::layout::SquadSlots:
                if (b.heap) {
                    const std::vector<uint16_t> slots = startup_squad_list(db);
                    for (size_t i = 0; i < slots.size() && 2 * i + 1 < data.size(); i++)
                        data[2 * i] = uint8_t(slots[i]), data[2 * i + 1] = uint8_t(slots[i] >> 8);
                }
                break;
            case ::bmh::layout::PlayerConditionTechniqueForm:
            case ::bmh::layout::PlayerSkillWords:
            case ::bmh::layout::PlayerIdentityWords:
                if (b.heap) {
                    for (size_t i = 0; i < db.players.size() && 4 * i + 3 < data.size(); i++) {
                        uint32_t w = b.address == ::bmh::layout::PlayerConditionTechniqueForm ? db.players[i].w35c : b.address == ::bmh::layout::PlayerSkillWords ? db.players[i].w368 : db.players[i].w364;
                        if (b.address == ::bmh::layout::PlayerSkillWords && !runtime_dimensions.expanded && i + 32 >= db.players.size())
                            w = (w & 0x07ffffffu) | uint32_t(exe.u8(::bmh::layout::Career_BuildStadium_RuntimeField4 - uint32_t(db.players.size() - i))) << 27;
                        for (int k = 0; k < 4; k++) data[4 * i + size_t(k)] = uint8_t(w >> (8 * k));
                    }
                }
                break;
            default: break;
        }
        stream.insert(stream.end(), data.begin(), data.end());
    }
    for (const Bytes& pool : db.pools) {
        stream.push_back(uint8_t(pool.size()));
        stream.push_back(uint8_t(pool.size() >> 8));
        stream.insert(stream.end(), pool.begin(), pool.end());
    }
    const SaveState save(stream, counts);
    GameState g = GameState::from_save(save, counts);
    competition_pointers(g.memory, exe);  // CompetitionPointers
    return g;
}

}  // namespace bmh

#include "game/matchday.hpp"
#include "game/season.hpp"
#include "game/week.hpp"

namespace bmh {

void new_career_markets_and_moods(GameState& g, GameRandom& game, WatcomRandom& rand) {
    StaticMemory& mem = g.memory;
    for (uint32_t k = 0; k < 12; k++) {
        if (k < 6) mem.set32(::bmh::layout::FundBlock + k * 0x1c, (1u << k) * 100000);
        const size_t s = size_t(k) * 0x5c;
        const uint32_t value = k * 2000 + 10000;
        for (int i = 0; i < 4; i++) g.stocks.at(s + size_t(i)) = uint8_t(value >> (8 * i));
        g.stocks.at(s + 0x4e) = uint8_t(k * 20 + 100), g.stocks.at(s + 0x4f) = uint8_t((k * 20 + 100) >> 8);
    }
    stir_random(game, rand);
    const int first = game.range(0, 1);
    if (g.texts) {
        auto name = [&](int record, uint32_t pointer) {
            const std::string& t = (*g.texts)[(pointer - ::bmh::layout::TextPointerTable) / 4];
            const size_t at = size_t(record) * 0xb0c + 0xa8;
            for (size_t i = 0; i <= t.size(); i++) g.manager_records.at(at + i) = i < t.size() ? uint8_t(t[i]) : 0;
        };
        name(first, ::bmh::layout::Career_NewCareerMarketsAndMoods_ExecutableField);
        name(1 - first, ::bmh::layout::Career_NewCareerMarketsAndMoods_ExecutableField2);
        name(2, ::bmh::layout::Career_NewCareerMarketsAndMoods_ExecutableField3);
        name(3, ::bmh::layout::Career_NewCareerMarketsAndMoods_ExecutableField4);
    }
    for (int day = 0; day < 30; day++) stock_market(g, game);
    for (uint32_t i = 0; i < 900; i++) mem.set8(::bmh::layout::Titles + i, 0xff);
    for (uint32_t i = 0; i < 4; i++) mem.set8(::bmh::layout::Career_NewCareerMarketsAndMoods_RuntimeField2 + i, uint8_t(1 << i));
    for (uint32_t i = 0; i < 0x2c; i++) mem.set16(::bmh::layout::Career_NewCareerMarketsAndMoods_RuntimeField + i * 0x58, 0xfff);
    reset_club_moods(g, game, rand, true);
}

bool new_career_players(GameState& g, GameRandom& game, WatcomRandom& rand, MinuteReport& report) {
    for (size_t slot = 0; slot < runtime_dimensions.pool_first; slot++) {
        const int id = g.squad_slots.at(slot) & 0x0fff;
        if (id == 0x0fff) continue;
        Player& p = g.players.at(size_t(id));
        p.w364 |= uint32_t(rand.range(4, 0x10)) << 21;
        p.w368 = (p.w368 & 0xffffc000u) + uint32_t(rand.range(0, 0x3fff));
    }
    for (int slot = runtime_dimensions.extra_slots_first; slot < runtime_dimensions.extra_slots_first + 0x36; slot++)
        if (!release_player(g, slot, game, rand, report)) return false;
    return !report.unsupported;
}

bool new_career_before_dialog(GameState& g, GameRandom& game, WatcomRandom& rand, MinuteReport& report) {
    // CareerFirstSteps2 clears all four pending offers immediately before calling
    // InitializeCareer. Raw executable BSS starts at zero, which is a valid offer
    // for manager/player zero in ResolvePendingOffer, rather than the empty sentinel.
    for (uint32_t offer = 0; offer < 4; ++offer)
        g.memory.set32(::bmh::layout::OfferSlots + offer * 4, 0xffffffff);
    new_career_markets_and_moods(g, game, rand);
    return new_career_players(g, game, rand, report);
}

}  // namespace bmh

#include "game/lineup.hpp"
#include "game/national.hpp"
#include "game/weekly.hpp"

namespace bmh {

namespace {

constexpr size_t kRecord = 0xb0c;

uint16_t team_of(const GameState& g, int m) {
    const size_t at = size_t(m) * kRecord + 0x290;
    return uint16_t(g.manager_records.at(at) | g.manager_records.at(at + 1) << 8);
}

// FindManagerForClub: a club of a human manager (manager slot 4 counts when manager 0 coaches
// the national team).
bool human_club(const GameState& g, int team) {
    const int managers = g.memory.u8(::bmh::layout::ManagerCount);
    for (int m = 0; m < managers; m++)
        if (team_of(g, m) == uint16_t(team)) return true;
    return team_of(g, 0) == kNoTeam && team_of(g, 4) == uint16_t(team);
}

// OpenBMH deviates from the original: skips reserved calendar positions when assigning a joining manager to a club.
// The supported rule profile patch reserves club places whose calendar word (CompetitionCalendar, read with the
// club id as index) has the bits 0x4001.
bool reserved_place(const GameState& g, int team) {
    return team >= 0 && (g.memory.u16(::bmh::layout::CompetitionCalendar + uint32_t(team) * 2) & 0x4001) == 0x4001;
}

// EuropeanClub: only the active prefixes of the three European lists count.
bool european_club(const GameState& g, int team) {
    for (const auto& entry : {std::pair<uint32_t, uint32_t>{::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField25, ::bmh::layout::Career_EuropeanClub_ExecutableField},
                             {::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField14, ::bmh::layout::Career_EuropeanClub_ExecutableField2}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField19, ::bmh::layout::Career_EuropeanClub_ExecutableField3}})
        for (uint32_t i = 0; i < g.memory.u8(entry.second); ++i)
            if (g.memory.u16(entry.first + i * 2) == uint16_t(team)) return true;
    return false;
}

// RemoveJoinTransfers: remove a joining club's sale entries and parallel bid fields.
void remove_join_transfers(GameState& g, int team) {
    StaticMemory& mem = g.memory;
    for (uint32_t row = 0; row < 44; ++row) {
        const uint32_t base = ::bmh::layout::EditorList + row * 0x58;
        if ((mem.u16(base + 8) & 0x7fff) == uint16_t(team)) {
            g.squad_slots.at(kPoolSlots + row) = kEmptySlot;
            continue;
        }
        int bid = 0;
        while (bid < 4 && mem.u16(base + uint32_t(bid) * 2) != uint16_t(team)) ++bid;
        if (bid == 4) continue;
        const uint32_t found = uint32_t(bid);
        if (bid != 3) {
            for (uint32_t k = found; k < 3; ++k) {
                mem.set16(base + k * 2, mem.u16(base + (k + 1) * 2));
                mem.set32(base + 0xc + k * 4, mem.u32(base + 0xc + (k + 1) * 4));
                mem.set32(base + 0x1c + k * 4, mem.u32(base + 0x1c + (k + 1) * 4));
                mem.set8(base + 0x34 + k, mem.u8(base + 0x34 + k + 1));
                for (uint32_t b = 0; b < 7; ++b)
                    mem.set8(base + 0x38 + k * 7 + b, mem.u8(base + 0x38 + (k + 1) * 7 + b));
            }
            ++bid;  // The Source terminator is found + 1, not always the last bid.
        }
        mem.set16(base + uint32_t(bid) * 2, 0xffff);
        if (row > 39) {
            g.squad_slots.at(kPoolSlots + row + 40) = kEmptySlot;
            mem.set32(::bmh::layout::Career_RemoveJoinTransfers_RuntimeField + row * 4, 0xffffffff);
        }
    }
}

// ConfirmManager: clean loans only after the final club swap/remapping.
bool remove_join_loans(GameState& g, int team, MinuteReport& report) {
    StaticMemory& mem = g.memory;
    for (uint32_t i = 0; i < 200; ++i) {
        const uint32_t at = ::bmh::layout::Career_RemoveJoinLoans_RuntimeField + i * 4, loan = mem.u32(at);
        if (loan == 0) continue;
        const int owner = int((loan >> 12) & 0x3ff), borrower = int((loan >> 22) & 0x3ff);
        if (owner == team) mem.set32(at, 0);
        else if (borrower == team) {
            mem.set32(at, 0);
            if (human_club(g, owner)) {
                uint32_t k = 0;
                while (k < 12 && mem.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField + k * 2) != (loan & 0xfff)) ++k;
                if (k == 12) {
                    report.fail("joining-manager loan has no human owner entry (PresentLoanReturn(0x3c))");
                    return false;
                }
                mem.set16(::bmh::layout::Career_CareerManagersEnd_RuntimeField + k * 2, kEmptySlot);
            }
        }
    }
    return true;
}

void put32(std::vector<uint8_t>& v, size_t o, uint32_t x) {
    for (int i = 0; i < 4; i++) v.at(o + size_t(i)) = uint8_t(x >> (8 * i));
}

// CareerAddManager2(team, mode): condition and technique of every player of a league club around
// a level of its league (mode 0 random W(min, max), 1 min + 3, 2 the middle + 1, 3 max - 3).
void set_club_strength(GameState& g, int team, int mode, WatcomRandom& rand) {
    if (team >= kLeagueTeams) return;
    const int league = team_league(team);
    const int lo = kLeagueStrength[league][0], hi = kLeagueStrength[league][1];
    int level = mode;
    switch (mode) {
        case 0: level = rand.range(lo, hi); break;
        case 1: level = lo + 3; break;
        case 2: level = (hi + lo) / 2 + 1; break;
        case 3: level = hi - 3; break;
        default: break;
    }
    level = int16_t(level);
    const SquadRange r = squad_range(team);
    for (size_t s = r.first; s < r.first + r.count; s++) {
        const int id = g.squad_slots.at(s) & 0x0fff;
        if (id == 0x0fff) continue;
        Player& p = g.players.at(size_t(id));
        const int condition = std::clamp(rand.range(level - 6, level + 6), 0, 99);  // ClampInteger
        const int technique = std::clamp(rand.range(level - 6, level + 6), 0, 99);
        p.w35c = (p.w35c & 0xffe0007fu) | uint32_t(condition) << 14 | uint32_t(technique) << 7;
    }
}

// CareerAddManager3(record, league, mode): the stadium of a human club - eight stand blocks
// (Career_BuildStadium_RuntimeField4[league] thousand places each), the facilities of the league
// (Career_BuildStadium_RuntimeField, levels W(Career_BuildStadium_RuntimeField2, Career_BuildStadium_RuntimeField3) << 2) and eight block values W(100, 140).
// Mode 1 (CareerDuration) builds everything one step better.
void build_stadium(GameState& g, const LeImage& exe, int m, int league, int mode, WatcomRandom& rand) {
    uint8_t* rec = &g.manager_records.at(size_t(m) * kRecord);
    for (int k = 0; k < 8; k++) {
        rec[0x4c + k] = uint8_t(exe.u8(::bmh::layout::Career_BuildStadium_RuntimeField4 + uint32_t(league)) + (mode == 1 ? 1 : 0));
        rec[0x5c] = uint8_t(rec[0x5c] + rec[0x4c + k]);
    }
    for (int i = 0; i < 0x16; i++) {
        const uint8_t level = mode == 1 ? 3 : exe.u8(::bmh::layout::Career_BuildStadium_RuntimeField + uint32_t(i + league * 0x17));
        if (i == 0x10 || i == 0x11 || level == 0) continue;
        if (i == 0) {
            rec[0] = uint8_t(exe.u8(::bmh::layout::Career_BuildStadium_RuntimeField + uint32_t(league * 0x17)) + (mode == 1 ? 0x55 : 0));
        } else {
            rec[i] = uint8_t(rand.range(exe.u8(::bmh::layout::Career_BuildStadium_RuntimeField2 + uint32_t(i * 5)), exe.u8(::bmh::layout::Career_BuildStadium_RuntimeField3 + uint32_t(i * 5))) << 2);
        }
        if (i > 0xd) rec[i + 0x2e] = uint8_t(level + (mode == 1 ? 1 : 0));
    }
    rec[0x3f] = rec[0x3e] = uint8_t((mode == 1) + 1);
    for (int k = 0x16; k < 0x1e; k++) rec[k] = uint8_t(rand.range(100, 0x8c));
}

}  // namespace

// StartingSponsors: in mode GameMode == 1 or in the one-year mode before
// the first season day, the three sponsor contracts start at the value for the
// selected league and difficulty. Use GameState.date: memory CalendarDay can be
// stale in the port after a new-career/season transition.
void starting_sponsors(GameState& g, int m) {
    const StaticMemory& mem = g.memory;
    if (!(mem.u8(::bmh::layout::GameMode) == 1 || (mem.u8(::bmh::layout::CareerDuration) == 1 && g.date == 0))) return;
    const size_t rec = size_t(m) * kRecord;
    int league = g.manager_records.at(rec + 0x210);
    if (league > 2) league--;
    const int v = (((league * -20 + 80) * 60000 + 1000000) / 12 * (mem.u8(::bmh::layout::Difficulty) * -5 + 0xaa)) / 3000000 * 10000;
    for (size_t o : {size_t(0xae8), size_t(0xadc), size_t(0xa34)}) put32(g.manager_records, rec + o, uint32_t(v));
    for (size_t o : {size_t(0xaed), size_t(0xae1), size_t(0xa39)}) g.manager_records.at(rec + o) = 1;
}

void career_managers_begin(GameState& g, bool adding) {
    g.memory.add_block(::bmh::layout::CareerSession_FormatError_RuntimeField6, 1);
    g.memory.set8(::bmh::layout::CareerSession_FormatError_RuntimeField6, 1);
    const size_t rec = 4 * kRecord;
    put32(g.manager_records, rec + 0x314, 1000000);
    put32(g.manager_records, rec + 0x324, 1000000);
    for (size_t i = 0; i < 0xd; i++) g.manager_records.at(rec + 0xc5 + i) = 2;
    if (adding) {
        const size_t count = g.memory.u8(::bmh::layout::ManagerCount);
        g.manager_teams.resize(count);
        for (size_t m = 0; m < count; ++m) g.manager_teams[m] = team_of(g, int(m));
    } else g.manager_teams.assign(g.memory.u8(::bmh::layout::ManagerCount), 0);
    // ConfirmManager(0) posts PostSeasonNotice(0xff) immediately after the accepted
    // new-career options dialog. The join path starts at a nonzero manager slot and
    // must not repeat these three introduction messages.
    if (adding || g.manager_teams.empty() || g.manager_teams.size() > 4) return;
    post_text(g, -1, 1001);  // CareerRuntimeField3
    post_text(g, -1, 1002);  // CareerRuntimeField4
    post_text(g, -1, 1003);  // CareerRuntimeField5
}

bool career_add_manager(GameState& g, const LeImage& exe, int m, const CareerManager& choice, bool adding,
                        GameRandom& game, WatcomRandom& rand, MinuteReport& report) {
    // Reject malformed requests before changing records or consuming either RNG.
    if (!g.memory.has(::bmh::layout::ManagerCount) || m < 0 || m >= 4 ||
        g.memory.u8(::bmh::layout::ManagerCount) > 4 || m >= g.memory.u8(::bmh::layout::ManagerCount) ||
        size_t(m) >= g.manager_teams.size() || g.manager_records.size() < 5 * kRecord ||
        choice.team < 0 || choice.team >= runtime_dimensions.record_teams || size_t(choice.team) >= g.club_ids.size() ||
        choice.league < 0 || choice.league > 4 || choice.strength < 0 || choice.strength > 2 ||
        choice.portrait < 0 || choice.portrait > 11 || choice.name.size() > 24 ||
        g.tactics.size() < size_t(m + 1) * 0x840 ||
        g.manager_ratings.size() < size_t(m + 1) * 0x3c ||
        g.manager_results.size() < g.manager_teams.size() * (runtime_dimensions.record_teams * 10) ||
        g.squad_slots.size() < kSquadSlots || g.club_ids.size() < runtime_dimensions.record_teams ||
        g.team_morale.size() < runtime_dimensions.record_teams || g.team_flags.size() < runtime_dimensions.record_teams ||
        g.club_heap_bc.size() < runtime_dimensions.record_teams * 2 || g.club_heap_78.size() < runtime_dimensions.record_teams * 2 ||
        g.club_heap_b8.size() < runtime_dimensions.record_teams || g.position_history.size() < runtime_dimensions.league_teams * 0x1a ||
        (!g.club_table.empty() && g.club_table.size() < runtime_dimensions.record_teams * 0x4a) ||
        g.player_bonus.size() < g.players.size() || g.player_cards.size() < g.players.size() ||
        !exe.contains(::bmh::layout::Career_CareerAddManager_ExecutableField) || !exe.contains(::bmh::layout::Career_BuildStadium_RuntimeField3 + 0x15 * 5) ||
        !exe.contains(::bmh::layout::Career_BuildStadium_RuntimeField + 4 * 0x17 + 0x15)) {
        report.fail("invalid career manager choice or slot");
        return false;
    }
    for (int other = 0; other < m; ++other)
        if (g.manager_teams[size_t(other)] == choice.team) {
            report.fail("career manager club is already managed");
            return false;
        }
    for (size_t slot = 0; slot < kPoolSlots; ++slot) {
        const uint16_t player = g.squad_slots[slot] & 0x0fff;
        if (player != kEmptySlot && size_t(player) >= g.players.size()) {
            report.fail("invalid career manager squad player");
            return false;
        }
    }
    StaticMemory& mem = g.memory;
    const size_t rec = size_t(m) * kRecord;
    std::vector<uint8_t>& r = g.manager_records;
    for (size_t i = 0; i < 0x12; i++) r.at(rec + 0x1f6 + i) = 1;
    for (size_t i = 0; i < 10; i++) r.at(rec + 0x8b6 + i) = 0xff;
    r.at(rec + 0x1f6) = 0x40;
    // the values of the dialog (ManagerScreen2)
    for (size_t i = 0; i < 0x19; i++) r.at(rec + 0xa8 + i) = i < choice.name.size() ? uint8_t(choice.name[i]) : 0;
    r.at(rec + 0x290) = uint8_t(choice.team), r.at(rec + 0x291) = uint8_t(choice.team >> 8);
    r.at(rec + 0x221) = uint8_t(choice.portrait);
    r.at(rec + 0x210) = uint8_t(choice.league);
    g.manager_teams.at(size_t(m)) = int16_t(choice.team);

    int place = 1 + m;  // the club's place in the first league
    if (!adding && mem.u8(::bmh::layout::CareerDuration) <= 1) mem.set8(::bmh::layout::Career_CareerAddManager_RuntimeField, 0);
    else place = m + 9;
    while (reserved_place(g, place) || human_club(g, place) || (adding && european_club(g, place))) place++;  // CareerAddManager4
    int league = r.at(rec + 0x210);  // local_18
    const int team = int16_t(team_of(g, m));
    if (adding) remove_join_transfers(g, team);
    if (team_league(team) != league) {
        int other = place;
        if (league != 0) {
            const int first = league_first_team(league), last = first + 12;  // CareerRuntimeField6 (league * 18 in DE)
            do other = int16_t(game.range(first, last));
            while (human_club(g, other));  // ReservedPlace2
            // ReservedPlace: the next of 13 places that is neither reserved nor human
            int c = other;
            for (int k = 0; k < 13; k++) {
                if (!reserved_place(g, c) && !human_club(g, c)) {
                    other = c;
                    break;
                }
                if (++c > last) c -= 13;
            }
        }
        if (adding) remove_join_transfers(g, other);
        clubs_change_places(g, team, other);
        set_club_strength(g, team, 0, rand);
        place = 0;
    }
    if (league == 0 && place != 0) {
        if (adding) remove_join_transfers(g, place);
        clubs_change_places(g, int16_t(team_of(g, m)), place);
    }
    set_club_strength(g, int16_t(team_of(g, m)), choice.strength + 1, rand);

    const int club = int16_t(team_of(g, m));
    if (adding && !remove_join_loans(g, club, report)) return false;
    g.team_morale.at(size_t(club)) |= 0x80;
    r.at(rec + 0x26f) = 0x32;
    if (league > 2) league--;
    starting_sponsors(g, m);
    r.at(rec + 0x26e) = uint8_t(std::max(20 - 2 * league, 14));
    build_stadium(g, exe, m, league, mem.u8(::bmh::layout::CareerDuration), rand);
    if (league > 2) league = 2;
    for (uint32_t i = 0; i < 3; i++) {
        const uint32_t base = ::bmh::layout::Career_CareerAddManager_RuntimeField2 + uint32_t(m) * 0x24;
        mem.set8(base + i, uint8_t(0x52 - league * 15));
        mem.set8(base + 3 + i, 6);
        mem.set8(base + 6 + i, uint8_t(league));
        mem.set32(base + 0x18 + i * 4, 5000);
    }
    const SquadRange squad = squad_range(club);
    r.at(rec + 0x288) = 0x16;
    for (size_t i = 0; i < 0xd; i++) r.at(rec + 0xc5 + i) = 2;
    for (size_t i = 0; i < 6; i++) r.at(rec + 0x8aa + 2 * i) = 0x26, r.at(rec + 0x8ab + 2 * i) = 0x02;
    for (size_t i = 0; i < 0x3c; i++) g.manager_ratings.at(size_t(m) * 0x3c + i) = 100;
    const int players = adding ? squad_count(g, club, 2) : 20;
    for (int i = 0; i < players; i++) {
        const size_t slot = squad.first + size_t(i);
        Player& p = g.players.at(size_t(g.squad_slots.at(slot) & 0x0fff));
        p.w364 &= ~(0x1fu << 21);
        p.w364 |= uint32_t(rand.range(4, 9)) << 21;
        new_board_entry(g, m, i, int(slot), rand);
        g.tactics.at(size_t(m) * 0x840 + size_t(i) * 0x58 + 0xd) = 100;
    }
    // Deviation from the original: a new career fills exactly 20 board rows (local_28 = 0x14),
    // but new_career_players runs before the dialog and may already have moved a free agent
    // into the chosen club (transfer_bid, "straight to the club"). That 21st player had no
    // board row - no salary, and team_rating divides by his base pay (the original crashes
    // once he plays). The remaining occupied rows get their entry the same way; with the
    // usual 20 players nothing changes and no random number is drawn.
    if (!adding)
        for (size_t i = 20; i < squad.count && i < 24; i++) {
            const size_t slot = squad.first + i;
            const int id = g.squad_slots.at(slot) & 0x0fff;
            if (id == 0x0fff) continue;
            Player& p = g.players.at(size_t(id));
            p.w364 &= ~(0x1fu << 21);
            p.w364 |= uint32_t(rand.range(4, 9)) << 21;
            new_board_entry(g, m, int(i), int(slot), rand);
            g.tactics.at(size_t(m) * 0x840 + i * 0x58 + 0xd) = 100;
        }
    for (size_t i = 0; i < (runtime_dimensions.record_teams * 10); i++) g.manager_results.at(size_t(m) * (runtime_dimensions.record_teams * 10) + i) = 0xff;
    const uint8_t level = mem.u8(::bmh::layout::Difficulty);
    const uint32_t money = level == 0 ? 50000000u : 4000000u - level * 200000u;
    put32(r, rec + 0x314, money);
    put32(r, rec + 0x324, money);
    r.at(rec + 0x219) = r.at(rec + 0x218) = 0xf;
    put32(r, rec + 0x31c, uint32_t(400000 - league * 100000));
    put32(r, rec + 0x310, 1);
    auto_lineup(g, club, 100);
    return !report.unsupported;
}

void career_managers_end(GameState& g, bool adding) {
    g.memory.add_block(::bmh::layout::CareerSession_FormatError_RuntimeField6, 1);
    g.memory.set8(::bmh::layout::CareerSession_FormatError_RuntimeField6, 0);
    if (!adding)
        for (uint32_t i = 0; i < 12; i++) g.memory.set16(::bmh::layout::Career_CareerManagersEnd_RuntimeField + i * 2, 0xfff);
}

}  // namespace bmh

namespace bmh {

// The English counterpart of CompetitionPointers: the Football Limited GermanRoundTable. The cup table
// (translated to CupTable, launch/fltd.hpp) is the English one; its entries 0-24 have the
// same roles as in German, so their arrays are the German ones (same sizes). Different:
// entry 0 (FA Cup qualifying stage) and the English-only competitions - entry 28 (FA Cup
// proper), 25-27 (League Cup) and 35 - which use memory blocks at their English addresses
// (english_competition_blocks); there are no indoor tournaments and no Masters.
namespace {

// English round-string pointer tables -> the German ones the engine knows (cup.cpp)
uint32_t german_round_table(uint32_t en) {
    switch (en) {
        case ::bmh::layout::Career_GermanRoundTable_ExecutableField2: return ::bmh::layout::CareerSession_RuntimeError_RuntimeField;  // 4 teams
        case ::bmh::layout::Career_GermanRoundTable_ExecutableField3: return ::bmh::layout::CareerSession_RuntimeError_RuntimeField2;  // 6
        case ::bmh::layout::Career_GermanRoundTable_ExecutableField: return ::bmh::layout::CareerSession_RuntimeError_ExecutableField2;  // 20
        case ::bmh::layout::Career_GermanRoundTable_ExecutableField4: return ::bmh::layout::CareerSession_RuntimeError_RuntimeField3;  // 12
        default: return en;
    }
}

void competition_pointers_english(StaticMemory& mem, const LeImage& exe) {
    auto entry = [](uint32_t k) { return ::bmh::layout::CupTable + k * 0x28; };
    english_competition_blocks(mem);
    for (uint32_t k = 1; k < 0xf; k++) {
        uint32_t b = k;
        if (k > 9) b -= 9;
        if (k > 0xc) b -= 3;
        mem.set_pointer(entry(k) + 8, ::bmh::layout::CareerSession_RuntimeError_RuntimeField);
        mem.set_pointer(entry(k), ::bmh::layout::Field0 + b * 8 - 8);
        mem.set_pointer(entry(k) + 0xc, ::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField13 + (b - 1) * 0xc);
    }
    uint32_t k = 7;
    for (uint32_t i = 0; i < 8; i += 2, k++) {
        if (k == 10) k = 0xc;
        mem.set_pointer(entry(k), ::bmh::layout::Field0 + uint32_t(exe.u8(::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField10 + i)) * 2);
        mem.set_pointer(entry(k) + 0xc, ::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField13 + uint32_t(exe.u8(::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField11 + i)) * 2);
    }
    const std::pair<uint32_t, uint32_t> single[] = {
        {::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField2, ::bmh::layout::CareerSession_RuntimeError_RuntimeField2}, {::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField3, ::bmh::layout::Field0}, {::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField4, ::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField13},
        {::bmh::layout::CupTable, kEnglishFaQualifyingField}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField, kEnglishFaResults},
        {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField2, ::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField25}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField3, ::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField22}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField4, ::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField23}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField5, ::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField30}};
    for (const auto& [at, to] : single) mem.set_pointer(at, to);
    for (uint32_t i = 0; i < 2; i++) mem.set_pointer(::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField5 + i * 4, ::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField17 + i * 0x10);
    for (uint32_t i = 0; i < 4; i++) {
        const uint32_t e = entry(exe.u8(::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField12 + i));
        mem.set_pointer(e, ::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField14 + i * 8 + 0x30);
        mem.set_pointer(e + 0xc, ::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField17 + i * 0x18 + 0x20);
        mem.set_pointer(e + 8, ::bmh::layout::CareerSession_RuntimeError_RuntimeField);
    }
    mem.set_pointer(::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField6, ::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField15);
    for (uint32_t i = 0; i < 2; i++) mem.set_pointer(::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField7 + i * 4, ::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField17 + 0x80 + i * 0xc);
    const std::pair<uint32_t, uint32_t> more[] = {
        {::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField8, ::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField16}, {::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField9, ::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField18}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField6, ::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField19}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField7, ::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField26}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField8, ::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField21},
        {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField9, ::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField20}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField10, ::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField27}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField11, ::bmh::layout::ReplayFixtures}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField12, ::bmh::layout::ReplayResults}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField13, ::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField28},
        {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField14, ::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField31}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField15, ::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField24}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField16, ::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField29}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField17, ::bmh::layout::FriendlyTeams}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField18, ::bmh::layout::FriendlyDays},
        // the English-only cup entry table (entries 25-28 and 35)
        {entry(28), ::bmh::layout::EnglishFaCupProperField}, {entry(28) + 0xc, ::bmh::layout::EnglishFaCupStageResults},                              // FA Cup proper
        {entry(25), ::bmh::layout::EnglishLeagueCupFirstRoundField}, {entry(25) + 0xc, ::bmh::layout::EnglishLeagueCupFirstRoundResults},                              // League Cup
        {entry(26), ::bmh::layout::EnglishLeagueCupQuarterFinalField}, {entry(26) + 0xc, ::bmh::layout::EnglishLeagueCupQuarterFinalResults}, {entry(26) + 0x10, ::bmh::layout::EnglishLeagueCupQuarterFinalOtherLegResults},
        {entry(27), ::bmh::layout::EnglishLeagueCupSemiFinalField}, {entry(27) + 0xc, ::bmh::layout::EnglishLeagueCupSemiFinalResults},
        {entry(35) + 8, ::bmh::layout::CareerSession_RuntimeError_RuntimeField}, {entry(35), ::bmh::layout::EnglishAdditionalCupField}, {entry(35) + 0xc, ::bmh::layout::EnglishAdditionalCupResults}};
    for (const auto& [at, to] : more) mem.set_pointer(at, to);
    // round tables the English table holds itself (static pointers to English strings)
    for (uint32_t e = 0; e < 0x24; e++) {
        const uint32_t at = entry(e) + 8;
        const uint32_t p = mem.u32(at);
        if (p >= ::bmh::layout::EnDataStart && p < ::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField) mem.set_pointer(at, german_round_table(p));
    }
}

}  // namespace

void english_competition_blocks(StaticMemory& mem) {
    for (const auto& [at, size] : kEnglishCompetitionBlocks)
        if (!mem.has(at)) mem.add_block(at, size);
}

void competition_pointers(StaticMemory& mem, const LeImage& exe) {
    if (kEnglishCompetitions) {
        competition_pointers_english(mem, exe);
        return;
    }
    auto entry = [](uint32_t k) { return ::bmh::layout::CupTable + k * 0x28; };
    // the cups of the leagues 1..14 (entries 1..14)
    for (uint32_t k = 1; k < 0xf; k++) {
        uint32_t b = k;
        if (k > 9) b -= 9;
        if (k > 0xc) b -= 3;
        mem.set_pointer(entry(k) + 8, ::bmh::layout::CareerSession_RuntimeError_RuntimeField);
        mem.set_pointer(entry(k), ::bmh::layout::Field0 + b * 8 - 8);
        mem.set_pointer(entry(k) + 0xc, ::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField13 + (b - 1) * 0xc);
    }
    // entries 7, 8, 9, 12 (Career_CompetitionPointersEnglish_RuntimeField10 pairs)
    uint32_t k = 7;
    for (uint32_t i = 0; i < 8; i += 2, k++) {
        if (k == 10) k = 0xc;
        mem.set_pointer(entry(k), ::bmh::layout::Field0 + uint32_t(exe.u8(::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField10 + i)) * 2);
        mem.set_pointer(entry(k) + 0xc, ::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField13 + uint32_t(exe.u8(::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField11 + i)) * 2);
    }
    const std::pair<uint32_t, uint32_t> single[] = {
        {::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField2, ::bmh::layout::CareerSession_RuntimeError_RuntimeField2}, {::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField3, ::bmh::layout::Field0}, {::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField4, ::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField13}, {::bmh::layout::CupTable, ::bmh::layout::Field}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField, ::bmh::layout::Career_CompetitionPointers_ExecutableField},
        {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField2, ::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField25}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField3, ::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField22}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField4, ::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField23}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField5, ::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField30}};
    for (const auto& [at, to] : single) mem.set_pointer(at, to);
    for (uint32_t i = 0; i < 2; i++) mem.set_pointer(::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField5 + i * 4, ::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField17 + i * 0x10);
    for (uint32_t i = 0; i < 4; i++) {
        const uint32_t e = entry(exe.u8(::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField12 + i));
        mem.set_pointer(e, ::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField14 + i * 8 + 0x30);
        mem.set_pointer(e + 0xc, ::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField17 + i * 0x18 + 0x20);
        mem.set_pointer(e + 8, ::bmh::layout::CareerSession_RuntimeError_RuntimeField);
    }
    mem.set_pointer(::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField6, ::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField15);
    for (uint32_t i = 0; i < 2; i++) mem.set_pointer(::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField7 + i * 4, ::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField17 + 0x80 + i * 0xc);
    const std::pair<uint32_t, uint32_t> more[] = {
        {::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField8, ::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField16}, {::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField9, ::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField18}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField6, ::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField19}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField7, ::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField26}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField8, ::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField21},
        {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField9, ::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField20}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField10, ::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField27}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField11, ::bmh::layout::ReplayFixtures}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField12, ::bmh::layout::ReplayResults}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField13, ::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField28},
        {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField14, ::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField31}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField15, ::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField24}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField16, ::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField29}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField17, ::bmh::layout::FriendlyTeams}, {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField18, ::bmh::layout::FriendlyDays}};
    for (const auto& [at, to] : more) mem.set_pointer(at, to);
    // the indoor tournaments and the Masters (entries 25..29)
    for (uint32_t i = 0; i < 5; i++) {
        const uint32_t e = entry(0x19 + i);
        mem.set_pointer(e + 8, ::bmh::layout::CareerSession_RuntimeError_RuntimeField);
        mem.set_pointer(e + 0xc, ::bmh::layout::MatchdayGroupWorkspace + uint32_t(exe.u8(::bmh::layout::Career_CompetitionPointers_RuntimeField4 + i)) * 2);
        mem.set_pointer(e, ::bmh::layout::Teams + uint32_t(exe.u8(::bmh::layout::Career_CompetitionPointers_RuntimeField5 + i)) * 2);
    }
    mem.set_pointer(::bmh::layout::Career_CompetitionPointers_RuntimeField2, ::bmh::layout::CareerSession_RuntimeError_RuntimeField);
    mem.set_pointer(::bmh::layout::Career_CompetitionPointers_RuntimeField, ::bmh::layout::Teams);
    mem.set_pointer(::bmh::layout::Career_CompetitionPointers_RuntimeField3, ::bmh::layout::MatchdayGroupWorkspace);
}

}  // namespace bmh
