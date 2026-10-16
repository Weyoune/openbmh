#include "../../tests/test_inputs.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/achievements.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include "game/messages.hpp"

namespace {
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: ui_achievements_tests ORIGINAL_DATA");
        const std::filesystem::path data(argv[1]);
        const auto exe = bmh::load_le(bmh::ui::asset_path(data, "BMH.exe"));
        const auto texts = bmh::load_textd(bmh::ui::asset_path(data, "TEXTD.BLK"));
        bmh::GameState game;
        game.manager_records.assign(4 * 0xb0c, 0);
        game.manager_teams = {0, 1, 2, 3};
        game.club_ids = {0, 1, 2, 3};
        game.club_names = {'B','a','y','e','r','n',0};
        game.manager_records[0x2aa] = 4; // format field, manager's first season offset.
        game.manager_records[0xda] = 1;
        game.manager_records[0xdb] = 1;
        game.manager_records[0xdc] = 0x21;
        game.manager_records[0xda + 3] = 5;
        game.manager_records[0xdb + 3] = 7;
        game.manager_records[0xdc + 3] = 0x43;
        game.manager_records[0xda + 6] = 0xff;
        game.season = 2;
        game.league_rounds = std::vector<uint8_t>(9, 0);
        game.texts = &texts;

        check(!bmh::ui::achievements_available(game), "legacy routine blocks before first recorded league round");
        game.league_rounds[0] = 1;
        check(bmh::ui::achievements_available(game), "cold-loaded league_rounds[0] opens Source gate");
        bmh::ui::DeskRequest owner;
        owner.kind = bmh::ui::DeskRequestKind::open_menu;
        owner.menu_kind = 4;
        owner.manager_slot = 0;
        owner.item_id = 12;
        owner.revision = 9;
        const bmh::Image underlay(320, 240, 0x40);
        const auto state = bmh::ui::achievements_from_game(game, exe, owner, underlay);
        check(state.season_count == 3 && state.selected_season == 2,
              "Source displays seasons 0 through format field, capped by the 50-entry history");
        check(state.seasons[0].league_result == 1 && state.seasons[0].cup_result == 1 &&
              state.seasons[0].europe_result == 0x21 && state.seasons[1].cup_result == 7 &&
              state.seasons[1].europe_result == 0x43 && state.seasons[2].league_result == 0xff,
              "projection preserves all three raw manager-history bytes per year");
        check(state.seasons[0].graph_y == int(uint8_t(0xa4 - 2 - 2 - (0x48 - 1) / 2)) &&
              state.seasons[0].season_label == format_number(game,int(exe.u16(::bmh::layout::Achievements_FormatError_RuntimeField2))+4,0xc4)+exe.cstring(::bmh::layout::Achievements_FormatError_RuntimeField) &&
              state.seasons[2].league_text.empty(),
              "Source graph coordinate, 16-bit year base, and 0xff prior-manager sentinel");
        bmh::ui::AchievementsScreen screen(data, state);
        auto frame = screen.render();
        check(frame.width == 320 && frame.height == 240 && frame.pixels != underlay.pixels,
              "Original KB129/SpriteFont and KB56 detail-panel renderer draws achievements");
        check(frame.pixels[size_t(int(exe.u8(::bmh::layout::Achievements_Fill_RuntimeField4))-2)*size_t(frame.width)+0x43]==exe.u8(::bmh::layout::Achievements_Fill_RuntimeField2),
              "legacy routine uses low-word color index 0 for the first nonzero league band");
        const auto header = bmh::load_font(bmh::ui::asset_path(data / "F", "BMH2.FNT"));
        const auto& heading = texts[bmh::text_id(::bmh::layout::LeagueHeaderTable)];
        bmh::Image heading_mask(header.text_width(heading), header.height);
        header.draw(heading_mask, 0, 0, heading, 1);
        const int heading_x = 319 / 2 - heading_mask.width / 2;
        const int heading_y = int(exe.u8(::bmh::layout::Achievements_Fill_RuntimeField4)) + 9 - header.baseline;
        int heading_ink = 0;
        for(int y=0;y<heading_mask.height;++y)for(int x=0;x<heading_mask.width;++x)
            if(heading_mask.at(x,y)){
                check(frame.at(heading_x+x,heading_y+y)==0x6b,
                      "legacy routine heading uses font 2 and Source inclusive-range centering");
                ++heading_ink;
            }
        check(heading_ink>0 && frame.at(0x108,int(exe.u8(::bmh::layout::Achievements_Fill_RuntimeField4)))==0x5f,
              "Source graph baseline includes its right endpoint");

