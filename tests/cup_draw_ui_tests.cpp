#include "runtime_fixture_tables.hpp"
#include "test_inputs.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/cup_draw.hpp"

#include <algorithm>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>

#include "formats/fbm.hpp"
#include "formats/m3d.hpp"
#include "formats/palette.hpp"
#include "formats/savestate.hpp"
#include "game/cup.hpp"
#include "game/save.hpp"
#include "game/state.hpp"
#include "ui/manager.hpp"

namespace {
void check(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
struct Native {
    bmh::GameState game; bmh::LeImage exe; bmh::TextDb texts; bmh::Bytes native_stream;
    Native(const std::filesystem::path& data,const std::filesystem::path& save)
      :exe(bmh::test::load_executable(bmh::ui::asset_path(data,"BMH.exe"))),texts(bmh::load_textd(bmh::ui::asset_path(data,"TEXTD.BLK"))) {
        const auto fbm=bmh::load_savegame(save);check(fbm.checksum_ok,"native fixture checksum");
        native_stream=fbm.stream;game=bmh::GameState::from_save(bmh::SaveState(native_stream));game.texts=&texts;
    }
    bmh::SeasonCupDrawEvent event(bmh::SeasonCupDrawPhase phase) const {
        bmh::SeasonCupDrawEvent e;e.phase=phase;e.entry_address=bmh::kCupTable;
        e.competition=game.memory.u8(e.entry_address+0x18);e.round=bmh::cup_round(game.memory,0);
        e.offset=bmh::cup_round_offset(game.memory,0,e.round);
        e.count=size_t(game.memory.u8(e.entry_address+0x1a))>>e.round;
        const uint32_t list=game.memory.pointer(e.entry_address)+uint32_t(e.offset)*2;
        for(size_t i=0;i<e.count;++i)e.participants.push_back(game.memory.u16(list+uint32_t(i)*2));
        return e;
    }
};
}

int main(int argc,char** argv)try{
    if(argc<3)throw std::runtime_error("expected Original data directory and fixture directory [native season input]");
    if(!*argv[1]){std::cout<<"Cup draw UI tests skipped (Original data not configured)\n";return 77;}
    const std::filesystem::path data=argv[1],fixtures=argv[2];
    Native draw_native(data,fixtures/"gameend/draw.FBM");
    const auto original_stream=draw_native.native_stream;
    const auto original_club_names=draw_native.game.club_names;
    static int draw_hook_calls=0;
    const auto previous_draw_hook=bmh::draw_hook;
    bmh::draw_hook=+[](int){++draw_hook_calls;};
    auto draw_event=draw_native.event(bmh::SeasonCupDrawPhase::draw);
    auto draw_projection=bmh::ui::cup_draw_from_game(draw_native.game,draw_event,draw_native.exe,draw_native.texts);
    check(draw_projection.count==64&&draw_projection.clubs.size()==64,"native draw has 64 owned club snapshots");
    check(draw_projection.source_eligible,"native national-cup draw is Source eligible");
    check(draw_projection.heading==bmh::test::expected_text("cup_draw_ui_tests-external-1").c_str()&&draw_projection.footer==bmh::test::expected_text("cup_draw_ui_tests-external-2").c_str(),"Original draw labels");
    // Exercise I9 through its real saved-byte projection and initial animation
    // poll. The Source small-value branch differs from the general delay scale.
    const auto saved_delay=draw_native.game.memory.u8(::bmh::layout::CupDraw_FormatError_RuntimeField);
    const auto delay_native_before=bmh::save_stream(draw_native.game);
    const uint8_t raw_delays[]={0,1,2,5,6,199};
    const int expected_delays[]={0,0,20,40,120,3980};
    for(size_t i=0;i<std::size(raw_delays);++i) {
        draw_native.game.memory.set8(::bmh::layout::CupDraw_FormatError_RuntimeField,raw_delays[i]);
        const auto changed_native=bmh::save_stream(draw_native.game);
        auto delayed=bmh::ui::cup_draw_from_game(draw_native.game,draw_event,draw_native.exe,draw_native.texts);
        bmh::ui::CupDrawScreen delayed_animation(data,std::move(delayed));
        check(delayed_animation.advance_step()&&delayed_animation.input_poll_needed()&&
              delayed_animation.option_delay_milliseconds()==expected_delays[i],
              "saved I9 controls the actual first animation poll with Source small-value scaling");
        check(bmh::save_stream(draw_native.game)==changed_native,
              "reading the I9 animation delay does not change the native save");
    }
    draw_native.game.memory.set8(::bmh::layout::CupDraw_FormatError_RuntimeField,saved_delay);
    check(bmh::save_stream(draw_native.game)==delay_native_before,
          "I9 projection checks restore the complete original native input");
    const auto first_name=draw_projection.clubs.front().original_name;
    auto draw_image=bmh::ui::CupDrawScreen(data,draw_projection).render();
    check(draw_image.width==320&&draw_image.height==240,"draw has native 320x240 canvas");
    check(draw_projection.clubs.front().original_name==first_name,"projection owns club name bytes");
    bmh::ui::CupDrawScreen owned_screen(data,draw_projection);
    const auto owned_pixels=owned_screen.render().pixels;
    const size_t first_club_name_offset=draw_native.game.club_ids[draw_projection.clubs.front().runtime_id];
    const uint8_t original_name_byte=draw_native.game.club_names[first_club_name_offset];
    draw_native.game.club_names[first_club_name_offset]^=0x20;
    check(draw_projection.clubs.front().original_name==first_name&&owned_screen.render().pixels==owned_pixels,
          "projection and renderer own their text after the source name pool changes");
    draw_native.game.club_names[first_club_name_offset]=original_name_byte;

    bmh::ui::CupDrawScreen animation(data,draw_projection);
    size_t waits=0,presented_waits=0,flushes=animation.queue_flush_needed()?1:0;
    std::optional<bmh::Image> first_flicker,second_flicker,third_flicker;
    for(int guard=0;guard<4000&&animation.advance_step();++guard){
        if(animation.input_poll_needed()){
            // Exercise an empty Source queue at all initial and between-club polls.
            animation.source_input(0);
        }
        if(animation.held_mouse_check_needed())animation.source_held_mouse(0);
        if(animation.wait_milliseconds()>0){++waits;if(animation.present_needed())++presented_waits;
            if(waits==6)first_flicker=animation.render();
            if(waits==7)second_flicker=animation.render();
            if(waits==8)third_flicker=animation.render();}
        if(animation.queue_flush_needed())++flushes;
    }
    check(animation.reveal_complete()&&!animation.reveal_aborted(),"uninterrupted draw reaches fresh-ACK boundary");
    check(animation.revealed_count()==64,"reveal counter preserves total after rolling window trims");
    check(waits==64u*14u,"Source scripts execute fourteen waits for every club");
    check(presented_waits==64u*14u,"all 14 Source wait markers present, including legacy routine(0) swaps");
    check(first_flicker&&second_flicker&&third_flicker&&
          first_flicker->pixels!=second_flicker->pixels&&first_flicker->pixels==third_flicker->pixels,
          "Source token-101 waits alternate the owned visible buffer without copy-back");
    check(flushes==65,"queue flushes only at initial and committed-name presentation boundaries");
    check(draw_native.native_stream==original_stream&&draw_native.game.club_names==original_club_names,
          "native save bytes and restored source names are unchanged through render and step");
    check(draw_hook_calls==0,"draw-only UI never invokes a Core callback");

    bmh::ui::CupDrawScreen skipped(data,draw_projection);
    for(int i=0;i<10;++i){check(skipped.advance_step(),"initial speed-delay poll remains active");
        if(skipped.input_poll_needed()&&i==0)skipped.source_input(8);}
    check(skipped.reveal_aborted()&&skipped.revealed_count()==0,"initial queued skip waits all ten iterations, then aborts reveal");

    auto overview_native=Native(data,fixtures/"gameend/overview.FBM");
    auto overview_event=overview_native.event(bmh::SeasonCupDrawPhase::overview);
    auto overview_projection=bmh::ui::cup_draw_from_game(overview_native.game,overview_event,overview_native.exe,overview_native.texts);
    bmh::ui::CupDrawScreen overview(data,overview_projection);
    const auto table=overview.render();
    check(table.width==320&&table.height==240,"overview has native canvas");
    const int baseline=((64-int(overview_projection.count))/2)*3+26;
    check(overview.hit_test_team(1,baseline-4)==overview_projection.clubs[0].runtime_id,"home hit interior");
    check(!overview.hit_test_team(0,baseline-4)&&!overview.hit_test_team(102,baseline-4),"home hit strict x edges");
    check(!overview.hit_test_team(103,baseline-4)&&overview.hit_test_team(104,baseline-4)==overview_projection.clubs[1].runtime_id,"away hit strict x edges");
    check(!overview.hit_test_team(104,baseline-6)&&!overview.hit_test_team(104,baseline),"team hit strict y edges");

    if(argc>=4) {
        Native group_native(data,argv[3]);
        int group_entry=-1;
        for(int i=0;i<bmh::kCupEntries;++i) {
            const uint32_t at=bmh::cup_entry_address(i);
            const int comp=group_native.game.memory.u8(at+0x18);
            if(comp>=4&&comp<=7&&(group_native.game.memory.u8(at+0x14)&4)==0&&
               (group_native.game.memory.u8(at+0x1a)==4||group_native.game.memory.u8(at+0x1a)==6)) {group_entry=i;break;}
        }
        check(group_entry>=0,"native season input contains a real initial international group entry");
        const uint32_t at=bmh::cup_entry_address(group_entry),list=group_native.game.memory.pointer(at);
        bmh::SeasonCupDrawEvent group_event;group_event.entry_address=at;group_event.competition=group_native.game.memory.u8(at+0x18);
        group_event.flags=group_native.game.memory.u8(at+0x14);group_event.group_stage=true;
        group_event.group_index=0;group_event.group_count=group_native.game.memory.u8(at+0x1e)&0xf;
        group_event.round=bmh::cup_round(group_native.game.memory,group_entry);group_event.offset=0;
        group_event.count=group_native.game.memory.u8(at+0x1a);
        for(size_t i=0;i<group_event.count;++i)group_event.participants.push_back(group_native.game.memory.u16(list+uint32_t(i)*2));
        const auto group_stream=bmh::save_stream(group_native.game);
        const auto source_texts=group_native.texts.strings;
        group_event.phase=bmh::SeasonCupDrawPhase::draw;
        auto group_draw_projection=bmh::ui::cup_draw_from_game(group_native.game,group_event,group_native.exe,group_native.texts);
        check(group_draw_projection.clubs.size()==group_event.count&&group_draw_projection.group_stage,"group reveal owns actual runtime IDs and metadata");
        bmh::ui::CupDrawScreen group_reveal(data,group_draw_projection);
        for(int guard=0;guard<5000&&group_reveal.advance_step();++guard) {
            if(group_reveal.input_poll_needed())group_reveal.source_input(0);
            if(group_reveal.held_mouse_check_needed())group_reveal.source_held_mouse(0);
        }
        check(group_reveal.reveal_complete()&&group_reveal.revealed_count()==group_event.count,"group reveal completes every participant with rolling rows");
        group_event.phase=bmh::SeasonCupDrawPhase::overview;
        auto group_projection=bmh::ui::cup_draw_from_game(group_native.game,group_event,group_native.exe,group_native.texts);
        check(group_projection.group_rows.size()==group_event.count&&group_projection.group_fixtures.size()==group_event.count/2,"group overview owns legacy routine rows and source-round fixtures");
        check(group_projection.group_caption.size()>25&&group_projection.group_caption[25]==char(group_event.group_index==0?'1':'0'+group_event.group_index),"Source group caption digit is copied into an owned string");
        check(group_native.texts.strings==source_texts,"group projection never mutates shared TEXTD globals");
        for(const auto& pair:group_projection.group_fixtures)
            check(std::find(group_event.participants.begin(),group_event.participants.end(),pair[0])!=group_event.participants.end()&&
                  std::find(group_event.participants.begin(),group_event.participants.end(),pair[1])!=group_event.participants.end(),
                  "round schedule positions resolve through the native group team list");
        bmh::ui::CupDrawScreen group_overview(data,group_projection);
        const auto group_image=group_overview.render();
        check(group_image.width==320&&group_image.height==240,"source group overview renders to native canvas");
        check(group_overview.hit_test_team(6,34)==group_projection.group_fixtures.front()[0],"group fixture strict interior maps to runtime club ID");
        check(!group_overview.hit_test_team(0,34)&&!group_overview.hit_test_team(319,34),"group fixture outer x bounds are excluded");
        check(group_overview.hit_test_team(127,34)==group_projection.group_fixtures.front()[1],"home right boundary remains inside the overlapping away hitbox");
        check(group_overview.hit_test_team(125,34)==group_projection.group_fixtures.front()[0]&&
              group_overview.hit_test_team(126,34)==group_projection.group_fixtures.front()[0],"home wins the Source overlap before the away hitbox");
        check(!group_overview.hit_test_team(6,33)&&!group_overview.hit_test_team(6,39),"group fixture hitbox uses strict Source y bounds");
        check(!group_overview.hit_test_team(30,132),"bottom standings are not Source fixture hit targets");
        check(bmh::save_stream(group_native.game)==group_stream,"group reveal and overview do not change native game state");
    }
    std::cout<<"Cup draw Source projection, framebuffer timeline, rolling reveal count, and overview hitboxes passed\n";
    bmh::draw_hook=previous_draw_hook;
    return 0;
}catch(const std::exception&e){std::cerr<<"cup_draw_tests: "<<e.what()<<'\n';return 1;}
