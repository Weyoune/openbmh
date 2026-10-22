// SPDX-License-Identifier: GPL-3.0-only
#include "editor/world.hpp"
#include "content/digest.hpp"
#include "content/package.hpp"
#include "editor/version.hpp"
#include "import/obw.hpp"
#include "launch/game_data.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace bmh::editor {
namespace {
Json table(const std::string &name) { return {{"schema", name + "@1.1"}, {"items", Json::array()}}; }
bool date_valid(const Json &value) {
    if (value.is_null())
        return true;
    if (!value.is_string())
        return false;
    auto s = value.get<std::string>();
    if (s.size() != 10 || s[4] != '-' || s[7] != '-')
        return false;
    for (size_t i = 0; i < s.size(); ++i)
        if (i != 4 && i != 7 && (s[i] < '0' || s[i] > '9'))
            return false;
    int y = std::stoi(s.substr(0, 4)), m = std::stoi(s.substr(5, 2)), d = std::stoi(s.substr(8));
    static constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return y >= 1583 && m >= 1 && m <= 12 && d >= 1 &&
           d <= days[m - 1] + (m == 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)));
}
void walk_refs(const Json &node, const std::function<void(const std::string &)> &emit, bool extension = false) {
    if (node.is_object()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            if (it.key() == "id" || it.key().starts_with("x-") || it.key() == "sources")
                continue;
            if (it.value().is_string() &&
                (it.key() == "nation" || it.key() == "team" || it.key() == "competition" || it.key() == "stadium" ||
                 it.key() == "owner" || it.key() == "person" || it.key() == "national_team" || it.key() == "photo" ||
                 it.key() == "crest" || it.key() == "trophy"))
                emit(it.value());
            else if ((it.key() == "nationalities" || it.key() == "members" || it.key() == "participants" ||
                      it.key() == "teams") &&
                     it.value().is_array()) {
                for (const auto &value : it.value())
                    if (value.is_string())
                        emit(value);
            } else if (it.value().is_object() || it.value().is_array())
                walk_refs(it.value(), emit, extension);
        }
    } else if (node.is_array())
        for (const auto &child : node)
            walk_refs(child, emit, extension);
}
Json parse(const std::string &data, const std::string &file) {
    try {
        return Json::parse(data);
    } catch (const Json::exception &e) {
        throw Error("invalid_json", {{"file", file}, {"detail", e.what()}});
    }
}
} // namespace
const std::vector<std::string> &World::tables() {
    static const std::vector<std::string> names{"nations", "competitions", "clubs",      "players", "stadiums",
                                                "staff",   "contracts",    "finance",    "rules",   "calendar",
                                                "assets",  "review",       "image-edits"};
    return names;
}
std::string World::file_for(const std::string &name) {
    if (name == "assets")
        return "assets/index.json";
    if (name == "review" || name == "image-edits")
        return "x-editor/" + name + ".json";
    return "world/" + name + ".json";
}
std::string World::identity(const Json &row, const std::string &name) {
    return str(row, name == "assets" ? "key" : "id");
}
World World::blank(const std::string &title_value) {
    World w;
    w.manifest = {{"format", "openbmh-world"},
                  {"format_version", "1.1"},
                  {"min_reader", "1.1"},
                  {"kind", "world"},
                  {"title", {{"default", title_value}}},
                  {"generator", editor_product()},
                  {"provenance", "native"},
                  {"content_license", "user-supplied"},
                  {"start_date", "2026-07-01"},
                  {"currency", {{"code", "EUR"}, {"minor_units", 2}}},
                  {"languages", {"de", "en"}},
                  {"requires", {"editor:world-v1"}},
                  {"extensions", Json::array()},
                  {"x-editor", {{"profile", "editor-world-v1"}, {"playable", false}}}};
    for (auto &name : tables())
        w.docs_[file_for(name)] = table(name == "assets" ? "asset-index" : name);
    w.docs_[file_for("rules")]["items"].push_back({{"id", "rule:unrated@1"},
                                                   {"type", "attribute_set"},
                                                   {"attribute_set", "unrated@1"},
                                                   {"fields", Json::object()}});
    for (auto &[name, _] : w.docs_)
        w.dirty_.insert(name);
    return w;
}
World World::open(const fs::path &source, const std::string &start_date) {
    World w;
    const auto input = fs::absolute(source);
    bool legacy = false;
    if (fs::is_directory(input))
        legacy = !fs::exists(input / "manifest.json");
    else
        legacy = folded(utf8(input.extension())) == ".exe";
    if (legacy) {
        if (!date_valid(start_date))
            throw Error("invalid_date");
        bmh::obw::Options options;
        options.start_date = start_date;
        auto docs = bmh::obw::import_installation(input, options);
        for (auto &[name, bytes] : docs)
            w.store->set(name, std::make_shared<const std::string>(std::move(bytes)));
        w.imported = true;
    } else
        w.store = ArchiveStore::open(input);
    w.manifest = parse(w.store->read("manifest.json"), "manifest.json");
    if (str(w.manifest, "format") != "openbmh-world" || str(w.manifest, "kind") != "world")
        throw Error("not_world");
    auto version = str(w.manifest, "format_version");
    if (!version.starts_with("1."))
        throw Error("unsupported_version", {{"version", version}});
    const bool editor = str(w.manifest.value("x-editor", Json::object()), "profile") == "editor-world-v1";
    if (editor && (version != "1.1" || str(w.manifest, "min_reader") != "1.1" ||
                   w.manifest.value("x-editor", Json::object()).value("playable", true)))
        throw Error("unsupported_version");
    if (!editor) {
        // Only bounded 1.0 Classic packages use the Classic validator. Its
        // compatibility and seal checks are not relaxed by the new reader.
        if (!w.imported) {
            auto docs = bmh::obw::read(input);
            if (docs.contains("x-classic/seal.json"))
                bmh::content::verify(docs);
        }
        w.imported = true;
    }
    w.source_path = input;
    w.load();
    if (w.imported)
        w.editor_profile();
    return w;
}
void World::load() {
    for (auto &name : tables()) {
        auto file = file_for(name);
        if (store->contains(file)) {
            auto bytes = store->read(file);
            auto value = parse(bytes, file);
            if (!value.is_object() || !value.contains("items") || !value["items"].is_array())
                throw Error("invalid_table", {{"file", file}});
            original_[file] = std::move(bytes);
            docs_[file] = std::move(value);
        } else {
            docs_[file] = table(name == "assets" ? "asset-index" : name);
            dirty_.insert(file);
        }
    }
}
void World::editor_profile() {
    manifest["x-editor"] = {{"profile", "editor-world-v1"},
                            {"playable", false},
                            {"original_manifest", manifest},
                            {"source_filename", utf8(source_path.filename())}};
    manifest["format_version"] = "1.1";
    manifest["min_reader"] = "1.1";
    manifest["requires"] = Json::array({"editor:world-v1"});
    manifest["generator"] = editor_product();
    // The historical seal remains original evidence only. The editor profile
    // deliberately cannot be passed to the Classic runtime as sealed content.
    for (auto &comp : rows("competitions")) {
        if (!comp.contains("members")) {
            comp["members"] = Json::array();
            for (auto &stage : comp.value("stages", Json::array()))
                for (auto &slot : stage.value("slots", Json::array())) {
                    auto teams = slot.value("teams", Json::array());
                    if (slot.contains("team") && slot["team"].is_string())
                        teams.push_back(slot["team"]);
                    for (const auto &team : teams)
                        if (team.is_string()) {
                            const auto tid = team.get<std::string>();
                            if (str(comp, "type") == "league")
                                if (auto club = find("clubs", tid))
                                    (*club)["competition"] = comp["id"];
                            if (std::find(comp["members"].begin(), comp["members"].end(), tid) == comp["members"].end())
                                comp["members"].push_back(tid);
                        }
                }
        }
    }
    for (auto &person : rows("players")) {
        if (person.contains("x-legacy") && person["x-legacy"].contains("squad_row"))
            person["squad_order"] = person["x-legacy"]["squad_row"];
    }
    touch("competitions");
    touch("clubs");
    touch("players");
}
Json &World::document(const std::string &name) { return docs_.at(file_for(name)); }
const Json &World::document(const std::string &name) const { return docs_.at(file_for(name)); }
Json &World::rows(const std::string &name) { return document(name)["items"]; }
const Json &World::rows(const std::string &name) const { return document(name).at("items"); }
const Json *World::find(const std::string &name, const std::string &id) const {
    for (auto &row : rows(name))
        if (identity(row, name) == id)
            return &row;
    return nullptr;
}
Json *World::find(const std::string &name, const std::string &id) {
    return const_cast<Json *>(std::as_const(*this).find(name, id));
}
void World::touch(const std::string &name) { dirty_.insert(file_for(name)); }
void World::change(const std::string &name, const std::string &id, const std::optional<Json> &after) {
    auto &values = rows(name);
    for (auto i = values.begin(); i != values.end(); ++i)
        if (identity(*i, name) == id) {
            if (after)
                *i = *after;
            else
                values.erase(i);
            touch(name);
            return;
        }
    if (after) {
        values.push_back(*after);
        touch(name);
    }
}
std::string World::title(const Json &value, const std::string &language) const {
    if (value.contains("name")) {
        const auto &name = value["name"];
        if (name.is_object() && name.contains(language) && name[language].is_string())
            return name[language];
        return text(name);
    }
    return str(value, "key", str(value, "id"));
}
Json World::statistics() const {
    Json counts = Json::object();
    for (auto &name : tables())
        counts[name] = rows(name).size();
    return {{"title", text(manifest.value("title", Json()))},
            {"start_date", manifest.value("start_date", Json())},
            {"format_version", manifest.value("format_version", Json())},
            {"counts", counts},
            {"source", utf8(source_path)},
            {"imported", imported},
            {"profile", "editor-world-v1"},
            {"playable", false}};
}
Json World::query(const Json &args) const {
    const auto name = str(args, "table", "clubs");
    if (std::find(tables().begin(), tables().end(), name) == tables().end())
        throw Error("unknown_table");
    std::map<std::string, const Json *> clubs;
    for (const auto &club : rows("clubs"))
        clubs[str(club, "id")] = &club;
    std::vector<const Json *> matches;
    const auto search = folded(str(args, "search")), nation = str(args, "nation"), comp = str(args, "competition"),
               team = str(args, "team");
    for (const auto &row : rows(name)) {
        const Json *club =
            name == "clubs" ? &row : (clubs.contains(str(row, "team")) ? clubs[str(row, "team")] : nullptr);
        if (!nation.empty() && str(row, "nation") != nation && (!club || str(*club, "nation") != nation))
            continue;
        if (!comp.empty() && str(row, "competition") != comp && (!club || str(*club, "competition") != comp))
            continue;
        if (!team.empty() && str(row, "team") != team && str(row, "owner") != team)
            continue;
        if (args.contains("role") && str(row, "role") != str(args, "role"))
            continue;
        if (name == "competitions" && args.value("leagues_only", false) && str(row, "type") != "league")
            continue;
        if (name == "review" && args.value("unresolved_only", false) && row.value("resolved", false))
            continue;
        if (!search.empty() && folded(title(row)).find(search) == std::string::npos &&
            folded(identity(row, name)).find(search) == std::string::npos &&
            (!club || folded(title(*club)).find(search) == std::string::npos))
            continue;
        matches.push_back(&row);
    }
    auto sort = str(args, "sort", "name");
    bool descending = args.value("descending", false);
    std::stable_sort(matches.begin(), matches.end(), [&](auto a, auto b) {
        Json av = sort == "name" ? Json(folded(title(*a))) : a->value(sort, Json());
        Json bv = sort == "name" ? Json(folded(title(*b))) : b->value(sort, Json());
        if (av == bv)
            return identity(*a, name) < identity(*b, name);
        return descending ? av > bv : av < bv;
    });
    const auto offset = std::max<int64_t>(0, args.value("offset", int64_t(0)));
    const auto limit = std::clamp<int64_t>(args.value("limit", int64_t(200)), 1, 200);
    Json result = Json::array();
    for (size_t i = size_t(std::min<int64_t>(offset, int64_t(matches.size())));
         i < matches.size() && int64_t(result.size()) < limit; ++i)
        result.push_back(*matches[i]);
    return {{"items", std::move(result)}, {"total", matches.size()}, {"offset", offset}, {"limit", limit}};
}
std::vector<std::pair<std::string, std::string>> World::references(const std::string &id) const {
    std::vector<std::pair<std::string, std::string>> found;
    for (auto &name : tables())
        if (name != "review" && name != "image-edits" && name != "assets")
            for (auto &row : rows(name))
                if (identity(row, name) != id) {
                    bool referenced = false;
                    walk_refs(row, [&](auto &ref) {
                        if (ref == id)
                            referenced = true;
                    });
                    if (referenced)
                        found.emplace_back(name, identity(row, name));
                }
    return found;
}
std::string World::asset_file(const std::string &key) const {
    auto row = find("assets", key);
    if (!row || !row->contains("files") || !(*row)["files"].is_object())
        return {};
    for (auto type : {"svg", "native", "default", "original", "512", "256", "128", "64"}) {
        auto found = (*row)["files"].find(type);
        if (found != (*row)["files"].end() && found->is_string() && store->contains(*found))
            return *found;
    }
    for (auto &value : (*row)["files"])
        if (value.is_string() && store->contains(value))
            return value;
    return {};
}
std::string World::portrait(const Json &person) const {
    auto key = str(person, "photo");
    auto file = asset_file(key.empty() ? "asset:player/" + str(person, "id") + "/photo" : key);
    return file;
}
Json World::validate() const {
    Json diagnostics = Json::array();
    auto issue = [&](const std::string &severity, const std::string &code, const std::string &name,
                     const std::string &id, Json details = Json::object()) {
        diagnostics.push_back(
            {{"severity", severity}, {"code", code}, {"table", name}, {"id", id}, {"details", details}});
    };
    std::map<std::string, std::string> ids;
    const auto editor = manifest.value("x-editor", Json::object());
    const auto requirements = manifest.value("requires", Json::array());
    if (str(manifest, "format") != "openbmh-world" || str(manifest, "kind") != "world" ||
        str(manifest, "format_version") != "1.1" || str(manifest, "min_reader") != "1.1" || !editor.is_object() ||
        str(editor, "profile") != "editor-world-v1" || !editor.contains("playable") || editor["playable"] != false ||
        !requirements.is_array() ||
        std::find(requirements.begin(), requirements.end(), Json("editor:world-v1")) == requirements.end())
        issue("error", "invalid_manifest", "manifest", "manifest");
    if (!date_valid(manifest.value("start_date", Json())))
        issue("error", "invalid_date", "manifest", "manifest");
    std::map<std::string, const Json *> attribute_sets;
    for (auto &rule : rows("rules"))
        if (str(rule, "type") == "attribute_set") {
            auto key = str(rule, "attribute_set", str(rule, "id"));
            attribute_sets[key] = &rule;
            if (key.starts_with("rule:"))
                attribute_sets[key.substr(5)] = &rule;
        }
    for (const auto &name : tables()) {
        if (name == "image-edits" || name == "review")
            continue;
        for (const auto &row : rows(name)) {
            auto id = identity(row, name);
            if (id.empty() || !ids.emplace(id, name).second)
                issue("error", "duplicate_id", name, id);
            if (name != "assets" && name != "calendar" && name != "rules" && title(row).empty())
                issue("warning", "missing_name", name, id);
            if (name == "players") {
                if (!date_valid(row.value("born", Json())))
                    issue("error", "invalid_date", name, id);
                if (!row.contains("born") && !row.contains("age_at_start"))
                    issue("warning", "missing_birthday", name, id);
                if (row.contains("born") && row["born"].is_null())
                    issue("warning", "missing_birthday", name, id);
                const auto attrs = row.value("attributes", Json::object());
                if (!attrs.is_object()) {
                    issue("error", "invalid_attributes", name, id);
                    continue;
                }
                const auto set = attribute_sets.find(str(row, "attribute_set"));
                if (set == attribute_sets.end())
                    issue("warning", "unknown_attribute_set", name, id);
                else {
                    auto fields = set->second->value("fields", Json::object());
                    for (auto it = attrs.begin(); it != attrs.end(); ++it) {
                        if (it->is_null() || !fields.contains(it.key()))
                            continue;
                        const auto &field = fields[it.key()];
                        if (!it->is_number_integer() ||
                            it->get<int64_t>() < field.value("min", std::numeric_limits<int64_t>::min()) ||
                            it->get<int64_t>() > field.value("max", std::numeric_limits<int64_t>::max()))
                            issue("error", "attribute_range", name, id, {{"field", it.key()}});
                    }
                }
            }
        }
    }
    for (auto &name : tables())
        if (name != "review" && name != "image-edits")
            for (auto &row : rows(name))
                walk_refs(row, [&](const std::string &ref) {
                    if (!ref.empty() && !ids.contains(ref) && !ref.starts_with("asset:"))
                        issue("error", "missing_reference", name, identity(row, name), {{"reference", ref}});
                });
    const std::map<std::string, std::string> reference_types{{"team", "clubs"},       {"owner", "clubs"},
                                                             {"nation", "nations"},   {"competition", "competitions"},
                                                             {"stadium", "stadiums"}, {"national_team", "clubs"}};
    for (auto &name : tables())
        if (name != "review" && name != "image-edits")
            for (const auto &row : rows(name)) {
                for (const auto &[field, target] : reference_types)
                    if (row.contains(field) && !row[field].is_null()) {
                        const auto key = str(row, field);
                        if (!row[field].is_string() || (!key.empty() && ids.contains(key) && ids.at(key) != target))
                            issue("error", "missing_reference", name, identity(row, name),
                                  {{"field", field}, {"expected_table", target}});
                    }
                for (const auto &[field, target] :
                     std::map<std::string, std::string>{{"nationalities", "nations"}, {"members", "clubs"}})
                    if (row.contains(field)) {
                        if (!row[field].is_array())
                            issue("error", "missing_reference", name, identity(row, name), {{"field", field}});
                        else
                            for (const auto &key : row[field])
                                if (!key.is_string() ||
                                    (ids.contains(key.get<std::string>()) && ids.at(key.get<std::string>()) != target))
                                    issue("error", "missing_reference", name, identity(row, name),
                                          {{"field", field}, {"expected_table", target}});
                    }
            }
    for (auto &asset : rows("assets")) {
        if (asset_file(str(asset, "key")).empty())
            issue("warning", "missing_asset", "assets", str(asset, "key"));
    }
    for (auto &review : rows("review"))
        if (!review.value("resolved", false))
            issue("warning", "review_open", "review", str(review, "id"));
    return diagnostics;
}
void World::save(const fs::path &destination) const {
    if (imported && !source_path.empty() &&
        fs::absolute(destination).lexically_normal() == source_path.lexically_normal())
        throw Error("preserve_source");
    const auto problems = validate();
    for (auto &row : problems)
        if (row["severity"] == "error")
            throw Error("validation_failed", {{"diagnostics", problems}});
    std::map<std::string, std::string> docs{{"manifest.json", manifest.dump(2)}};
    for (const auto &[file, value] : docs_)
        docs[file] = !dirty_.contains(file) && original_.contains(file) ? original_.at(file) : value.dump(2);
    store->save(destination, docs);
}
World World::demo() {
    auto w = blank("OpenBMH Editor – Demo");
    w.rows("nations").push_back({{"id", "nation:de"},
                                 {"name", {{"default", "Deutschland"}, {"en", "Germany"}}},
                                 {"confederation", "uefa"},
                                 {"playable", true}});
    w.rows("competitions")
        .push_back({{"id", "comp:demo"},
                    {"name", {{"default", "Demo-Liga"}, {"en", "Demo league"}}},
                    {"nation", "nation:de"},
                    {"type", "league"},
                    {"tier", 1},
                    {"visible", true},
                    {"members", {"club:north", "club:south"}},
                    {"stages", Json::array()}});
    w.rows("rules").push_back({{"id", "demo@1"},
                               {"type", "attribute_set"},
                               {"fields",
                                {{"strength", {{"min", 0}, {"max", 99}}},
                                 {"technique", {{"min", 0}, {"max", 99}}},
                                 {"condition", {{"min", 0}, {"max", 100}}},
                                 {"form", {{"min", 0}, {"max", 99}}}}}});
    for (int i = 0; i < 2; ++i) {
        auto id = i ? "club:south" : "club:north";
        auto stadium = i ? "stadium:south" : "stadium:north";
        const auto key = std::string("asset:demo/") + (i ? "south" : "north");
        const auto file = std::string("assets/demo/") + (i ? "south.svg" : "north.svg");
        const auto color = i ? "#b44b5c" : "#3698aa";
        std::string svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"128\" "
                          "height=\"144\" viewBox=\"0 0 128 144\"><path d=\"M10 8h108v80q0 26-54 "
                          "48Q10 114 10 88Z\" fill=\"" +
                          std::string(color) +
                          "\" stroke=\"#e5efef\" stroke-width=\"6\"/><path d=\"M34 98V40h15l30 "
                          "38V40h15v58H79L49 60v38Z\" fill=\"#fff\"/></svg>";
        w.store->set(file, std::make_shared<const std::string>(svg));
        w.rows("assets").push_back({{"key", key},
                                    {"name", {{"default", i ? "Südstadt" : "Nordstadt"}}},
                                    {"files", {{"svg", file}}},
                                    {"source", "synthetic-demo"},
                                    {"license", "GPL-3.0-only"},
                                    {"kind", "vector"},
                                    {"width", 128},
                                    {"height", 144}});
        w.rows("clubs").push_back({{"id", id},
                                   {"name", {{"default", i ? "SV Südstadt" : "FC Nordstadt"}}},
                                   {"short_name", i ? "Südstadt" : "Nordstadt"},
                                   {"type", "club"},
                                   {"nation", "nation:de"},
                                   {"competition", "comp:demo"},
                                   {"stadium", stadium},
                                   {"crest", key},
                                   {"colors", {i ? "#9c3949" : "#277c8b", "#ffffff"}},
                                   {"founded", 1900 + i}});
        w.rows("stadiums")
            .push_back({{"id", stadium},
                        {"name", {{"default", i ? "Südpark" : "Nordpark"}}},
                        {"owner", id},
                        {"capacity", {{"total", 22000 + 6000 * i}, {"seated", 18000 + 6000 * i}, {"standing", 4000}}}});
        for (int n = 0; n < 26; ++n) {
            w.rows("players").push_back(
                {{"id", "player:demo-" + std::to_string(i * 26 + n)},
                 {"name", {{"first", n % 2 ? "Alex" : "Robin"}, {"last", "Spieler " + std::to_string(i * 26 + n + 1)}}},
                 {"born", "2000-03-12"},
                 {"nationalities", {"nation:de"}},
                 {"positions",
                  {n < 2    ? "gk"
                   : n < 10 ? "def"
                   : n < 18 ? "mid"
                            : "fwd"}},
                 {"team", id},
                 {"squad_number", n + 1},
                 {"squad_order", n},
                 {"attribute_set", "demo@1"},
                 {"attributes",
                  {{"strength", 68 + (n % 15)}, {"technique", 65 + n % 20}, {"condition", 90}, {"form", 70}}}});
        }
    }
    w.rows("staff").push_back({{"id", "staff:referee-demo"},
                               {"name", {{"default", "Kim Beispiel"}}},
                               {"role", "referee"},
                               {"nation", "nation:de"},
                               {"attributes", {{"strictness", 2}}}});
    return w;
}
} // namespace bmh::editor
