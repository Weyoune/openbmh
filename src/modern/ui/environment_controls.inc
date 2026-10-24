// SPDX-License-Identifier: GPL-3.0-only
bool App::environment_action(const std::string &action, Rml::Element *target) {
  if (action != "environment-level") return false;
  if (menu_ != "environment" || !source_ || !target) return true;
  const auto index = target->GetAttribute<Rml::String>("data-index", "");
  const auto value = target->GetAttribute<Rml::String>("data-id", "");
  if (index.size() != 1 || index[0] < '0' || index[0] > '5' ||
      value.size() != 1 || value[0] < '0' || value[0] > '6') return true;
  const auto snapshot = snap();
  const auto result = source_->apply({snapshot.revision, "environment",
      {{"kind", "level"}, {"index", index[0] - '0'}, {"value", value[0] - '0'}}});
  if (!result.ok) {
    message("Das Umfeldbudget konnte nicht übernommen werden. Bitte die Ansicht mit dem aktuellen Stand erneut öffnen.");
    return true;
  }
  focus_after_render_ = target->GetId();
  refresh();
  return true;
}
