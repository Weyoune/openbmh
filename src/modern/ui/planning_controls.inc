// SPDX-License-Identifier: GPL-3.0-only
bool App::planning_action(const std::string &action, Rml::Element *target) {
  const auto id = target ? target->GetAttribute<Rml::String>("data-id", "") : "";
  if (action == "planning-close") {
    planning_dialog_.clear();
    focus_after_render_ = planning_return_focus_;
    refresh();
    return true;
  }
  if (action.starts_with("postpone-")) {
    if (menu_ != "postpone") return true;
    if (action == "postpone-undo") {
      const auto calendar = planning_calendar();
      if (!calendar.at("moves").empty() && calendar_day_occupied(calendar, *world_, manager(snap()).value("club", ""), calendar.at("moves").back().at("from"))) {
        planning_dialog_ = "undo-blocked";
        planning_notice_ = "Der ursprüngliche Termin ist inzwischen belegt.";
        planning_return_focus_ = "postpone-undo";
        focus_after_render_ = "planning-close";
        refresh();
        return true;
      }
    }
    if (apply("calendar", {{"kind", action}})) {
      focus_after_render_ = action == "postpone-cancel" ? "postpone-source"
          : "calendar-day-" + std::to_string(snap().state.at("calendar").at("selected").get<int>());
      refresh();
    }
    return true;
  }
  if (!action.starts_with("friendly-")) return false;
  if (menu_ != "friendlies") return true;
  const auto calendar = planning_calendar();
  if (action == "friendly-opponent" || action == "friendly-calendar") {
    if (action == "friendly-opponent" && !apply("calendar", {{"kind", "friendly-opponent"}, {"value", id}})) return true;
    if (snap().state.at("calendar").at("opponent") == "") return true;
    planning_return_focus_ = target ? target->GetId() : "friendly-calendar";
    planning_dialog_ = "venue";
    focus_after_render_ = "friendly-home";
  } else if (action == "friendly-venue") {
    if (!apply("calendar", {{"kind", "friendly-venue"}, {"value", id == "home"}})) return true;
    planning_dialog_ = "quote";
    focus_after_render_ = "planning-close";
  } else if (action == "friendly-quote-accept") {
    if (!apply("calendar", {{"kind", "friendly-stage"}, {"value", 1}})) return true;
    planning_dialog_.clear();
    // Keep a valid selected date; otherwise choose the first chronological free day.
    int selected = calendar.at("selected");
    const auto own = manager(snap()).value("club", "");
    const int count = int(calendar_days(calendar.at("start")).size());
    if (selected < 7 || calendar_day_occupied(calendar, *world_, own, selected)) {
      selected = 7;
      while (selected < count && calendar_day_occupied(calendar, *world_, own, selected)) ++selected;
    }
    focus_after_render_ = selected < count ? "calendar-day-" + std::to_string(selected) : "friendly-back";
  } else if (action == "friendly-back") {
    if (!apply("calendar", {{"kind", "friendly-stage"}, {"value", 0}})) return true;
    planning_dialog_.clear();
    focus_after_render_ = "friendly-opponent-" + calendar.at("opponent").get<std::string>();
  } else if (action == "friendly-book") {
    if (!apply("calendar", {{"kind", "friendly-book"}})) return true;
    const auto days = calendar_days(calendar.at("start"));
    const auto *opponent = world_->find("clubs", calendar.at("opponent"));
    planning_notice_ = calendar_date_label(days.at(calendar.at("selected").get<size_t>()).iso) + " · " +
        (opponent ? world_->name(*opponent) : "—") + " · " + (calendar.at("home").get<bool>() ? "Heim" : "Auswärts");
    planning_return_focus_ = "friendly-list";
    planning_dialog_ = "booked";
    focus_after_render_ = "planning-close";
  } else if (action == "friendly-list") {
    planning_return_focus_ = "friendly-list";
    planning_dialog_ = "list";
    focus_after_render_ = "planning-close";
  } else return false;
  close_report_picker(false);
  refresh();
  return true;
}
