// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "editor/types.hpp"
#include <functional>

namespace bmh::editor {
// Asset bytes are read on demand. A private source snapshot permits replacing
// the user's own editor file atomically on Windows as well as POSIX systems.
class ArchiveStore {
  public:
    ArchiveStore();
    static std::shared_ptr<ArchiveStore> open(const fs::path &);
    ~ArchiveStore();
    bool contains(const std::string &) const;
    std::vector<std::string> names() const;
    uint64_t size(const std::string &) const;
    std::string read(const std::string &) const;
    void set(const std::string &, std::shared_ptr<const std::string>);
    std::shared_ptr<const std::string> overlay(const std::string &) const;
    void restore(const std::string &, std::shared_ptr<const std::string>);
    void save(const fs::path &, const std::map<std::string, std::string> &documents,
              const std::set<std::string> &excluded = {}) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace bmh::editor
