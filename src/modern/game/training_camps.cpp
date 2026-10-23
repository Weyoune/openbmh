// SPDX-License-Identifier: GPL-3.0-only
#include "modern/game/training_camps.hpp"

namespace bmh::modern {

const CampCatalogue& default_camp_catalogue() {
    // Names, places, prices and grades are fictional interface data
    // (docs/new_ui/desktop/trainingcamp); the ids 0-17 are the interface's ids.
    static const CampCatalogue catalogue = [] {
        CampCatalogue c;
        c.maps = {{"map:de", "Deutschland", {}, {}}, {"map:eu", "Europa", {}, {}}, {"map:me", "Naher Osten", {}, {}}};
        const auto camp = [&](int id, const char* map, const char* name, const char* place, const char* country,
                              std::array<uint8_t, 4> quality, int32_t price) {
            TrainingCamp t;
            t.id = "resort:" + std::to_string(id);
            t.map = map;
            t.name = name;
            t.place = place;
            t.country = country;
            t.quality = quality;
            t.base_price = price;
            c.camps.push_back(t);
        };
        camp(0, "map:eu", "Kraftmühle Łódź", "Łódź", "nation:pl", {4, 4, 4, 4}, 410000);
        camp(1, "map:eu", "Vitalpark Vinadio", "Vinadio", "nation:it", {4, 4, 4, 3}, 450000);
        camp(2, "map:eu", "Kraftwerk Stettin", "Stettin", "nation:pl", {4, 4, 3, 4}, 420000);
        camp(3, "map:de", "Wellness Resort Ulm", "Ulm", "nation:de", {2, 3, 2, 2}, 145000);
        camp(4, "map:eu", "Sportzentrum Groningen", "Groningen", "nation:nl", {4, 4, 4, 4}, 485000);
        camp(5, "map:de", "Ball-Arena Potsdam", "Potsdam", "nation:de", {3, 2, 3, 2}, 175000);
        camp(6, "map:eu", "Camp Royal Saint-Denis", "Saint-Denis", "nation:fr", {5, 4, 5, 4}, 650000);
        camp(7, "map:eu", "Elite-Fußballcamp Ancona", "Ancona", "nation:it", {5, 5, 5, 5}, 840000);
        camp(8, "map:de", "Sportpark Sauerland", "Schmallenberg", "nation:de", {1, 1, 1, 1}, 25000);
        camp(9, "map:de", "Trainingshof Harz", "Goslar", "nation:de", {2, 2, 2, 2}, 55000);
        camp(10, "map:de", "Alpencamp Allgäu", "Kempten", "nation:de", {3, 3, 2, 3}, 195000);
        camp(11, "map:de", "Leistungszentrum Schwarzwald", "Freiburg", "nation:de", {5, 5, 4, 5}, 490000);
        camp(12, "map:eu", "Algarve Performance Camp", "Faro", "nation:pt", {5, 4, 5, 5}, 920000);
        camp(13, "map:eu", "Costa del Sol Sport Resort", "Málaga", "nation:es", {5, 5, 5, 4}, 895000);
        camp(14, "map:me", "Doha Elite Football Resort", "Doha", "nation:qa", {6, 6, 6, 6}, 2800000);
        camp(15, "map:me", "Abu Dhabi Royal Training", "Abu Dhabi", "nation:ae", {6, 6, 6, 6}, 3200000);
        camp(16, "map:me", "Dubai Platinum Sports Camp", "Dubai", "nation:ae", {6, 6, 6, 6}, 3600000);
        camp(17, "map:eu", "Riviera Trainingscamp Belek", "Belek", "nation:tr", {5, 5, 5, 4}, 790000);
        return c;
    }();
    return catalogue;
}

}  // namespace bmh::modern
