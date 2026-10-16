// SPDX-License-Identifier: GPL-3.0-only
#include <iostream>
#include <stdexcept>
#include "game/club_info.hpp"
#include "game/random.hpp"
using namespace bmh;
static void check(bool ok,const char* m){if(!ok)throw std::runtime_error(m);}
static GameState fixture(int team,int count=20){GameState g;auto r=squad_range(team);g.squad_slots.assign(r.first+r.count,0x0fff);g.players.resize(64);for(int i=0;i<count;++i)g.squad_slots[r.first+size_t(i)]=uint16_t(i|(i<11?0x4000:i<17?0x8000:0x2000));return g;}
int main(){try{
 int draws=0;auto prior=draw_hook;draw_hook=+[](int){};
 auto ai=fixture(0);auto human=ai;human.manager_teams={0};human.manager_records.assign(0xb0c,0);
 ai.squad_slots[3]|=0x1000;human.squad_slots[3]|=0x1000;ai.players[3].w364=human.players[3].w364=uint32_t(1)<<16;
 // An injured reserve beyond the17-row normalization limit still loses bits13..15.
 ai.players[18].w364=human.players[18].w364=uint32_t(2)<<16;
 auto player_before=ai.players;auto records=human.manager_records;auto board=human.tactics;
 auto displayed=prepare_club_info_entry(ai,0);auto human_displayed=prepare_club_info_entry(human,0);
 check(displayed.displayed_slots==human_displayed.displayed_slots && displayed.displayed_slots.size()==24,"owned display rows preserve normalized flags for AI/human");
 check(displayed.displayed_slots[3]==0x9003 && displayed.displayed_slots[18]==0x2012,"Source draws before injured flag cleanup");
 check(ai.squad_slots==human.squad_slots,"AI and human entry have same Source normalization");
 for(int i=0;i<20;++i){uint16_t expected=uint16_t(i|(i<11?0x8000:i<17?0x4000:0x2000));if(i==3)expected=0x1003;if(i==18)expected=18;check(ai.squad_slots[size_t(i)]==expected,"independent field/bench/injury bit expectation");}
 check(ai.squad_slots[20]==0x0fff && human.manager_records==records && human.tactics==board,"empty tails, records and tactics unchanged");
 for(size_t i=0;i<ai.players.size();++i)check(ai.players[i].w364==player_before[i].w364 && ai.players[i].w35c==player_before[i].w35c && ai.players[i].w368==player_before[i].w368,"player attributes unchanged");
 auto once=ai.squad_slots;prepare_club_info_entry(ai,0);check(ai.squad_slots==once,"repeated entry stable");
 auto shortg=fixture(90,16);prepare_club_info_entry(shortg,90);auto sr=squad_range(90);check((shortg.squad_slots[sr.first+15]&0xc000)==0x4000,"small squad16 supported");
 auto national=fixture(kNoTeam);prepare_club_info_entry(national,kNoTeam);check((national.squad_slots[squad_range(kNoTeam).first]&0xc000)==0x8000,"national squad supported");
 auto unflagged=fixture(0,8);for(int i=0;i<8;++i)unflagged.squad_slots[i]=uint16_t(i);auto unchanged=unflagged.squad_slots;prepare_club_info_entry(unflagged,0);check(unflagged.squad_slots==unchanged,"no field or bench markers means no normalization");
 // Validation must finish before the first Source write, including IDs at the end.
 for(int kind=0;kind<3;++kind){auto invalid=fixture(0);int team=0;if(kind==0)invalid.squad_slots[23]=200;if(kind==1)invalid.squad_slots.resize(23);if(kind==2)team=138;auto before=invalid.squad_slots;bool rejected=false;try{prepare_club_info_entry(invalid,team);}catch(const std::invalid_argument&){rejected=true;}check(rejected && invalid.squad_slots==before,"invalid request is inert");}
 // Source helper exposes no generators; a draw callback proves none are invoked.
 static int observed_draws=0;draw_hook=+[](int){++observed_draws;};prepare_club_info_entry(ai,0);check(observed_draws==draws,"no RNG draw");draw_hook=prior;
 std::cout<<"AI/human, first17 flags, both injury bits, short/national squads, empty tails, replay, invalid inert and no RNG OK\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
