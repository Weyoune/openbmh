// SPDX-License-Identifier: GPL-3.0-only
#include "modern/ui/model.hpp"
#include "modern/ui/demo_sports.hpp"
#include "modern/ui/desktop.hpp"
#include <algorithm>
#include <chrono>

namespace bmh::modern_ui {
namespace {
bool valid_date(const Json &value) {
  if (!value.is_string())
    return false;
  const auto text = value.get<std::string>();
  if (text.size() != 10 || text[4] != '-' || text[7] != '-')
    return false;
  for (size_t i = 0; i < text.size(); ++i)
    if (i != 4 && i != 7 && (text[i] < '0' || text[i] > '9'))
      return false;
  const int year = std::stoi(text.substr(0, 4)),
            month = std::stoi(text.substr(5, 2)),
            day = std::stoi(text.substr(8, 2));
  return year > 0 &&
         std::chrono::year_month_day{std::chrono::year{year},
                                     std::chrono::month{unsigned(month)},
                                     std::chrono::day{unsigned(day)}}
             .ok();
}
bool camp_id(const Json &value) {
  if (!value.is_string())
    return false;
  const auto id = value.get<std::string>();
  for (int i = 0; i < 18; ++i)
    if (id == "resort:" + std::to_string(i))
      return true;
  return false;
}
bool preferences(const Json &value) {
  if (!value.is_object())
    return false;
  const auto theme = value.value("theme", std::string("midnight"));
  return (theme == "midnight" || theme == "silver" || theme == "matchday") &&
         (!value.contains("volume") ||
          (value["volume"].is_number_integer() && value["volume"] >= 0 &&
           value["volume"] <= 100)) &&
         (!value.contains("effects") || value["effects"].is_boolean());
}
} // namespace
DemoSource::DemoSource(std::shared_ptr<const Catalog> catalog)
    : catalog_(std::move(catalog)) {
  std::string club;
  for (const auto *c : catalog_->clubs()) {
    if (!catalog_->manageable(c->value("id", "")))
      continue;
    club = c->value("id", "");
    break;
  }
  if (club.empty())
    throw editor::Error("no_manageable_demo_club");
  state_ = {
      {"schema", "openbmh-ui-demo@1"},
      {"world", catalog_->manifest().value("id", "")},
      {"date", catalog_->manifest().value("start_date", "2026-07-01")},
      {"managers",
       Json::array(
           {{{"name", "Alex Berger"}, {"club", club}, {"portrait", 0}}})},
      {"cash", 12450000},
      {"deposit", 600000},
      {"debt", 0},
      {"ticket", 13},
      {"fans", 67},
      {"camp",
       {{"selected", "resort:4"}, {"confirmed", ""}, {"focus", {5, 4, 6}}}},
      {"opening", "complete"},
      {"preferences",
       {{"theme", "midnight"}, {"volume", 80}, {"effects", true}}},
      {"edits", Json::object()}};
  state_["training"] = demo_training(*catalog_, club);
  state_["tactics"] = demo_tactics();
  saved_ = state_;
  if (!valid_date(state_.at("date")))
    throw editor::Error("invalid_demo_date");
}
Snapshot DemoSource::snapshot() const { return {revision_, catalog_, state_}; }
ActionResult DemoSource::apply(const Action &action) {
  if (action.revision != revision_)
    return {false, revision_, "stale_revision"};
  auto next = state_;
  try {
    if (action.kind == "ticket") {
      if (!action.value.is_number_integer() || action.value < 0 ||
          action.value > 25)
        return {false, revision_, "invalid_demo_ticket"};
      next["ticket"] = action.value;
    } else if (action.kind == "managers") {
      if (!action.value.is_array() || action.value.empty() ||
          action.value.size() > 4)
        return {false, revision_, "invalid_managers"};
      std::set<std::string> clubs;
      std::set<int> portraits;
      for (const auto &m : action.value) {
        const auto club = m.at("club").get<std::string>();
        const auto name = m.at("name").get<std::string>();
        if (!catalog_->manageable(club))
          return {false, revision_, "club_locked"};
        const auto portrait = m.value("portrait", 0);
        if (name.find_first_not_of(" \t\r\n") == name.npos ||
            name.size() > 160 || portrait < 0 || portrait > 11 ||
            !clubs.insert(club).second || !portraits.insert(portrait).second)
          return {false, revision_, "invalid_manager"};
      }
      next["managers"] = action.value;
      const auto first = action.value.at(0).at("club").get<std::string>();
      if (next.at("training").at("club") != first) {
        next["training"] = demo_training(*catalog_, first);
        next["tactics"] = demo_tactics();
      }
    } else if (action.kind == "training") {
      const auto error = edit_demo_training(next["training"], action.value);
      if (!error.empty())
        return {false, revision_, error};
    } else if (action.kind == "tactics") {
      const auto error = edit_demo_tactics(next["tactics"], action.value);
      if (!error.empty())
        return {false, revision_, error};
    } else if (action.kind == "camp.select") {
      if (!camp_id(action.value))
        return {false, revision_, "camp_unknown"};
      next["camp"]["selected"] = action.value;
    } else if (action.kind == "camp.focus") {
      if (!action.value.is_array() || action.value.size() != 3)
        return {false, revision_, "invalid_focus"};
      for (const auto &v : action.value)
        if (!v.is_number_integer() || v < 0 || v > 15)
          return {false, revision_, "invalid_focus"};
      next["camp"]["focus"] = action.value;
    } else if (action.kind == "camp.remember")
      next["camp"]["confirmed"] = next["camp"]["selected"];
    else if (action.kind == "opening")
      next["opening"] = action.value;
    else if (action.kind == "preferences") {
      if (!preferences(action.value))
        return {false, revision_, "invalid_preferences"};
      next["preferences"] = action.value;
    } else if (action.kind == "edit") {
      const auto key = action.value.at("key").get<std::string>();
      if (key.empty() || key.size() > 100)
        return {false, revision_, "invalid_edit"};
      next["edits"][key] = action.value.at("value");
    } else
      return {false, revision_, "unsupported_demo_action"};
  } catch (const Json::exception &) {
    return {false, revision_, "invalid_action"};
  }
  state_ = std::move(next);
  return {true, ++revision_, {}};
}
void DemoSource::save(const fs::path &file, const Json &desktop) {
  if (!valid_desktop(desktop))
    throw editor::Error("invalid_demo_desktop");
  auto out = state_;
  out["desktop"] = desktop;
  editor::write_atomic(file, out.dump(2));
  saved_ = state_;
}
Json DemoSource::load(const fs::path &file) {
  auto data = Json::parse(editor::read_file(file));
  if (data.value("schema", "") != "openbmh-ui-demo@1" ||
      data.value("world", "") != catalog_->manifest().value("id", ""))
    throw editor::Error("wrong_demo_world");
  auto validation = DemoSource(catalog_);
  const auto result = validation.apply({0, "managers", data.at("managers")});
  if (!result.ok)
    throw editor::Error(result.error);
  const auto club = data.at("managers").at(0).at("club").get<std::string>();
  if (!data.contains("training"))
    data["training"] = demo_training(*catalog_, club);
  if (!data.contains("tactics"))
    data["tactics"] = demo_tactics();
  if (!valid_demo_training(data.at("training"), *catalog_, club) ||
      !valid_demo_tactics(data.at("tactics")))
    throw editor::Error("invalid_demo_sports");
  if (!data.at("ticket").is_number_integer() || data.at("ticket") < 0 ||
      data.at("ticket") > 25)
    throw editor::Error("invalid_demo_ticket");
  for (const auto *key : {"cash", "deposit", "debt", "fans"})
    if (!data.at(key).is_number_integer())
      throw editor::Error("invalid_demo_number");
  if (!valid_date(data.at("date")) || !preferences(data.at("preferences")) ||
      !data.at("edits").is_object() || !data.at("opening").is_string() ||
      !camp_id(data.at("camp").at("selected")))
    throw editor::Error("invalid_demo_state");
  const auto &focus = data.at("camp").at("focus");
  if (!focus.is_array() || focus.size() != 3)
    throw editor::Error("invalid_focus");
  for (const auto &v : focus)
    if (!v.is_number_integer() || v < 0 || v > 15)
      throw editor::Error("invalid_focus");
  auto next = data;
  const auto desktop = next.value("desktop", Json::object());
  if (!valid_desktop(desktop))
    throw editor::Error("invalid_demo_desktop");
  next.erase("desktop");
  state_ = std::move(next);
  saved_ = state_;
  ++revision_;
  return desktop;
}
} // namespace bmh::modern_ui
