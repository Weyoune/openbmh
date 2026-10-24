// SPDX-License-Identifier: GPL-3.0-only
bool App::toto_action(const std::string &action, Rml::Element *target) {
  if (menu_ != "toto" || !action.starts_with("toto-")) return false;
  if (action == "toto-dialog-close") {
    toto_dialog_.clear(); focus_after_render_ = toto_return_focus_; refresh(); return true;
  }
  if (!toto_dialog_.empty()) return true;
  if (action == "toto-column") {
    const auto col = target ? target->GetAttribute<int>("data-column", -1) : -1;
    if (col < 0 || col > 5) return true;
    toto_column_ = col; focus_after_render_ = "toto-column-" + std::to_string(col); refresh(); return true;
  }
  if (action == "toto-stake") {
    const auto revision = snap().revision; const auto world = world_;
    AmountRequest request;
    request.title = "TOTO · Gesamteinsatz"; request.unit = money_unit();
    request.suggestion_step = 1000;
    request.value = snap().state.at("toto").at("stake").get<int64_t>() / finance_currency_factor(*world_);
    request.return_focus = "toto-stake";
    request.accept = [this, revision, world](int64_t value) {
      if (!source_ || world_ != world || menu_ != "toto") return false;
      const auto result = source_->apply({revision, "toto", {{"kind", "stake"}, {"value", value}}});
      if (!result.ok) { message("Der Einsatz konnte nicht übernommen werden: " + result.error); return false; }
      refresh(); return true;
    };
    open_amount(std::move(request)); return true;
  }
  if (action == "toto-last") {
    if (snap().state.at("toto").at("coupon").is_null()) return true;
    toto_dialog_ = "receipt"; toto_return_focus_ = "toto-last";
    focus_after_render_ = "toto-dialog-close"; refresh(); return true;
  }
  Json change;
  if (action == "toto-pick" && target) {
    const auto col = target->GetAttribute<int>("data-column", -1), row = target->GetAttribute<int>("data-row", -1), pick = target->GetAttribute<int>("data-pick", -1);
    if (col < 0 || col > 5 || row < 0 || row > 10 || pick < 0 || pick > 2) return true;
    change = {{"kind", "pick"}, {"column", col}, {"row", row}, {"pick", pick}};
    if (apply("toto", change)) {
      toto_column_ = col; toto_focus_[size_t(col)] = {row, pick};
      focus_after_render_ = toto_pick_id(col, row, pick); refresh();
    }
    return true;
  }
  if (action == "toto-auto" || action == "toto-clear") change = {{"kind", action == "toto-auto" ? "auto" : "clear"}, {"column", toto_column_}};
  else if (action == "toto-submit") change = {{"kind", "submit"}};
  else return false;
  if (apply("toto", change)) {
    if (action == "toto-submit") {
      toto_dialog_ = "notice"; toto_return_focus_ = "toto-submit"; focus_after_render_ = "toto-dialog-close";
    } else {
      const auto value = snap().state.at("toto").at("columns")[size_t(toto_column_)][0].get<int>();
      toto_focus_[size_t(toto_column_)] = {0, value < 0 ? 1 : value};
      focus_after_render_ = action == "toto-clear" ? "toto-column-" + std::to_string(toto_column_) : "toto-auto";
    }
    refresh();
  }
  return true;
}
bool App::toto_event(Rml::Event &event) {
  if (menu_ != "toto" || amount_ || !popup_.empty()) return false;
  const auto type = event.GetType(); const bool keyboard = type == "keydown";
  const auto key = keyboard ? Rml::Input::KeyIdentifier(event.GetParameter<int>("key_identifier", 0)) : Rml::Input::KI_UNKNOWN;
  auto *target = event.GetTargetElement();
  if (!toto_dialog_.empty()) {
    if ((keyboard && key == Rml::Input::KI_ESCAPE) || (type == "mouseup" && event.GetParameter<int>("button", 0) == 1)) {
      toto_action("toto-dialog-close", nullptr); event.StopImmediatePropagation(); return true;
    }
    bool inside = false;
    for (auto *n = target; n && n != doc_; n = n->GetParentNode()) if (n->GetId() == "toto-layer") inside = true;
    if (keyboard && key == Rml::Input::KI_TAB) {
      if (auto *button = element("toto-dialog-close")) button->Focus(true);
      event.StopImmediatePropagation(); return true;
    }
    if (!inside && (keyboard || type == "click" || type == "dblclick" || type == "mousedown" || type == "mouseup")) {
      event.StopImmediatePropagation(); return true;
    }
    return false;
  }
  if (!keyboard) return false;
  if (key == Rml::Input::KI_TAB) {
    Rml::ElementList fields; doc_->QuerySelectorAll(fields, ".window button");
    std::erase_if(fields, [](Rml::Element *n) { return !n->IsVisible(true) || n->HasAttribute("disabled") || n->GetAttribute<Rml::String>("data-tabstop", "true") == "false"; });
    const auto found = std::find(fields.begin(), fields.end(), context_->GetFocusElement());
    if (!fields.empty()) {
      const int count = int(fields.size()), old = found == fields.end() ? -1 : int(found - fields.begin());
      const bool back = event.GetParameter<int>("shift_key", 0);
      const int next = old < 0 ? back ? count - 1 : 0 : (old + (back ? -1 : 1) + count) % count;
      fields[size_t(next)]->Focus(true);
    }
    event.StopImmediatePropagation(); return true;
  }
  if (!target->HasAttribute("data-row") ||
      (key != Rml::Input::KI_LEFT && key != Rml::Input::KI_RIGHT && key != Rml::Input::KI_UP &&
       key != Rml::Input::KI_DOWN && key != Rml::Input::KI_HOME && key != Rml::Input::KI_END)) return false;
  constexpr std::array<int, 3> picks{1, 0, 2};
  int row = target->GetAttribute<int>("data-row", 0);
  const int column = target->GetAttribute<int>("data-column", 0), pick = target->GetAttribute<int>("data-pick", 1);
  int cell = column * 3 + int(std::find(picks.begin(), picks.end(), pick) - picks.begin());
  if (key == Rml::Input::KI_UP || key == Rml::Input::KI_DOWN) row = std::clamp(row + (key == Rml::Input::KI_UP ? -1 : 1), 0, 10);
  else if (key == Rml::Input::KI_LEFT || key == Rml::Input::KI_RIGHT) cell = std::clamp(cell + (key == Rml::Input::KI_LEFT ? -1 : 1), 0, 17);
  else { cell = key == Rml::Input::KI_HOME ? 0 : 17; if (event.GetParameter<int>("ctrl_key", 0) || event.GetParameter<int>("meta_key", 0)) row = key == Rml::Input::KI_HOME ? 0 : 10; }
  const auto col = cell / 3, next = picks[size_t(cell % 3)];
  toto_focus_[size_t(col)] = {row, next};
  Rml::ElementList fields; doc_->QuerySelectorAll(fields, ".toto-pick");
  for (auto *n : fields) if (n->GetAttribute<int>("data-column", -1) == col) {
    const bool active = n->GetId() == toto_pick_id(col, row, next);
    n->SetAttribute("data-tabstop", active ? "true" : "false"); n->SetProperty("tab-index", active ? "auto" : "none");
  }
  if (auto *button = element(toto_pick_id(col, row, next))) button->Focus(true);
  dirty_ = true; event.StopImmediatePropagation(); return true;
}
