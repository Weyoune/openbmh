// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// M3D.MAN master database (docs/formats/M3D.md), read in CareerFirstSteps2
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "formats/binary.hpp"

namespace bmh {

// Table sizes, initial values of CareerSession_DataDirectory_RuntimeField / CareerSession_DataDirectory_RuntimeField3 / CareerSession_DataDirectory_RuntimeField2 / CareerSession_DataDirectory_RuntimeField4 in BMH.exe V2.24
struct DbCounts {
    uint32_t clubs = 419;
    uint32_t players = 2574;
    uint32_t n42 = 168;
    uint32_t n66 = 3096;
    uint32_t league_teams = 138;
    uint32_t cup_teams = 30;
};

// A player as stored in the three parallel u32 tables of the database/savegame
// (named after the globals holding them in the original: *PlayerConditionTechniqueForm, *PlayerSkillWords, *PlayerIdentityWords).
// Player ids are 12 bit (squad lists store id | flags << 12).
//
//   w35c  bits  0- 6  form (FO)                 shown in the squad screen
//         bits  7-13  technique (TE)
//         bits 14-20  condition (KO)
//         bits 21-26  unknown (0-63)
//         bits 27-31  unknown, read by the code
//   w368  bits  0-13  seven skills, 2 bits each (0-3), order: teamplay, speed, shooting,
//                     passing, tackling, free kick, corner (texts 135-141, "T.S.S.P.Z.F.E.")
//         bits 14-26  unknown / runtime state
//         bits 27-31  nationality (0 = Germany)
//   w364  bits  0- 1  position: 0 goalkeeper, 1 defender, 2 midfielder, 3 forward
//         bits  2-20  runtime state (injuries etc., not yet decoded)
//         bits 21-25  age - 16
//         bits 26-31  unknown
//
// The values in M3D.MAN differ from a running game: they are varied when a career starts.
struct Player {
    std::string name;  // ISO 646-DE
    uint32_t w35c = 0, w368 = 0, w364 = 0;

    enum Skill { TEAMPLAY, SPEED, SHOOTING, PASSING, TACKLING, FREE_KICK, CORNER };

    int form() const { return int(w35c & 0x7F); }
    int technique() const { return int(w35c >> 7 & 0x7F); }
    int condition() const { return int(w35c >> 14 & 0x7F); }
    int skill(Skill s) const { return int(w368 >> (2 * s) & 3); }  // 0-3
    int nationality() const { return int(w368 >> 27); }            // 0 = Germany
    int position() const { return int(w364 & 3); }                 // 0 GK, 1 DEF, 2 MID, 3 FWD
    int age() const { return int(w364 >> 21 & 0x1F) + 16; }

    // Strength "STÄ", Strength: form scales the sum of technique and condition.
    // display = true gives the 0-99 value shown in the squad screen.
    int strength(bool display = true) const {
        int raw = ((form() >> 3) + 0x5E) * (technique() + condition()) / 100;
        if (!display) return raw;
        return raw / 2 > 99 ? 99 : raw / 2;
    }
};

struct Database {
    DbCounts counts{};
    std::vector<std::string> club_names;
    std::vector<std::string> referees;
    std::vector<std::string> stadiums;
    std::vector<Player> players;
    Bytes squad_list;  // *SquadSlots, u16 entries (& 0xFFF = player id)
    // The data as read into memory (CareerFirstSteps2): string pools (0 clubs, 1 players, 2 referees,
    // 3 stadiums) and their offset tables (*ClubIdentities clubs, *PlayerNameOffsets players).
    Bytes pools[4];
    Bytes raw_club_name_offsets, raw_player_name_offsets;
    Bytes raw_player_bonus_values, raw_database_metadata, raw_club_auxiliary_words, raw_club_badge_selectors, raw_foreign_club_strength, raw_league_auxiliary_bytes, raw_referee_properties, raw_club_kit_selectors, raw_club_short_name_offsets;
};

Database load_database(const std::filesystem::path& path, const DbCounts& counts);
Database load_database(const std::filesystem::path& path);

}  // namespace bmh
