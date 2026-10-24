// SPDX-License-Identifier: GPL-3.0-only
std::optional<int64_t> App::amount_value() const {
  if (!amount_ || amount_->raw.empty() || amount_->raw.size() > 8 ||
      std::any_of(amount_->raw.begin(), amount_->raw.end(), [](char c) { return c < '0' || c > '9'; }))
    return {};
  return std::stoll(amount_->raw);
}
std::string App::amount_error() const {
  if (!amount_) return {};
  if (!amount_->error.empty()) return amount_->error;
  const auto &r = amount_->request;
  if (r.max < r.min) return "Derzeit ist kein Betrag oder Bestand verfügbar.";
  const auto value = amount_value();
  if (!value) return "Bitte eine ganze Zahl mit höchstens acht Ziffern eingeben.";
  if (*value < r.min || *value > r.max)
    return "Erlaubt: " + number(r.min) + " bis " + number(r.max) + (r.unit.empty() ? "" : " " + r.unit) + ".";
  if ((*value - r.min) % r.step)
    return "Bitte in Schritten von " + number(r.step) + " ab " + number(r.min) + " eingeben.";
  return {};
}
void App::open_amount(AmountRequest request) {
  if (!source_) return;
  close_report_picker(false);
  request.step = std::max<int64_t>(1, request.step);
  request.suggestion_step = std::max<int64_t>(1, request.suggestion_step);
  if (request.return_focus.empty() && context_->GetFocusElement())
    request.return_focus = context_->GetFocusElement()->GetId();
  amount_ = AmountDraft{std::move(request), {}, {}, menu_, false, snap().revision, world_};
  amount_->raw = std::to_string(amount_->request.value);
  amount_select_all_ = true;
  amount_caret_end_ = false;
  focus_after_render_ = "calculator-value";
  refresh();
}
void App::close_amount(bool restore) {
  if (!amount_) return;
  const auto id = amount_->request.return_focus, menu = amount_->menu;
  amount_.reset();
  amount_select_all_ = false;
  amount_caret_end_ = false;
  if (restore && menu == menu_) focus_after_render_ = id;
  refresh();
}
void App::update_amount(bool focus, bool select_all) {
  if (!amount_ || amount_->review) return;
  const auto error = amount_error();
  if (auto *feedback = element("calculator-feedback")) {
    feedback->SetInnerRML("<span>" + esc(error.empty()
        ? amount_->request.review ? "Nach „Übernehmen“ wird der Betrag noch einmal bestätigt." : "Erst „Übernehmen“ bestätigt den Wert."
        : error) + "</span>");
    feedback->SetClass("invalid", !error.empty());
  }
  if (auto *confirm = element("calculator-confirm")) {
    if (error.empty()) confirm->RemoveAttribute("disabled");
    else confirm->SetAttribute("disabled", true);
  }
  if (auto *input = dynamic_cast<Rml::ElementFormControlInput *>(element("calculator-value"))) {
    if (input->GetValue() != amount_->raw) input->SetValue(amount_->raw);
    if (focus) {
      input->Focus(keyboard_navigation_);
      if (select_all) input->Select();
      else input->SetSelectionRange(int(amount_->raw.size()), int(amount_->raw.size()));
    }
  }
  Rml::ElementList proposals;
  if (auto *layer = element("number-calculator")) layer->QuerySelectorAll(proposals, ".amount-proposal");
  const auto current = amount_value();
  for (auto *button : proposals) {
    const auto value = button->GetAttribute<int64_t>("data-value", -1);
    button->SetClass("selected", current && *current == value);
    button->SetAttribute("aria-pressed", current && *current == value ? "true" : "false");
  }
  dirty_ = true;
}
std::string App::render_amount() {
  if (!amount_) return {};
  const auto &r = amount_->request;
  const auto button = [&](const std::string &action, const std::string &label,
                          const std::string &css, const std::string &attrs = "") {
    return btn(action, esc(label), "amount-button " + css, attrs);
  };
  std::string out = "<div id=\"number-calculator\" class=\"amount-layer\">";
  if (amount_->review) {
    const auto value = amount_value().value_or(0);
    const auto text = r.review_text ? r.review_text(value) : "Diesen Betrag übernehmen?";
    return out + "<section class=\"amount-box amount-review\" role=\"dialog\" aria-modal=\"true\" aria-labelledby=\"calculator-review-title\">"
        "<h2 id=\"calculator-review-title\">" + esc(r.title + " bestätigen") + "</h2><p>" + esc(text) +
        "</p><strong>" + esc(number(value) + (r.unit.empty() ? "" : " " + r.unit)) + "</strong>" +
        (amount_->error.empty() ? "" : "<p class=\"amount-review-error\">" + esc(amount_->error) + "</p>") +
        "<footer class=\"amount-review-actions\">" +
        button("calc-cancel", "Abbrechen", "", "id=\"calculator-cancel\"") +
        button("calc-back", "Ändern", "", "id=\"calculator-back\"") +
        button("calc-confirm", "Bestätigen", "amount-accept", "id=\"calculator-confirm\" " + std::string(amount_error().empty() ? "" : "disabled")) +
        "</footer></section></div>";
  }
  out += "<section class=\"amount-box\" role=\"dialog\" aria-modal=\"true\" aria-labelledby=\"calculator-title\">"
      "<header class=\"amount-header\">" + button("calc-cancel", "×", "amount-close", "id=\"calculator-close\" aria-label=\"Zahleneingabe schließen\"") +
      "<h2 id=\"calculator-title\">" + esc(r.title) + "</h2></header><label class=\"amount-label\">" +
      esc(r.unit == "Stück" ? "Stückzahl" : r.unit.empty() ? "Wert" : r.unit == "€" ? "Betrag in Euro" : "Betrag in " + r.unit) +
      "</label><input type=\"text\" id=\"calculator-value\" maxlength=\"8\" value=\"" + esc(amount_->raw) +
      "\" aria-label=\"" + esc(r.title) + "\"/><div class=\"amount-context\"><span class=\"amount-limit\"><span>" +
      esc(number(r.min) + " – " + number(r.max) + (r.unit.empty() ? "" : " " + r.unit)) + "</span></span>" +
      button("calc-clear", "C", "amount-clear", "id=\"calculator-clear\"") + button("calc-max", "MAX", "amount-max", "id=\"calculator-max\"") + "</div>";
  const int64_t first = std::max<int64_t>(1, (r.min + r.suggestion_step - 1) / r.suggestion_step);
  const auto value = amount_value();
  for (int i = 0; i < 6; ++i) {
    const int64_t proposal = (first + i) * r.suggestion_step;
    const bool enabled = proposal >= r.min && proposal <= r.max && (proposal - r.min) % r.step == 0;
    out += button("calc-preset", number(proposal), "amount-proposal" + std::string(value && *value == proposal ? " selected" : ""),
        "id=\"calculator-preset-" + std::to_string(i) + "\" data-value=\"" + std::to_string(proposal) + "\" style=\"" +
        rect(26 + (i % 2) * 388, 334 + (i / 2) * 74, 376, 64) + "\" " + (enabled ? "" : "disabled"));
  }
  const std::array<const char *, 12> keys = {"7","8","9","4","5","6","1","2","3","0","00","←"};
  for (int i = 0; i < 12; ++i)
    out += button("calc-digit", keys[size_t(i)], "amount-key", "id=\"calculator-key-" + std::to_string(i) + "\" data-id=\"" +
        keys[size_t(i)] + "\" style=\"" + rect(26 + (i % 3) * 258, 566 + (i / 3) * 98, 248, 88) + "\"");
  const auto error = amount_error();
  out += "<p id=\"calculator-feedback\" class=\"amount-feedback " + std::string(error.empty() ? "" : "invalid") + "\"><span>" +
      esc(error.empty() ? r.review ? "Nach „Übernehmen“ wird der Betrag noch einmal bestätigt." : "Erst „Übernehmen“ bestätigt den Wert." : error) +
      "</span></p><div class=\"amount-actions\">" + button("calc-cancel", "Abbrechen", "", "id=\"calculator-cancel\"") +
      button("calc-confirm", "Übernehmen", "amount-accept", "id=\"calculator-confirm\" " + std::string(error.empty() ? "" : "disabled"));
  return out + "</div></section></div>";
}
bool App::amount_action(const std::string &action, Rml::Element *target) {
  if (!action.starts_with("calc-")) return false;
  if (!amount_) return true;
  if (action == "calc-cancel") { close_amount(); return true; }
  if (action == "calc-back") {
    amount_->review = false;
    focus_after_render_ = "calculator-value";
    amount_select_all_ = false;
    amount_caret_end_ = true;
    refresh();
    return true;
  }
  if (action == "calc-confirm") {
    if (!amount_error().empty()) return true;
    if (world_ != amount_->world || !source_ || snap().revision != amount_->revision) {
      amount_->error = "Die Daten haben sich geändert. Bitte die Eingabe schließen und neu öffnen.";
      if (amount_->review) refresh(); else update_amount();
      return true;
    }
    if (amount_->request.review && !amount_->review) {
      amount_->review = true;
      focus_after_render_ = "calculator-confirm";
      refresh();
      return true;
    }
    const auto value = *amount_value();
    auto accept = amount_->request.accept;
    close_amount();
    if (accept && !accept(value) && popup_.empty() && contracts_dialog_ != "notice")
      message("Die Eingabe konnte nicht übernommen werden. Bitte den Dialog mit dem aktuellen Stand erneut öffnen.");
    return true;
  }
  if (amount_->review) return true;
  if (action == "calc-clear") amount_->raw = "0";
  else if (action == "calc-max") amount_->raw = std::to_string(amount_->request.max);
  else if (action == "calc-preset" && target) amount_->raw = target->GetAttribute<Rml::String>("data-value", "0");
  else if (action == "calc-digit" && target) {
    const auto digit = target->GetAttribute<Rml::String>("data-id", "");
    int start = int(amount_->raw.size()), end = start;
    if (auto *input = dynamic_cast<Rml::ElementFormControlInput *>(element("calculator-value"))) input->GetSelection(&start, &end, nullptr);
    start = std::clamp(start, 0, int(amount_->raw.size()));
    end = std::clamp(end, start, int(amount_->raw.size()));
    if (digit == "←") {
      if (start == end && start) --start;
      amount_->raw.erase(size_t(start), size_t(end - start));
      if (amount_->raw.empty()) amount_->raw = "0";
    } else if ((digit.size() == 1 || digit == "00") && std::all_of(digit.begin(), digit.end(), [](char c) { return c >= '0' && c <= '9'; })) {
      auto next = amount_->raw.substr(0, size_t(start)) + digit + amount_->raw.substr(size_t(end));
      while (next.size() > 1 && next.front() == '0') next.erase(0, 1);
      if (next.size() <= 8) amount_->raw = std::move(next);
    }
  } else return false;
  update_amount(true);
  return true;
}
bool App::amount_event(Rml::Event &event) {
  if (!amount_ || !popup_.empty()) return false;
  const auto type = event.GetType();
  if (type == "contextmenu" || ((type == "mousedown" || type == "mouseup") && event.GetParameter<int>("button", 0) == 1)) {
    event.StopImmediatePropagation(); return true;
  }
  bool inside = false;
  for (auto *n = event.GetTargetElement(); n && n != doc_; n = n->GetParentNode()) if (n->GetId() == "number-calculator") inside = true;
  const bool keyboard = type == "keydown";
  const auto key = keyboard ? Rml::Input::KeyIdentifier(event.GetParameter<int>("key_identifier", 0)) : Rml::Input::KI_UNKNOWN;
  if (keyboard && key == Rml::Input::KI_ESCAPE) { close_amount(); event.StopImmediatePropagation(); return true; }
  if (keyboard && key == Rml::Input::KI_TAB) {
    Rml::ElementList fields;
    if (auto *layer = element("number-calculator")) layer->QuerySelectorAll(fields, "input,button");
    std::erase_if(fields, [](auto *n) { return !n->IsVisible(true) || n->HasAttribute("disabled"); });
    if (!fields.empty()) {
      const auto at = std::find(fields.begin(), fields.end(), context_->GetFocusElement());
      const int old = at == fields.end() ? -1 : int(at - fields.begin()), count = int(fields.size());
      const bool back = event.GetParameter<int>("shift_key", 0) != 0;
      const int next = old < 0 ? back ? count - 1 : 0 : (old + (back ? -1 : 1) + count) % count;
      fields[size_t(next)]->Focus(true);
    }
    event.StopImmediatePropagation(); return true;
  }
  if (!inside) { if (type != "blur") event.StopImmediatePropagation(); return true; }
  auto *target = event.GetTargetElement();
  if ((type == "input" || type == "change") && target->GetId() == "calculator-value") {
    if (!amount_->review) {
      amount_->raw = dynamic_cast<Rml::ElementFormControlInput *>(target)->GetValue();
      update_amount();
    }
    event.StopImmediatePropagation(); return true;
  }
  if (keyboard && !amount_->review && target->GetId() != "calculator-value" &&
      !event.GetParameter<int>("ctrl_key", 0) && !event.GetParameter<int>("alt_key", 0) && !event.GetParameter<int>("meta_key", 0)) {
    const int digit = key >= Rml::Input::KI_0 && key <= Rml::Input::KI_9 ? key - Rml::Input::KI_0
        : key >= Rml::Input::KI_NUMPAD0 && key <= Rml::Input::KI_NUMPAD9 ? key - Rml::Input::KI_NUMPAD0 : -1;
    if (digit >= 0 || key == Rml::Input::KI_BACK) {
      const int keypad = digit < 0 ? 11 : digit == 0 ? 9 : (2 - (digit - 1) / 3) * 3 + (digit - 1) % 3;
      if (auto *button = element("calculator-key-" + std::to_string(keypad))) amount_action("calc-digit", button);
      event.StopImmediatePropagation(); return true;
    }
  }
  if (keyboard && target->GetId() == "calculator-value" && (key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER)) {
    amount_action("calc-confirm", target); event.StopImmediatePropagation(); return true;
  }
  if (type == "click" || (keyboard && (key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER || key == Rml::Input::KI_SPACE))) {
    for (auto *node = target; node && node != doc_; node = node->GetParentNode())
      if (node->HasAttribute("data-action")) {
        if (!node->HasAttribute("disabled")) amount_action(node->GetAttribute<Rml::String>("data-action", ""), node);
        event.StopImmediatePropagation(); return true;
      }
  }
  return true; // Native text input retains its own default action; underlying menus do not see it.
}

