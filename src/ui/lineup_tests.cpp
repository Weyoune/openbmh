// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "../../tests/test_inputs.hpp"
#include "ui/lineup.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <utility>

#include "formats/fbm.hpp"
#include "formats/savestate.hpp"
#include "game/lineup.hpp"
#include "game/save.hpp"
#include "game/state.hpp"

using namespace bmh;
using namespace bmh::ui;

namespace {
void check(bool ok, const char* message) { if(!ok)throw std::runtime_error(message); }
void rejected(const std::function<void()>& action) {
    try { action(); } catch(const FormatError&) { return; }
    throw std::runtime_error("expected rejected lineup operation");
}
int source_draws = 0;
void count_source_draw(int) { ++source_draws; }
struct DrawHookGuard {
    void (*previous)(int) = draw_hook;
    DrawHookGuard() { source_draws = 0; draw_hook = count_source_draw; }
    ~DrawHookGuard() { draw_hook = previous; }
};
GameState fixture() {
    GameState g;
    for(auto [at,size]:std::vector<std::pair<uint32_t,size_t>>{{::bmh::layout::ActiveManager,1},{::bmh::layout::CareerSession_AnyOf_ExecutableField,8},{::bmh::layout::LeaguePresentationOptions,0xb4}})
        g.memory.add_block(at,size);
    g.memory.set16(::bmh::layout::CareerSession_AnyOf_ExecutableField,0x15);
    g.manager_teams={12}; g.manager_options.resize(20,0);
    g.manager_options[0]=1;
    g.squad_slots.resize(3096,0x0fff);g.players.resize(24);g.player_cards.resize(24);g.player_bonus.resize(24);
    g.tactics.resize(0x2940);g.player_name_offsets.resize(24);g.club_ids.resize(419);
    g.club_heap_78.resize(419*2);g.team_flags.resize(168);
    g.manager_records.resize(5*0xB0C);g.formations.resize(5*0x160);
    g.manager_records[0x290]=12;
    g.club_names={'B','A','Y','E','R','N',0};
    for(int row=0;row<24;++row){
        const auto n="PLAYER"+std::to_string(row);
        g.player_name_offsets[row]=uint16_t(g.player_names.size());
        g.player_names.insert(g.player_names.end(),n.begin(),n.end());g.player_names.push_back(0);
        g.squad_slots[12*24+row]=uint16_t(row|(row<11?0x8000:row<17?0x4000:0));
        g.players[row].w35c=50|(90<<7)|(80<<14);
        g.players[row].w364=uint32_t(row%4)|((row%16)<<21);
        g.player_cards[row]=uint16_t(row);g.player_bonus[row]=uint8_t(row);
        g.tactics[size_t(row)*0x58+0xf]=uint8_t(row+1);
    }
    return g;
}
void test_prepare_lineup_entry_modes() {
    constexpr int team=12;
    constexpr size_t first=size_t(team)*24;
    auto set_badge=[](GameState& g,uint16_t word) {
        g.club_heap_78[size_t(team)*2]=uint8_t(word);
        g.club_heap_78[size_t(team)*2+1]=uint8_t(word>>8);
    };
    auto marker_fixture=[](GameState& g) {
        for(size_t row=0;row<24;++row)g.squad_slots[first+row]&=0x3fff;
        g.squad_slots[first+16]|=0x4000; // last Original-marked row is 16
    };
    auto expected_native=[](const GameState& input) {
        GameState expected=input;
        for(size_t row=0;row<=16;++row)
            expected.squad_slots[first+row]=uint16_t((expected.squad_slots[first+row]&0x3fff)|
                (row<11?0x8000:0x4000));
        return save_stream(expected);
    };
    for(const auto [indoor,badge_indoor]:std::array<std::pair<bool,bool>,4>{{
            {false,false},{false,true},{true,false},{true,true}}}) {
        auto g=fixture();g.indoor=indoor;marker_fixture(g);
        const uint16_t badge=uint16_t(0x0045|(badge_indoor?0x0200:0));set_badge(g,badge);
        const auto before_badge=g.club_heap_78;
        const auto native_expected=expected_native(g);
        const auto save_before=save_stream(g);
        const auto rng_before=source_draws;
        DrawHookGuard draws;
        prepare_lineup_entry(g,team);
        check(save_stream(g)==native_expected && native_expected!=save_before,
              "Source entry only normalizes marker bits through the last marked row");
        check(g.club_heap_78==before_badge,
              "lineup entry does not repair or otherwise mutate the stored club badge");
        check(source_draws==0 && rng_before==0,
              "lineup entry consumes no observed GameRandom or WatcomRandom draws");
    }

    // A valid stored assistant formation skips legacy routine's manual bit-9
    // normalization. legacy routine still accepts the subsequent indoor entry.
    {
        auto g=fixture();g.indoor=true;set_badge(g,0x0045);marker_fixture(g);
        g.manager_options[0]=1;
        const auto badge_before=g.club_heap_78;
        DrawHookGuard draws;
        auto_lineup(g,team,0xe4);
        check(g.club_heap_78==badge_before && ((uint16_t(g.club_heap_78[team*2]|
              uint16_t(g.club_heap_78[team*2+1])<<8)>>9)&1)==0,
              "assistant-manager branch retains its mismatched stored bit 9");
        prepare_lineup_entry(g,team);
        check((g.squad_slots[first]&0xc000)==0x8000 && source_draws==0,
              "assistant-managed indoor entry proceeds and stays RNG-neutral");
    }

    // The manual-only branch keeps its Source mode correction before the same
    // entry helper. This test prevents deleting that separate correction.
    {
        auto g=fixture();g.indoor=true;set_badge(g,0xA145);marker_fixture(g);
        g.manager_options[0]=0;
        DrawHookGuard draws;
        auto_lineup(g,team,0xe4);
        const uint16_t badge=uint16_t(g.club_heap_78[team*2]|
                                      uint16_t(g.club_heap_78[team*2+1])<<8);
        check(badge==uint16_t((0xA145&0x01ff)|0x0200),
              "manual mismatch retains legacy routine bit-9 normalization");
        prepare_lineup_entry(g,team);
        check(source_draws==0,"manual normalization and entry consume no observed RNG draws");
    }

    // Removing the mode gate must not weaken the existing compact-roster and
    // player-ID validation. Rejections happen before any marker write.
    for(int bad_case=0;bad_case<3;++bad_case) {
        auto g=fixture();g.indoor=true;set_badge(g,0x0045);marker_fixture(g);
        if(bad_case==0)g.squad_slots[first+4]=0x0fff; // sparse IDs
        else if(bad_case==1)for(size_t row=10;row<24;++row)g.squad_slots[first+row]=0x0fff; // only ten players
        else g.squad_slots[first+4]=0x03e8; // ID beyond the player table
        const auto before=save_stream(g);
        bool threw=false;
        try { prepare_lineup_entry(g,team); } catch(const std::invalid_argument&) { threw=true; }
        check(threw && save_stream(g)==before,
              "indoor mismatch does not bypass sparse, minimum-roster or player-ID guards");
    }
    auto outside=fixture();
    const auto outside_before=save_stream(outside);
    bool bounds_threw=false;
    try { prepare_lineup_entry(outside,138); } catch(const std::invalid_argument&) { bounds_threw=true; }
    check(bounds_threw && save_stream(outside)==outside_before,
          "removing the badge-mode check leaves manager/team bounds validation intact");
}
}

