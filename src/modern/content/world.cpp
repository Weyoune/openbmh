// SPDX-License-Identifier: GPL-3.0-only
#include "modern/content/world.hpp"

#include <ctime>

#include "import/detail.hpp"

namespace bmh::modern {

namespace {

using namespace obw::detail;

const J* find(const J& value, std::string_view key) {
    if (!value.object()) return nullptr;
    const auto it = value.object()->find(key);
    return it == value.object()->end() ? nullptr : &it->second;
}
std::string text(const J* value) {
    if (!value) return {};
    if (value->string()) return *value->string();
    if (const J* d = find(*value, "default"); d && d->string()) return *d->string();
    return {};
}
int integer(const J* value, int fallback = 0) {
    return value && value->integer() ? int(*value->integer()) : fallback;
}
int age_on(const std::string& born, const std::string& date) {
    int by = 0, bm = 0, bd = 0, y = 0, m = 0, d = 0;
    if (std::sscanf(born.c_str(), "%d-%d-%d", &by, &bm, &bd) != 3 || std::sscanf(date.c_str(), "%d-%d-%d", &y, &m, &d) != 3)
        return -1;
    return y - by - ((m < bm || (m == bm && d < bd)) ? 1 : 0);
}
int position_of(const std::string& p) {
    if (p == "gk") return 0;
    if (p == "def") return 1;
    if (p == "mid") return 2;
    if (p == "fwd") return 3;
    fail("unknown player position: " + p);
}

}  // namespace

const WorldClub* World::club(const std::string& id) const {
    const auto it = club_by_id.find(id);
    return it == club_by_id.end() ? nullptr : &clubs[it->second];
}

World read_world(const obw::Documents& documents) {
    World w;
    const J manifest = parse(documents, "manifest.json");
    w.id = str(get(manifest, "id"));
    w.title = text(find(manifest, "title"));
    w.start_date = str(get(manifest, "start_date"));
    w.start_year = std::stoi(w.start_date.substr(0, 4));
    for (const J& r : arr(get(manifest, "requires"))) w.requires_.push_back(str(r));
    for (const char* extension : {"x-diamond-modern", "x-world"})
        if (const J* x = find(manifest, extension)) w.data_revision = text(find(*x, "data_revision"));

    for (const J& n : items(documents, "nations"))
        if (const J* legacy = find(n, "x-legacy"); legacy && find(*legacy, "nationality_index"))
            w.nationality[str(get(n, "id"))] = integer(find(*legacy, "nationality_index"));

    std::map<std::string, std::vector<std::string>> dates;
    for (const J& c : items(documents, "calendar")) {
        const J* list = find(c, "dates");
        if (!list || !list->array()) continue;
        auto& out = dates[str(get(c, "id"))];
        for (const J& d : arr(*list)) out.push_back(str(d));
    }

    w.leagues.resize(9);
    for (const J& c : items(documents, "competitions")) {
        if (text(find(c, "type")) != "league") continue;
        const J* legacy = find(c, "x-legacy");
        const int index = legacy ? integer(find(*legacy, "league_index"), -1) : -1;
        if (index < 0 || index >= 9) fail("league without a league index: " + text(find(c, "id")));
        WorldLeague& l = w.leagues[size_t(index)];
        if (l.index >= 0) fail("two leagues with index " + std::to_string(index));
        l.index = index;
        l.id = str(get(c, "id"));
        l.name = text(find(c, "name"));
        for (const J& m : arr(get(c, "members"))) l.members.push_back(str(m));
        const auto& stages = arr(get(c, "stages"));
        if (stages.size() != 1) fail("league with more than one stage: " + l.id);
        l.generator = text(find(stages[0], "generator"));
        const std::string calendar = text(find(stages[0], "matchdays"));
        if (const auto it = dates.find(calendar); it != dates.end()) l.dates = it->second;
    }
    for (const WorldLeague& l : w.leagues)
        if (l.index < 0) fail("the world needs the nine Classic leagues");

    for (const J& c : items(documents, "clubs")) {
        WorldClub club;
        club.id = str(get(c, "id"));
        club.name = text(find(c, "name"));
        club.short_name = text(find(c, "short"));
        club.nation = text(find(c, "nation"));
        club.crest = text(find(c, "crest"));
        club.competition = text(find(c, "competition"));
        if (const J* legacy = find(c, "x-legacy")) {
            club.legacy_index = integer(find(*legacy, "team_index"), -1);
            club.kit_selector = integer(find(*legacy, "kit_selector"), -1);
        }
        if (const J* kit = find(c, "kit")) club.kit_selector = integer(find(*kit, "selector"), club.kit_selector);
        w.club_by_id[club.id] = w.clubs.size();
        w.clubs.push_back(std::move(club));
    }

    // Training camps (data format F4): only a catalogue with maps replaces the built-in one; the
    // eight Classic resorts of an imported installation carry no map.
    for (const J& r : items(documents, "rules")) {
        if (text(find(r, "id")) != "rule:training-camps" || !find(r, "maps")) continue;
        CampCatalogue catalogue;
        for (const J& m : arr(get(r, "maps"))) {
            CampMap map;
            map.id = str(get(m, "id"));
            map.name = text(find(m, "name"));
            if (const J* v = find(m, "width"); v && v->integer()) map.width = int(*v->integer());
            if (const J* v = find(m, "height"); v && v->integer()) map.height = int(*v->integer());
            catalogue.maps.push_back(std::move(map));
        }
        for (const J& c : arr(get(r, "resorts"))) {
            TrainingCamp camp;
            camp.id = str(get(c, "id"));
            camp.map = str(get(c, "map"));
            if (const J* v = find(c, "x"); v && v->integer()) camp.x = int(*v->integer());
            if (const J* v = find(c, "y"); v && v->integer()) camp.y = int(*v->integer());
            camp.name = text(find(c, "name"));
            camp.place = text(find(c, "place"));
            camp.country = text(find(c, "country"));
            camp.base_price = integer(find(c, "base_price"));
            const auto& quality = arr(get(c, "quality"));
            if (quality.size() != 4) fail("training camp needs four quality grades: " + camp.id);
            for (size_t k = 0; k < 4; ++k) {
                const auto grade = num(quality[k]);
                if (grade < 0 || grade > 6) fail("training camp grade outside 0-6: " + camp.id);
                camp.quality[k] = uint8_t(grade);
            }
            if (camp.base_price < 0) fail("negative training camp price: " + camp.id);
            catalogue.camps.push_back(std::move(camp));
        }
        w.camps = std::move(catalogue);
    }

    for (const J& p : items(documents, "players")) {
        WorldPlayer pl;
        pl.id = str(get(p, "id"));
        const J& name = get(p, "name");
        pl.first = text(find(name, "first"));
        pl.last = text(find(name, "last"));
        pl.team = text(find(p, "team"));
        pl.squad_order = integer(find(p, "squad_order"), -1);
        const auto& positions = arr(get(p, "positions"));
        if (positions.empty()) fail("player without position: " + pl.id);
        pl.position = position_of(str(positions[0]));
        const auto& nations = arr(get(p, "nationalities"));
        if (!nations.empty()) pl.nation = str(nations[0]);
        if (const J* born = find(p, "born")) pl.age = age_on(str(*born), w.start_date);
        else if (const J* at = find(p, "age_at_start")) pl.age = integer(find(*at, "years"), -1);
        pl.photo = text(find(p, "photo"));
        const J& a = get(p, "attributes");
        pl.technique = integer(find(a, "technique"));
        pl.condition = integer(find(a, "condition"));
        pl.form = integer(find(a, "form"));
        pl.bonus = integer(find(a, "bonus"));
        pl.teamplay = integer(find(a, "teamplay"));
        pl.speed = integer(find(a, "speed"));
        pl.shooting = integer(find(a, "shooting"));
        pl.passing = integer(find(a, "passing"));
        pl.tackling = integer(find(a, "tackling"));
        pl.free_kick = integer(find(a, "free_kick"));
        pl.corner = integer(find(a, "corner"));
        if (const J* legacy = find(p, "x-legacy"); legacy && find(*legacy, "player_index")) {
            pl.legacy = true;
            pl.legacy_index = integer(find(*legacy, "player_index"), -1);
            const auto word = [&](const char* key) {
                const J* v = find(*legacy, key);
                return v && v->integer() ? uint32_t(*v->integer()) : 0u;
            };
            pl.w35c = word("condition_technique_form_word");
            pl.w368 = word("skill_word");
            pl.w364 = word("identity_word");
        }
        w.players.push_back(std::move(pl));
    }
    return w;
}

}  // namespace bmh::modern
