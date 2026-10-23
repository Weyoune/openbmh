// SPDX-License-Identifier: GPL-3.0-only
#include "modern/ui/gui_assets.hpp"
#include <algorithm>
#include <cmath>

namespace bmh::modern_ui {
GuiAssets::GuiAssets(fs::path root) : root_(std::move(root)) {
  if (root_.empty() || !fs::exists(root_ / "gui-assets.json"))
    return;
  root_ = fs::weakly_canonical(root_);
  auto document = Json::parse(editor::read_file(root_ / "gui-assets.json"));
  if (document.value("schema", "") != "openbmh-gui-assets@1" ||
      !document.at("items").is_object())
    throw editor::Error("invalid_gui_catalog");
  items_ = document.at("items");
  for (const auto &[key, item] : items_.items()) {
    const auto relative = editor::path(item.at("file").get<std::string>());
    const auto full = fs::weakly_canonical(root_ / relative);
    const auto inside = full.lexically_relative(root_);
    if (relative.is_absolute() || inside.empty() || *inside.begin() == ".." ||
        !fs::is_regular_file(full))
      throw editor::Error("missing_or_unsafe_gui_asset", {{"key", key}});
  }
}
editor::images::Raster GuiAssets::decode(const std::string &key,
                                         int pixels) const {
  if (!items_.contains(key))
    throw editor::Error("missing_gui_asset", {{"key", key}});
  const auto item = items_.at(key);
  const auto file = root_ / editor::path(item.at("file").get<std::string>());
  auto image = editor::images::decode(editor::read_file(file),
                                      file.extension().string(), pixels);
  if (!item.contains("crop"))
    return image;
  const auto crop = item.at("crop");
  if (!crop.is_array() || crop.size() != 4)
    throw editor::Error("invalid_gui_crop");
  for (const auto &n : crop)
    if (!n.is_number() || !std::isfinite(n.get<double>()) || n < 0 || n > 1)
      throw editor::Error("invalid_gui_crop");
  if (crop[2] <= 0 || crop[3] <= 0 ||
      crop[0].get<double>() + crop[2].get<double>() > 1.000001 ||
      crop[1].get<double>() + crop[3].get<double>() > 1.000001)
    throw editor::Error("invalid_gui_crop");
  const auto x = std::clamp(
      int(std::round(crop[0].get<double>() * image.width)), 0, image.width - 1);
  const auto y =
      std::clamp(int(std::round(crop[1].get<double>() * image.height)), 0,
                 image.height - 1);
  const auto width = std::clamp(
      int(std::round(crop[2].get<double>() * image.width)), 1, image.width - x);
  const auto height =
      std::clamp(int(std::round(crop[3].get<double>() * image.height)), 1,
                 image.height - y);
  editor::images::Raster out{width, height,
                             std::vector<uint8_t>(size_t(width) * height * 4)};
  for (int row = 0; row < height; ++row)
    std::copy_n(image.pixels.data() + (size_t(y + row) * image.width + x) * 4,
                size_t(width) * 4, out.pixels.data() + size_t(row) * width * 4);
  return out;
}
} // namespace bmh::modern_ui
