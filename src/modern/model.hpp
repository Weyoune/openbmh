// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "app/career_session.hpp"
#include "app/window_actions.hpp"
#include "content/package.hpp"
#include "launch/rml_window.hpp"
namespace bmh::modern {
struct ClubChoice {
    int selection = 0, master = 0, league = 0;
    std::string name;
};
struct PitchMarker {
    int row;
    float depth, lane;
};
struct Snapshot {
    ui::DeskState desk;
    ui::LineupState squad;
    std::vector<ui::LeagueTableState> tables;
    std::vector<PitchMarker> pitch;
};
class Model {
    std::filesystem::path data_, saves_;
    std::shared_ptr<const content::Classic> content_;
    uint32_t seed_;
    std::unique_ptr<app::CareerSession> session_, pending_;
    ui::SetupOptions options_;
    std::vector<ui::ManagerChoice> managers_;
    uint64_t revision_ = 0;
    bool dirty_ = false, prepared_ = false;

  public:
    Model(std::filesystem::path data, std::filesystem::path saves, std::shared_ptr<const content::Classic>,
          uint32_t seed);
    bool active() const { return bool(session_); }
    bool dirty() const { return dirty_; }
    uint64_t revision() const { return revision_; }
    const app::CareerSession &session() const;
    void begin_new(int difficulty, int managers);
    void cancel_new() {
        pending_.reset();
        managers_.clear();
    }
    int manager_number() const { return int(managers_.size()) + 1; }
    std::vector<ClubChoice> clubs() const;
    bool confirm_manager(std::string name, int selection);
    void load(const std::filesystem::path &);
    void save(std::string name, bool overwrite = false);
    std::vector<std::filesystem::path> saves() const;
    Snapshot snapshot() const;
    void prepare();
    void swap(const ui::LineupState &observed, int first_row, int second_row);
    std::string crest(int stable_club) const;
    std::string photo(int player) const;
    std::optional<rml::Bitmap> image(const std::string &) const;
};
int run(const std::filesystem::path &data, const std::filesystem::path &saves,
        std::shared_ptr<const content::Classic>, uint32_t seed);
} // namespace bmh::modern
