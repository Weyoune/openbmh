// SPDX-License-Identifier: GPL-3.0-only
#include "editor/comparison.hpp"
#include "editor/world.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace bmh::editor {
namespace {
bool date_valid(const std::string &date) {
    if (date.size() != 10 || date[4] != '-' || date[7] != '-')
        return false;
    for (size_t i = 0; i < date.size(); ++i)
        if (i != 4 && i != 7 && (date[i] < '0' || date[i] > '9'))
            return false;
    if (std::stoi(date.substr(0, 4)) < 1583)
        return false;
    return std::chrono::year_month_day{std::chrono::year(std::stoi(date.substr(0, 4))),
                                       std::chrono::month(unsigned(std::stoi(date.substr(5, 2)))),
                                       std::chrono::day(unsigned(std::stoi(date.substr(8, 2))))}
        .ok();
}
bool source_duplicate(const Json &player) {
    const auto simulation = player.value("x-simulation", Json::object());
    return str(simulation, "squad_role") == "source-only-duplicate";
}
std::optional<int64_t> money(const Json &value) {
    if (!value.is_number_integer() ||
        (value.is_number_unsigned() && value.get<uint64_t>() > uint64_t(std::numeric_limits<int64_t>::max())))
        return {};
    const auto number = value.get<int64_t>();
    return number >= 0 ? std::optional<int64_t>(number) : std::nullopt;
}
struct Mean {
    long double sum = 0;
    size_t count = 0;
    void add(const Json &value) {
        if (value.is_number() && std::isfinite(value.get<double>())) {
            sum += value.get<double>();
            ++count;
        }
    }
    Json value() const { return count ? Json(double(sum / count)) : Json(); }
};
struct Group {
    size_t players = 0, known = 0, ambiguous = 0;
    int64_t salary = 0;
    bool overflow = false;
    std::map<std::string, Mean> attributes;
    void add(const Json &player) {
        ++players;
        for (auto it = player["attributes"].begin(); it != player["attributes"].end(); ++it)
            attributes[it.key()].add(it.value());
        ambiguous += player.value("salary_ambiguous", size_t(0));
        if (const auto amount = money(player["salary"])) {
            ++known;
            if (salary > std::numeric_limits<int64_t>::max() - *amount)
                overflow = true;
            else
                salary += *amount;
        }
    }
    void write(Json &row, const Json &fields) const {
        row["player_count"] = players;
        row["salary_known"] = known;
        row["salary_unknown"] = players - known;
        row["salary_ambiguous"] = ambiguous;
        row["salary_overflow"] = overflow;
        row["salary_complete"] = players > 0 && known == players && !overflow;
        row["salary_total"] = known && !overflow ? Json(salary) : Json();
        // Salaries remain integer minor units. Round the mean to the nearest
        // minor unit without overflowing the sum or inventing unknown wages.
        row["salary_average"] =
            known && !overflow
                ? Json(salary / int64_t(known) + (salary % int64_t(known) >= int64_t((known + 1) / 2)))
                : Json();
        row["attributes"] = Json::object();
        row["attribute_counts"] = Json::object();
        for (const auto &field : fields) {
            const auto key = str(field, "key");
            const auto found = attributes.find(key);
            row["attributes"][key] = found == attributes.end() ? Json() : found->second.value();
            row["attribute_counts"][key] = found == attributes.end() ? 0 : found->second.count;
        }
    }
};
} // namespace

