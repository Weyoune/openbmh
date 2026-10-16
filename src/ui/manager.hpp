// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <filesystem>
#include <vector>

#include "formats/font.hpp"
#include "formats/le.hpp"
#include "formats/m3d.hpp"
#include "formats/textd.hpp"
#include "ui/setup.hpp"

namespace bmh::ui {

// Master-database ID, not the remapped team ID of an initialized career.
// See MANAGER_SELECTION.md for original manager-block offsets and evidence.
struct ManagerChoice {
    int slot = 0;                  // UI selection order; not active-engine identity
    std::string name;              // Original ISO 646-DE bytes
    int master_club = 0;
    int start_league = 0;          // 0..4
    int strength = 1;              // 0 lower, 1 middle, 2 upper third
    int portrait = 0;              // 0..11
    int selection_club = 0;        // Index in the selection context at confirmation
    uint64_t roster_revision = 0;  // Prevent applying an ID against a later club order
    bool operator==(const ManagerChoice&) const = default;
};

struct SelectionContext {
    // Selection index -> stable original master-database index. Updated by engine
    // after every accepted manager; ConfirmManager swaps clubs between dialogs.
    std::vector<int> club_order;
    uint64_t revision = 0;
    std::string source = "master_database";
};
SelectionContext load_selection_context(const std::filesystem::path& path, uint64_t revision);

enum class ManagerView { selection, name_edit, club_picker, confirmation, accepted };

// ManagerScreen2, ManagerScreen and PresentManagerDialog. Owns UI choices only.
class ManagerScreen {
public:
    ManagerScreen(const std::filesystem::path& data, int slot,
                  const std::vector<ManagerChoice>& confirmed = {}, SelectionContext context = {});
    const ManagerChoice& choice() const { return choice_; }
    ManagerView view() const { return view_; }
    std::string screen_name() const;
    bool click(int x, int y, bool right = false);
    bool move(int x, int y);
    bool text(std::string_view utf8);
    bool key(std::string_view name);
    Image render() const;

    // The seven club picker tabs (Bundesliga ... Oberliga, Zusatzteams A/B) as the original
    // builds them from the executable's tables: label (original encoding) and the inclusive
    // range of selection indices. Used by the console (openbmh_cli) for ListTeams.
    static constexpr int group_count = 7;
    const std::string& group_label(int group) const { return leagues_.at(size_t(group)); }
    int group_first(int group) const { return first_.at(size_t(group)); }
    int group_last(int group) const { return last_.at(size_t(group)); }
    int master_of(int selection_id) const { return master_id(selection_id); }
    const std::string& club_name(int master) const { return db_.club_names.at(size_t(master)); }

private:
    bool club_locked(int id) const;
    bool portrait_locked(int id) const;
    int master_id(int selection_id) const;
    int hovered_club(int x, int y) const;
    void portrait(Image&, int x, int y, int id) const;
    void badge(Image&, int x, int y, int id) const;
    void pattern(Image&, int x, int y, int w, int h) const;
    void frame(Image&, int x, int y, int w, int h, bool shadow = true) const;
    void button(Image&, int x, int y, int w, std::string_view label, bool selected) const;
    void small_text(Image&, int x, int baseline, std::string_view text) const;
    int sprite_text_width(std::string_view text) const;
    void sprite_text(Image&, int x, int y, std::string_view text, int font) const;
    ManagerChoice choice_;
    std::vector<ManagerChoice> confirmed_;
    SelectionContext context_;
    ManagerView view_ = ManagerView::selection;
    std::filesystem::path data_;
    LeImage exe_;
    Database db_;
    TextDb texts_;
    Font small_, large_;
    Image background_, faces_, widgets_, letters_;
    std::array<int, 4> text_x_{}, baseline_{}, field_x_{}, field_y_{}, field_width_{};
    std::array<std::string, 7> leagues_;
    std::array<int, 7> first_{}, last_{};
    int group_ = 0, hover_ = -1;
};

// JSON emits UTF-8; rendering/model preserve the original encoding.
std::string json_quote(std::string_view utf8);
std::string original_to_utf8(std::string_view original);
std::string utf8_to_original(std::string_view utf8);
std::string manager_json(const ManagerChoice& choice);

}  // namespace bmh::ui
