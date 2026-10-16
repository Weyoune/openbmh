#include "runtime_fixture_tables.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>

#include "formats/fbm.hpp"
#include "formats/le.hpp"
#include "game/career.hpp"
#include "game/national.hpp"
#include "game/save.hpp"
#include "game/weekly.hpp"

using namespace bmh;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Candidate { int player; int score; };
}
int main(int argc,char**argv){try{
    if(argc<3||argc>4)throw std::runtime_error("usage: national_nomination_tests <BMH data> <native 2PNWK001.FBM> [output stream]");
    if(!*argv[1]) { std::cout<<"Nomination tests skipped: Original data not configured\n"; return 77; }
    const auto exe=bmh::test::load_executable(std::filesystem::path(argv[1])/"BMH.exe");
    const auto texts=load_textd(std::filesystem::path(argv[1])/"TEXTD.BLK");
    const auto file=load_savegame(argv[2]);check(file.checksum_ok,"native fixture checksum");
    const DbCounts counts{exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4)};
    GameState g=GameState::from_save(SaveState(file.stream,counts),counts);
    competition_pointers(g.memory,exe);g.texts=&texts;national_coach_after_load(g);
    check(g.manager_teams.size()==2,"fixture has two human managers and spare national slot");
    // Independent ranking oracle: choose the strongest positional quota, preserving
    // first encountered players at equal score. This tests the national selection's
    // contract without reproducing its bounded ascending insertion loop.
    const std::array<int,4> quotas{3,5,7,5};
    std::array<std::vector<Candidate>,4> candidates;
    for(size_t slot=0;slot<0xb70;++slot){
        const int id=g.squad_slots.at(slot)&0xfff;if(id==0xfff)continue;
        const auto& p=g.players.at(size_t(id));
        if(((p.w35c>>27)!=0 && ((p.w364>>16)&0x1f)!=0)||nationality(g,id)!=0)continue;
        const int pos=int(p.w364&3);int score=p.strength(false);
        if(pos>1)score+=int((p.w364>>2)&0x3f)+int(((p.w368>>18)&0x3f)>>1);
        if(slot>0x86f)score=score*11/10;
        candidates[size_t(pos)].push_back({id,score});
    }
    std::set<int> expected;
    for(size_t pos=0;pos<4;++pos){
        auto& list=candidates[pos];check(list.size()>size_t(quotas[pos]),"more candidates than positional capacity");
        std::stable_sort(list.begin(),list.end(),[](const Candidate& a,const Candidate& b){return a.score>b.score;});
        for(int row=0;row<quotas[pos];++row)expected.insert(list[size_t(row)].player);
    }
    check(expected.size()==20,"Source nominates 3+5+7+5 distinct players");
    WatcomRandom rand(589388519u);MinuteReport report;
    national_team_day(g,rand,report);check(!report.unsupported,"native nomination is supported");
    std::set<int> actual;std::array<int,4> actual_by_position{};
    const auto national=squad_range(kNoTeam);
    for(size_t slot=national.first;slot<national.first+national.count;++slot){
        const int id=g.squad_slots.at(slot)&0xfff;if(id==0xfff)continue;
        check(actual.insert(id).second,"national squad has no duplicate player");
        ++actual_by_position[size_t(g.players.at(size_t(id)).w364&3)];
    }
    check(actual_by_position==quotas,"national squad fills every positional quota, including all seven midfielders");
    check(actual==expected,"national nomination selects the Source strongest players including full-capacity replacements");
    check(g.manager_teams.size()==2,"two-manager career is restored after nomination");
    std::cout<<"Full seven-player midfield, strongest twenty German players, all quotas and manager restoration OK\n";
    std::cout<<"Watcom "<<rand.seed()<<'/'<<rand.calls()<<'\n';
    if(argc==4){const auto bytes=save_stream(g);std::ofstream out(argv[3],std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));}
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
