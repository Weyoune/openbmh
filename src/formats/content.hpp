// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include "formats/font.hpp"
#include "formats/palette.hpp"
#include "formats/le.hpp"
#include "formats/m3d.hpp"
#include "formats/textd.hpp"

namespace bmh::content {
// Immutable decoded Classic content. Logical aliases keep the existing UI geometry
// independent of both storage layout and the legacy binary codecs.
struct Classic {
    bool english = false;
    Database database;
    TextDb texts;
    LeImage executable, source_tables;
    Bytes desktops, formations, text_header;
    Palette palette;
    std::map<std::string, Image> images;
    std::map<std::string, Font> fonts;
    std::map<std::string, Palette> image_palettes;
    std::map<std::string, Bytes> audio;
    std::map<std::string, Bytes> supplemental;
    std::map<std::string, std::string> documents;
    std::string content_id;
    int start_year = 2026;
};
struct Resource {
    std::shared_ptr<const Classic> content;
    std::string key;
    int display_start_year = 2026;
};
// The mount owns a shared content reference; callers never reconstruct legacy files.
// One game runs per process. Multiple mounts are supported by tools and tests.
class Mount {
  public:
    Mount(std::filesystem::path root, std::shared_ptr<const Classic> content);
    ~Mount();
    Mount(const Mount &) = delete;
    Mount &operator=(const Mount &) = delete;
    const std::filesystem::path &root() const { return root_; }

  private:
    std::filesystem::path root_;
};
std::optional<Resource> find(const std::filesystem::path &locator);
void set_display_start_year(const std::filesystem::path &root, int year);
Bytes bytes(const Resource &resource);
std::string key(const std::filesystem::path &relative);
} // namespace bmh::content
