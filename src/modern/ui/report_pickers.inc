// SPDX-License-Identifier: GPL-3.0-only
namespace {
bool report_picker_menu(const std::string &menu) {
  return menu == "tables" || menu == "strengths" || menu == "league" ||
         menu == "other" || menu == "calendar" || menu == "friendlies" || menu == "almanac";
}
bool report_picker_kind(const std::string &kind) {
  return kind == "country" || kind == "league" || kind == "filter" ||
         kind == "competition" || kind == "year";
}
} // namespace

std::vector<std::pair<std::string, std::string>>
App::report_picker_items(const std::string &kind) const {
  std::vector<std::pair<std::string, std::string>> items;
  if (!world_ || !report_picker_kind(kind))
    return items;
  if (kind == "year") {
    if (!source_) return items;
    const auto index = source_->read_almanac({{"kind", "index"}});
    if (!index.value("ok", false)) return items;
    for (const auto &year : index.at("years"))
      items.emplace_back(std::to_string(year.get<int>()), std::to_string(year.get<int>()));
    std::reverse(items.begin(), items.end());
    return items;
  }
  if (kind == "filter" || kind == "competition") {
    const std::vector<std::string> captions =
        kind == "filter"
            ? std::vector<std::string>{"Alle Spiele", "Hinrunde", "Rückrunde",
                                       "Heim", "Auswärts"}
            : std::vector<std::string>{"Meisterschaft", "Champ. League", "EM",
                                       "DFB-Pokal", "Pokalsieger", "Hallenmasters",
                                       "UEFA-Cup", "WM", "Super-Cup"};
    for (size_t i = 0; i < captions.size(); ++i)
      items.emplace_back(std::to_string(i), captions[i]);
    return items;
  }
  if (kind == "country") {
    std::set<std::string> seen;
    for (const auto *league : world_->leagues()) {
      const auto nation = league->value("nation", "");
      if (!nation.empty() && seen.insert(nation).second)
        items.emplace_back(nation, world_->nation_name(nation));
    }
    const std::array<std::string, 5> gold_order = {
        "Deutschland", "Spanien", "Italien", "Frankreich", "England"};
    const auto rank = [&](const std::string &caption) {
      return std::find(gold_order.begin(), gold_order.end(), caption) -
             gold_order.begin();
    };
    std::stable_sort(items.begin(), items.end(), [&](const auto &a, const auto &b) {
      const auto first = rank(a.second), second = rank(b.second);
      return first != second ? first < second : a.second < b.second;
    });
    return items;
  }
  const auto *league = world_->find("competitions", report_picker_value("league"));
  if (league)
    for (const auto *candidate : world_->leagues(league->value("nation", "")))
      items.emplace_back(candidate->value("id", ""), world_->name(*candidate));
  return items;
}

std::string App::report_picker_value(const std::string &kind) const {
  if (!world_ || !report_picker_kind(kind))
    return {};
  if (kind == "year") return std::to_string(almanac_year_);
  const auto reports = snap().state.at("reports");
  if (kind == "filter" || kind == "competition")
    return std::to_string(reports.at(kind).get<int>());
  const auto league = menu_ == "friendlies" ? snap().state.at("calendar").at("friendly_league").get<std::string>()
                    : menu_ == "calendar" ? snap().state.at("calendar").at("league").get<std::string>()
                    : reports.at("league").get<std::string>();
  if (kind == "league")
    return league;
  const auto *row = world_->find("competitions", league);
  return row ? row->value("nation", "") : "";
}

