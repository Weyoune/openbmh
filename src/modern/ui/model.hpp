// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "modern/ui/catalog.hpp"

namespace bmh::modern_ui {
struct Snapshot {
  uint64_t revision = 0;
  std::shared_ptr<const Catalog> world;
  Json state;
};
struct Action {
  uint64_t revision = 0;
  std::string kind;
  Json value;
};
struct ActionResult {
  bool ok = false;
  uint64_t revision = 0;
  std::string error;
};
// The native view depends on this presentation boundary, never on GameState.
class DataSource {
public:
  virtual ~DataSource() = default;
  virtual Snapshot snapshot() const = 0;
  virtual ActionResult apply(const Action &) = 0;
  // Read-side history projections can be backed by lazy engine-season decoding.
  // Browsing does not change the game revision or copy the archive into a save.
  virtual Json read_almanac(const Json &) const {
    return {{"ok", false}, {"error", "almanac_unavailable"}};
  }
};
class DemoSource final : public DataSource {
public:
  explicit DemoSource(std::shared_ptr<const Catalog>);
  Snapshot snapshot() const override;
  ActionResult apply(const Action &) override;
  Json read_almanac(const Json &) const override;
  void save(const fs::path &, const Json &desktop);
  Json load(const fs::path &);
  bool dirty() const { return state_ != saved_; }

private:
  std::shared_ptr<const Catalog> catalog_;
  const Json almanac_;
  Json state_, saved_;
  uint64_t revision_ = 0;
};
} // namespace bmh::modern_ui
