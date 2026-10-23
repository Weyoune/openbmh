// SPDX-License-Identifier: GPL-3.0-only
#include "modern/ui/ticket_popover.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <sstream>

namespace bmh::modern_ui {
namespace {
std::string escaped(const std::string &value) {
  std::string out;
  for (char c : value) {
    if (c == '&')
      out += "&amp;";
    else if (c == '<')
      out += "&lt;";
    else if (c == '>')
      out += "&gt;";
    else if (c == '"')
      out += "&quot;";
    else
      out += c;
  }
  return out;
}
std::string px(float value) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << value << "px";
  return out.str();
}
std::optional<int> price(const Snapshot &snapshot) {
  const auto it = snapshot.state.find("ticket");
  if (it == snapshot.state.end() || !it->is_number_integer())
    return {};
  const auto n = it->get<int64_t>();
  if (n < std::numeric_limits<int>::min() ||
      n > std::numeric_limits<int>::max())
    return {};
  return int(n);
}
std::string button(const char *action, const char *label, bool primary = false,
                   bool disabled = false) {
  return "<button data-ticket-action=\"" + std::string(action) + "\" class=\"" +
         (primary ? "native-ticket-primary" : "") + "\"" +
         (disabled ? " disabled=\"disabled\"" : "") + ">" + label + "</button>";
}
bool inside(Rml::Element *node, Rml::Element *root) {
  for (; node; node = node->GetParentNode())
    if (node == root)
      return true;
  return false;
}
} // namespace

TicketPopover::TicketPopover(Rml::Context &context,
                             Rml::ElementDocument &document, DataSource &source,
                             std::function<void()> on_commit)
    : context_(context), document_(document), source_(source),
      on_commit_(std::move(on_commit)) {
  auto layer = document_.CreateElement("div");
  layer->SetId("native-ticket-layer");
  layer->SetProperty("display", "none");
  layer_ = document_.AppendChild(std::move(layer));
  for (const char *type : {"click", "change", "input", "mousedown", "mouseup",
                           "mousemove", "contextmenu"})
    layer_->AddEventListener(type, this);
  // Capture before the range widget's default key handler to avoid double
  // steps.
  layer_->AddEventListener("keydown", this, true);
}
TicketPopover::~TicketPopover() {
  // The owner must destroy this editor before closing its document/context.
  close(false);
  if (layer_)
    document_.RemoveChild(layer_);
}

