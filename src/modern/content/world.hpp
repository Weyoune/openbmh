// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The world of a Modern data set (docs/new_engine, M3): the parts of the OBW tables the
// Modern season core needs, read once when a world is opened. Identities are the stable
// OBW ids; runtime team and player indices are assigned by the world loader.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "import/obw.hpp"
#include "modern/game/training_camps.hpp"

namespace bmh::modern {

struct WorldLeague {
    std::string id;                    // comp:...
    std::string name;
    int index = -1;                    // league 0-8 (x-legacy.league_index)
    std::vector<std::string> members;  // club ids in table order
    std::string generator;             // legacy:<n> or round-robin
    std::vector<std::string> dates;    // explicit league dates, empty = Classic calendar
};

struct WorldClub {
    std::string id;
    std::string name;
    std::string short_name;
    std::string nation;
    std::string crest;
    std::string competition;           // league id for league clubs
    int legacy_index = -1;             // Classic team id, -1 for clubs added by the data set
    int kit_selector = -1;
};

struct WorldPlayer {
    std::string id;
    std::string first, last;
    std::string team;                  // club id, empty = no club
    int squad_order = -1;
    int position = 0;                  // 0 GK, 1 DEF, 2 MID, 3 FWD
    std::string nation;                // first nationality
    int age = -1;                      // age on the start date
    std::string photo;
    // Attributes of bmh-classic@1.
    int technique = 0, condition = 0, form = 0, bonus = 0;
    int teamplay = 0, speed = 0, shooting = 0, passing = 0, tackling = 0, free_kick = 0, corner = 0;
    // The original database words of an imported player (x-legacy).
    bool legacy = false;
    int legacy_index = -1;
    uint32_t w35c = 0, w368 = 0, w364 = 0;
};

struct World {
    std::string id, title, data_revision;
    std::vector<std::string> requires_;
    int start_year = 2026;
    std::string start_date;
    std::vector<WorldLeague> leagues;          // by league index 0-8
    std::vector<WorldClub> clubs;
    std::vector<WorldPlayer> players;
    std::map<std::string, int> nationality;    // nation id -> Classic nationality code (0 = Germany)
    std::map<std::string, size_t> club_by_id;  // club id -> index in clubs
    // rule:training-camps with maps and camp details (data format F4); empty = built-in catalogue.
    std::optional<CampCatalogue> camps;

    const WorldClub* club(const std::string& id) const;
};

// Reads the world tables. Throws FormatError for missing or malformed required fields.
World read_world(const obw::Documents& documents);

}  // namespace bmh::modern
