// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "editor/archive.hpp"

namespace bmh::editor {
class World {
  public:
    Json manifest;
    std::shared_ptr<ArchiveStore> store = std::make_shared<ArchiveStore>();
    fs::path source_path;
    bool imported = false;
    static World open(const fs::path &, const std::string &start_date = "2026-07-01");
    static World blank(const std::string &title = "Untitled world");
    static World demo();
    Json &document(const std::string &);
    const Json &document(const std::string &) const;
    Json &rows(const std::string &);
    const Json &rows(const std::string &) const;
    const Json *find(const std::string &, const std::string &) const;
    Json *find(const std::string &, const std::string &);
    void change(const std::string &table, const std::string &id, const std::optional<Json> &);
    void touch(const std::string &table);
    Json query(const Json &) const;
    Json validate() const;
    Json statistics() const;
    std::string asset_file(const std::string &key) const;
    std::string portrait(const Json &) const;
    std::string title(const Json &entity, const std::string &language = "de") const;
    std::vector<std::pair<std::string, std::string>> references(const std::string &id) const;
    void save(const fs::path &) const;
    static std::string file_for(const std::string &table);
    static std::string identity(const Json &row, const std::string &table);
    static const std::vector<std::string> &tables();

  private:
    std::map<std::string, Json> docs_;
    std::map<std::string, std::string> original_;
    std::set<std::string> dirty_;
    void load();
    void editor_profile();
};
} // namespace bmh::editor
