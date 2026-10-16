// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <cstdio>
#include <stdexcept>

#include "game/reality_motion.hpp"
#include "game/random.hpp"
#include "ui/live_reality.hpp"
#include "ui/live_matchday.hpp"
#include "ui/setup.hpp"

using namespace bmh;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejected(F f) {
    bool failed = false; try { f(); } catch (const FormatError&) { failed = true; }
    check(failed, "invalid Reality input must fail before drawing");
}
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: reality_motion_tests <Original data>");
        const std::filesystem::path data = argv[1];
        const auto exe = load_le(ui::asset_path(data, "BMH.exe"));
        const auto original_bytes = exe.bytes();
        const auto digits = load_vga(ui::asset_path(data / "KB", "13.VGA"));
        const auto raw_atlas = load_vga(ui::asset_path(data / "KB", "50.VGA"));
        const int directions[8][3] = {{1,0,2},{-1,0,6},{0,-1,0},{0,1,4},
                                      {1,-1,1},{-1,-1,5},{1,1,3},{-1,1,7}};
        for (const auto& d : directions) check(reality_direction(d[0],d[1]) == d[2], "Source eight-direction convention");
        const auto skew = reality_path(0,0,4,1);
        check(skew.count == 5 && skew.points[2] == std::pair{2,0} && skew.points[3] == std::pair{3,1},
              "Source integer pass line has its original asymmetric threshold");
        check(reality_path(7,9,7,9).count == 1, "zero-length Source path contains its endpoint once");
        for (int y = 0; y <= 24; ++y) for (int goal : {0,42}) {
            const auto path = reality_goal_path(21,y,goal);
            check(path.count <= 43 && path.points[0] == std::pair{21,y} &&
                  path.points[path.count-1] == std::pair{goal,y < 8 ? 11 : y > 16 ? 13 : 12},
                  "both Source goal paths end in the corresponding goal-mouth third");
        }
        RealityGrid grid;
        check(grid.place(0,10,12,0) && grid.occupant(10,12,0) == 1,
              "Source grid stores global figure index plus one");
        check(!grid.place(1,10,12,0) && grid.position(1) == std::pair{0,0},
              "teammate collision rejects placement without moving the rejected figure");
        check(grid.place(11,10,12,1) && grid.occupant(10,12,1) == 12,
              "opposing occupancy planes can share a cell");
        check(grid.place(22,10,12,2) && grid.place(22,42,13,2) &&
              grid.ball() == std::pair{42,13} && grid.previous_ball() == std::pair{10,12} &&
              grid.occupant(42,13,0) == 0 && grid.occupant(42,13,1) == 0,
              "Source ball movement retains its previous cell without inventing a player occupant");
        check(!grid.place(2,43,12,0) && grid.position(2) == std::pair{0,0}, "out-of-pitch placement leaves state unchanged");
        grid.clear(10,12,0);
        check(grid.place(0,11,12,0) && grid.occupant(10,12,0) == 0 && grid.occupant(10,12,1) == 12,
              "Source caller removes the old own-cell without erasing the opposing plane");
        for (int x = -42; x <= 42; ++x) for (int y = -24; y <= 24; ++y)
            check(reality_direction(x,y) >= 0 && reality_direction(x,y) < 8, "all legal Source movement vectors");
        for (int y = 0; y <= 24; ++y) for (int x = 0; x <= 42; ++x) {
            const auto left = reality_project(exe,x,y), right = reality_project(exe,42-x,y);
            check(left.first + right.first == 306 && left.second == right.second,
                  "Source perspective has a fixed center and symmetric opposite sides");
            check(reality_project(exe,21,y).first == 153, "grid center projects to Source x153");
        }
        RealityMotionInput input;
        input.figures[0] = {true,10,8,14,12,0};
        input.figures[11] = {true,32,8,28,12,0};
        input.figures[22] = {true,10,8,14,12,0};
        input.figures[23] = {false,5,5,6,6,0}; // Source replaces this with figure22's trajectory.
        input.ball_airborne = true;
        const auto prior = input;
        // Any accidental draw is a failure, including drawing from a new generator.
        draw_hook = [](int) { throw std::runtime_error("Reality drawing consumed RNG"); };
        const auto motion = reality_motion(input,exe);
        check(input == prior && exe.bytes() == original_bytes, "motion consumes an owned observation without Source writes");
        check(motion == reality_motion(input,exe), "motion replay is deterministic");
        check(motion.frames[9] == motion.frames[10], "Source renders its target twice after nine interpolated frames");
        for (int tick = 0; tick < 11; ++tick) {
            const auto& frame = motion.frames[size_t(tick)];
            check(frame.count == 4, "absent figures are not invented");
            check(frame.sprites[0].figure == 0 && frame.sprites[1].figure == 11 &&
                  frame.sprites[2].figure == 22 && frame.sprites[3].figure == 23,
                  "equal Source depths preserve player/shadow/ball slot order");
            check(frame.sprites[0].number == 1 && frame.sprites[1].number == 1 &&
                  frame.sprites[2].number == 0 && frame.sprites[3].number == 0,
                  "shirt numbering restarts on the other side and excludes ball/shadow");
            check(frame.sprites[1].away_palette && !frame.sprites[2].away_palette && !frame.sprites[3].away_palette,
                  "only away players use the other shirt palette bank");
            check(frame.sprites[2].y - frame.sprites[3].y == exe.u8(::bmh::layout::RealityMotion_FormatError_RuntimeField3Unsigned+uint32_t(tick)),
                  "Source eleven-frame flight curve separates ball and shadow");
        }
        const auto target = reality_project(exe,14,12);
        check(motion.frames[9].sprites[0].x == target.first - 1 &&
              motion.frames[9].sprites[0].y == target.second, "Source home-player offset at the actual endpoint");
        input.figures[0] = {true,21,12,21,12,0};input.figures[11] = {true,21,12,21,12,0};
        const auto stationary = reality_motion(input,exe);
        check(stationary.frames[0].sprites[0].cell == 29 && stationary.frames[0].sprites[1].cell == 30,
              "stationary home/away players use Source idle cells");
        input.numbers = false;
        const auto unnumbered = reality_motion(input,exe);
        for (const auto& frame : unnumbered.frames) for (size_t i = 0; i < frame.count; ++i)
            check(frame.sprites[i].number == 0, "saved C5 suppression removes every shirt label");
        ui::LiveMatchdayScreen board(data);
        ui::LiveMatchdayFrame occupied;occupied.minute=42;occupied.matches.resize(4);
        for (auto& row:occupied.matches) {row.home="TEST HOME";row.away="TEST AWAY";row.score_known=true;row.home_goals=3;}
        ui::LiveChanceProjection header;header.stadium="STADION";header.home="HOME";header.away="AWAY";
        const auto field_board=board.render_reality(occupied,header);
        check(field_board.pixels==board.render_reality({},header).pixels,
              "Reality uses the Source field backdrop, never lower live-table rows");
        const auto pitch=load_vga(ui::asset_path(data / "KB","133.VGA"));
        check(std::equal(field_board.pixels.begin()+320*129,field_board.pixels.end(),pitch.pixels.begin()+320*129),
              "pristine Reality field equals Original KB133 before every figures layer");
        Image backdrop(320,240);std::fill(backdrop.pixels.begin(),backdrop.pixels.end(),uint8_t(90));
        for (int style = 0; style < 4; ++style) {
            const ui::RealityFieldScreen screen(data,uint8_t(style));
            const auto image = screen.render(unnumbered.frames[0],backdrop);
            check(image.pixels == screen.render(unnumbered.frames[0],backdrop).pixels,
                  "repeated Original-sprite drawing is deterministic");
            const int sy = reality_project(exe,21,12).second + 129;
            bool away = false;
            const auto center = reality_project(exe,21,12);
            for (int row = 0; row < 8; ++row) for (int column = 0; column < 14; ++column) {
                const auto pixel = raw_atlas.pixels[size_t(16+row)*251 + size_t(224+column)];
                if (pixel == 0 || pixel >= 16) continue;
                away = true;check(image.pixels[size_t(sy+row)*320+size_t(center.first+1+column)] == pixel+16,
                                  "Original away idle sprite palette bank");
            }
            check(away, "away Original sprite has palette pixels");
            RealityMotionFrame isolated;isolated.count = 1;isolated.sprites[0] = {11,20,20,30,0,true};
            const auto transparent = screen.render(isolated,backdrop);
            check(transparent.pixels[149*320+20] == 90,
                  "zero-index Original sprite corner preserves the backdrop instead of drawing a black rectangle");
            for (int yy = 0; yy < 129; ++yy) for (int xx = 0; xx < 320; ++xx)
                check(image.pixels[size_t(yy)*320+size_t(xx)] == 90, "Reality figures preserve header/reporter area");
            RealityMotionFrame numbered;numbered.count = 1;
            numbered.sprites[0] = {9,20,20,29,10,false};
            const auto label = screen.render(numbered,backdrop);
            for (int row = 0; row < 5; ++row) for (int column = 0; column < 3; ++column) {
                for (int place = 0; place < 2; ++place) {
                    const auto pixel = digits.pixels[size_t(16+row)*size_t(digits.width)+size_t(129+place*3+column)];
                    if (pixel != 0)
                        check(label.pixels[size_t(157+row)*320+size_t((place?25:29)+column)] == pixel,
                              "Source two-digit shirt number is centered with four-pixel advance");
                    else if (row == 4)
                        check(label.pixels[size_t(157+row)*320+size_t((place?25:29)+column)] == 90,
                              "small shirt label preserves transparent Source background");
                }
            }
        }
        draw_hook = nullptr;
        rejected([&] { reality_project(exe,-1,0); });
        rejected([&] { reality_project(exe,0,25); });
        rejected([&] { reality_direction(43,0); });
        input.figures[0].phase = 255;rejected([&] { reality_motion(input,exe); });
        std::puts("PASS: Source Reality projection, movement, eleven frames, depth, C5 labels, ball flight and Original sprites");
        return 0;
    } catch (const std::exception& e) { draw_hook = nullptr;std::fprintf(stderr,"FAIL: %s\n",e.what());return 1; }
}
