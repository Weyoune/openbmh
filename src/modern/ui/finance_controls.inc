// SPDX-License-Identifier: GPL-3.0-only
bool App::commit_finance(const Json &change, uint64_t revision) {
  if (!source_) return false;
  const auto result = source_->apply({revision, "finance", change});
  if (!result.ok) {
    message("Die Eingabe konnte nicht übernommen werden. Bitte den aktuellen Stand prüfen und erneut eingeben. (" + result.error + ")");
    return false;
  }
  refresh();
  return true;
}
bool App::finance_action(const std::string &action, Rml::Element *target) {
  if (menu_ != "balance") return false;
  if (action == "balance-toggle") {
    balance_future_ = !balance_future_;
    focus_after_render_ = "balance-toggle";
    refresh();
    return true;
  }
  if (action == "balance-plan" && target) {
    const int index = target->GetAttribute<int>("data-id", -1);
    const auto rows = demo_balance_rows(snap().state, *world_);
    if (!balance_future_ || !rows.value("ok", false) || index < 0 || size_t(index) >= rows.at("plan").size()) return true;
    const auto &row = rows.at("plan")[size_t(index)];
    const auto revision = snap().revision;
    const auto world = world_;
    AmountRequest request;
    request.title = row.at("name").get<std::string>() + " in " + (money_unit() == "€" ? "Euro" : money_unit());
    request.unit = money_unit();
    request.value = row.at("amount").get<int64_t>() / finance_currency_factor(*world_);
    request.min = 0;
    request.max = 99999999;
    request.step = 1000;
    request.suggestion_step = 50000;
    request.return_focus = target->GetId();
    request.accept = [this, index, revision, world](int64_t value) {
      if (world != world_ || menu_ != "balance") return false;
      return commit_finance({{"kind", "plan"}, {"index", index}, {"value", value}}, revision);
    };
    open_amount(std::move(request));
    return true;
  }
  return false;
}
bool App::finance_event(Rml::Event &event) {
  if (menu_ != "balance" || !balance_future_ || amount_) return false;
  const auto type = event.GetType();
  if ((type == "keydown" && event.GetParameter<int>("key_identifier", 0) == Rml::Input::KI_ESCAPE) ||
      (type == "mouseup" && event.GetParameter<int>("button", 0) == 1)) {
    balance_future_ = false;
    focus_after_render_ = "balance-toggle";
    refresh();
    event.StopImmediatePropagation();
    return true;
  }
  return false;
}
