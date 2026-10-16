#include "runtime_fixture_tables.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include "formats/fbm.hpp"
#include "formats/textd.hpp"
#include "game/messages.hpp"
#include "game/reality_reporter.hpp"
#include "game/save.hpp"
using namespace bmh;
namespace {
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
// Independent Source cursor interpreter. Unlike the product's parsed tree,
// this walks # delimiters and skips unchosen nested expressions (legacy routine).
size_t skip(std::string_view text,size_t at) {
    while(!(at&&text[at]=='x'&&text[at-1]=='#')) {
        if(text[at]=='%'&&text[at+1]=='x')at=skip(text,at+4);
        ++at;
    }
    return at;
}
std::string cursor(std::string_view text,size_t& at,const GameState& g,const RealitySetup& scene,
    const LeImage& exe,const RealityReporterFrame& f,const RealityReporterState& s,WatcomRandom& w) {
    std::string out;bool stop=false;
    while(at<text.size()&&text[at]!='#'&&!stop) {
        if(text[at]!='%'){out+=text[at++];continue;}
        const char token=text[at+1];size_t next=at+1;
        if(token=='x') {
            uint8_t choice=text[at+2]=='0'?uint8_t(w.range(1,text[at+3]-'0')):s.selectors[size_t(text[at+2]-'0'+10*(text[at+3]-'0'))];
            next=at+4;
            do {
                if(choice<2)out+=cursor(text,next,g,scene,exe,f,s,w);
                else {if(text[next]=='%'&&text[next+1]=='x')next=skip(text,next+4);while(text[++next]!='#')if(text[next]=='%'&&text[next+1]=='x')next=skip(text,next+4);}
                ++next;--choice;if(text[next-1]=='#'&&text[next]=='x')choice=0;
            }while(choice);
            next=skip(text,next);
        } else if(token=='0') {
            uint8_t metres=uint8_t((int(f.distance_before)*25-12+w.range(0,26))/10);
            if(metres>20)metres=uint8_t(metres/5*5);out+=format_number(g,metres,1);
        } else if(token=='a'||token=='b')out+=g.club_name(scene.teams[size_t(token=='a'?0:1)]);
        else if(token=='c'||token=='d')out+=g.player_name(token=='c'?s.current_player:f.receiver_player);
        else if(token=='f'||token=='z') {
            const int id=token=='f'?f.line_selected[size_t(1-f.player_side)]:f.opponent;
            out+=g.player_name(scene.players[size_t(id-1)].player);
        } else if(token=='s')out+=message_text(g,::bmh::layout::RealityReporter_Evaluate_TextPointer2); // referee0 fixture
        else if(token=='e')stop=true;
        at=next+1;
    }
    return out;
}
}
int main(int argc,char** argv){try{
    check(argc==2,"usage: reality_reporter_tests <Original data>");const auto data=std::filesystem::path(argv[1]);
    const auto exe=bmh::test::load_executable(data/"BMH.exe");
    const auto saved=load_savegame(data/"AUTOSAVE.FBM");const DbCounts counts{exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4)};
    auto game=GameState::from_save(SaveState(saved.stream,counts),counts);
    const auto texts=load_textd(data/"TEXTD.BLK");game.texts=&texts;const auto native=save_stream(game);
    RealitySetup scene;scene.teams={0,1};for(int i=0;i<22;++i)scene.players[size_t(i)].player=i;
    scene.grid.place(22,10,12,2);scene.grid.place(22,20,12,2);scene.grid.place(12,25,12,1);
    RealityReporterFrame frame;frame.player_side=0;frame.receiver=1;frame.receiver_player=1;frame.opponent=13;
    frame.line_selected={1,13};frame.distance_before=22;frame.incoming_bh=0;
    auto state=initialize_reality_reporter(exe);state.last_side=0;state.current_player=0;
    // 22 real Original templates x32 clocks x14 selector contexts, including
    // legal scene selector values. Unchosen nesting must draw nothing.
    int cases=0;
    for(int selection=0;selection<14;++selection)for(int code=0;code<22;++code)for(uint32_t seed=1;seed<=32;++seed) {
        state.selectors.fill(0);
        state.selectors[1]=uint8_t(selection?1+selection%2:0);
        state.selectors[2]=uint8_t(1+selection%2);state.selectors[3]=uint8_t(selection%3==2?4:1+selection%3);
        state.selectors[4]=uint8_t(1+selection%4);
        for(int i:{5,6,7,8,9,11})state.selectors[size_t(i)]=uint8_t(1+selection%2);
        state.selectors[12]=uint8_t(1+selection%3);
        const auto text=exe.string_at_pointer(::bmh::layout::RealityReporter_FormatError_TextPointer+uint32_t(code)*4);
        WatcomRandom actual(seed),expected(seed);size_t at=0;
        const auto wanted=cursor(text,at,game,scene,exe,frame,state,expected);
        const auto result=reality_reporter_text(game,scene,exe,frame,state,text,actual);
        check(result==wanted&&actual.seed()==expected.seed()&&actual.calls()==expected.calls(),"all Original templates equal direct Source cursor output and full Watcom state");++cases;
    }
    state.selectors.fill(2);WatcomRandom w(51);const auto a=reality_reporter_text(game,scene,exe,frame,state,"%x10%x05A#B#C#D#E#x#SECOND#x",w);
    check(a=="SECOND"&&!w.calls(),"unchosen nested random alternatives consume no draw");
    state.last_side=-1;
    check(reality_reporter_text(game,scene,exe,frame,state,"%a/%b",w)==game.club_name(0)+"/"+game.club_name(1),"Source club tokens are fixed home/away and do not require reporter history");
    state.last_side=1;
    check(reality_reporter_text(game,scene,exe,frame,state,"%a/%b",w)==game.club_name(0)+"/"+game.club_name(1),"away possession never swaps fixed team entity tokens");
    check(!message_text(game,::bmh::layout::RealityReporter_Evaluate_TextPointer2).empty()&&reality_reporter_text(game,scene,exe,frame,state,"%s",w)==message_text(game,::bmh::layout::RealityReporter_Evaluate_TextPointer2),"referee0 fallback is the loaded Original TEXTD.BLK text, not an empty EXE placeholder");
    frame.referee=1;const size_t offset=le16(game.master_database_metadata,2);std::string name;
    for(size_t i=offset;i<game.referee_names.size()&&game.referee_names[i];++i)name+=char(game.referee_names[i]);
    check(reality_reporter_text(game,scene,exe,frame,state,"%s",w)==message_text(game,::bmh::layout::RealityReporter_Evaluate_TextPointer)+name&&!w.calls(),"nonzero referee uses exact Source prefix and Native name pool without a variant roll");
    frame.referee=0;state.last_side=0;
    check(reality_reporter_text(game,scene,exe,frame,state,"BEFORE%eAFTER",w)=="BEFORE","Source e stops the current text segment");
    state=initialize_reality_reporter(exe);frame.action=2;frame.opponent=0;frame.card_kind=0;w=WatcomRandom(17);
    auto result=report_reality(game,scene,exe,frame,state,0,0,0,w);
    check(result.codes==std::vector<uint8_t>{2}&&state.last_owner==0&&state.last_player==0,"first dribble names its owner and commits Source history");
    check(state.selectors[3]==1,"x-dominant move uses caller BH0, not max(dx,dy)");
    const auto before=w.calls();result=report_reality(game,scene,exe,frame,state,0,0,0,w);
    check(result.text.empty()&&result.codes.empty()&&w.calls()==before,"identical non-shot action suppresses text and variant draws");
    result=report_reality(game,scene,exe,frame,state,1,0,0,w);
    check(result.codes==std::vector<uint8_t>({7,2})&&state.selectors[1]==1,"new ball owner emits pickup then pronoun for the action");
    frame.incoming_bh=255;report_reality(game,scene,exe,frame,state,1,0,0,w);check(state.selectors[3]==1,"BH is a signed Source char");
    frame.incoming_bh=12;report_reality(game,scene,exe,frame,state,1,0,0,w);check(state.selectors[3]==4,"incoming BH12 selects long pass class independently of horizontal distance");
    auto missing=frame;missing.incoming_bh.reset();bool boundary=false;
    try{report_reality(game,scene,exe,missing,state,1,0,0,w);}catch(const FormatError&){boundary=true;}
    check(boundary,"missing incoming BH remains an explicit Source context boundary");
    auto vertical=scene;vertical.grid.place(22,20,24,2);missing.action=2;missing.opponent=0;
    report_reality(game,vertical,exe,missing,state,1,0,0,w);check(state.selectors[3]==4,"dy>dx overwrites BH in the Source and needs no incoming register");
    state=initialize_reality_reporter(exe);frame.action=2;frame.opponent=13;frame.lost_duel=true;frame.incoming_bh=0;
    result=report_reality(game,scene,exe,frame,state,0,0,0,w);
    check(frame.action==9&&state.last_owner==12&&state.last_player==12,"lost duel gives Source possession history to the actual opponent");
    const auto prior=w.calls();frame.card_kind=3;result=report_reality(game,scene,exe,frame,state,0,0,21,w);
    check(result.suppressed&&result.text.empty()&&w.calls()==prior&&state.current_player==0,"explicit cue after action9 is suppressed but still assigns Source speaker");
    state.last_action=3;auto expected_card_random=w;
    result=report_reality(game,scene,exe,frame,state,0,0,21,w);
    size_t card_at=0;
    auto expected_card=cursor(exe.string_at_pointer(::bmh::layout::RealityReporter_FormatError_TextPointer+21*4),card_at,game,scene,exe,frame,state,expected_card_random);
    check(result.codes==std::vector<uint8_t>{21}&&state.selectors[12]==3&&result.text.at(0)==expected_card&&
          w.seed()==expected_card_random.seed()&&w.calls()==expected_card_random.calls(),
          "real card selector chooses the supplied red-card branch with independent cursor/RNG output");
    const auto selectors=state.selectors;const auto reset_calls=w.calls();
    report_reality(game,scene,exe,frame,state,100,0,0,w);
    check(state.last_side==-1&&state.last_owner==-1&&state.last_player==-1&&state.last_action==-1&&state.selectors==selectors&&w.calls()==reset_calls,"Source reset clears only four history words and draws nothing");
    state=initialize_reality_reporter(exe);frame.action=1;frame.opponent=0;frame.lost_duel=false;
    frame.distance_before=10;frame.distance_after=3;frame.leading_run=true;frame.offside=true;frame.line_selected={0,0};
    frame.blocked_attempt=false;frame.inaccurate=false;scene.grid.stage_position(1,25,12);
    result=report_reality(game,scene,exe,frame,state,0,0,0,w);
    check(result.codes==std::vector<uint8_t>({1,5,14})&&frame.opponent==13&&state.last_owner==1,"accurate forward pass appends running cue and actual marked receiver defender");
    for(int kind=1;kind<=3;++kind){
        frame.card_kind=uint8_t(kind);state.last_action=3;expected_card_random=w;
        result=report_reality(game,scene,exe,frame,state,0,0,21,w);card_at=0;
        expected_card=cursor(exe.string_at_pointer(::bmh::layout::RealityReporter_FormatError_TextPointer+21*4),card_at,game,scene,exe,frame,state,expected_card_random);
        check(state.selectors[12]==kind&&result.text.at(0)==expected_card&&
              w.seed()==expected_card_random.seed()&&w.calls()==expected_card_random.calls(),
              "all card kinds select their runtime text through the independent cursor/RNG oracle");
    }
    frame.action=0;frame.scene_status=1;frame.distance_before=38;frame.received=false;frame.intercepted=false;
    state=initialize_reality_reporter(exe);result=report_reality(game,scene,exe,frame,state,0,0,0,w);
    check(frame.action==8&&result.codes==std::vector<uint8_t>({8,11}),"long clearance and out-of-play cue retain Source order");
    check(save_stream(game)==native,"reporter changes no Native game bytes");
    std::printf("PASS: %d real Original template/cursor/RNG comparisons; Source history, old BH, pass, duel, card/reset; no layout/Main claim\n",cases);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
