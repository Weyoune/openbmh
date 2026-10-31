// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/state.hpp"

#include <stdexcept>

#include "game/rating.hpp"

namespace bmh {

namespace {

// Squad table layout constants of V2.24 (TeamsWithSquads, LargeSquadClubCount, NoTeamSquadStart).
const int& kBigSquadTeams = runtime_dimensions.big_squad_teams;
const int& kNoTeamSquadStart = runtime_dimensions.national_slots_first;
const int& kTeamsWithSquads = runtime_dimensions.record_teams;

// C-style signed division by 8 as compiled by Watcom (rounds toward zero).
int div8(int x) { return x / 8; }

}  // namespace

GameState GameState::from_save(const SaveState& save, const DbCounts& c) {
    GameState g; g.counts=c;
    const SaveBlock& squads = save.block(::bmh::layout::SquadSlots);
    for (size_t i = 0; i + 1 < squads.size; i += 2)
        g.squad_slots.push_back(uint16_t(save.data(::bmh::layout::SquadSlots)[i] | save.data(::bmh::layout::SquadSlots)[i + 1] << 8));

    auto word = [&](uint32_t block, size_t i) {
        const uint8_t* p = save.data(block) + i * 4;
        return uint32_t(p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24);
    };
    g.players.resize(c.players);
    for (size_t i = 0; i < c.players; i++)
        g.players[i] = Player{"", word(::bmh::layout::PlayerConditionTechniqueForm, i), word(::bmh::layout::PlayerSkillWords, i), word(::bmh::layout::PlayerIdentityWords, i)};

    const SaveBlock& morale = save.block(::bmh::layout::TeamMorale);
    g.team_morale.assign(save.data(::bmh::layout::TeamMorale), save.data(::bmh::layout::TeamMorale) + morale.size);

    const SaveBlock& flags = save.block(::bmh::layout::TeamFlags);
    for (size_t i = 0; i + 1 < flags.size; i += 2)
        g.team_flags.push_back(uint16_t(save.data(::bmh::layout::TeamFlags)[i] | save.data(::bmh::layout::TeamFlags)[i + 1] << 8));

    const SaveBlock& bonus = save.block(::bmh::layout::PlayerBonuses);
    g.player_bonus.assign(save.data(::bmh::layout::PlayerBonuses), save.data(::bmh::layout::PlayerBonuses) + bonus.size);

    const SaveBlock& form = save.block(::bmh::layout::Formations);
    g.formations.assign(save.data(::bmh::layout::Formations), save.data(::bmh::layout::Formations) + form.size);

    // Manager records: 5 x 0xB0C bytes, the team id is the i16 at +0x290.
    int managers = save.data(::bmh::layout::ManagerCount)[0];
    for (int m = 0; m < managers; m++) {
        const uint8_t* p = save.data(::bmh::layout::ManagerRecords) + m * 0xB0C + 0x290;
        g.manager_teams.push_back(int16_t(p[0] | p[1] << 8));
    }
    g.manager_records.assign(save.data(::bmh::layout::ManagerRecords), save.data(::bmh::layout::ManagerRecords) + save.block(::bmh::layout::ManagerRecords).size);
    g.tactics.assign(save.data(::bmh::layout::TacticsBoards), save.data(::bmh::layout::TacticsBoards) + save.block(::bmh::layout::TacticsBoards).size);
    g.level = save.data(::bmh::layout::Difficulty)[0];
    g.manager_options.assign(save.data(::bmh::layout::ManagerAutomation), save.data(::bmh::layout::ManagerAutomation) + save.block(::bmh::layout::ManagerAutomation).size);

    // Matchday bookkeeping. The club records are dumped twice (pointer, then the heap
    // data if the pointer was set); the heap copy is the one needed.
    for (const SaveBlock& b : save.blocks())
        if (b.address == ::bmh::layout::TournamentName && b.heap)
            g.tournament_name.assign(save.stream().begin() + b.offset, save.stream().begin() + b.offset + b.size);
        else if (b.address == ::bmh::layout::ClubSeasonRecords && b.heap)
            g.club_table.assign(save.stream().begin() + b.offset, save.stream().begin() + b.offset + b.size);
        else if (b.heap && (b.address == ::bmh::layout::ClubAuxiliaryWords || b.address == ::bmh::layout::ClubBadgeSelectors || b.address == ::bmh::layout::ClubAuxiliaryBytes)) {
            auto& v = b.address == ::bmh::layout::ClubAuxiliaryWords ? g.club_heap_bc : b.address == ::bmh::layout::ClubBadgeSelectors ? g.club_heap_78 : g.club_heap_b8;
            v.assign(save.stream().begin() + b.offset, save.stream().begin() + b.offset + b.size);
        } else if (b.heap && b.address == ::bmh::layout::MasterDatabaseMetadata) {
            g.master_database_metadata.assign(save.stream().begin() + b.offset, save.stream().begin() + b.offset + b.size);
        }
    auto words16 = [&](uint32_t address) {
        std::vector<uint16_t> v;
        const SaveBlock& b = save.block(address);
        for (size_t i = 0; i + 1 < b.size; i += 2) v.push_back(uint16_t(save.data(address)[i] | save.data(address)[i + 1] << 8));
        return v;
    };
    g.club_ids = words16(::bmh::layout::ClubIdentities);
    g.player_cards = words16(::bmh::layout::PlayerCards);
    auto u32at = [&](uint32_t address) {
        const uint8_t* p = save.data(address);
        return uint32_t(p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24);
    };
    g.date = u32at(::bmh::layout::CalendarDay);
    g.season_start = g.date - uint32_t(save.data(::bmh::layout::DaysIntoSeason)[0] | save.data(::bmh::layout::DaysIntoSeason)[1] << 8);
    g.development_balance = int32_t(u32at(::bmh::layout::PlayerDevelopmentBalance));
    // PlayerTrendRotation is not saved; it restarts at its initial value 0 after loading.
    auto bytes = [&](uint32_t address) {
        return std::vector<uint8_t>(save.data(address), save.data(address) + save.block(address).size);
    };
    g.club_kits = bytes(layout::ClubKitSelectors);
    g.club_short_offsets = bytes(layout::ClubShortNameOffsets);
    g.manager_results = bytes(::bmh::layout::ManagerMatchHistory);
    g.manager_ratings = bytes(::bmh::layout::ManagerRatings);
    g.position_history = bytes(::bmh::layout::LeagueRankHistory);
    g.league_rounds = bytes(::bmh::layout::LeagueRoundCounters);
    g.results = bytes(::bmh::layout::LeagueResults);
    g.stocks = bytes(::bmh::layout::StockExchange);
    g.foreign_strength = bytes(::bmh::layout::ForeignClubStrength);
    g.memory = StaticMemory(save);
    // memory OpenBMH saves behind the trailer (the English cup blocks, career.hpp)
    for (const auto& b : save.extension()) {
        if (!g.memory.has(b.address)) g.memory.add_block(b.address, uint32_t(b.bytes.size()));
        for (size_t i = 0; i < b.bytes.size(); ++i) g.memory.set8(b.address + uint32_t(i), b.bytes[i]);
    }
    g.mailbox = bytes(::bmh::layout::ManagerMailbox);
    g.club_names = save.pool(0);
    g.player_names = save.pool(1);
    g.referee_names = save.pool(2);
    g.stadium_names = save.pool(3);
    g.player_name_offsets = words16(::bmh::layout::PlayerNameOffsets);
    g.season = int16_t(save.data(::bmh::layout::SeasonIndex)[0] | save.data(::bmh::layout::SeasonIndex)[1] << 8);
    return g;
}

uint8_t GameState::club_kit(int team) const {
    return !projected_match_clubs.empty() || counts.league_teams == 164 ? club_kits.at(size_t(team)) : memory.u8(layout::ClubKitSelectors + uint32_t(team));
}
uint8_t GameState::club_short_offset(int team) const {
    return !projected_match_clubs.empty() || counts.league_teams == 164 ? club_short_offsets.at(size_t(team)) : memory.u8(layout::ClubShortNameOffsets + uint32_t(team));
}

int GameState::manager_of(int team) const {
    for (size_t m = 0; m < manager_teams.size(); m++)
        if (manager_teams[m] == team) return int(m);
    return -1;
}

std::string GameState::club_name(int team) const {
    team &= 0xFFF;
    if (team == 0xFFF) return texts ? (*texts)[(::bmh::layout::NoClubText - ::bmh::layout::TextPointerTable) / 4] : std::string();
    std::string s;
    for (size_t i = club_ids.at(size_t(team)); i < club_names.size() && club_names[i] != 0; i++) s.push_back(char(club_names[i]));
    return s;
}

std::string GameState::player_name(int player) const {
    player &= 0xFFF;
    if (player == 0xFFF) return texts ? (*texts)[(::bmh::layout::NoPlayerText - ::bmh::layout::TextPointerTable) / 4] : std::string();
    std::string s;
    for (size_t i = player_name_offsets.at(size_t(player)); i < player_names.size() && player_names[i] != 0; i++)
        s.push_back(char(player_names[i]));
    return s;
}

int GameState::formation(int team) const {
    // FormationPosition: bit 4 selects bits 10-11 instead of bits 8-9
    unsigned v = team_flags.at(team);
    return int(v >> (((v >> 4) & 1) * 2 + 8) & 3);
}

void GameState::formation_position(int team, int slot, int& x, int& y) const {
    int manager = manager_of(team);
    if (manager >= 0) {
        size_t at = size_t(manager) * 0x840 + size_t(slot) * 0x58;
        x = int8_t(tactics.at(at + 0x41));
        y = int8_t(tactics.at(at + 0x42));
        return;
    }
    size_t base = (indoor ? 0xB0 : 0) + size_t(formation(team)) * 0x16 + size_t(slot);
    x = formations.at(base);
    y = formations.at(base + 11);
}

SquadRange squad_range(int team) {
    if (team == kNoTeam) return {size_t(kNoTeamSquadStart), 24};
    if (team < kBigSquadTeams) return {size_t(team) * 24, 24};
    if (team < runtime_dimensions.league_teams) return {size_t(team) * 16 + kBigSquadTeams * 8, 16};
    return {0, 0};
}

TeamAverage team_average(const GameState& g, int team, uint16_t flags, WatcomRandom* rand) {
    if (team >= kTeamsWithSquads && team != kNoTeam) {
        // TeamRating(team, profile 2 at layout field): a random value around the club's level
        if (!rand) throw std::invalid_argument("team_average: a team without squad needs the Watcom generator");
        RatingContext ctx;
        ctx.rand = rand;
        TeamAverage r;
        r.value = team_rating(const_cast<GameState&>(g), team, kRatingProfiles[2], ctx);
        r.players = 0;
        return r;
    }

    TeamAverage r;
    int sum_condition = 0, sum_technique = 0, sum_form = 0, sum_morale = 0;
    int counted = 0;  // "local_14" in the original, a byte

    SquadRange range = squad_range(team);
    for (size_t s = range.first; s < range.first + range.count; s++) {
        uint16_t slot = g.squad_slots.at(s);
        if (slot == kEmptySlot) continue;
        if ((flags & avg::LINEUP_ONLY) && !(slot & kSlotInLineup)) continue;
        const Player& p = g.players.at(slot & 0x0FFF);
        if ((flags & avg::FIT_ONLY) && (p.w364 >> 16 & 0x1F) != 0) continue;
        // position filter: bit (position + 4) of the flags, evaluated on the signed flags
        if (!((int16_t(flags) >> (p.position() + 4)) & 1)) continue;

        int half = 0;  // KO + TE of this player, for best_half_sum
        if (flags & avg::CONDITION) {
            half = p.condition();
            sum_condition += p.condition();
        }
        if (flags & avg::TECHNIQUE) {
            half += p.technique();
            sum_technique += p.technique();
        }
        if (flags & avg::FORM) sum_form += p.form();
        if (flags & avg::MORALE) sum_morale += (g.team_morale.at(team) & 0x3F) * 2;
        if (r.best_half_sum < half / 2) r.best_half_sum = half / 2;
        counted++;
    }
    r.players = counted + (counted == 0);
    counted &= 0xFF;
    if ((flags & 0x1F0) == 0x1F0) counted = 11;

    int value = 0;
    if ((flags & 3) == 0) {
        if (flags == (avg::FORM | avg::MORALE)) value = (sum_morale + sum_form) / 2;
        else value = (flags & avg::FORM) ? sum_form : sum_morale;
    } else {
        int attributes = 0;
        if (flags & avg::CONDITION) value += sum_condition, attributes++;
        if (flags & avg::TECHNIQUE) value += sum_technique, attributes++;
        value /= attributes;
        // form and morale scale the result by +-(average - 50) / 8 percent
        if (sum_form != 0) value = (div8(sum_form / counted - 50) + 100) * value / 100;
        if (sum_morale != 0) value = (div8(sum_morale / counted - 50) + 100) * value / 100;
    }
    if (!(flags & avg::NO_DIVIDE) && counted != 0) {
        value /= counted;
        if (value < 0) value = 0;
        if (value > 99) value = 99;
    }
    if (value == 0) value = 1;
    r.value = value;
    return r;
}

}  // namespace bmh
