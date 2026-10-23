// SPDX-License-Identifier: GPL-3.0-only
#include "editor/regulations.hpp"
#include "editor/world.hpp"
#include <algorithm>

namespace bmh::editor {
namespace {
Json object(const Json &value, const char *key) {
    const auto found = value.find(key);
    return found != value.end() && found->is_object() ? *found : Json::object();
}
Json array(const Json &value, const char *key) {
    const auto found = value.find(key);
    return found != value.end() && found->is_array() ? *found : Json::array();
}
std::optional<int> number(const Json &value) {
    if (!value.is_number_integer() || value.get<double>() < 0 || value.get<double>() > 100000)
        return {};
    return value.get<int>();
}
int count_teams(const Json &comp) {
    if (comp.value("members", Json()).is_array() && !comp["members"].empty())
        return int(comp["members"].size());
    for (const auto &stage : array(comp, "stages"))
        if (auto value = number(stage.value("teams", Json())))
            return *value;
    return 0;
}
} // namespace

Json regulation_view(const World &world, const Json &args) {
    const auto language = str(args, "language", "de"), search = folded(str(args, "search")),
               nation = str(args, "nation"), chosen = str(args, "competition");
    if (language != "de" && language != "en")
        throw Error("invalid_change");
    std::map<std::string, const Json *> competitions;
    std::map<int, const Json *> indexed;
    for (const auto &comp : world.rows("competitions")) {
        competitions[str(comp, "id")] = &comp;
        const auto legacy = object(comp, "x-legacy");
        if (const auto index = number(legacy.value("league_index", Json()));
            index && str(comp, "type") == "league")
            indexed[*index] = &comp;
    }
    const Json *ladder = nullptr;
    bool classic = false;
    for (const auto &rule : world.rows("rules")) {
        if (str(rule, "type") == "legacy_ladder")
            ladder = &rule;
        if (str(rule, "type") == "classic_runtime" &&
            str(rule, "qualifications") == "classic-season-entrants@1")
            classic = true;
    }
    auto target = [&](const std::string &id) {
        return Json{{"id", id},
                    {"name", competitions.contains(id) ? world.title(*competitions.at(id), language) : id}};
    };
    std::map<std::string, Json> movement, qualification;
    std::set<std::string> movement_known;
    // Explicit OBW movement always takes precedence over compatibility policy.
    for (const auto &[id, comp] : competitions) {
        movement[id] = Json::array();
        qualification[id] = Json::array();
        if (comp->contains("movement") && (*comp)["movement"].is_array()) {
            movement_known.insert(id);
            for (const auto &definition : (*comp)["movement"]) {
                const auto to = str(definition, "to");
                const auto role = str(definition, "role");
                std::string kind = role == "challenger" || role == "defender" || role == "incumbent"
                                       ? "playoff"
                                       : "movement";
                if (kind == "movement" && competitions.contains(to)) {
                    auto from_tier = number(comp->value("tier", Json()));
                    auto to_tier = number(competitions.at(to)->value("tier", Json()));
                    if (from_tier && to_tier && str(*competitions.at(to), "type") == "league")
                        kind = *to_tier < *from_tier   ? "promotion"
                               : *to_tier > *from_tier ? "relegation"
                                                       : "movement";
                }
                movement[id].push_back({{"kind", kind},
                                        {"ranks", definition.value("ranks", Json())},
                                        {"targets", Json::array({target(to)})},
                                        {"source", "declared"},
                                        {"definition", definition}});
            }
        }
    }
    if (classic && ladder) {
        auto span = [&](const char *first_key, const char *count_key, int group) {
            std::vector<const Json *> result;
            const auto first = ladder->value(first_key, Json()), count = ladder->value(count_key, Json());
            if (!first.is_array() || !count.is_array() || size_t(group) >= first.size() ||
                size_t(group) >= count.size())
                return result;
            const auto start = number(first[group]), length = number(count[group]);
            if (!start || !length || *length < 1 || *length > 24)
                return result;
            for (int i = *start; i < *start + *length; ++i) {
                if (!indexed.contains(i))
                    return std::vector<const Json *>{};
                result.push_back(indexed.at(i));
            }
            return result;
        };
        // The versioned Classic ladder has four domestic movement groups.
        // Its final group draws newcomers; below[3] is not a foreign league
        // destination. License withdrawals can change the actual season result.
        for (int group = 0; group < 4; ++group) {
            const auto upper = span("first_league", "league_count", group);
            const auto lower = group < 3 ? span("below", "below_count", group) : std::vector<const Json *>{};
            if (upper.empty() || (group < 3 && lower.empty()))
                continue;
            int down_total = 0;
            bool complete = true;
            for (const auto *comp : upper) {
                const auto down = number(object(*comp, "x-legacy").value("relegation_places", Json()));
                if (!down || *down > count_teams(*comp)) {
                    complete = false;
                    break;
                }
                down_total += *down;
            }
            if (!complete)
                continue;
            Json destinations = Json::array(), origins = Json::array();
            for (const auto *comp : lower)
                destinations.push_back(target(str(*comp, "id")));
            for (const auto *comp : upper)
                origins.push_back(target(str(*comp, "id")));
            for (const auto *comp : upper) {
                const auto id = str(*comp, "id");
                if (comp->contains("movement"))
                    continue;
                movement_known.insert(id);
                const int down = *number(object(*comp, "x-legacy")["relegation_places"]);
                if (down)
                    movement[id].push_back({{"kind", "relegation"},
                                            {"places", down},
                                            {"ranks", {count_teams(*comp) - down + 1, count_teams(*comp)}},
                                            {"targets", destinations},
                                            {"newcomer_pool", group == 3},
                                            {"pool_size", ladder->value("newcomer_count", Json())},
                                            {"conditional_playoffs", lower.size() > 1},
                                            {"source", "classic-ladder@1"}});
            }
            if (!lower.empty())
                for (const auto *comp : lower) {
                    const auto id = str(*comp, "id");
                    if (comp->contains("movement"))
                        continue;
                    const bool playoffs = lower.size() > 1;
                    const int up = playoffs ? 1 : down_total;
                    if (up > count_teams(*comp) || down_total % int(lower.size()) != 0)
                        continue;
                    movement_known.insert(id);
                    if (up)
                        movement[id].push_back({{"kind", "promotion"},
                                                {"places", up},
                                                {"ranks", {1, up}},
                                                {"targets", origins},
                                                {"source", "classic-ladder@1"}});
                    if (playoffs)
                        movement[id].push_back({{"kind", "playoff"},
                                                {"ranks", nullptr},
                                                {"selector", "classic-playoff-winner"},
                                                {"conditional_playoffs", true},
                                                {"targets", origins},
                                                {"source", "classic-ladder@1"}});
                }
        }
    }
    auto slots = [&](const std::string &to, const std::string &stage, const Json &definitions) {
        if (!definitions.is_array())
            return;
        for (const auto &definition : definitions) {
            const auto from = str(definition, "from"), holder = str(definition, "holder");
            // Fixed team lists are already participants, not a qualification
            // rule. Keep the untouched stages available separately below.
            if (from.empty() && holder.empty() && !definition.contains("winner") &&
                !definition.contains("resolver"))
                continue;
            const auto destination = str(definition, "to", to);
            const Json rule{
                {"from", target(from)},
                {"to", target(destination)},
                {"stage", str(definition, "stage", stage)},
                {"definition", definition},
                {"source", str(definition, "resolver").empty() ? "declared" : str(definition, "resolver")}};
            qualification[destination].push_back(rule);
            if (competitions.contains(from) && from != destination)
                qualification[from].push_back(rule);
        }
    };
    for (const auto &[id, comp] : competitions) {
        slots(id, "", comp->value("slots", Json::array()));
        for (const auto &stage : array(*comp, "stages"))
            slots(id, str(stage, "id"), stage.value("slots", Json::array()));
    }
    Json items = Json::array();
    for (const auto &[id, comp] : competitions) {
        const auto label = world.title(*comp, language);
        if ((!chosen.empty() && id != chosen) || (!nation.empty() && str(*comp, "nation") != nation) ||
            (!search.empty() && folded(label).find(search) == std::string::npos &&
             folded(id).find(search) == std::string::npos))
            continue;
        Json policy;
        if (classic) {
            const auto index = number(object(*comp, "x-legacy").value("league_index", Json()));
            const bool declared =
                std::any_of(qualification[id].begin(), qualification[id].end(),
                            [](const auto &rule) { return str(rule, "source") != "classic-entry@1"; });
            // Registry for classic-season-entrants@1, the named format policy
            // consumed by the existing Classic adapter. These quotas are not
            // the historical x-legacy.europe_places prediction/colouring field.
            if (!declared && index && (*index == 0 || (*index >= 5 && *index <= 8)))
                policy = {{"id", "classic-season-entrants@1"},
                          {"uefa_base_places", *index == 8 ? 3 : 4},
                          {"champion", true},
                          {"cup_winner_or_finalist", true},
                          {"titleholder_adjustments", true}};
            else if (!declared &&
                     str(object(*comp, "classic"), "qualification") == "classic-season-entrants@1")
                policy = {{"id", "classic-season-entrants@1"}, {"runtime_participants", true}};
            if (index && *index >= 5)
                movement_known.insert(id); // Closed foreign Classic leagues.
        }
        items.push_back({{"id", id},
                         {"name", label},
                         {"nation", str(*comp, "nation")},
                         {"type", str(*comp, "type")},
                         {"tier", comp->value("tier", Json())},
                         {"teams", count_teams(*comp)},
                         {"movement_defined", movement_known.contains(id)},
                         {"movement", movement[id]},
                         {"qualification", qualification[id]},
                         {"classic_qualification", policy},
                         {"stages", array(*comp, "stages")},
                         {"record", *comp}});
    }
    std::stable_sort(items.begin(), items.end(), [](const auto &a, const auto &b) {
        const auto av = folded(str(a, "name")), bv = folded(str(b, "name"));
        return av == bv ? str(a, "id") < str(b, "id") : av < bv;
    });
    return {{"read_only", true},
            {"items", items},
            {"rules", world.rows("rules")},
            {"classic_policy", classic},
            {"date", world.manifest.value("start_date", Json())}};
}
} // namespace bmh::editor
