#include "test_inputs.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "formats/fbm.hpp"
#include "formats/savestate.hpp"
#include "formats/textd.hpp"
#include "game/save.hpp"
#include "game/match_engine.hpp"
#include "game/transfer_bid.hpp"

using namespace bmh;
namespace {
constexpr uint32_t list_start=::bmh::layout::EditorList;
constexpr size_t pool_start=2928, record_size=0xb0c, board_size=0x840;
void require(bool yes,const std::string& why) { if(!yes)throw std::runtime_error(why); }
void put32(std::vector<uint8_t>& bytes,size_t at,uint32_t n) {
    for(size_t k=0;k<4;++k)bytes.at(at+k)=uint8_t(n>>(8*k));
}
const GameState* drawing_state=nullptr;
const std::vector<uint8_t>* drawing_board=nullptr;
bool board_restored_at_draw=true;
void observe_draw(int generator) {
    if(generator!=1 || !drawing_state || !drawing_board || drawing_state->tactics!=*drawing_board)
        board_restored_at_draw=false;
}
struct OracleW {
    uint32_t seed; uint64_t calls=0;
    int range(int lo,int hi) {
        seed=seed*0x41c64e6du+0x3039u;++calls;
        return lo+int(((seed>>16)&0x7fffu)*uint32_t(hi-lo+1)>>15);
    }
};
// Source round strings CareerSession_RuntimeError_ExecutableField, first eight rounds. Independent of production
// fixtures/tables/prediction/bonus/rating; controls deliberately remain here.
const std::string& rounds18(size_t round) {
    return bmh::test::expected_text("transfer-pairing-row-"+std::to_string(round));
}
constexpr int sizes[]={18,18,18,18,18,12,12,12,12};
constexpr int relegation[]={3,4,2,2,3,0,0,0,0};
int league(int team) {
    int end=0;for(int l=0;l<9;++l){end+=sizes[l];if(team<end)return l;}
    throw std::runtime_error("oracle requires genuine league club");
}
// legacy routine mode4 composition, bounded to genuine 18-team league0 and declared
// rounds0/1/end or first8 complete result rounds. No production helper calls.
int predicted_rank(const GameState& g,int team) {
    const int l=league(team), n=sizes[l], played=int(int8_t(g.league_rounds.at(size_t(l))));
    if(played<2 || played>=n*2-2)return 11;
    require(l==0&&played<=8,"independent prediction controls use league0 first8 rounds");
    std::array<int,18> points{},recent_points{},games{},recent_games{};
    const int from=std::max(0,played-5);
    for(int r=0;r<played;++r)for(int m=0;m<9;++m) {
        const int h=rounds18(size_t(r))[m*2]-'a',a=rounds18(size_t(r))[m*2+1]-'a';
        const uint8_t v=g.results.at(size_t(r)*10+size_t(m));
        require((v&0xcc)!=0xcc,"prediction control must contain complete results");
        const int hg=v>>4,ag=v&15;
        const int hp=hg>ag?2:hg==ag?1:0,ap=ag>hg?2:ag==hg?1:0;
        points[size_t(h)]+=hp;points[size_t(a)]+=ap;++games[size_t(h)];++games[size_t(a)];
        if(r>=from) {
            recent_points[size_t(h)]+=hp;recent_points[size_t(a)]+=ap;
            ++recent_games[size_t(h)];++recent_games[size_t(a)];
        }
    }
    for(size_t m=0;m<g.manager_teams.size();++m) {
        const int t=g.manager_teams[m];if(t>=18)continue;
        const uint8_t d=g.manager_records.at(m*record_size+0x220);
        if((d>>5)==3)points[size_t(t)]=std::max(0,points[size_t(t)]-(d&31));
    }
    require(recent_games[size_t(team)]!=0,"Source prediction division must be valid");
    const int rate=recent_points[size_t(team)]*500/recent_games[size_t(team)];
    const uint16_t own=uint16_t(points[size_t(team)]*500+(34-games[size_t(team)])*rate);
    int behind=0;
    for(int t=0;t<18;++t)if(t!=team) {
        require(recent_games[size_t(t)]!=0,"Source opponent rate division must be valid");
        const int other_rate=recent_points[size_t(t)]*500/recent_games[size_t(t)];
        if(uint16_t(points[size_t(t)]*500+(34-games[size_t(t)])*other_rate)<own)++behind;
    }
    // mode4 starts with18 zero entries and marks18-behind-1 as the first1;
    // returned best-rank is that index+1, then5fc59 increments once again.
    return 18-behind+1;
}
int average499(const GameState& g,int team) {
    require(team>=0&&team<90,"independent fitness oracle requires actual24-slot club");
    int sum=0;
    for(size_t s=size_t(team)*24;s<size_t(team+1)*24;++s) {
        const uint16_t word=g.squad_slots.at(s);
        if(word==0xfff || !(word&0x8000))continue;
        const auto& p=g.players.at(word&0xfff);
        sum+=int(p.w35c>>14&127)+int(p.w35c>>7&127);
    }
    // legacy routine flags499 selects lineup/all positions/both attrs, fixed divisor11.
    return std::max(1,std::clamp((sum/2)/11,0,99));
}
uint32_t source_bonus(const GameState& g,int manager,int player,uint32_t salary,
                      const std::array<uint8_t,7>& bonuses) {
    const int team=g.manager_teams.at(size_t(manager)),l=league(team);
    const auto& p=g.players.at(size_t(player));
    const int strength=int(((p.w35c>>14&127)+(p.w35c>>7&127))>>1);
    const int gap=std::max(-1,int(int16_t(average499(g,team)-strength)));
    const int goals[]={0,2,4,8};
    const int times[]={std::max(0,goals[p.w364&3]-gap),34,std::max(0,25-gap*5)};
    uint32_t total=0;
    for(size_t k=0;k<3;++k) {
        const uint32_t first=(uint32_t(bonuses[k])*salary)/100u;
        total+=(first*uint32_t(times[k]))/12u;
    }
    const int rank=predicted_rank(g,team);
    uint32_t season=0;
    for(int k=3;k<7;++k) {
        const bool qualifies=k==3 ? l==0&&rank==0 : k==4 ? rank<5 : k==5 ? rank<10
                             :rank<sizes[l]-relegation[l];
        if(qualifies&&season==0)season=(5u*uint32_t(bonuses[size_t(k)])*salary)/100u;
    }
    return total+season/12u;
}
struct Expected { uint32_t fee=0,salary=0; };
Expected source_rating(const GameState& g,const TransferBidRatingRequest& r,OracleW& w) {
    const uint32_t at=list_start+uint32_t(r.list_entry)*0x58;
    const uint8_t term=g.memory.u8(at+0x34+uint32_t(r.bid_slot));
    const uint8_t years=uint8_t((int(term&127)-1)/12);
    uint32_t fee_base=(g.memory.u32(at+0x2c)/10000u)*12500u;
    if(term&128)fee_base>>=2;
    const uint32_t salary_base=(((uint32_t(years)*20u+100u)*g.memory.u32(at+0x30))/10000u)*100u;
    require(fee_base!=0&&salary_base!=0,"valid Source scalar oracle denominators");
    Expected out;
    out.fee=(g.memory.u32(at+0x1c+uint32_t(r.bid_slot)*4)*100u)/fee_base;
    uint32_t salary=g.memory.u32(at+0x0c+uint32_t(r.bid_slot)*4);
    const int bidder=int(int16_t(g.memory.u16(at+uint32_t(r.bid_slot)*2)));
    int manager=-1;
    for(size_t m=0;m<g.manager_teams.size();++m)if(g.manager_teams[m]==bidder)manager=int(m);
    if(manager>=0) {
        std::array<uint8_t,7> bonuses{};
        for(size_t k=0;k<7;++k)bonuses[k]=g.memory.u8(at+0x38+uint32_t(r.bid_slot)*7+uint32_t(k));
        salary+=source_bonus(g,manager,int(g.squad_slots.at(size_t(r.source_slot))&0xfff),salary,bonuses);
    }
    out.salary=(salary*100u)/salary_base;
    const uint32_t threshold=uint32_t(w.range(90,100));
    if(out.fee<threshold)out.fee=0;
    if(out.salary<g.memory.u8(at+0x55))out.salary=0;
    return out;
}
TransferBidRatingRequest control(GameState& g,int bidder=1,int entry=0,int bid=0) {
    // Frozen genuine Native foundation; these listing scalars form a declared
    // evaluation control, not an Original negotiation/runtime/clock recording.
    TransferBidRatingRequest r;
    r.source_slot=int(pool_start);r.list_entry=entry;r.bid_slot=bid;
    r.expected_bidder=bidder;r.expected_player=g.squad_slots.at(pool_start)&0xfff;
    const uint32_t at=list_start+uint32_t(entry)*0x58;
    g.memory.set16(at+uint32_t(bid)*2,uint16_t(bidder));
    g.memory.set32(at+0x2c,1234567);g.memory.set32(at+0x30,9876);
    g.memory.set32(at+0x1c+uint32_t(bid)*4,1537500);
    g.memory.set32(at+0x0c+uint32_t(bid)*4,9800);
    g.memory.set8(at+0x34+uint32_t(bid),12);g.memory.set8(at+0x55,100);
    for(int k=0;k<7;++k)g.memory.set8(at+0x38+uint32_t(bid)*7+uint32_t(k),0);
    for(auto& n:g.league_rounds)n=0;
    return r;
}
void compare_rating(GameState g,const TransferBidRatingRequest& r,const std::string& label,
                    uint32_t seed=37,std::optional<Expected> hard={}) {
    const auto native=save_stream(g),boards=g.tactics;
    const GameRandom game(765432);const auto gs=game.seed();const auto gc=game.calls();
    OracleW expected_w{seed};const auto expected=source_rating(g,r,expected_w);
    if(hard)require(expected.fee==hard->fee&&expected.salary==hard->salary,label+": independent hard Source vector");
    WatcomRandom actual_w(seed);MinuteReport report;
    require(draw_hook==nullptr,"rating draw observer must have sole ownership");
    drawing_state=&g;drawing_board=&boards;board_restored_at_draw=true;draw_hook=observe_draw;
    struct ResetObserver { ~ResetObserver(){draw_hook=nullptr;drawing_state=nullptr;drawing_board=nullptr;} } reset_observer;
    const auto actual=rate_transfer_bid_source(g,r,actual_w,report);
    require(actual.supported&&!report.unsupported,label+": supported rating");
    require(actual.fee_pct==expected.fee&&actual.salary_pct==expected.salary,label+": exact Source32 percentages");
    require(actual_w.seed()==expected_w.seed&&actual_w.calls()==1,label+": exactly one Source thresholdW");
    require(board_restored_at_draw,label+": full borrowed board restored before threshold callback");
    require(game.seed()==gs&&game.calls()==gc,label+": independent G stream unchanged");
    require(g.tactics==boards,label+": every board row restored after temporary bonus rating");
    require(save_stream(g)==native,label+": complete Native unchanged by scalar rating");
}
void unsupported(GameState g,TransferBidRatingRequest r,const std::string& label) {
    const auto native=save_stream(g),boards=g.tactics;
    WatcomRandom w(37);MinuteReport report;const auto actual=rate_transfer_bid_source(g,r,w,report);
    require(!actual.supported&&report.unsupported,label+": typed unsupported");
    require(w.seed()==37&&w.calls()==0&&save_stream(g)==native&&g.tactics==boards,
            label+": reject before Native/board/RNG mutation");
}
void set_strength(GameState& g,int player,int strength,int position) {
    auto& p=g.players.at(size_t(player));
    p.w35c=(p.w35c&~((127u<<7)|(127u<<14)))|(uint32_t(strength)<<7)|(uint32_t(strength)<<14);
    p.w364=(p.w364&~3u)|uint32_t(position);
}
void fitness_control(GameState& g,int manager,int strength) {
    const int team=g.manager_teams.at(size_t(manager));int selected=0;
    for(size_t at=size_t(team)*24;at<size_t(team+1)*24;++at) {
        auto& word=g.squad_slots.at(at);
        if((word&0xfff)==0xfff)continue;
        word=uint16_t(word&~0x8000u);
        if(selected<11) {
            word|=0x8000;set_strength(g,word&0xfff,strength,selected==0?0:1);++selected;
        }
    }
    require(selected==11,"genuine buyer has11 actual players for declared fitness control");
}
void result_control(GameState& g,bool own_wins) {
    g.league_rounds[0]=8;
    g.manager_records.at(0x220)=0;
    for(int r=0;r<8;++r)for(int m=0;m<9;++m) {
        const int home=rounds18(size_t(r))[m*2]-'a',away=rounds18(size_t(r))[m*2+1]-'a';
        g.results.at(size_t(r)*10+size_t(m))=uint8_t(own_wins&&home==1?0x20:own_wins&&away==1?0x02:0);
    }
}
} // namespace