std::string App::report_picker(const std::string &kind, double left,
                                double width) {
  if (!report_picker_menu(menu_) || !report_picker_kind(kind))
    return {};
  const auto items = report_picker_items(kind);
  const auto selected = report_picker_value(kind);
  std::string caption = "—";
  for (const auto &[value, label] : items)
    if (value == selected)
      caption = label;
  if (kind == "year") caption = "Jahr " + std::to_string(almanac_year_);
  const bool open = report_picker_ == kind;
  const std::string menu_label = kind == "country" ? "Land"
                                : kind == "league" ? "Liga"
                                : kind == "filter" ? "Spiele" : kind == "year" ? "Kalenderjahr" : "Wettbewerb";
  std::string out =
      "<div class=\"report-picker-wrapper\" data-report-picker=\"" + kind +
      "\" style=\"left:" + scalar(left) + "dp;width:" + scalar(width) + "dp;\">" +
      btn("report-picker", "<span class=\"report-picker-label\">" + esc(caption) +
                               "</span><span class=\"report-picker-arrow\" aria-hidden=\"true\">"
                               "<span></span></span>",
          "report-picker-trigger",
          "id=\"report-picker-" + kind + "\" data-kind=\"" + kind +
              "\" aria-haspopup=\"menu\" aria-expanded=\"" +
              (open ? "true" : "false") +
              "\" aria-controls=\"report-picker-options-" + kind + "\" " +
              (items.empty() ? "disabled" : "")) +
      "<div class=\"report-picker-popup\" id=\"report-picker-options-" + kind +
      "\" role=\"menu\" aria-label=\"" + menu_label + "\" style=\"display:" +
      (open ? "block" : "none") + ";\">";
  for (size_t i = 0; i < items.size(); ++i) {
    const bool checked = items[i].first == selected;
    const auto marker =
        "<span class=\"report-picker-check-slot\" aria-hidden=\"true\">" +
        std::string(checked ? "<span class=\"report-picker-check\"></span>" : "") +
        "</span>";
    const auto option_label = "<span class=\"report-picker-option-label\">" + esc(items[i].second) + "</span>";
    out += btn("report-choice", kind == "year" ? option_label + marker : marker + option_label,
               "report-picker-option",
               "id=\"report-option-" + kind + "-" + std::to_string(i) +
                   "\" data-kind=\"" + kind + "\" data-value=\"" +
                   esc(items[i].first) + "\" data-index=\"" + std::to_string(i) +
                   "\" role=\"menuitemradio\" aria-checked=\"" +
                   (checked ? "true" : "false") + "\" style=\"tab-index:none;\"");
  }
  return out + "</div></div>";
}

void App::close_report_picker(bool focus) {
  if (report_picker_.empty())
    return;
  const auto kind = report_picker_;
  report_picker_.clear();
  if (auto *panel = element("report-picker-options-" + kind))
    panel->SetProperty("display", "none");
  auto *trigger = element("report-picker-" + kind);
  if (trigger)
    trigger->SetAttribute("aria-expanded", "false");
  // Commit visibility before the parent Tab trap examines ancestor visibility.
  // No node is replaced, so an outside mouse event retains its original target.
  context_->Update();
  if (focus && trigger)
    trigger->Focus(keyboard_navigation_);
  if (kind == "year") {
    if (focus) focus_after_render_ = "report-picker-year";
    refresh();
  }
  dirty_ = true;
}

bool App::report_picker_action(const std::string &action, Rml::Element *target) {
  if (action != "report-picker" && action != "report-choice")
    return false;
  if (!report_picker_menu(menu_) || !target)
    return true;
  const auto kind = target->GetAttribute<Rml::String>("data-kind", "");
  if (!report_picker_kind(kind))
    return true;
  const auto items = report_picker_items(kind);
  if (items.empty())
    return true;
  if (action == "report-picker") {
    if (report_picker_ == kind) {
      close_report_picker(true);
      return true;
    }
    close_report_picker(false);
    report_picker_ = kind;
    const auto selected = report_picker_value(kind);
    const auto found = std::find_if(items.begin(), items.end(), [&](const auto &item) {
      return item.first == selected;
    });
    const auto index = found == items.end() ? 0 : found - items.begin();
    focus_after_render_ = "report-option-" + kind + "-" + std::to_string(index);
    refresh();
    return true;
  }
  if (report_picker_ != kind)
    return true;
  const auto value = target->GetAttribute<Rml::String>("data-value", "");
  if (std::none_of(items.begin(), items.end(), [&](const auto &item) {
        return item.first == value;
      }))
    return true;
  const Json selection = kind == "filter" || kind == "competition"
                             ? Json(std::stoi(value)) : Json(value);
  close_report_picker(false);
  // Commit even an unchanged country: the Gold resets its first league.
  if (kind == "year" && menu_ == "almanac") {
    almanac_year_ = std::stoi(value);
    almanac_day_.clear();
    almanac_match_.clear();
    almanac_tab_ = "lineups";
  } else if (menu_ == "friendlies")
    apply("calendar", {{"kind", "friendly-" + kind}, {"value", selection}});
  else if (menu_ == "calendar") {
    const auto leagues = kind == "country" ? world_->leagues(value) : std::vector<const Json *>{};
    const auto league = kind == "country" && !leagues.empty() ? leagues.front()->value("id", "") : value;
    apply("calendar", {{"kind", "league"}, {"value", league}});
  } else
    apply("reports", {{"kind", kind}, {"value", selection}});
  focus_after_render_ = "report-picker-" + kind;
  refresh();
  return true;
}

