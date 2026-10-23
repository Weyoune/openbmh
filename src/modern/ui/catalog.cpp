// SPDX-License-Identifier: GPL-3.0-only
#include "modern/ui/catalog.hpp"
#include <algorithm>
#include <array>
#include <limits>

namespace bmh::modern_ui {
std::string localized(const Json &v, const std::string &language) {
  if (v.is_string())
    return v.get<std::string>();
  if (!v.is_object())
    return {};
  for (const auto &key : {language, std::string("default"), std::string("en")})
    if (auto i = v.find(key); i != v.end() && i->is_string() &&
                              !i->get_ref<const std::string &>().empty())
      return i->get<std::string>();
  if (v.contains("first") || v.contains("last")) {
    const auto first = localized(v.value("first", Json())),
               last = localized(v.value("last", Json()));
    return first + (first.empty() || last.empty() ? "" : " ") + last;
  }
  return {};
}
std::string number(int64_t n) {
  const auto raw = std::to_string(n);
  std::string out;
  for (size_t i = 0; i < raw.size(); ++i) {
    if (i && raw[i - 1] != '-' && (raw.size() - i) % 3 == 0)
      out += '.';
    out += raw[i];
  }
  return out;
}
std::string money(int64_t n, const Json &currency) {
  const int units = currency.value("minor_units", 0);
  const auto code = currency.value("code", std::string("EUR"));
  const auto suffix = code == "EUR" ? " €" : " " + code;
  if (!units)
    return number(n) + suffix;
  if (units != 2)
    throw editor::Error("unsupported_currency_precision");
  const auto remainder = int(n % 100 < 0 ? -(n % 100) : n % 100);
  return (n < 0 && n > -100 ? "-" : "") + number(n / 100) + "," +
         (remainder < 10 ? "0" : "") + std::to_string(remainder) + suffix;
}
bool Catalog::modern_manifest(const Json &m) {
  if (!m.is_object() || m.value("format", "") != "openbmh-world" ||
      m.value("kind", "") != "world")
    return false;
  const auto required = m.value("requires", Json::array());
  if (!required.is_array())
    return false;
  if (std::find(required.begin(), required.end(), "runtime:modern-v1") !=
      required.end())
    return true;
  return std::find(required.begin(), required.end(), "editor:world-v1") !=
             required.end() &&
         m.value("x-editor", Json::object()).value("profile", "") ==
             "editor-world-v1";
}
std::shared_ptr<const Catalog> Catalog::open(const fs::path &input) {
  auto c = std::shared_ptr<Catalog>(new Catalog);
  c->source_ = fs::absolute(input).lexically_normal();
  c->store_ = editor::ArchiveStore::open(c->source_);
  c->manifest_ = Json::parse(c->store_->read("manifest.json"));
  if (!modern_manifest(c->manifest_))
    throw editor::Error("modern_world_required");
  if (!c->manifest_.value("format_version", std::string()).starts_with("1."))
    throw editor::Error("unsupported_version");
  for (const char *table :
       {"nations", "clubs", "players", "competitions", "contracts", "finance",
        "staff", "stadiums", "rules", "calendar", "assets"}) {
    const auto file = std::string(table) == "assets"
                          ? "assets/index.json"
                          : std::string("world/") + table + ".json";
    Json items = Json::array();
    if (c->store_->contains(file)) {
      const auto doc = Json::parse(c->store_->read(file));
      if (!doc.is_object() || !doc.contains("items") ||
          !doc["items"].is_array())
        throw editor::Error("invalid_table", {{"file", file}});
      items = doc["items"];
    } else if (std::string(table) == "clubs" ||
               std::string(table) == "players" ||
               std::string(table) == "competitions")
      throw editor::Error("missing_table", {{"file", file}});
    auto &index = c->indices_[table];
    for (size_t i = 0; i < items.size(); ++i) {
      if (!items[i].is_object())
        throw editor::Error("invalid_record", {{"file", file}});
      const auto id = items[i].value(
          std::string(table) == "assets" ? "key" : "id", std::string());
      if (id.empty() || !index.emplace(id, i).second)
        throw editor::Error("duplicate_or_missing_id", {{"file", file}});
    }
    c->tables_[table] = std::move(items);
  }
  if (const auto file = c->manifest_.value("presentation", std::string());
      !file.empty())
    c->presentation_ = Json::parse(c->store_->read(file));
  for (const auto *league : c->leagues()) {
    const auto id = league->value("id", "");
    for (const auto *club : c->clubs(id))
      if (!c->find("nations", club->value("nation", "")))
        throw editor::Error("unknown_nation");
  }
  return c;
}
const Json &Catalog::rows(const std::string &table) const {
  return tables_.at(table);
}
const Json *Catalog::find(const std::string &table,
                          const std::string &id) const {
  const auto t = indices_.find(table);
  if (t == indices_.end())
    return nullptr;
  const auto i = t->second.find(id);
  return i == t->second.end() ? nullptr : &tables_.at(table).at(i->second);
}
std::string Catalog::title() const {
  return localized(manifest_.value("title", Json()));
}
std::string Catalog::name(const Json &row) const {
  return localized(row.value("name", Json()));
}
std::string Catalog::nation_name(const std::string &id) const {
  const auto *n = find("nations", id);
  return n ? name(*n) : id;
}
std::vector<const Json *> Catalog::leagues(const std::string &nation) const {
  std::vector<const Json *> out;
  for (const auto &r : rows("competitions"))
    if (r.value("type", "") == "league" &&
        (nation.empty() || r.value("nation", "") == nation))
      out.push_back(&r);
  return out;
}
std::vector<const Json *> Catalog::clubs(const std::string &league) const {
  std::vector<const Json *> out;
  for (const auto &r : rows("clubs"))
    if (r.value("type", "club") == "club" &&
        (league.empty() || r.value("competition", "") == league))
      out.push_back(&r);
  return out;
}
std::vector<const Json *> Catalog::squad(const std::string &club) const {
  std::vector<const Json *> out;
  for (const auto &r : rows("players"))
    if (r.value("team", "") == club)
      out.push_back(&r);
  std::stable_sort(out.begin(), out.end(), [](auto a, auto b) {
    return a->value("squad_order", 999) < b->value("squad_order", 999);
  });
  return out;
}
bool Catalog::manageable(const std::string &club) const {
  const auto *r = find("clubs", club);
  const auto *league =
      r ? find("competitions", r->value("competition", "")) : nullptr;
  return r && r->value("type", "club") == "club" &&
         r->value("nation", "") == "nation:de" && league &&
         league->value("nation", "") == "nation:de" &&
         league->value("type", "") == "league";
}
std::pair<std::string, std::string> Catalog::asset(const std::string &key,
                                                   int pixels) const {
  const auto *a = find("assets", key);
  if (!a && !presentation_.is_null() && key.find("/photo") != std::string::npos)
    a = find("assets", presentation_.value("player_photo_fallback", ""));
  if (!a)
    return {};
  const auto files = a->value("files", Json::object());
  std::string file;
  if (files.contains("svg") && files["svg"].is_string())
    file = files["svg"].get<std::string>();
  if (file.empty()) {
    std::vector<std::pair<int, std::string>> candidates;
    for (const auto &[size, value] : files.items()) {
      if (!value.is_string() || size == "native" || size == "svg")
        continue;
      try {
        candidates.emplace_back(std::stoi(size), value.get<std::string>());
      } catch (const std::exception &) {
        if (file.empty())
          file = value.get<std::string>();
      }
    }
    if (!candidates.empty()) {
      std::sort(candidates.begin(), candidates.end());
      file = candidates.back().second;
      for (const auto &[size, name] : candidates)
        if (size >= pixels) {
          file = name;
          break;
        }
    }
  }
  if (file.empty())
    file = files.value("native", "");
  if (file.empty() || !store_->contains(file))
    return {};
  return {store_->read(file), fs::path(file).extension().string()};
}
Json Catalog::summary() const {
  Json leagues_json = Json::array();
  for (auto *r : leagues())
    leagues_json.push_back({{"id", r->value("id", "")},
                            {"name", name(*r)},
                            {"clubs", clubs(r->value("id", "")).size()}});
  return {{"title", title()},
          {"leagues", leagues_json},
          {"clubs", rows("clubs").size()},
          {"players", rows("players").size()},
          {"engine", false}};
}
} // namespace bmh::modern_ui
