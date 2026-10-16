// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <filesystem>
#include <optional>

#include "formats/font.hpp"
#include "formats/le.hpp"
#include "formats/textd.hpp"
#include "ui/desk_state.hpp"

namespace bmh::ui {

// Source pair selection for SourceDrawerTeam's paired drawers. `local_kind` is
// the switch value (native item kind minus 50), not the native record kind.
std::optional<int> source_drawer_team(const DeskState&, int local_kind);

class DeskDrawerRenderer {
public:
    explicit DeskDrawerRenderer(const std::filesystem::path& data);
    bool handles(int native_kind) const;
    // Draws content only; the caller retains ownership of the native drawer frame.
    void draw(Image& target, const DeskState&, const DeskItem&,
              const Image& own_badge_atlas, const std::optional<Image>& rival_badge_atlas,
              const Image& fallback_badge_atlas) const;

private:
    LeImage exe_;
    Image icons_;
    Font small_;
    TextDb texts_;

    void copy(Image&, int sx, int sy, int x, int y, int w, int h) const;
    void text(Image&, const DeskState&, int x, int baseline, std::string_view, uint8_t color, int right = -1) const;
    void line(Image&, int x1, int y1, int x2, int y2, uint8_t color) const;
    void folder(Image&, const DeskState&, const DeskItem&) const;
    void curve(Image&, const DeskState&, const DeskItem&, int local_kind) const;
    void recent(Image&, const DeskState&, const DeskItem&) const;
    void head_to_head(Image&, const DeskState&, const DeskItem&) const;
    void rank_strength(Image&, const DeskState&, const DeskItem&, int local_kind) const;
    void opponent_name(Image&, const DeskState&, const DeskItem&, int local_kind) const;
    void badge(Image&, const DeskState&, const DeskItem&, int local_kind,
               const Image& own_badge_atlas, const std::optional<Image>& rival_badge_atlas,
               const Image& fallback_badge_atlas) const;
};

} // namespace bmh::ui
