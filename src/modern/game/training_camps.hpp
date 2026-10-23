// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Training camp catalogue of the Modern season core (docs/new_engine/trainingslager.md, M4).
//
// Classic has eight resorts with four quality grades 1-5 read from the executable. Modern
// worlds list their camps in rule:training-camps: map (Germany, Europe, Middle East, ...),
// position on that map, place, country, base price and the four grades 0-6 (pitch, fitness,
// catering, lodging). A world without such a catalogue gets the built-in, fictional
// catalogue of 18 camps the Modern interface was designed with.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bmh::modern {

struct CampMap {
    std::string id;                // map:de, map:eu, map:me
    std::string name;
    std::optional<int> width, height;  // coordinate system, once the interface defines it
};

struct TrainingCamp {
    std::string id;                // resort:<n>
    std::string map;               // CampMap id
    std::optional<int> x, y;       // position on the map, null until the interface sets it
    std::string name, place, country;
    int32_t base_price = 0;        // the price is base_price + 5000 per squad player
    std::array<uint8_t, 4> quality{};  // pitch, fitness, catering, lodging (0-6)
};

struct CampCatalogue {
    std::vector<CampMap> maps;
    std::vector<TrainingCamp> camps;
};

// The built-in catalogue: 18 fictional camps on three maps (Germany 6, Europe 9, Middle East 3).
const CampCatalogue& default_camp_catalogue();

}  // namespace bmh::modern
