// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <filesystem>
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "formats/font.hpp"
#include "formats/le.hpp"
#include "formats/palette.hpp"
#include "game/match_engine.hpp"

namespace bmh::ui {

struct LiveMatchRow {
    std::string home;
    std::string away;
    int home_goals = 0;
    int away_goals = 0;
    bool score_known = false;
    int home_team = -1;
    int away_team = -1;
    std::array<int, 4> home_stats{}; // SquadRange3 and three source event-log tag counts
    std::array<int, 4> away_stats{};
    uint32_t spectators = 0;
    bool spectators_known = false;
    bool attendance_at_capacity = false;
    std::string spectators_text;
    uint8_t rain = 0;
    int8_t temperature = 0;
    std::string temperature_text;
    bool operator==(const LiveMatchRow&) const = default;
};

// Owned copy made synchronously from the engine's observation hook.
struct LiveMatchdayFrame {
    int minute = 0;
    std::string minute_text; // owned FormatNumber(0x81) text
    bool gradient_text = true; // Source TextGradientDisabled; minute deliberately toggles the font mode
    std::vector<LiveMatchRow> matches;
    bool operator==(const LiveMatchdayFrame&) const = default;
};

LiveMatchdayFrame live_matchday_frame(int minute, const std::vector<MatchGroup>& groups,
                                     const GameState& state);

struct LiveChanceProjection {
    LiveChanceStage stage = LiveChanceStage::preview;
    int minute = 0, group = 0, match = 0, side = 0;
    std::string stadium, home, away, description, shooter_detail;
    std::string outcome, participants, ticker_score;
    bool ticker = false;
    bool operator==(const LiveChanceProjection&) const = default;
};

LiveChanceProjection live_chance_projection(const LiveChanceEvent&,
    const std::vector<MatchGroup>&, const GameState&, const LeImage&);

struct LiveBookingProjection {
    LiveChanceProjection header;
    int kind = 0, home_goals = 0, away_goals = 0;
    bool score_known = false;
    std::string message;
    bool operator==(const LiveBookingProjection&) const = default;
};
LiveBookingProjection live_booking_projection(const LiveBookingEvent&,
    const std::vector<MatchGroup>&, const GameState&, const LeImage&);

struct LiveShootoutProjection {
    LiveChanceProjection header;
    int home_goals = 0, away_goals = 0;
    std::string message;
    bool operator==(const LiveShootoutProjection&) const = default;
};
LiveShootoutProjection live_shootout_projection(const LiveShootoutEvent&,
    const std::vector<MatchGroup>&, const GameState&, const LeImage&);

class LiveMatchdayScreen {
public:
    explicit LiveMatchdayScreen(const std::filesystem::path& data);
    Image render(const LiveMatchdayFrame& frame) const;
    // BallFrameCount draws positions 0, +/-2, ... up to two pixels before
    // the even target. Its last target step exits without another present.
    static int ball_frame_count(int displacement);
    Image render_ball(const LiveMatchdayFrame&, int displacement, int frame_index,
                      const LiveChanceProjection* chance = nullptr) const;
    Image render_reality(const LiveMatchdayFrame&, const LiveChanceProjection&) const;
    Image render_chance(const LiveMatchdayFrame&, const LiveChanceProjection&) const;
    // The 18 Source frames, reconstructed once from an owned, already drawn shot.
    std::array<Image, 18> render_shot(const LiveMatchdayFrame&, const LiveShotEvent&,
                                   const Image* source_backdrop = nullptr) const;
    Palette shot_palette(const Palette&, const LiveShotEvent&,
                         const std::vector<MatchGroup>&, const GameState&) const;
    std::vector<Image> render_set_piece(const LiveMatchdayFrame&, const LiveSetPieceEvent&,
                                      const Image* source_backdrop = nullptr) const;
    Image render_sportschau_header(const LiveChanceProjection&, int home_before,
                                  int away_before, bool gradient_text) const;
    Image render_booking(const LiveMatchdayFrame&, const LiveBookingProjection&) const;
    Image render_shootout(const LiveMatchdayFrame&, const LiveShootoutProjection&) const;
private:
    Image render_header(const LiveMatchdayFrame&, const LiveChanceProjection&,
                        int home_goals, int away_goals, bool score_known, uint8_t actor_color) const;
    LeImage exe_;
    Image background_;
    Image board_;
    Image score_digits_;
    Image weather_;
    Image goal_background_;
    Image goal_sprites_;
    Image set_piece_background_;
    Image set_piece_sprites_;
    Font font_;
    Font heading_;
    void text(Image&, int x, int baseline, std::string_view, uint8_t color, int max_width,
              bool centered = false, bool gradient = false) const;
};

} // namespace bmh::ui
