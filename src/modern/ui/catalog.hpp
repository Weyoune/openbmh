// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "editor/archive.hpp"
#include <unordered_map>

namespace bmh::modern_ui {
using Json = editor::Json;
namespace fs = std::filesystem;

// Immutable presentation data. This reader does not construct a game state.
class Catalog {
public:
  static std::shared_ptr<const Catalog> open(const fs::path &);
  static bool modern_manifest(const Json &);
  const Json &manifest() const { return manifest_; }
  const Json &rows(const std::string &) const;
  const Json *find(const std::string &, const std::string &) const;
  std::string title() const;
  std::string name(const Json &) const;
  std::string nation_name(const std::string &) const;
  std::vector<const Json *> leagues(const std::string &nation = {}) const;
  std::vector<const Json *> clubs(const std::string &league = {}) const;
  std::vector<const Json *> squad(const std::string &) const;
  bool manageable(const std::string &) const;
  std::pair<std::string, std::string> asset(const std::string &,
                                            int pixels = 512) const;
  Json summary() const;
  const fs::path &source() const { return source_; }

private:
  fs::path source_;
  std::shared_ptr<editor::ArchiveStore> store_;
  Json manifest_, presentation_;
  std::map<std::string, Json> tables_;
  std::map<std::string, std::unordered_map<std::string, size_t>> indices_;
};
std::string localized(const Json &, const std::string &language = "de");
std::string number(int64_t);
std::string money(int64_t, const Json &currency);
} // namespace bmh::modern_ui
