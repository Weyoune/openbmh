// SPDX-License-Identifier: GPL-3.0-only
#include "editor/session.hpp"
#include "content/digest.hpp"
#include "editor/comparison.hpp"
#include "editor/images.hpp"
#include "editor/regulations.hpp"
#include <algorithm>
#include <limits>

namespace bmh::editor {
namespace {
bool editable(const std::string &name) {
    return name == "nations" || name == "competitions" || name == "clubs" || name == "players" || name == "stadiums" ||
           name == "staff" || name == "assets" || name == "review" || name == "image-edits" || name == "manifest";
}
Json defaults(const std::string &table, const std::string &name) {
    auto localized = Json{{"default", name}};
    if (table == "players")
        return {{"name", {{"first", ""}, {"last", name}}},
                {"born", nullptr},
                {"nationalities", Json::array()},
                {"positions", Json::array()},
                {"attributes", Json::object()},
                {"attribute_set", "unrated@1"}};
    if (table == "clubs")
        return {{"name", localized}, {"type", "club"}, {"nation", nullptr}, {"colors", Json::array()}};
    if (table == "nations")
        return {{"name", localized}, {"confederation", "other"}, {"playable", false}};
    if (table == "competitions")
        return {{"name", localized}, {"type", "league"},         {"nation", nullptr},      {"tier", 1},
                {"visible", true},   {"members", Json::array()}, {"stages", Json::array()}};
    if (table == "stadiums")
        return {{"name", localized}, {"capacity", Json::object()}, {"facilities", Json::object()}};
    if (table == "staff")
        return {{"name", localized}, {"role", "referee"}, {"attributes", Json::object()}};
    return Json::object();
}
std::string prefix(const std::string &table) {
    if (table == "nations")
        return "nation:";
    if (table == "competitions")
        return "comp:";
    if (table == "assets")
        return "asset:";
    if (table == "staff")
        return "staff:";
    if (table == "image-edits")
        return "image-edit:";
    return table.substr(0, table.size() - 1) + ':';
}
} // namespace
World &Session::world() {
    if (!world_)
        throw Error("no_world");
    return *world_;
}
bool Session::dirty() const { return world_ && state_ != saved_state_; }
Json Session::info() const {
    Json result = world_ ? world_->statistics() : Json{{"title", ""}, {"counts", Json::object()}};
    result["loaded"] = bool(world_);
    result["revision"] = revision_;
    result["world_generation"] = world_generation_;
    result["dirty"] = dirty();
    result["can_undo"] = cursor_ > 0;
    result["can_redo"] = cursor_ < history_.size();
    result["path"] = utf8(saved_path_);
    return result;
}
void Session::replace(World value) {
    world_ = std::make_unique<World>(std::move(value));
    ++world_generation_;
    history_.clear();
    cursor_ = 0;
    ++revision_;
    state_ = next_state_++;
    saved_state_ = world_->source_path.empty() || world_->imported ? 0 : state_;
    saved_path_ = world_->source_path.empty() || world_->imported || !fs::is_regular_file(world_->source_path)
                      ? fs::path{}
                      : world_->source_path;
    if (!saved_path_.empty() && fs::is_regular_file(saved_path_)) {
        saved_time_ = fs::last_write_time(saved_path_);
        saved_size_ = fs::file_size(saved_path_);
    }
}
void Session::frame(const Frame &f, bool forward) {
    for (const auto &change : f.changes) {
        auto value = forward ? change.after : change.before;
        if (change.table == "manifest") {
            if (value)
                world().manifest = *value;
        } else
            world().change(change.table, change.id, value);
    }
    for (const auto &file : f.files)
        world().store->restore(file.name, forward ? file.after : file.before);
    state_ = forward ? f.after : f.before;
}
Json Session::apply(const Json &args, std::vector<FileDelta> files, const std::string &label) {
    auto &w = world();
    if (args.contains("expected_revision") && args["expected_revision"].get<uint64_t>() != revision_)
        throw Error("stale_revision");
    if (!args.contains("changes") || !args["changes"].is_array() || args["changes"].size() > 5000)
        throw Error("invalid_change");
    Frame transaction;
    transaction.label = label;
    transaction.files = std::move(files);
    transaction.before = state_;
    transaction.after = next_state_;
    std::map<std::pair<std::string, std::string>, size_t> staged;
    Json created = Json::array();
    for (const auto &action : args["changes"]) {
        const auto name = str(action, "table"), op = str(action, "op", "update");
        if (!editable(name))
            throw Error("unknown_table");
        auto id = str(action, "id");
        if (op == "create" && id.empty()) {
            id = new_id(prefix(name) + "editor-");
            created.push_back(id);
        }
        if (name == "manifest")
            id = "manifest";
        auto key = std::make_pair(name, id);
        if (!staged.contains(key)) {
            const auto *existing = name == "manifest" ? &w.manifest : w.find(name, id);
            std::optional<Json> before = existing ? std::optional<Json>(*existing) : std::nullopt;
            staged[key] = transaction.changes.size();
            transaction.changes.push_back({name, id, before, before});
        }
        auto &delta = transaction.changes[staged[key]];
        if (op == "create") {
            if (delta.after || name == "manifest")
                throw Error("duplicate_id");
            delta.after = defaults(name, str(action, "name"));
            if (action.contains("data"))
                delta.after->update(action["data"]);
            (*delta.after)[name == "assets" ? "key" : "id"] = id;
        } else if (op == "delete") {
            if (!delta.after)
                throw Error("record_missing", {{"table", name}, {"id", id}});
            if (name == "manifest")
                throw Error("invalid_change");
            delta.after.reset();
        } else if (op == "update") {
            if (!delta.after)
                throw Error("record_missing", {{"table", name}, {"id", id}});
            const auto set = action.value("set", Json::object());
            if (!set.is_object())
                throw Error("invalid_change");
            for (auto it = set.begin(); it != set.end(); ++it) {
                const auto pointer = it.key();
                if (pointer.empty() || pointer[0] != '/' || pointer == "/id" || pointer.starts_with("/id/") ||
                    (name == "assets" && pointer == "/key"))
                    throw Error("immutable_id");
                if (name == "manifest" && pointer != "/title" && !pointer.starts_with("/title/") &&
                    pointer != "/start_date" && pointer != "/currency" && !pointer.starts_with("/currency/") &&
                    pointer != "/languages")
                    throw Error("immutable_id");
                (*delta.after)[Json::json_pointer(pointer)] = it.value();
            }
            for (const auto &field : action.value("unset", Json::array())) {
                auto pointer = field.get<std::string>();
                if (pointer.empty() || pointer[0] != '/' || pointer == "/id" || (name == "assets" && pointer == "/key"))
                    throw Error("immutable_id");
                if (name == "manifest" && pointer != "/title" && !pointer.starts_with("/title/") &&
                    pointer != "/start_date" && pointer != "/currency" && !pointer.starts_with("/currency/") &&
                    pointer != "/languages")
                    throw Error("immutable_id");
                auto split = pointer.rfind('/');
                auto parent = Json::json_pointer(pointer.substr(0, split));
                if (delta.after->contains(parent)) {
                    auto leaf_pointer = Json::json_pointer(pointer);
                    auto &container = (*delta.after)[parent];
                    const auto leaf = leaf_pointer.back();
                    if (container.is_object())
                        container.erase(leaf);
                    else if (container.is_array()) {
                        const auto index = std::stoull(leaf);
                        if (index < container.size())
                            container.erase(container.begin() + std::ptrdiff_t(index));
                    }
                }
            }
        } else
            throw Error("invalid_change");
    }
    transaction.changes.erase(std::remove_if(transaction.changes.begin(), transaction.changes.end(),
                                             [](const Delta &d) { return d.before == d.after; }),
                              transaction.changes.end());
    // Membership is edited once, at the club, and stays consistent in each
    // affected league. This derived change belongs to the same undo frame.
    std::map<std::string, Json> memberships;
    for (const auto &d : transaction.changes)
        if (d.table == "clubs") {
            const auto old_league = d.before ? str(*d.before, "competition") : "";
            const auto new_league = d.after ? str(*d.after, "competition") : "";
            if (old_league == new_league)
                continue;
            for (auto league : {old_league, new_league})
                if (!league.empty()) {
                    auto *comp = w.find("competitions", league);
                    if (!memberships.contains(league)) {
                        if (comp)
                            memberships[league] = *comp;
                        for (const auto &staged_delta : transaction.changes)
                            if (staged_delta.table == "competitions" && staged_delta.id == league && staged_delta.after)
                                memberships[league] = *staged_delta.after;
                        if (!memberships.contains(league))
                            throw Error("missing_reference");
                    }
                    auto &members = memberships[league]["members"];
                    if (members.is_null())
                        members = Json::array();
                    if (!members.is_array())
                        throw Error("invalid_change");
                    members.erase(std::remove(members.begin(), members.end(), Json(d.id)), members.end());
                    if (league == new_league)
                        members.push_back(d.id);
                }
        }
    for (auto &[id, after] : memberships) {
        bool found = false;
        for (auto &d : transaction.changes)
            if (d.table == "competitions" && d.id == id) {
                d.after = after;
                found = true;
                break;
            }
        if (!found)
            transaction.changes.push_back({"competitions", id, *w.find("competitions", id), after});
    }
    if (transaction.changes.empty() && transaction.files.empty())
        return {{"changed", false}, {"revision", revision_}, {"created", created}};
    try {
        frame(transaction, true);
        for (auto &change : transaction.changes)
            if (!change.after) {
                auto refs = w.references(change.id);
                if (!refs.empty()) {
                    Json list = Json::array();
                    for (auto &[table, id] : refs)
                        list.push_back({{"table", table}, {"id", id}});
                    throw Error("record_referenced", {{"references", list}});
                }
            }
        for (const auto &problem : w.validate())
            if (problem["severity"] == "error")
                throw Error("validation_failed", {{"diagnostics", w.validate()}});
    } catch (...) {
        frame(transaction, false);
        throw;
    }
    history_.resize(cursor_);
    history_.push_back(std::move(transaction));
    ++cursor_;
    ++next_state_;
    ++revision_;
    if (history_.size() > 100) {
        history_.erase(history_.begin());
        --cursor_;
    }
    return {{"changed", true}, {"revision", revision_}, {"created", created}};
}
Json Session::import_image(const Json &args) {
    auto &w = world();
    std::string original, extension, original_name;
    const auto source_key = str(args, "source_key", str(args, "key"));
    const auto *existing = w.find("assets", source_key);
    if (args.contains("file")) {
        original = read_file(path(str(args, "file")));
        extension = folded(utf8(path(str(args, "file")).extension()));
        original_name = utf8(path(str(args, "file")).filename());
    } else if (source_key.starts_with("portrait:") && w.find("players", source_key.substr(9))) {
        auto value = asset_bytes(source_key);
        original = std::move(value.first);
        extension = value.second;
        original_name = w.title(*w.find("players", source_key.substr(9)));
    } else {
        if (!existing)
            throw Error("record_missing");
        auto source = str(existing->value("x-editor", Json::object()), "original");
        if (source.empty())
            source = w.asset_file(source_key);
        original = w.store->read(source);
        extension = folded(utf8(path(source).extension()));
        original_name = text(existing->value("name", Json(str(args, "key"))));
    }
    if (extension != ".png" && extension != ".jpg" && extension != ".jpeg" && extension != ".webp" &&
        extension != ".svg")
        throw Error("invalid_image");
    auto result = images::transform(original, extension, args.value("transform", Json::object()));
    auto key = str(args, "key");
    const auto target = args.value("target", Json::object());
    const auto target_table = str(target, "table");
    const auto target_field = str(target, "field",
                                  target_table == "players" || target_table == "staff" ? "photo"
                                  : target_table == "competitions"                     ? "trophy"
                                                                                       : "crest");
    if (key.empty()) {
        if (!target.empty()) {
            auto id = str(target, "id");
            auto table = str(target, "table");
            key = "asset:" + table + '/' + id + '/' + target_field;
        } else
            key = new_id("asset:uploaded/");
    }
    if (!target.empty() && w.find("assets", key)) {
        for (const auto &[table, id] : w.references(key))
            if (table != str(target, "table") || id != str(target, "id")) {
                key = "asset:editor/" + target_table + '/' + str(target, "id") + '/' + target_field;
                break;
            }
    }
    const auto original_hash = content::sha256(original), edited_hash = content::sha256(result.bytes);
    auto source_file = "x-editor/originals/" + original_hash + extension;
    auto output_file = "assets/edited/" + edited_hash + result.extension;
    std::vector<FileDelta> files;
    for (auto &[name, data] :
         std::vector<std::pair<std::string, std::string>>{{source_file, original}, {output_file, result.bytes}}) {
        if (!w.store->contains(name))
            files.push_back({name, w.store->overlay(name), std::make_shared<const std::string>(std::move(data))});
    }
    Json row{
        {"key", key},
        {"name", {{"default", original_name}}},
        {"files", {{result.extension == ".svg" ? "svg" : "native", output_file}}},
        {"width", result.width},
        {"height", result.height},
        {"source", "user-import"},
        {"author", "user-supplied"},
        {"license", "user-supplied"},
        {"kind", result.extension == ".svg" ? "vector" : "raster"},
        {"x-editor", {{"original", source_file}, {"original_sha256", original_hash}, {"transform", result.recipe}}}};
    if (!args.contains("file") && !source_key.empty() && source_key != key)
        row["x-editor"]["source_asset"] = source_key;
    Json changes = Json::array();
    if (const auto *before = w.find("assets", key)) {
        auto after = *before;
        after.update(row);
        Json set = Json::object();
        for (auto it = after.begin(); it != after.end(); ++it)
            if (it.key() != "key")
                set['/' + it.key()] = it.value();
        changes.push_back({{"op", "update"}, {"table", "assets"}, {"id", key}, {"set", set}});
    } else
        changes.push_back({{"op", "create"}, {"table", "assets"}, {"id", key}, {"data", row}});
    if (!target.empty())
        changes.push_back({{"op", "update"},
                           {"table", str(target, "table")},
                           {"id", str(target, "id")},
                           {"set", {{'/' + target_field, key}}}});
    Json command{{"changes", changes}};
    if (args.contains("expected_revision"))
        command["expected_revision"] = args["expected_revision"];
    auto response = apply(command, std::move(files), "image_import");
    response["key"] = key;
    return response;
}
std::pair<std::string, std::string> Session::asset_bytes(const std::string &key) const {
    std::lock_guard guard(mutex_);
    if (!world_)
        return {images::silhouette(), ".svg"};
    if (key.starts_with("portrait:")) {
        const auto *player = world_->find("players", key.substr(9));
        if (player) {
            auto photo = world_->asset_file(str(*player, "photo"));
            if (!photo.empty())
                return {world_->store->read(photo), utf8(path(photo).extension())};
            auto recipe = player->value("appearance", Json::object());
            auto set = str(recipe, "set");
            const auto file = "assets/portrait-sets/" + set + "/set.json";
            if (safe_name(file) && world_->store->contains(file)) {
                const auto parts = Json::parse(world_->store->read(file));
                int width = parts.value("width", 0), height = parts.value("height", 0);
                if (width <= 0 || height <= 0 || width > 8192 || height > 8192 ||
                    uint64_t(width) * height > 32ull * 1024 * 1024)
                    throw Error("image_dimensions");
                images::Raster portrait{width, height, std::vector<uint8_t>(size_t(width) * height * 4)};
                auto paint = [&](const std::string &asset, int x, int y) {
                    auto input = world_->asset_file(asset);
                    if (input.empty())
                        return;
                    auto layer = images::decode(world_->store->read(input), utf8(path(input).extension()));
                    for (int sy = 0; sy < layer.height; ++sy)
                        for (int sx = 0; sx < layer.width; ++sx) {
                            int dx = x + sx, dy = y + sy;
                            if (dx < 0 || dy < 0 || dx >= width || dy >= height)
                                continue;
                            const auto *src = layer.pixels.data() + (size_t(sy) * layer.width + sx) * 4;
                            auto *dst = portrait.pixels.data() + (size_t(dy) * width + dx) * 4;
                            const unsigned alpha = src[3] * 255u + dst[3] * (255u - src[3]);
                            for (int c = 0; c < 3; ++c)
                                dst[c] =
                                    alpha
                                        ? uint8_t((src[c] * src[3] * 255u + dst[c] * dst[3] * (255u - src[3])) / alpha)
                                        : 0;
                            dst[3] = uint8_t((alpha + 127) / 255);
                        }
                };
                auto backgrounds = parts.value("backgrounds", Json::array());
                auto background = recipe.value("background", size_t(0));
                if (background < backgrounds.size()) {
                    const auto &b = backgrounds[background];
                    paint(str(b, "asset"), b.value("x", 0), b.value("y", 0));
                }
                auto layers = parts.value("layers", Json::array()), choices = recipe.value("layers", Json::array());
                for (size_t i = 0; i < std::min(layers.size(), choices.size()); ++i) {
                    auto options = layers[i].value("parts", Json::array());
                    const auto choice = choices[i].get<size_t>();
                    if (choice < options.size())
                        paint(options[choice].get<std::string>(), layers[i].value("x", 0), layers[i].value("y", 0));
                }
                return {images::encode_png(portrait), ".png"};
            }
        }
        return {images::silhouette(), ".svg"};
    }
    auto file = key.starts_with("asset:") ? world_->asset_file(key) : key;
    if (file.empty() || !world_->store->contains(file))
        return {images::silhouette(), ".svg"};
    return {world_->store->read(file), utf8(path(file).extension())};
}
Json Session::execute(const std::string &command, const Json &args) {
    std::lock_guard guard(mutex_);
    if (!args.is_object())
        throw Error("invalid_change");
    if (args.contains("expected_revision") && args["expected_revision"].get<uint64_t>() != revision_)
        throw Error("stale_revision");
    if (command == "info")
        return info();
    if (command == "open" || command == "new" || command == "demo") {
        if (dirty() && !args.value("discard", false))
            throw Error("unsaved_changes");
        if (command == "open")
            replace(World::open(path(str(args, "path")), str(args, "start_date", "2026-07-01")));
        else
            replace(command == "demo" ? World::demo() : World::blank(str(args, "title", "Untitled world")));
        return info();
    }
    auto &w = world();
    if (command == "list")
        return w.query(args);
    if (command == "compare") {
        const Json key{{"table", str(args, "table", "players")},
                       {"attribute_set", str(args, "attribute_set")},
                       {"language", str(args, "language", "de")}};
        if (comparison_revision_ != revision_ || comparison_key_ != key.dump()) {
            auto data = comparison_data(w, key);
            comparison_cache_ = std::move(data);
            comparison_key_ = key.dump();
            comparison_revision_ = revision_;
        }
        auto result = query_comparison(comparison_cache_, args);
        result["revision"] = revision_;
        return result;
    }
    if (command == "get") {
        auto name = str(args, "table"), id = str(args, "id");
        const auto *found = name == "manifest" ? &w.manifest : w.find(name, id);
        if (!found)
            throw Error("record_missing");
        return {{"record", *found}, {"revision", revision_}};
    }
    if (command == "rules")
        return w.rows("rules");
    if (command == "regulations") {
        auto result = regulation_view(w, args);
        result["revision"] = revision_;
        return result;
    }
    if (command == "validate")
        return {{"diagnostics", w.validate()}, {"revision", revision_}};
    if (command == "history") {
        Json values = Json::array();
        for (size_t i = 0; i < history_.size(); ++i) {
            Json affected = Json::array();
            for (auto &delta : history_[i].changes)
                affected.push_back({{"table", delta.table},
                                    {"id", delta.id},
                                    {"before", delta.before ? *delta.before : Json()},
                                    {"after", delta.after ? *delta.after : Json()}});
            values.push_back({{"label", history_[i].label}, {"applied", i < cursor_}, {"changes", affected}});
        }
        return {{"items", values}, {"revision", revision_}};
    }
    if (command == "apply")
        return apply(args);
    if (command == "undo" || command == "redo") {
        if (args.contains("expected_revision") && args["expected_revision"].get<uint64_t>() != revision_)
            throw Error("stale_revision");
        if (command == "undo") {
            if (!cursor_)
                throw Error("nothing_to_undo");
            frame(history_[--cursor_], false);
        } else {
            if (cursor_ >= history_.size())
                throw Error("nothing_to_redo");
            frame(history_[cursor_++], true);
        }
        ++revision_;
        return info();
    }
    if (command == "save") {
        const auto destination = args.contains("path") ? path(str(args, "path")) : saved_path_;
        if (destination.empty())
            throw Error("output_parent_missing");
        if (!saved_path_.empty() && fs::absolute(destination) == fs::absolute(saved_path_) &&
            fs::is_regular_file(saved_path_) &&
            (fs::last_write_time(saved_path_) != saved_time_ || fs::file_size(saved_path_) != saved_size_))
            throw Error("source_changed");
        w.save(destination);
        saved_path_ = fs::absolute(destination);
        saved_time_ = fs::last_write_time(saved_path_);
        saved_size_ = fs::file_size(saved_path_);
        saved_state_ = state_;
        return info();
    }
    if (command == "roster.move") {
        auto person = str(args, "player");
        Json set{{"/team", args.value("team", Json())}};
        if (args.contains("number"))
            set["/squad_number"] = args["number"];
        auto change = Json{{"op", "update"}, {"table", "players"}, {"id", person}, {"set", set}};
        return apply({{"changes", Json::array({change})}}, {}, "move_player");
    }
    if (command == "roster.swap") {
        auto first = w.find("players", str(args, "first")), second = w.find("players", str(args, "second"));
        if (!first || !second || first == second)
            throw Error("invalid_change");
        Json changes = Json::array();
        for (auto pair : {std::pair{first, second}, std::pair{second, first}}) {
            Json set;
            for (auto field : {"team", "squad_number", "squad_order"})
                set['/' + std::string(field)] = pair.second->value(field, Json());
            changes.push_back({{"op", "update"}, {"table", "players"}, {"id", (*pair.first)["id"]}, {"set", set}});
        }
        return apply({{"changes", changes}}, {}, "swap_player");
    }
    if (command == "roster.order") {
        auto team = str(args, "team");
        auto order = args.value("players", Json::array());
        std::set<std::string> expected, actual;
        for (auto &row : w.rows("players"))
            if (str(row, "team") == team)
                expected.insert(str(row, "id"));
        for (auto &row : order)
            actual.insert(row.get<std::string>());
        if (expected != actual || actual.size() != order.size())
            throw Error("invalid_change");
        Json changes = Json::array();
        for (size_t i = 0; i < order.size(); ++i)
            changes.push_back(
                {{"op", "update"}, {"table", "players"}, {"id", order[i]}, {"set", {{"/squad_order", i}}}});
        return apply({{"changes", changes}}, {}, "roster");
    }
    if (command == "review.resolve") {
        auto record = w.find("review", str(args, "id"));
        if (!record)
            throw Error("record_missing");
        const auto choice = args.value("choice", size_t(0));
        auto alternatives = record->value("alternatives", Json::array());
        if (choice >= alternatives.size())
            throw Error("invalid_change");
        const auto chosen = alternatives[choice].at("value");
        Json changes =
            Json::array({{{"op", "update"},
                          {"table", "review"},
                          {"id", str(args, "id")},
                          {"set", {{"/resolved", true}, {"/selected_index", choice}, {"/chosen_value", chosen}}}}});
        if (record->contains("entity") && record->contains("field"))
            changes.push_back({{"op", "update"},
                               {"table", record->value("table", "players")},
                               {"id", (*record)["entity"]},
                               {"set", {{str(*record, "field"), chosen}}}});
        return apply({{"changes", changes}}, {}, "resolve");
    }
    if (command == "asset.import" || command == "asset.edit")
        return import_image(args);
    if (command == "asset.preview") {
        auto [bytes, extension] = asset_bytes(str(args, "key"));
        auto raster = images::preview(bytes, extension, args.value("transform", Json::object()), 1024);
        return {{"mimeType", "image/png"},
                {"data", images::base64(images::encode_png(raster))},
                {"width", raster.width},
                {"height", raster.height}};
    }
    if (command == "asset.export") {
        auto [bytes, extension] = asset_bytes(str(args, "key"));
        auto data = images::encode_png(images::decode(bytes, extension, 2048));
        auto target = path(str(args, "path"));
        if (folded(utf8(target.extension())) != ".png")
            throw Error("invalid_image");
        write_atomic(target, data);
        return {{"path", utf8(target)}};
    }
    throw Error("invalid_change", {{"command", command}});
}
} // namespace bmh::editor
