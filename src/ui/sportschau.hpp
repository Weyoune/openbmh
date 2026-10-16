// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "formats/font.hpp"
#include "formats/le.hpp"
#include "formats/palette.hpp"
#include "formats/textd.hpp"
#include "formats/vga.hpp"
#include "game/sportschau_presentation.hpp"

namespace bmh::ui {

// CompetitionWinnerScreen(param1=0): championship/cup winner artwork and Original font2 labels.
class CompetitionWinnerScreen {
public:
    explicit CompetitionWinnerScreen(const std::filesystem::path& data_directory);
    Image render(const GameState& state, const CompetitionWinnerView& view) const;
    const std::optional<Palette>& palette_override(uint8_t title) const;
private:
    LeImage exe_;
    Font font_;
    std::array<Image, 2> backdrop_, lower_;
    std::array<Image, 9> trophies_;
    std::array<std::optional<Palette>, 2> palettes_;
};

// Pure indexed-image projection of SportschauIntro's intro animation.
// Presentation timing, input pumping, and audio remain the caller's job.
class SportschauIntro {
public:
    static constexpr uint32_t frame_count = 40;
    static constexpr uint32_t frames_per_atlas = 20;
    static constexpr int frame_width = 70;
    static constexpr int frame_height = 47;
    static constexpr int destination_x = 99;
    static constexpr int destination_y = 46;

    explicit SportschauIntro(const std::filesystem::path& data_directory);

    // Returns one complete 320x240 Original logical frame. The intro sequence
    // itself is exactly frames [0, frame_count); invalid indices are rejected.
    Image render_frame(uint32_t index) const;

    // PaletteOverride installs KB/132's optional trailing 256-color palette into
    // the active VGA page. Main can use this while presenting the intro; an
    // absent trailer leaves the current/global M3PC palette in effect.
    const std::optional<Palette>& palette_override() const noexcept { return palette_override_; }

private:
    Image backdrop_;
    std::array<Image, 2> atlases_;
    std::optional<Palette> palette_override_;
};

// Source-owned, immutable inputs used by MatchdayIntro. Runtime style and the
// two W-atlas words are supplied by the caller's accepted projection; the
// renderer does not inspect GameState or derive gameplay data.
struct SportschauReportContext {
    bool solid_panel_background = false;
    bool gradient_text = false;
};

using bmh::SportschauReportStep;
using bmh::sportschau_report_steps;

class MatchdayIntro {
public:
    explicit MatchdayIntro(const std::filesystem::path& data_directory);
    Image render(const MatchdayIntroView&) const;
    const std::optional<Palette>& palette_override() const noexcept { return palette_override_; }
private:
    Image backdrop_;
    Font font_;
    std::optional<Palette> palette_override_;
};

// SportschauEleven: eleven owned candidates over KB57, four KB16 figure sprites,
// Source position tables and centered player/club names. No engine access.
class SportschauEleven {
public:
    explicit SportschauEleven(const std::filesystem::path& data_directory);
    Image render(const SportschauAwardsView& view, bool gradient_text = false) const;
private:
    LeImage exe_;
    Font font_;
    Image room_, figures_;
};

// SportschauTeamInfo: four accumulated matchday totals, KB59 icons and digit atlas.
class SportschauTeamInfo {
public:
    explicit SportschauTeamInfo(const std::filesystem::path& data_directory);
    Image render(const SportschauTeamInfoView& view, bool gradient_text = false) const;
private:
    LeImage exe_;
    Font font_;
    Image room_, atlas_;
};

// Pure report renderer for MatchdayIntro. ShotAnimation scene draws belong to Core;
// Main renders those owned observations using the existing scene renderers.
class SportschauMatchReport {
public:
    SportschauMatchReport(const std::filesystem::path& data_directory,
                          SportschauFixtureView fixture,
                          SportschauReportContext context);

    // `visible_log_entries` reveals the first N records of the already ordered
    // Source log projection. It never advances the match or derives ShowEvents.
    Image render_report(size_t visible_log_entries) const;
    Image render_report(const SportschauReportStep& step) const;

private:
    SportschauFixtureView fixture_;
    SportschauReportContext context_;
    LeImage exe_;
    Font small_, heading_;
    TextDb texts_;
    Image room_, event_art_, panel_art_, fallback_badge_;
    std::array<std::optional<Image>, 2> badge_atlas_;
    Image render_report(size_t visible_log_entries, std::optional<uint8_t> progress,
                        bool transient_chance = false) const;
    void text(Image&, const Font&, int x, int baseline, std::string_view,
              uint8_t color, int right = -1, bool shadow = false) const;
    void panel(Image&, int x1, int y1, int x2, int y2) const;
    void draw_badge(Image&, int x, int y, int team, uint16_t badge_word, size_t side) const;
};

} // namespace bmh::ui