int main(int argc,char** argv) try {
    require(argc==3,"usage: transfer_bid_qualification_tests DATA_DIR genuine_2PNWK001.FBM");
    if(!*argv[1]){std::puts("transfer bid qualification tests: skipped (Original data not configured)");return 77;}
    const auto fbm=load_savegame(argv[2]);require(fbm.checksum_ok,"genuine Native checksum");
    auto texts=load_textd(std::filesystem::path(argv[1])/"TEXTD.BLK");
    auto original=GameState::from_save(SaveState(fbm.stream));original.texts=&texts;
    require(original.manager_teams==std::vector<int>{1,54},"genuine2PN manager identities");
    require((original.squad_slots.at(pool_start)&0xfff)!=0xfff,"genuine ordinary free-agent source player");
    for(int bidder:{0,1,54})for(int bid:{0,3}) {
        auto g=original;auto r=control(g,bidder,0,bid);
        compare_rating(g,r,"AI and both genuine human bidders/basic scalar",37,Expected{100,100});
    }
    for(uint8_t term:{uint8_t(0),uint8_t(1),uint8_t(12),uint8_t(24),uint8_t(48),uint8_t(0x80),
                      uint8_t(0x81),uint8_t(0x8c),uint8_t(0x98)}) {
        auto g=original;auto r=control(g);const uint32_t at=list_start;
        g.memory.set8(at+0x34,term);
        g.memory.set32(at+0x1c,(term&128)?384375u:1537500u);
        g.memory.set32(at+0x0c,term==24||term==0x98?11800u:term==48?15800u:9800u);
        compare_rating(g,r,"signed term quotient and separate loan fee basis",37,Expected{100,100});
    }
    for(const auto& v:std::array<std::array<uint32_t,4>,4>{{
        {1383750,9800,90,100},{1368375,9800,0,100},{1537500,9702,100,0},{1537500,9800,100,100}}}) {
        auto g=original;auto r=control(g);g.memory.set32(list_start+0x1c,v[0]);g.memory.set32(list_start+0x0c,v[1]);
        compare_rating(g,r,"threshold equality and short independent fee/salary filters",37,Expected{v[2],v[3]});
    }
    {
        auto g=original;auto r=control(g);g.memory.set32(list_start+0x1c,1383750);
        // This controlled starting W state produces threshold100. Equality90
        // passed at seed37 above, but must now fail without any extra draw.
        compare_rating(g,r,"higher real W threshold rejects fee90",0x81a985fau,Expected{0,100});
    }
    {
        auto g=original;auto r=control(g);g.memory.set32(list_start+0x2c,80000);
        g.memory.set32(list_start+0x1c,0x40000001u);g.memory.set8(list_start+0x55,1);
        compare_rating(g,r,"fee IMUL32 before unsignedDIV",37,Expected{0,100});
        g.memory.set32(list_start+0x1c,100000);g.memory.set32(list_start+0x0c,0x40000001u);
        compare_rating(g,r,"human salary ADD/IMUL32 before unsignedDIV",37,Expected{100,0});
    }
    // Exact all7/fitness/rankFF controls: player strength50, actual11-player
    // buyer lineup of strength50/58/49, with known Source bonus checkpoints.
    for(const auto& v:std::array<std::array<int,3>,5>{{{0,50,17800},{3,50,18600},{0,58,10300},{0,49,19400},{3,49,20200}}}) {
        auto g=original;auto r=control(g);fitness_control(g,0,v[1]);
        set_strength(g,r.expected_player,50,v[0]);g.memory.set32(list_start+0x0c,12000);
        const std::array<uint8_t,7> bonuses={10,20,30,40,50,60,70};
        for(size_t k=0;k<7;++k)g.memory.set8(list_start+0x38+uint32_t(k),bonuses[k]);
        require(source_bonus(g,0,r.expected_player,12000,bonuses)==uint32_t(v[2]),"hard Source fitness and all7 bonus vector");
        compare_rating(g,r,"all7 bonuses, fitness and prediction fallback",37,
                       Expected{100,uint32_t((12000+v[2])*100/9800)});
    }
    for(bool wins:{false,true}) {
        auto g=original;auto r=control(g);fitness_control(g,0,50);set_strength(g,r.expected_player,50,0);
        result_control(g,wins);
        require(predicted_rank(g,1)==(wins?2:19),"independent mode4 result-composed rank");
        g.memory.set32(list_start+0x0c,12000);
        for(size_t k=0;k<7;++k)g.memory.set8(list_start+0x38+uint32_t(k),uint8_t((k+1)*10));
        compare_rating(g,r,"rankFF predicts actual Native results without rating oracle reuse",37,
                       Expected{100,uint32_t((12000+(wins?16800:14300))*100/9800)});
    }
    {
        auto g=original;auto r=control(g);fitness_control(g,0,50);set_strength(g,r.expected_player,50,0);
        result_control(g,false);
        // A separate declared result-byte generator; this is not a Source RNG
        // or clock seed. The resulting actual Native standings predict rank9.
        uint32_t recipe=4;
        for(int round=0;round<8;++round)for(int m=0;m<9;++m) {
            recipe=recipe*1664525u+1013904223u;
            const uint32_t outcome=recipe%3u;
            g.results.at(size_t(round)*10+size_t(m))=uint8_t(outcome==0?0x20:outcome==1?0:0x02);
        }
        require(predicted_rank(g,1)==9,"independent controlled Native prediction selects top10 bonus");
        g.memory.set32(list_start+0x0c,12000);
        for(size_t k=0;k<7;++k)g.memory.set8(list_start+0x38+uint32_t(k),uint8_t((k+1)*10));
        compare_rating(g,r,"all7 top10 season branch from actual result bytes",37,Expected{100,298});
    }
    {
        auto g=original;auto r=control(g);fitness_control(g,0,50);set_strength(g,r.expected_player,50,3);
        g.memory.set32(list_start+0x0c,10000000u);
        for(uint32_t k=0;k<7;++k)g.memory.set8(list_start+0x38+k,100);
        compare_rating(g,r,"bonus products and total salary wrap at Source32 boundaries",37);
    }
    {
        auto g=original;auto r=control(g);fitness_control(g,0,50);
        set_strength(g,r.expected_player,50,0);const int pool_player=r.expected_player;
        // Explicit SourceSlot points at a different actual owned player while
        // the list's ordinary pool word retains its genuine free-agent player.
        r.source_slot=24;r.expected_player=g.squad_slots.at(24)&0xfff;
        require(r.expected_player!=pool_player&&r.expected_player!=0xfff,"actual distinct squad-index source");
        set_strength(g,r.expected_player,50,3);g.memory.set32(list_start+0x0c,12000);
        g.memory.set8(list_start+0x38,100);
        compare_rating(g,r,"Sourceparam1 is absolute squad slot and never silently listpool",37);
    }
    for(int mode:{1,2,3}) {
        auto g=original;auto r=control(g);g.memory.set8(::bmh::layout::GameMode,uint8_t(mode));
        compare_rating(g,r,"425ee does not own career mode or extra modeRNG",37,Expected{100,100});
    }
    {
        auto g=original;auto r=control(g);
        auto bad=r;bad.expected_player^=1;unsupported(g,bad,"wrong explicit player identity");
        bad=r;bad.expected_bidder=54;unsupported(g,bad,"wrong explicit bidder identity");
        bad=r;bad.source_slot=-1;unsupported(g,bad,"negative SourceSlot");
        bad=r;bad.source_slot=int(g.squad_slots.size());unsupported(g,bad,"SourceSlot past native storage");
        bad=r;bad.list_entry=-1;unsupported(g,bad,"negative list entry");
        bad=r;bad.list_entry=44;unsupported(g,bad,"list entry past ordinary+special range");
        bad=r;bad.bid_slot=4;unsupported(g,bad,"invalid bid index");
        auto inconsistent=g;inconsistent.manager_records.at(0x290)=54;
        unsupported(inconsistent,r,"Native bidder record disagrees with runtime team");
        auto invalid_lineup=g;invalid_lineup.squad_slots.at(24)=0xffff;
        unsupported(invalid_lineup,r,"FULLWORD FFFF lineup references player4095");
        auto missing_results=g;missing_results.league_rounds[0]=8;
        std::fill(missing_results.results.begin(),missing_results.results.begin()+8*10,0xcc);
        unsupported(missing_results,r,"Source prediction cannot divide by zero recent games");
        auto empty=g;empty.squad_slots.at(size_t(r.source_slot))=0xfff;unsupported(empty,r,"missing Sourceplayer");
        auto zero=g;zero.memory.set32(list_start+0x2c,9999);unsupported(zero,r,"zero rounded fee basis");
        zero=g;zero.memory.set32(list_start+0x30,0);unsupported(zero,r,"zero salary basis");
        zero=g;zero.memory.set32(list_start+0x30,99);unsupported(zero,r,"nonzero salary rounds to zero basis");
        zero=g;zero.memory.set32(list_start+0x2c,30000);zero.memory.set8(list_start+0x34,0x80);
        // 37500>>2 is9375, not0: loan should still be supported here.
        compare_rating(zero,r,"small nonzero loan basis is not synthesized/rounded",37);
    }
    std::puts("PASS independent legacy routine Native/board/Source32/W vectors; caller flow remains separately bounded");
    return 0;
} catch(const std::exception& e) {std::fprintf(stderr,"transfer_bid_qualification_tests: %s\n",e.what());return 1;}