bool App::report_picker_event(Rml::Event &event) {
  if (!report_picker_menu(menu_))
    return false;
  const auto type = event.GetType();
  const auto ancestor = [&](const std::string &attribute) -> Rml::Element * {
    for (auto *node = event.GetTargetElement(); node && node != doc_;
         node = node->GetParentNode())
      if (node->HasAttribute(attribute))
        return node;
    return nullptr;
  };
  if (!report_picker_.empty()) {
    // Defer the right-button dismissal to mouseup so the same gesture cannot
    // also fall through to the menu's right-button close handler.
    if ((type == "mousedown" || type == "mouseup") &&
        event.GetParameter<int>("button", 0) == 1) {
      if (type == "mouseup")
        close_report_picker(true);
      event.StopImmediatePropagation();
      return true;
    }
    if (type == "contextmenu") {
      close_report_picker(true);
      event.StopImmediatePropagation();
      return true;
    }
    if (type == "mousedown") {
      if (report_picker_ == "year") return false; // Dedicated backdrop dismisses this toolbar popup.
      auto *wrapper = ancestor("data-report-picker");
      if (!wrapper || wrapper->GetAttribute<Rml::String>("data-report-picker", "") !=
                          report_picker_)
        close_report_picker(false);
      return false;
    }
  }
  if (type != "keydown")
    return false;
  const auto key = Rml::Input::KeyIdentifier(
      event.GetParameter<int>("key_identifier", 0));
  if (!report_picker_.empty()) {
    if (key == Rml::Input::KI_ESCAPE) {
      keyboard_navigation_ = true;
      close_report_picker(true);
      event.StopImmediatePropagation();
      return true;
    }
    if (key == Rml::Input::KI_TAB && report_picker_ != "year") {
      keyboard_navigation_ = true;
      close_report_picker(true);
      return false;
    }
    if (key == Rml::Input::KI_UP || key == Rml::Input::KI_DOWN ||
        key == Rml::Input::KI_HOME || key == Rml::Input::KI_END ||
        (key == Rml::Input::KI_TAB && report_picker_ == "year")) {
      const auto items = report_picker_items(report_picker_);
      if (items.empty())
        return false;
      int current = -1;
      for (auto *node = context_->GetFocusElement(); node && node != doc_;
           node = node->GetParentNode())
        if (node->HasAttribute("data-index") &&
            node->GetAttribute<Rml::String>("data-kind", "") == report_picker_) {
          current = node->GetAttribute<int>("data-index", -1);
          break;
        }
      const int count = int(items.size());
      const int next = key == Rml::Input::KI_HOME ? 0
                       : key == Rml::Input::KI_END ? count - 1
                       : (current + ((key == Rml::Input::KI_UP || (key == Rml::Input::KI_TAB && event.GetParameter<int>("shift_key", 0))) ? -1 : 1) + count) % count;
      if (auto *node = element("report-option-" + report_picker_ + "-" +
                               std::to_string(next))) {
        keyboard_navigation_ = true;
        node->Focus(true);
        node->ScrollIntoView({Rml::ScrollAlignment::Nearest});
        dirty_ = true;
      }
      event.StopImmediatePropagation();
      return true;
    }
    // Enter/Space use the shared native button click handler, without a draft mutation.
    return false;
  }
  auto *trigger = ancestor("data-kind");
  if (!trigger || trigger->GetAttribute<Rml::String>("data-action", "") !=
                      "report-picker" ||
      (key != Rml::Input::KI_UP && key != Rml::Input::KI_DOWN))
    return false;
  const auto kind = trigger->GetAttribute<Rml::String>("data-kind", "");
  keyboard_navigation_ = true;
  report_picker_action("report-picker", trigger);
  if (key == Rml::Input::KI_UP && report_picker_ == kind) {
    const auto items = report_picker_items(kind);
    if (!items.empty())
      focus_after_render_ = "report-option-" + kind + "-" +
                            std::to_string(items.size() - 1);
  }
  event.StopImmediatePropagation();
  return true;
}
