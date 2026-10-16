// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "formats/font.hpp"
#include "formats/le.hpp"
#include "formats/textd.hpp"
#include "formats/vga.hpp"
#include "ui/sprite_font.hpp"

namespace bmh::ui {

enum class SaveLoadMode { load, save };
enum class SaveLoadAction { load, save };
enum class SaveLoadFileKind { game, menu_layout, settings };

struct SaveLoadRequest {
    SaveLoadAction action = SaveLoadAction::load;
    int manager_slot = -1;
    uint64_t revision = 0;
    std::filesystem::path path;
    bool overwrite = false;
    SaveLoadFileKind file_kind = SaveLoadFileKind::game;
    int origin_item_id = -1;
    bool operator==(const SaveLoadRequest&) const = default;
};

struct SaveLoadEntry { std::filesystem::path path; };

class SaveLoadScreen {
public:
    SaveLoadScreen(const std::filesystem::path& data, std::filesystem::path directory, SaveLoadMode mode,
                   int manager_slot, uint64_t revision, Image backdrop, bool gradient_text = true,
                   SaveLoadFileKind file_kind = SaveLoadFileKind::game, int origin_item_id = -1);
    Image render() const;
    // InfoPress opens5870f on right DOWN; its popup restores on mouse UP.
    bool info_press(int x, int y);
    bool info_open() const { return bool(info_); }
    bool click(int x, int y, bool right = false);
    bool key(std::string_view key);
    bool text(std::string_view value);
    const std::optional<SaveLoadRequest>& pending() const { return pending_; }
    const std::vector<SaveLoadEntry>& entries() const { return entries_; }
    int selected() const { return selected_; }
    bool close_requested() const { return close_requested_; }
    SaveLoadFileKind file_kind() const { return file_kind_; }
    void acknowledge();
    void reject(std::string reason);
    std::string state_json() const;
private:
    std::filesystem::path directory_;
    SaveLoadMode mode_;
    SaveLoadFileKind file_kind_;
    int manager_slot_;
    int origin_item_id_;
    uint64_t revision_;
    Image backdrop_;
    Font font_;
    LeImage exe_;
    Image widgets_;
    TextDb texts_;
    SpriteFont sprite_font_;
    Font footer_font_;
    bool gradient_text_ = true;
    std::vector<SaveLoadEntry> entries_;
    int selected_ = 0, scroll_ = 0;
    std::string filename_, feedback_;
    bool filename_focus_ = false, overwrite_armed_ = false, close_requested_ = false;
    int last_clicked_row_ = -1;
    std::optional<SaveLoadRequest> pending_;
    struct InfoRow { std::string manager, club, league; };
    struct FileInfo { std::string path, date; std::vector<InfoRow> rows; };
    std::optional<FileInfo> info_;
    void request_action();
    bool select_row(int y);
    void frame(Image&, int x, int y, int width, int height) const;
    void button(Image&, int x, int y, int width, std::string_view label = {}) const;
    void text(Image&, int x, int baseline, std::string_view value, uint8_t color, int max_width,
              const Font&, bool gradient = false) const;
};

std::string save_load_request_json(const SaveLoadRequest&);

}  // namespace bmh::ui