bool App::amount_trigger_event(Rml::Event &event) {
  if (amount_ || event.GetType() != "keydown") return false;
  Rml::Element *trigger = nullptr;
  for (auto *n = event.GetTargetElement(); n && n != doc_; n = n->GetParentNode())
    if (n->IsClassSet("amount-trigger")) { trigger = n; break; }
  if (!trigger || trigger->HasAttribute("disabled")) return false;
  const auto key = Rml::Input::KeyIdentifier(event.GetParameter<int>("key_identifier", 0));
  const bool modified = event.GetParameter<int>("ctrl_key", 0) || event.GetParameter<int>("meta_key", 0),
             paste = modified && key == Rml::Input::KI_V;
  const int digit = key >= Rml::Input::KI_0 && key <= Rml::Input::KI_9 ? key - Rml::Input::KI_0
      : key >= Rml::Input::KI_NUMPAD0 && key <= Rml::Input::KI_NUMPAD9 ? key - Rml::Input::KI_NUMPAD0 : -1;
  if (event.GetParameter<int>("alt_key", 0) || (modified && !paste) ||
      (!paste && digit < 0 && key != Rml::Input::KI_RETURN && key != Rml::Input::KI_NUMPADENTER &&
       key != Rml::Input::KI_SPACE && key != Rml::Input::KI_F4 && key != Rml::Input::KI_UP &&
       key != Rml::Input::KI_DOWN && key != Rml::Input::KI_BACK && key != Rml::Input::KI_DELETE)) return false;
  dispatch(trigger->GetAttribute<Rml::String>("data-action", ""), trigger);
  if (amount_ && (digit >= 0 || paste)) {
    if (paste) {
      char *clipboard = SDL_GetClipboardText();
      std::string raw = clipboard ? clipboard : "";
      SDL_free(clipboard);
      const auto first = raw.find_first_not_of(" \r\n\t"), last = raw.find_last_not_of(" \r\n\t");
      raw = first == std::string::npos ? "" : raw.substr(first, last - first + 1);
      if (raw.ends_with("€")) raw.resize(raw.size() - std::string("€").size());
      while (!raw.empty() && raw.back() == ' ') raw.pop_back();
      bool grouping = raw.find('.') != std::string::npos;
      size_t start = 0, group = 0;
      while (grouping && start < raw.size()) {
        const auto dot = raw.find('.', start), end = dot == std::string::npos ? raw.size() : dot;
        const auto length = end - start;
        grouping = length > 0 && (group == 0 ? length <= 3 : length == 3) &&
            std::all_of(raw.begin() + start, raw.begin() + end, [](char c) { return c >= '0' && c <= '9'; });
        start = end + 1; ++group;
      }
      if (grouping && !raw.ends_with('.')) std::erase(raw, '.');
      amount_->raw = std::move(raw);
    } else amount_->raw = std::to_string(digit);
    amount_select_all_ = false;
    amount_caret_end_ = true;
  }
  event.StopImmediatePropagation();
  return true;
}
