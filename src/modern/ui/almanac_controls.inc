// SPDX-License-Identifier: GPL-3.0-only
bool App::almanac_action(const std::string &action, Rml::Element *target) {
  if (!action.starts_with("alm-")) return false;
  if (menu_ != "almanac" || !source_) return true;
  const auto id = target ? target->GetAttribute<Rml::String>("data-id", "") : "";
  if (action == "alm-year-cancel" || (action == "alm-back" && report_picker_ == "year")) {
    close_report_picker(true);
    return true;
  }
  if (action == "alm-back") {
    if (!almanac_match_.empty()) {
      focus_after_render_ = "alm-match-" + almanac_match_;
      almanac_match_.clear();
    } else if (!almanac_day_.empty()) {
      focus_after_render_ = "alm-day-" + almanac_day_;
      almanac_day_.clear();
    } else {
      menu_.clear();
      focus_after_render_ = "object-app-almanac";
    }
  } else if (action == "alm-day") {
    const auto result = source_->read_almanac({{"kind", "day"}, {"date", id}});
    if (!result.value("ok", false) || result.at("matches").empty()) return true;
    almanac_day_ = id;
    almanac_match_.clear();
    focus_after_render_ = "alm-heading";
  } else if (action == "alm-match") {
    const auto result = source_->read_almanac({{"kind", "match"}, {"id", id}});
    if (!result.value("ok", false) || result.at("match").at("date") != almanac_day_) return true;
    almanac_match_ = id;
    almanac_tab_ = "lineups";
    focus_after_render_ = "alm-heading";
  } else if (action == "alm-tab") {
    if (almanac_match_.empty() || (id != "lineups" && id != "events")) return true;
    almanac_tab_ = id;
    focus_after_render_ = "alm-tab-" + id;
  } else return false;
  close_report_picker(false);
  refresh();
  return true;
}
