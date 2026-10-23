// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "editor/images.hpp"
#include "modern/ui/catalog.hpp"

namespace bmh::modern_ui {
class GuiAssets {
public:
  explicit GuiAssets(fs::path root = {});
  bool available() const { return !items_.empty(); }
  editor::images::Raster decode(const std::string &, int pixels = 1024) const;
  bool contains(const std::string &key) const { return items_.contains(key); }
  const Json &items() const { return items_; }
  const fs::path &root() const { return root_; }

private:
  fs::path root_;
  Json items_ = Json::object();
};
} // namespace bmh::modern_ui
