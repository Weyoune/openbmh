// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "modern/ui/model.hpp"
#include <RmlUi/Core/EventListener.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace Rml {
class Context;
class Element;
class ElementDocument;
} // namespace Rml

namespace bmh::modern_ui {
// A presentation-only editor. Default bounds describe the demo, not engine
// rules.
class TicketPopover final : public Rml::EventListener {
public:
  TicketPopover(Rml::Context &, Rml::ElementDocument &, DataSource &,
                std::function<void()> on_commit);
  ~TicketPopover() override;
  TicketPopover(const TicketPopover &) = delete;
  TicketPopover &operator=(const TicketPopover &) = delete;

  bool open(Rml::Element *anchor, float device_pixel_ratio, int min = 0,
            int max = 25, int step = 1);
  void close(bool restore_focus = true);
  void resize(float device_pixel_ratio);
  void update();
  bool is_open() const { return open_; }
  void ProcessEvent(Rml::Event &) override;

private:
  void render(bool focus_range = true);
  void position();
  void draft(int value);
  void confirm();
  void focus_opener();
  bool fresh() const;

  Rml::Context &context_;
  Rml::ElementDocument &document_;
  DataSource &source_;
  std::function<void()> on_commit_;
  Rml::Element *layer_ = nullptr;
  std::string anchor_id_, error_;
  std::shared_ptr<const Catalog> world_;
  uint64_t revision_ = 0;
  float ratio_ = 1.f;
  int original_ = 0, value_ = 0, min_ = 0, max_ = 25, step_ = 1;
  bool open_ = false, review_ = false;
};
} // namespace bmh::modern_ui