Json comparison_data(const World &world, const Json &args) {
    const auto table = str(args, "table", "players"), language = str(args, "language", "de");
    if (table != "players" && table != "clubs" && table != "leagues")
        throw Error("unknown_table");
    if (language != "de" && language != "en")
        throw Error("invalid_change");
    std::map<std::string, const Json *> clubs, leagues;
    std::map<std::string, std::set<std::string>> memberships;
    for (const auto &league : world.rows("competitions"))
        if (str(league, "type") == "league") {
            const auto id = str(league, "id");
            leagues[id] = &league;
            for (const auto &member : league.value("members", Json::array()))
                if (member.is_string())
                    memberships[member.get<std::string>()].insert(id);
        }
    for (const auto &club : world.rows("clubs"))
        clubs[str(club, "id")] = &club;
    auto league_for = [&](const std::string &id) {
        const auto comp = clubs.contains(id) ? str(*clubs.at(id), "competition") : "";
        if (leagues.contains(comp))
            return comp;
        const auto found = memberships.find(id);
        return found != memberships.end() && found->second.size() == 1 ? *found->second.begin()
                                                                       : std::string();
    };
    std::map<std::string, size_t> sets;
    size_t excluded = 0;
    for (const auto &player : world.rows("players")) {
        if (source_duplicate(player)) {
            ++excluded;
            continue;
        }
        ++sets[str(player, "attribute_set")];
    }
    auto selected_set = str(args, "attribute_set");
    if (!selected_set.empty() && !sets.contains(selected_set))
        throw Error("invalid_attribute_set");
    if (selected_set.empty() && sets.size() == 1)
        selected_set = sets.begin()->first;
    // Equal-looking numbers can have different meanings (FC physical strength
    // versus Classic playing strength). Never average or sort mixed systems.
    const bool mixed = selected_set.empty() && sets.size() > 1;
    std::map<std::string, Json> fields;
    if (!mixed) {
        for (const auto &rule : world.rows("rules")) {
            auto id = str(rule, "attribute_set", str(rule, "id"));
            if (str(rule, "type") == "attribute_set" &&
                (id == selected_set || id == "rule:" + selected_set)) {
                const auto definitions = rule.value("fields", Json::object());
                for (auto it = definitions.begin(); it != definitions.end(); ++it)
                    fields[it.key()] = {{"key", it.key()},
                                        {"min", it.value().value("min", Json())},
                                        {"max", it.value().value("max", Json())}};
            }
        }
        for (const auto &player : world.rows("players"))
            if (!source_duplicate(player) && str(player, "attribute_set") == selected_set) {
                const auto attributes = player.value("attributes", Json::object());
                for (auto it = attributes.begin(); it != attributes.end(); ++it)
                    if (it.value().is_number() && !fields.contains(it.key()))
                        fields[it.key()] = {{"key", it.key()}, {"min", nullptr}, {"max", nullptr}};
            }
    }
    Json definitions = Json::array();
    for (const auto &[_, field] : fields)
        definitions.push_back(field);
    Json available_sets = Json::array();
    for (const auto &[id, count] : sets)
        available_sets.push_back({{"id", id}, {"players", count}});
    const auto date = str(world.manifest, "start_date");
    std::map<std::pair<std::string, std::string>, std::vector<const Json *>> contracts;
    if (date_valid(date))
        for (const auto &contract : world.rows("contracts")) {
            const auto start = str(contract, "start"), end = str(contract, "end");
            // An absent date is an open bound. A malformed bound cannot be
            // treated as evidence of an active employment contract.
            const auto start_value = contract.value("start", Json()),
                       end_value = contract.value("end", Json());
            if ((!start_value.is_null() && !start_value.is_string()) ||
                (!end_value.is_null() && !end_value.is_string()) ||
                (!start.empty() && (!date_valid(start) || start > date)) ||
                (!end.empty() && (!date_valid(end) || end < date)))
                continue;
            contracts[{str(contract, "person"), str(contract, "team")}].push_back(&contract);
        }
    Json players = Json::array();
    std::map<std::string, Group> club_groups, league_groups;
    for (const auto &player : world.rows("players")) {
        if (source_duplicate(player) ||
            (!selected_set.empty() && str(player, "attribute_set") != selected_set))
            continue;
        const auto id = str(player, "id"), team = str(player, "team"), league = league_for(team);
        const auto *club = clubs.contains(team) ? clubs.at(team) : nullptr;
        Json row{{"id", id},
                 {"name", world.title(player, language)},
                 {"team", team},
                 {"nation", club ? str(*club, "nation") : ""},
                 {"competition", league},
                 {"positions", player.value("positions", Json::array())},
                 {"attribute_set", str(player, "attribute_set")},
                 {"player_count", 1},
                 {"attributes", Json::object()},
                 {"salary", nullptr},
                 {"salary_ambiguous", 0}};
        const auto attributes = player.value("attributes", Json::object());
        for (const auto &[key, _] : fields) {
            const auto value = attributes.value(key, Json());
            row["attributes"][key] = value.is_number() && std::isfinite(value.get<double>()) ? value : Json();
        }
        if (!team.empty()) {
            const auto found = contracts.find({id, team});
            if (found != contracts.end()) {
                if (found->second.size() == 1) {
                    const auto salary = money(found->second.front()->value("salary", Json()));
                    if (salary)
                        row["salary"] = *salary;
                } else
                    row["salary_ambiguous"] = 1;
            }
        }
        if (club)
            club_groups[team].add(row);
        if (!league.empty())
            league_groups[league].add(row);
        players.push_back(std::move(row));
    }
    Json rows = Json::array();
    if (table == "players")
        rows = std::move(players);
    else if (table == "clubs") {
        for (const auto &[id, club] : clubs) {
            Json row{{"id", id},
                     {"name", world.title(*club, language)},
                     {"nation", str(*club, "nation")},
                     {"competition", league_for(id)}};
            club_groups[id].write(row, definitions);
            rows.push_back(std::move(row));
        }
    } else
        for (const auto &[id, league] : leagues) {
            Json row{{"id", id},
                     {"name", world.title(*league, language)},
                     {"nation", str(*league, "nation")},
                     {"competition", id},
                     {"club_count", 0}};
            for (const auto &[club_id, _] : clubs)
                if (league_for(club_id) == id)
                    row["club_count"] = row["club_count"].get<size_t>() + 1;
            league_groups[id].write(row, definitions);
            rows.push_back(std::move(row));
        }
    const auto simulation = world.manifest.value("x-simulation", Json());
    return {{"table", table},
            {"items", std::move(rows)},
            {"attributes", definitions},
            {"attribute_sets", available_sets},
            {"attribute_set", selected_set},
            {"mixed_attribute_sets", mixed},
            {"currency", world.manifest.value("currency", Json())},
            {"date", date},
            {"excluded_players", excluded},
            {"fictional_completion",
             simulation.is_object() && simulation.value("fictional_completion", Json()) == true}};
}

