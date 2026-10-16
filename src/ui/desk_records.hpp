// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "formats/font.hpp"
#include "formats/le.hpp"
#include "formats/textd.hpp"
#include "formats/vga.hpp"
#include "game/highscore.hpp"
#include "game/state.hpp"
#include "ui/game_end_alternate.hpp"
#include "ui/sprite_font.hpp"

namespace bmh::ui {

enum class DeskRecordKind { fair_play, highscore };
struct FairPlayRow { int team = -1, rank = 0, yellow = 0, red = 0, points = 0; std::string name; uint8_t name_color=0x5f; };
struct DeskRecordsState {
    DeskRecordKind kind = DeskRecordKind::fair_play;
    int league = -1;
    std::vector<FairPlayRow> fair_play_rows;
    HighscoreProjection highscore;
    Image underlay;
    bool gradient_text = true;
    bool original_panel = true;
    bool decorated_background = true;
    std::string manager_name;
};

// Original menu-25/FairPlayStateFromGame and menu-26/GameEndAlternateScoreScreen report views. Their engine
// projections are read-only here; only the highscore file's Source update is
// performed by the caller before constructing this presentation.
DeskRecordsState fair_play_state_from_game(const GameState&, int league, const Image& underlay = {}, int manager_slot=-1);
class DeskRecordsScreen {
public:
    DeskRecordsScreen(const std::filesystem::path& data, DeskRecordsState);
    Image render() const;
    bool pointer_down(int x,int y,bool right=false);
    bool pointer_up(int x,int y,bool right=false);
    bool advance_point() { return alternate_ && alternate_->advance_point(); }
    bool finish_points() { return alternate_ && alternate_->finish_points(); }
    bool points_active() const { return alternate_ && alternate_->phase()==AlternateScorePhase::Points; }
    bool text_input(std::string_view);
    bool key_input(std::string_view);
    bool is_alternate_calculator() const { return bool(alternate_); }
    bool close_input() { if (closed_) return false; closed_ = true; return true; }
    bool closed() const { return closed_; }
    DeskRecordKind kind() const { return state_.kind; }
private:
    LeImage exe_; TextDb texts_; Font font_; SpriteFont sprite_font_; Image kb56_, kb51_, background_;
    DeskRecordsState state_; bool closed_ = false;
    std::unique_ptr<GameEndAlternateScoreScreen> alternate_;
    void text(Image&, int x, int baseline, std::string_view, uint8_t color, bool shadow=false) const;
};

} // namespace bmh::ui
