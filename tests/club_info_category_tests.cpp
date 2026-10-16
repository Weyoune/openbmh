// SPDX-License-Identifier: GPL-3.0-only
#include <iostream>
#include <stdexcept>
#include "game/club_info.hpp"
#include "game/random.hpp"
using namespace bmh;
static void check(bool ok,const char* m){if(!ok)throw std::runtime_error(m);}
static GameState fixture(int n){GameState g;g.team_morale.resize(358);for(size_t i=0;i<g.team_morale.size();++i)g.team_morale[i]=uint8_t(i%64);for(int i=0;i<n;++i)g.team_morale[size_t(i)]|=0x80;return g;}
static int category(const GameState& g,int team){return g.team_morale[size_t(team)]>>6;}
int main(){try{
 auto three=fixture(3);check(set_club_info_category(three,10,2)==2 && category(three,10)==2,"three existing permits a fourth AI club");
 auto four=fixture(4);auto before=four.team_morale;check(set_club_info_category(four,10,2)==0,"four existing declines new AI category2");check(four.team_morale==before,"declined AI target0 preserves whole raw pool");
 auto owner=fixture(4);owner.manager_teams={10};auto old=owner.team_morale;check(set_club_info_category(owner,10,2)==2,"human request retains2");for(size_t i=0;i<owner.team_morale.size();++i)check(owner.team_morale[i]==(i==0?uint8_t((old[i]&63)|64):i==10?uint8_t((old[i]&63)|128):old[i]),"exact first AI replacement and two-club change");
 auto preference=fixture(4);preference.manager_teams={0,10};set_club_info_category(preference,10,2);check(category(preference,0)==2 && category(preference,1)==1,"first existing human preserved even unnamed manager");
 auto target=fixture(4);check(set_club_info_category(target,0,2)==0 && category(target,0)==0,"Source includes already selected AI target in scan");
 auto same=fixture(4);same.manager_teams={0};set_club_info_category(same,0,2);check(category(same,0)==2 && category(same,1)==1,"same human target still causes Source replacement");
 auto overflow=fixture(6);overflow.manager_teams={10};set_club_info_category(overflow,10,2);check(category(overflow,0)==1 && category(overflow,1)==1 && category(overflow,2)==1 && category(overflow,3)==2 && category(overflow,10)==2,"literal overfull counter repeatedly demotes first AI");
 auto end=fixture(3);end.team_morale[326]|=128;end.team_morale[327]|=128;check(set_club_info_category(end,10,2)==0 && category(end,327)==2,"327-entry scan includes326 and excludes327");
 auto outside=fixture(3);outside.team_morale[327]|=128;check(set_club_info_category(outside,10,2)==2,"category2 outside327-entry pool does not affect count");
 auto coach=fixture(4);coach.manager_teams={kNoTeam};coach.manager_records.resize(5*0xb0c);coach.manager_records[4*0xb0c+0x290]=10;check(set_club_info_category(coach,10,2)==2 && category(coach,0)==1,"Source fifth-record national club has human priority");
 for(int value:{0,1}){auto g=fixture(4);check(set_club_info_category(g,2,value)==value && (g.team_morale[2]&63)==2,"category0/1 retains lower6 bits");}
 for(int kind=0;kind<4;++kind){auto g=fixture(4);int team=10,value=2;if(kind==0)team=358;if(kind==1)value=3;if(kind==2)g.team_morale.resize(326);if(kind==3){g.manager_teams={kNoTeam};}auto snapshot=g.team_morale;bool rejected=false;try{set_club_info_category(g,team,value);}catch(const std::invalid_argument&){rejected=true;}check(rejected && g.team_morale==snapshot,"invalid request is inert before any demotion");}
 static int draws=0;auto prior=draw_hook;draw_hook=+[](int){++draws;};auto g=fixture(4);g.manager_teams={10};set_club_info_category(g,10,2);check(draws==0,"no RNG");draw_hook=prior;
 std::cout<<"Source category literal4, AI/human priority,327 scan, two-club effects, self/overfull quirks, national fallback, invalid inert and no RNG OK\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
