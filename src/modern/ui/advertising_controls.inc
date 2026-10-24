// SPDX-License-Identifier: GPL-3.0-only
bool App::advertising_action(const std::string &action, Rml::Element *target) {
  if (!action.starts_with("ad-")) return false;
  if (menu_ != "advertising" || !source_ || !world_) return true;
  if (action == "ad-contract" && target) {
    const int slot = target->GetAttribute<int>("data-id", -1);
    if (slot < 0 || slot > 15) return true;
    advertising_selected_ = slot;
    advertising_offer_ = 0;
    advertising_revision_ = snap().revision;
    advertising_world_ = world_;
    focus_after_render_ = "ad-contract-" + std::to_string(slot);
    refresh();
    return true;
  }
  if (action == "ad-cancel") {
    const int slot = advertising_selected_;
    advertising_selected_ = -1;
    advertising_offer_ = 0;
    advertising_world_.reset();
    if (slot >= 0 && slot <= 15) focus_after_render_ = "ad-contract-" + std::to_string(slot);
    refresh();
    return true;
  }
  if (action == "ad-offer" && target) {
    const int step = target->GetAttribute<int>("data-step", 0);
    if ((step != -1 && step != 1) || advertising_selected_ < 0 || advertising_selected_ > 15 ||
        advertising_world_ != world_ || snap().revision != advertising_revision_) return true;
    const auto q = demo_advertising_quote(snap().state, advertising_selected_, advertising_offer_, *world_);
    if (!q.value("ok", false) || q.at("active") == true) return true;
    advertising_offer_ = (advertising_offer_ + step + 3) % 3;
    focus_after_render_ = target->GetId();
    refresh();
    return true;
  }
  if (action == "ad-accept") {
    if (advertising_selected_ < 0 || advertising_selected_ > 15 || advertising_world_ != world_) return true;
    const auto result = source_->apply({advertising_revision_, "advertising",
        {{"kind", "accept"}, {"slot", advertising_selected_}, {"offer", advertising_offer_}}});
    if (!result.ok) {
      message("Das Angebot konnte nicht übernommen werden. Bitte den aktuellen Stand erneut auswählen. (" + result.error + ")");
      return true;
    }
    advertising_revision_ = result.revision;
    focus_after_render_ = "ad-cancel";
    refresh();
    return true;
  }
  if (action == "ad-spending" && target) {
    const int index = target->GetAttribute<int>("data-id", -1);
    const auto snapshot = snap();
    const auto view = demo_advertising_view(snapshot.state, *world_);
    if (!view.value("ok", false) || index < 0 || index > 2) return true;
    const auto &row = view.at("spending")[size_t(index)];
    const auto world = world_;
    const auto *source = source_.get();
    const auto revision = snapshot.revision;
    AmountRequest request;
    request.title = row.at("name").get<std::string>();
    request.unit = money_unit();
    request.return_focus = target->GetId();
    request.value = row.at("amount").get<int64_t>() / finance_currency_factor(*world_);
    request.min = 0;
    request.max = 99999999;
    request.step = 1;
    request.suggestion_step = row.at("suggestion_step");
    request.review = true;
    request.accept = [this, world, source, revision, index](int64_t amount) {
      if (menu_ != "advertising" || world_ != world || source_.get() != source) return false;
      const auto result = source_->apply({revision, "advertising", {{"kind", "spending"}, {"index", index}, {"value", amount}}});
      if (!result.ok) {
        message("Das Budget konnte nicht übernommen werden. Bitte den aktuellen Stand prüfen. (" + result.error + ")");
        return false;
      }
      if (advertising_selected_ >= 0 && advertising_world_ == world && advertising_revision_ == revision)
        advertising_revision_ = result.revision;
      refresh();
      return true;
    };
    open_amount(std::move(request));
    return true;
  }
  return true;
}
bool App::advertising_event(Rml::Event &event) {
  if (menu_ != "advertising" || advertising_selected_ < 0 || amount_ || !popup_.empty()) return false;
  const auto type = event.GetType();
  if ((type == "keydown" && event.GetParameter<int>("key_identifier", 0) == Rml::Input::KI_ESCAPE) ||
      (type == "mouseup" && event.GetParameter<int>("button", 0) == 1)) {
    advertising_action("ad-cancel", nullptr);
    event.StopImmediatePropagation();
    return true;
  }
  return false;
}