        // WEEK196's first three history triples straddle the signed-char boundary.
        // A signed 132 -> -124 trace to 123 crossed the top of the graph and
        // produced the user's black pixels at 67,29 and 71,29.
        auto boundary_game=game;
        const std::array<uint8_t,9> history{{11,1,0,10,5,0,18,1,0}};
        std::copy(history.begin(),history.end(),boundary_game.manager_records.begin()+0xda);
        auto boundary_state=bmh::ui::achievements_from_game(boundary_game,exe,owner,underlay);
        check(boundary_state.seasons[0].graph_y==132 &&
              boundary_state.seasons[1].graph_y==123 && boundary_state.seasons[2].graph_y==135,
              "legacy routine retains unsigned byte y coordinates across 127/128");
        boundary_state.selected_season=-1;
        bmh::ui::AchievementsScreen boundary_screen(data,boundary_state);
        auto boundary_frame=boundary_screen.render();
        auto panel=bmh::load_vga(bmh::ui::asset_path(data/"KB","56.VGA"));
        check(boundary_frame.at(67,29)==panel.at(66,9) &&
              boundary_frame.at(71,29)==panel.at(70,9),
              "unsigned Source graph traces leave the upper-panel pixels intact");
        auto check_traces=[&](const std::array<int,3>& points){
            auto traces=boundary_state;
            for(int i=0;i<3;++i){
                traces.seasons[size_t(i)].league_result=0xfe;
                traces.seasons[size_t(i)].cup_result=0;
                traces.seasons[size_t(i)].europe_result=0;
                traces.seasons[size_t(i)].graph_y=points[size_t(i)];
            }
            bmh::ui::AchievementsScreen trace_screen(data,traces);
            auto actual=trace_screen.render();
            auto blank=traces;
            for(int i=0;i<3;++i)blank.seasons[size_t(i)].graph_y=240;
            bmh::ui::AchievementsScreen blank_screen(data,blank);
            auto expected=blank_screen.render();
            // Closed-form positions are an independent oracle for legacy routine's
            // zero accumulator: minor displacement=floor(step*minor/major).
            for(int season=1;season<3;++season)for(int line=0;line<3;++line){
                const int x0=0x41+(season-1)*4;
                const int y0=uint8_t(points[size_t(season-1)]+line);
                const int y1=uint8_t(points[size_t(season)]+line);
                const int dy=std::abs(y1-y0),sy=y1<y0?-1:1;
                const int major=std::max(4,dy);
                for(int step=0;step<=major;++step){
                    const int x=x0+(dy<4?step:step*4/dy);
                    const int y=y0+sy*(dy<4?step*dy/4:step);
                    expected.at(x,y)=exe.u8(::bmh::layout::Achievements_Fill_RuntimeField+uint32_t(line));
                }
            }
            // legacy routine draws baselines after all traces.
            for(uint32_t address:{::bmh::layout::Achievements_Fill_RuntimeField4Unsigned,::bmh::layout::Achievements_Fill_RuntimeField5Unsigned,::bmh::layout::Achievements_Fill_RuntimeField6Unsigned})
                for(int x=0x3f;x<=0x108;++x)expected.at(x,exe.u8(address))=0x5f;
            for(int y=100;y<150;++y)for(int x=0x41;x<=0x49;++x)
                check(actual.at(x,y)==expected.at(x,y),
                      "achievement trace pixels follow legacy routine's zero-accumulator raster");
        };
        check_traces({132,123,135}); // WEEK196: steep ascending and descending traces.
        check_traces({100,101,100}); // Shallow traces select the other major-axis branch.
        check_traces({120,120,120}); // Horizontal traces include both endpoints.
        check(screen.move(0x41,0x63)&&screen.state().selected_season==0&&
              !screen.move(0x41+4*3,0x63)&&screen.state().selected_season==0&&
              screen.move(0x40,0x63)&&screen.state().selected_season==-1,
              "Source hover selects valid seasons, preserves beyond-history columns, and clears outside");
        check(screen.pointer_down(0x41 + 4, 0x63).kind == bmh::ui::AchievementsRequestKind::none &&
              screen.state().selected_season == 1,
              "click selection remains compatible with the Source four-pixel season column");
        const auto close = screen.pointer_down(280, 220, true);
        check(close.kind == bmh::ui::AchievementsRequestKind::close && close.owner == owner,
              "Source right-button event closes only the matching desk-owned view");
        check(texts[1190] == "Erfolge" && texts[1191] == bmh::test::expected_text("achievements_tests-external-1").c_str(),
              "German Original title and historical sentinel message are loaded from TEXTD");
        std::cout << "ui achievements tests: source projection, gate, renderer, and input passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ui achievements tests: " << e.what() << '\n';
        return 1;
    }
}