Json query_comparison(const Json &data, const Json &args) {
    const auto search = folded(str(args, "search")), nation = str(args, "nation"),
               league = str(args, "competition"), team = str(args, "team"), sort = str(args, "sort", "name");
    const bool descending = args.value("descending", false);
    bool valid_sort = sort == "name" || sort == "salary" || sort == "salary_total" ||
                      sort == "salary_average" || sort == "player_count" || sort == "club_count" ||
                      sort == "salary_known";
    for (const auto &field : data["attributes"])
        valid_sort |= sort == "attributes." + str(field, "key");
    if (!valid_sort)
        throw Error("invalid_comparison_sort");
    auto value = [&](const Json &row) {
        if (sort == "name")
            return Json(folded(str(row, "name")));
        if (sort.starts_with("attributes."))
            return row["attributes"].value(sort.substr(11), Json());
        return row.value(sort, Json());
    };
    std::vector<const Json *> matches;
    for (const auto &row : data["items"]) {
        if ((!nation.empty() && str(row, "nation") != nation) ||
            (!league.empty() && str(row, "competition") != league) ||
            (!team.empty() && str(row, "team", str(row, "id")) != team) ||
            (!search.empty() && folded(str(row, "name")).find(search) == std::string::npos &&
             folded(str(row, "id")).find(search) == std::string::npos))
            continue;
        matches.push_back(&row);
    }
    std::stable_sort(matches.begin(), matches.end(), [&](auto a, auto b) {
        const auto av = value(*a), bv = value(*b);
        if (av.is_null() != bv.is_null())
            return !av.is_null(); // Unknown values are last in both directions.
        if (av == bv)
            return str(*a, "id") < str(*b, "id");
        return descending ? av > bv : av < bv;
    });
    const auto offset = std::max<int64_t>(0, args.value("offset", int64_t(0)));
    const auto limit = std::clamp<int64_t>(args.value("limit", int64_t(200)), 1, 200);
    Json result = Json::object();
    for (auto it = data.begin(); it != data.end(); ++it)
        if (it.key() != "items")
            result[it.key()] = it.value();
    result["items"] = Json::array();
    for (size_t i = size_t(std::min<int64_t>(offset, int64_t(matches.size())));
         i < matches.size() && int64_t(result["items"].size()) < limit; ++i)
        result["items"].push_back(*matches[i]);
    result["total"] = matches.size();
    result["offset"] = offset;
    result["limit"] = limit;
    return result;
}
} // namespace bmh::editor