int main(int argc,char**argv) {
    try {
        if(argc<2)throw std::runtime_error("original data directory required");
        test_prepare_lineup_entry_modes();
        const std::filesystem::path data=argv[1];const auto exe=load_le(asset_path(data,"BMH.exe"));
        auto g=fixture();
        const auto squads=g.squad_slots;const auto records=g.tactics;const auto names=g.player_names;
        const auto options=g.manager_options;
        auto state=lineup_from_game(g,exe,7);
        check(state.manager_slot==0&&state.runtime_club==12&&state.revision==7,"authoritative manager identity");
        check(state.source=="engine_projection"&&!state.entry_prepared,"projection never claims original entry effects");
        check(state.players.size()==24&&state.players[13].name=="PLAYER13"&&state.players[13].board[0xf]==14,"owned names and tactics projection");
        check(state.players[13].cards==13&&state.players[13].bonus==13,"typed player card and bonus projection");
        check(state.players[0].attributes.condition()==80&&state.players[0].attributes.strength()==85,"original player display strength");
        check(g.squad_slots==squads&&g.tactics==records&&g.player_names==names&&g.manager_options==options,"projection has no engine writes");
        g.player_names[0]='X';g.tactics[0xf]=99;
        check(state.players[0].name=="PLAYER0"&&state.players[0].board[0xf]==1,"projection owns its data");
        g=fixture();
        g.memory.set8(::bmh::layout::ActiveManager,4);rejected([&]{lineup_from_game(g,exe,8);});
        g.memory.set8(::bmh::layout::ActiveManager,0);g.manager_teams[0]=138;rejected([&]{lineup_from_game(g,exe,8);});
        g.manager_teams[0]=12;g.player_name_offsets[0]=uint16_t(g.player_names.size());rejected([&]{lineup_from_game(g,exe,8);});
        g=fixture();g.player_names.pop_back();rejected([&]{lineup_from_game(g,exe,8);});

        LineupScreen entry(data,state);
        const auto prepare=entry.initialize();
        check(prepare.kind==LineupRequestKind::prepare_lineup&&prepare.manager_slot==0&&prepare.runtime_club==12&&prepare.revision==7,"entry command carries exact context");
        check(entry.initialize().kind==LineupRequestKind::none&&entry.click(20,33).kind==LineupRequestKind::none&&!entry.selected_row(),"pending entry blocks input/repeat");
        auto updated=state;updated.revision=8;
        rejected([&]{entry.acknowledge(updated);});
        check(entry.pending().has_value(),"failed entry acknowledgement preserves pending");
        updated.entry_prepared=true;entry.acknowledge(updated);
        check(!entry.pending()&&entry.state().entry_prepared&&entry.initialize().kind==LineupRequestKind::none,"prepared entry is not repeated");

        LineupScreen list(data,updated);const auto before=list.render().pixels;
        check(list.render().pixels==before,"render is pure");
        check(list.move(20,33)&&list.render().pixels!=before,"player name hover is local visual state");
        check(!list.move(20,33)&&list.move(306,228)&&list.render().pixels==before,"stable hover and leaving the name column");
        check(list.click(20,33).kind==LineupRequestKind::none&&list.selected_row()==0,"first click is local selection");
        const auto swap=list.click(20,40);
        check(swap.kind==LineupRequestKind::swap_players&&swap.first_row==0&&swap.second_row==1&&swap.first_player==0&&swap.second_player==1,
              "two name clicks request exact player exchange");
        check(swap.first_slot==0x8000&&swap.second_slot==0x8001&&swap.revision==8,"command carries optimistic concurrency slot words");
        check(list.state().players[0].player_id==0&&list.state().players[1].player_id==1,"pending exchange does not reorder projection");
        check(list.click(20,47).kind==LineupRequestKind::none&&list.key("ESC").kind==LineupRequestKind::none&&!list.close_requested(),"pending exchange blocks repeats and exit");
        rejected([&]{list.acknowledge(updated);});
        auto wrong=updated;wrong.revision=9;wrong.manager_slot=1;rejected([&]{list.acknowledge(wrong);});
        wrong=updated;wrong.revision=9;wrong.runtime_club=11;rejected([&]{list.acknowledge(wrong);});
        wrong=updated;wrong.revision=9;wrong.players[0].player_id=4;rejected([&]{list.acknowledge(wrong);});
        list.reject_action("engine rejected selection");
        check(!list.pending()&&!list.selected_row()&&list.render().pixels==before,"rejection preserves original squad/frame");
        rejected([&]{list.reject_action("no request");});
        const auto flags=list.state().saved_display_flags;
        list.click(28,219);
        check(list.state().view==LineupView::data_b&&list.state().saved_display_flags==flags&&!list.pending(),"data view toggle is local and preserves saved flags");
        check(list.click(70,33).kind==LineupRequestKind::open_player_info,"click behind player name requests info");
        list.reject_action("player info not yet implemented");
        check(list.click(85,219).kind==LineupRequestKind::open_pitch,"pitch navigation remains an explicit request");
        list.reject_action("pitch renderer not yet implemented");
        list.click(20,33);list.click(0,0,true);
        check(!list.selected_row()&&!list.close_requested(),"right click cancels local selection");
        const auto idle_sheet=list.state_json();const auto idle_pixels=list.render().pixels;
        list.key("ESC");list.key("ESCAPE");
        check(list.state_json()==idle_sheet&&list.render().pixels==idle_pixels&&!list.close_requested(),
              "Original desktop data sheet ignores Escape without state or pixel changes");
        list.click(0,0,true);check(list.close_requested(),"next right click closes menu");

        g=fixture();g.manager_teams.push_back(11);g.memory.set8(::bmh::layout::ActiveManager,1);g.memory.set16(::bmh::layout::FixtureSecondManagerClub,0x16);
        g.manager_options[5]=2;
        // Explicit empty second-manager squad is a valid state; no guessed manager order.
        auto second=lineup_from_game(g,exe,19);
        check(second.manager_slot==1&&second.runtime_club==11&&second.view==LineupView::data_b&&second.automatic_formation==2,"manager context remains separate");

        if(argc>2&&!std::string(argv[2]).empty()) {
            const auto bytes=read_file(argv[2]);const auto savegame=load_savegame(argv[2]);
            check(savegame.checksum_ok&&savegame.version=="V2.24","reference container validation");
            SaveState save(savegame.stream,{exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4)});
            auto reference=lineup_from_save(save,exe,3);
            check(reference.runtime_club==12&&reference.players.size()==24&&reference.club_name==bmh::test::expected_text("lineup_tests.0"),"original reference identity");
            check(reference.players[0].player_id>=0&&reference.source=="original_save_read_only"&&!reference.entry_prepared,"reference is read-only and requires explicit live preparation");
            LineupScreen original(data,reference);check(original.render().pixels.size()==320*240,"original assets render both data lists");
            original.click(28,219);check(original.render().pixels.size()==320*240,"other original list view renders");
            check(read_file(argv[2])==bytes,"reference save unchanged");
        }
        std::cout<<"Lineup projection, entry, command and rendering checks passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
