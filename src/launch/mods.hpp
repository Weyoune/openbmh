// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "content/package.hpp"
#include <functional>
namespace bmh::launch {
struct ModEntry {
    std::string name, type = "Classic", content_id, error;
    std::filesystem::path directory;
};
struct AddMod {
    std::filesystem::path source, saves;
    std::string name;
    int start_year = 2026;
    bool migrate = true;
};
using SaveMigrator =
    std::function<void(const std::filesystem::path &content, const std::filesystem::path &source,
                       const std::filesystem::path &destination)>;
using ImportProgress = std::function<void(const std::string &)>;
struct ImportReport {
    ModEntry mod;
    size_t migrated = 0;
    std::vector<std::string> errors;
};
class ModLibrary {
  public:
    explicit ModLibrary(std::filesystem::path portable_root);
    std::vector<ModEntry> list() const;
    ImportReport add(const AddMod &, const SaveMigrator &, const ImportProgress &progress = {});
    void rename(const ModEntry &, const std::string &name);
    void remove(const ModEntry &);
    std::filesystem::path user_directory(const ModEntry &) const;
    const std::filesystem::path &root() const { return root_; }
    static void validate_name(const std::string &);

  private:
    std::filesystem::path root_;
    void available(const std::string &) const;
    void owned(const ModEntry &) const;
};
} // namespace bmh::launch