bool TicketPopover::open(Rml::Element *anchor, float device_pixel_ratio,
                         int min, int max, int step) {
  close(false);
  const auto snapshot = source_.snapshot();
  const auto current = price(snapshot);
  const auto interval = int64_t(max) - min;
  if (!anchor || anchor->GetOwnerDocument() != &document_ ||
      anchor->GetId().empty() || !anchor->IsVisible(true) ||
      !std::isfinite(device_pixel_ratio) || device_pixel_ratio <= 0 ||
      min > max || step <= 0 || interval % step != 0 || !current ||
      *current < min || *current > max || (int64_t(*current) - min) % step != 0)
    return false;
  anchor_id_ = anchor->GetId();
  revision_ = snapshot.revision;
  world_ = snapshot.world;
  original_ = value_ = *current;
  min_ = min;
  max_ = max;
  step_ = step;
  ratio_ = device_pixel_ratio;
  review_ = false;
  error_.clear();
  open_ = true;
  layer_->SetProperty("display", "block");
  layer_->SetProperty("font-size", px(14.f * ratio_));
  render();
  return open_;
}
void TicketPopover::focus_opener() {
  if (auto *anchor = document_.GetElementById(anchor_id_);
      anchor && anchor->IsVisible(true))
    anchor->Focus(true);
}
void TicketPopover::close(bool restore_focus) {
  if (!open_)
    return;
  open_ = false;
  layer_->SetProperty("display", "none");
  layer_->SetInnerRML("");
  world_.reset();
  error_.clear();
  if (restore_focus)
    focus_opener();
}
bool TicketPopover::fresh() const {
  const auto snapshot = source_.snapshot();
  return snapshot.revision == revision_ && snapshot.world == world_ &&
         price(snapshot) == original_;
}
void TicketPopover::resize(float device_pixel_ratio) {
  if (!open_)
    return;
  if (!std::isfinite(device_pixel_ratio) || device_pixel_ratio <= 0) {
    close(false);
    return;
  }
  ratio_ = device_pixel_ratio;
  layer_->SetProperty("font-size", px(14.f * ratio_));
  position();
}
void TicketPopover::update() {
  if (!open_)
    return;
  if (!fresh()) {
    close(false);
    return;
  }
  position();
  if (open_ && !inside(context_.GetFocusElement(), layer_)) {
    if (auto *control = layer_->GetElementById(review_ ? "native-ticket-back"
                                                       : "native-ticket-range"))
      control->Focus(true);
  }
}
void TicketPopover::position() {
  if (!open_)
    return;
  auto *anchor = document_.GetElementById(anchor_id_);
  auto *panel = layer_->GetElementById("native-ticket-panel");
  if (!anchor || !anchor->IsVisible(true) || !panel) {
    close(false);
    return;
  }
  const auto viewport = context_.GetDimensions();
  const float margin = 8.f * ratio_, gap = 8.f * ratio_;
  const float available_width = float(viewport.x) - 2 * margin;
  const float available_height = float(viewport.y) - 2 * margin;
  if (available_width <= 0 || available_height <= 0) {
    close(false);
    return;
  }
  panel->SetProperty("width", px(std::min(330.f * ratio_, available_width)));
  panel->SetProperty("max-height", px(available_height));
  document_.UpdateDocument();
  const auto at = anchor->GetAbsoluteOffset(Rml::BoxArea::Border);
  const auto size = anchor->GetBox().GetSize(Rml::BoxArea::Border);
  const auto popup = panel->GetBox().GetSize(Rml::BoxArea::Border);
  if (size.x <= 0 || size.y <= 0) {
    close(false);
    return;
  }
  const float below = float(viewport.y) - at.y - size.y - gap;
  const float above = at.y - gap;
  const bool down = below >= popup.y || below >= above;
  const float left =
      std::clamp(at.x + size.x - popup.x, margin,
                 std::max(margin, float(viewport.x) - popup.x - margin));
  const float top =
      std::clamp(down ? at.y + size.y + gap : at.y - gap - popup.y, margin,
                 std::max(margin, float(viewport.y) - popup.y - margin));
  panel->SetProperty("left", px(left));
  panel->SetProperty("top", px(top));
  panel->SetAttribute("data-side", down ? "below" : "above");
}
void TicketPopover::render(bool focus_range) {
  const auto amount = std::to_string(value_);
  std::string content =
      "<section id=\"native-ticket-panel\" role=\"dialog\" aria-modal=\"true\" "
      "aria-labelledby=\"native-ticket-title\">"
      "<div class=\"native-ticket-heading\"><h2 "
      "id=\"native-ticket-title\">Eintrittspreis</h2><span>Aktuell: " +
      std::to_string(original_) + " €</span></div>";
  if (review_) {
    content +=
        "<div class=\"native-ticket-editor native-ticket-review\"><strong "
        "aria-label=\"Bisheriger und neuer Eintrittspreis\">" +
        std::to_string(original_) +
        " € <span class=\"native-ticket-arrow\" aria-hidden=\"true\"><span "
        "class=\"native-ticket-arrow-line\"></span><span "
        "class=\"native-ticket-arrow-head\"></span></span> " +
        amount + " €</strong></div>";
  } else {
    content += "<div class=\"native-ticket-editor\"><div "
               "id=\"native-ticket-amount\">" +
               amount +
               " €</div>"
               "<input id=\"native-ticket-range\" type=\"range\" min=\"" +
               std::to_string(min_) + "\" max=\"" + std::to_string(max_) +
               "\" step=\"" + std::to_string(step_) + "\" value=\"" + amount +
               "\" aria-label=\"Eintrittspreis in Euro\" aria-valuetext=\"" +
               amount + " Euro\"/><div class=\"native-ticket-limits\"><span>" +
               std::to_string(min_) + " €</span><span>" + std::to_string(max_) +
               " €</span></div></div>";
  }
  if (!error_.empty())
    content += "<p id=\"native-ticket-error\" role=\"status\">" +
               escaped(error_) + "</p>";
  content +=
      "<div class=\"native-ticket-actions\">" + button("cancel", "Abbrechen");
  if (review_)
    content += "<button id=\"native-ticket-back\" "
               "data-ticket-action=\"back\">Ändern</button>" +
               button("confirm", "Bestätigen", true);
  else
    content += button("review", "Übernehmen", true, value_ == original_);
  content += "</div></section>";
  layer_->SetInnerRML(content);
  position();
  if (open_ && focus_range) {
    if (auto *control = layer_->GetElementById(review_ ? "native-ticket-back"
                                                       : "native-ticket-range"))
      control->Focus(true);
  }
}
void TicketPopover::draft(int value) {
  if (review_ || value < min_ || value > max_ ||
      (int64_t(value) - min_) % step_ != 0 || value == value_)
    return;
  value_ = value;
  layer_->GetElementById("native-ticket-amount")
      ->SetInnerRML(std::to_string(value_) + " €");
  auto *range = layer_->GetElementById("native-ticket-range");
  range->SetAttribute("aria-valuetext", std::to_string(value_) + " Euro");
  if (range->GetAttribute<int>("value", value_) != value_)
    range->SetAttribute("value", value_);
  Rml::ElementList buttons;
  layer_->GetElementsByTagName(buttons, "button");
  for (auto *node : buttons)
    if (node->GetAttribute<Rml::String>("data-ticket-action", "") == "review")
      if (auto *control = dynamic_cast<Rml::ElementFormControl *>(node))
        control->SetDisabled(value_ == original_);
}
void TicketPopover::confirm() {
  if (!fresh()) {
    close(false);
    return;
  }
  const auto result = source_.apply({revision_, "ticket", value_});
  if (!result.ok) {
    if (!fresh()) {
      close(false);
      return;
    }
    error_ = result.error.empty()
                 ? "Der Eintrittspreis konnte nicht übernommen werden."
                 : result.error;
    render();
    return;
  }
  close();
  if (on_commit_)
    on_commit_();
}
void TicketPopover::ProcessEvent(Rml::Event &event) {
  if (!open_)
    return;
  const auto &type = event.GetType();
  auto *target = event.GetTargetElement();
  if (type == "keydown") {
    const auto key =
        Rml::Input::KeyIdentifier(event.GetParameter<int>("key_identifier", 0));
    event.StopImmediatePropagation();
    if (key == Rml::Input::KI_ESCAPE) {
      close();
      return;
    }
    if (key == Rml::Input::KI_TAB) {
      Rml::ElementList controls;
      layer_->QuerySelectorAll(controls, "input,button");
      controls.erase(
          std::remove_if(controls.begin(), controls.end(),
                         [](auto *node) {
                           auto *control =
                               dynamic_cast<Rml::ElementFormControl *>(node);
                           return !node->IsVisible(true) ||
                                  (control && control->IsDisabled());
                         }),
          controls.end());
      if (controls.empty())
        return;
      const auto current = std::find(controls.begin(), controls.end(),
                                     context_.GetFocusElement());
      const bool backwards = event.GetParameter<int>("shift_key", 0) != 0;
      const int n = int(controls.size()),
                old = current == controls.end()
                          ? (backwards ? 0 : -1)
                          : int(current - controls.begin());
      controls[size_t((old + (backwards ? -1 : 1) + n) % n)]->Focus(true);
      return;
    }
    if (!review_ && target->GetId() == "native-ticket-range") {
      const int64_t delta =
          (key == Rml::Input::KI_LEFT || key == Rml::Input::KI_DOWN)
              ? -int64_t(step_)
              : int64_t(step_);
      if (key == Rml::Input::KI_HOME)
        draft(min_);
      else if (key == Rml::Input::KI_END)
        draft(max_);
      else if (key == Rml::Input::KI_LEFT || key == Rml::Input::KI_RIGHT ||
               key == Rml::Input::KI_UP || key == Rml::Input::KI_DOWN)
        draft(int(
            std::clamp(int64_t(value_) + delta, int64_t(min_), int64_t(max_))));
      return;
    }
    if (key != Rml::Input::KI_RETURN && key != Rml::Input::KI_NUMPADENTER &&
        key != Rml::Input::KI_SPACE)
      return;
  } else {
    // Leave slider default mouse handling intact, but never bubble into desktop
    // drag/actions.
    event.StopPropagation();
    if (type == "change" || type == "input") {
      if (!review_ && target->GetId() == "native-ticket-range") {
        const auto raw = event.GetParameter<float>(
            "value", std::numeric_limits<float>::quiet_NaN());
        if (std::isfinite(raw) && raw >= min_ && raw <= max_) {
          const auto n = std::llround(raw);
          if (std::abs(raw - double(n)) < 1e-4)
            draft(int(n));
        }
      }
      return;
    }
    if (type != "click")
      return;
    if (target == layer_) {
      close();
      return;
    }
  }
  if (!fresh()) {
    close(false);
    return;
  }
  for (auto *node = target; node && node != layer_;
       node = node->GetParentNode()) {
    const auto action =
        node->GetAttribute<Rml::String>("data-ticket-action", "");
    if (action.empty())
      continue;
    if (auto *control = dynamic_cast<Rml::ElementFormControl *>(node);
        control && control->IsDisabled())
      return;
    if (action == "cancel")
      close();
    else if (action == "back") {
      review_ = false;
      error_.clear();
      render();
    } else if (action == "review" && value_ != original_) {
      review_ = true;
      error_.clear();
      render();
    } else if (action == "confirm" && review_)
      confirm();
    return;
  }
}
} // namespace bmh::modern_ui
