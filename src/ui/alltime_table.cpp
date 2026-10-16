// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "alltime_table.hpp"
#include <algorithm>
#include "game/table.hpp"
#include "ui/manager.hpp"
#include "ui/setup.hpp"
namespace bmh::ui { namespace {
constexpr uint32_t kFirst=::bmh::layout::First, kLeague=::bmh::layout::AlltimeTable_Layout_ExecutableField;
std::string original_text(const TextDb& t,uint32_t at){if(at<::bmh::layout::TextPointerTable||((at-::bmh::layout::TextPointerTable)&3U))throw FormatError("alltime: invalid TEXTD address");return original_to_utf8(t[(at-::bmh::layout::TextPointerTable)/4]);}
void fill(Image&o,int x,int y,int w,int h,uint8_t c){for(int j=0;j<h;++j)for(int i=0;i<w;++i)o.at(x+i,y+j)=c;}
void copy(Image&o,const Image&s,int sx,int sy,int x,int y,int w,int h,int tr=-1){if(sx<0||sy<0||x<0||y<0||sx+w>s.width||sy+h>s.height||x+w>o.width||y+h>o.height)throw FormatError("alltime: Original asset bounds");for(int j=0;j<h;++j)for(int i=0;i<w;++i){auto c=s.pixels[size_t(sy+j)*s.width+sx+i];if(c!=tr)o.at(x+i,y+j)=c;}}
std::string source_number(uint32_t value,uint8_t flags){
 const unsigned width=flags&0x3f;std::string digits=std::to_string(value);
 const bool grouped=(flags&0x40)==0&&width>3;
 unsigned separators=0;
 if(grouped){for(size_t p=digits.size();p>3;p-=3){digits.insert(p-3,1,'.');++separators;}}
 const unsigned groups=grouped?(width-1)/3:0;const unsigned field=width+separators;
 if(digits.size()<field)digits.insert(0,field-digits.size(),'%');
 // FormatGroupedDigits pads unused grouping positions with spaces, not '%' glyphs.
 if(groups>separators)digits.insert(0,groups-separators,' ');return digits;
}
uint8_t team_color(const GameState&g,int team){size_t count=std::min<size_t>(g.manager_teams.size(),4);int active=g.memory.has(::bmh::layout::ActiveManager)?g.memory.u8(::bmh::layout::ActiveManager):-1;int managers=g.memory.has(::bmh::layout::ManagerCount)?g.memory.u8(::bmh::layout::ManagerCount):int(count);bool current=g.memory.has(::bmh::layout::DeskPosition)&&g.memory.u8(::bmh::layout::DeskPosition)<managers;for(size_t slot=0;slot<count;++slot){size_t at=slot*0xb0c+0xa8;if(at>=g.manager_records.size()||g.manager_records[at]==0)continue;if(g.manager_teams[slot]==team)return int(slot)==active&&current?0x7a:0x75;}if(g.memory.has(::bmh::layout::AlltimeTable_TeamColor_RuntimeField)&&g.memory.u16(::bmh::layout::AlltimeTable_TeamColor_RuntimeField)==team)return 0xf7;return 0x5f;}
void button(Image&out,const Image&a,const SpriteFont&spr,const LeImage&exe,int x,int y,std::string_view label,bool selected){constexpr int width=96,height=10;int dx=x+(selected?2:0),dy=y+(selected?2:0);if(selected){auto bg=out.at(x,y+height);fill(out,x,y,width+1,2,bg);fill(out,x,y,2,height+1,bg);}else{fill(out,x+2,y+height,width-1,2,0x40);fill(out,x+width,y+2,2,height-2,0x40);}int sy=selected?44:74;copy(out,a,24,sy,dx,dy,31,height,0xc0);int used=31;while(width-used>20){int count=std::min(8,width-used-20);copy(out,a,24,sy+height,dx+used,dy,count,height,0xc0);used+=count;}int tail=width-used;copy(out,a,45-tail,sy+2*height,dx+used,dy,tail,height,0xc0);spr.draw(out,dx+width/2-spr.text_width(label)/2,dy+height/2+2,label,exe.u8(::bmh::layout::Advertising_Button_RuntimeField + uint32_t(selected)));}
}
AllTimeTableState alltime_table_from_game(const GameState&g,const LeImage&exe,DeskRequest owner,const Image&underlay,int nation,bool relative){
 if(nation<0||nation>4)throw FormatError("alltime: nation outside 0..4");if(underlay.width!=320||underlay.height!=240)throw FormatError("alltime: parent underlay must be 320x240");AllTimeTableState s;s.owner=owner;s.underlay=underlay;s.nation=nation;s.relative=relative;s.mask=uint8_t((1U<<nation)|(relative?0x20:0));
 if(g.club_table.size()<168*0x4a)return s;s.has_data=true;s.records.resize(168);
 for(int t=0;t<168;++t){size_t b=size_t(t)*0x4a+0x1e;auto u=[&](int o){return uint16_t(g.club_table[b+size_t(o)]|uint16_t(g.club_table[b+size_t(o+1)])<<8);};auto&r=s.records[size_t(t)];r.points=u(0);r.goals_for=u(2);r.goals_against=u(4);r.wins=u(6);r.draws=u(8);r.losses=u(10);if(t<int(g.club_ids.size()))s.club_names[size_t(t)]=g.club_name(t);s.club_name_colors[size_t(t)]=team_color(g,t);}
 for(int n=0;n<5;++n)if(0x165+n<int(g.club_ids.size()))s.nation_names[size_t(n)]=g.club_name(0x165+n);
 const uint32_t start=kFirst+uint32_t(nation)*8;const int league=exe.u8(kLeague+uint32_t(nation));const uint32_t full=uint32_t(league_matchdays(league));
 for(int part=0;part<2;++part){int first=exe.u16(start+uint32_t(part)*4),end=exe.u16(::bmh::layout::AlltimeTable_AlltimeTableFromGame_RuntimeField+uint32_t(nation)*8+uint32_t(part)*4);for(int team=first;team<end;++team){if(team<0||team>=168)continue;const auto&r=s.records[size_t(team)];int played=int(r.wins)+r.draws+r.losses;if(!played)continue;uint32_t p=r.points,gf=r.goals_for,ga=r.goals_against;if(relative){p=p*full*10U/uint32_t(played);gf=gf*full*10U/uint32_t(played);ga=ga*full*10U/uint32_t(played);}AllTimeRow row;row.team=team;row.played=played;row.points=p;row.goals_for=gf;row.goals_against=ga;row.points_key=p*0x10001U+0x7d000000U;row.goal_key=gf*0x10001U-ga*0x10000U+0x7d000000U;s.rows.push_back(row);}}
 std::stable_sort(s.rows.begin(),s.rows.end(),[](const auto&a,const auto&b){if(a.points_key!=b.points_key)return a.points_key>b.points_key;return a.goal_key>b.goal_key;});if(s.rows.size()>24)s.rows.resize(24);return s;
}
AllTimeTableScreen::AllTimeTableScreen(const std::filesystem::path&data,AllTimeTableState s):state_(std::move(s)),data_(data),exe_(load_le(asset_path(data,"BMH.exe"))),font_(load_font(asset_path(data/"F","BMH3.FNT"))),header_(load_font(asset_path(data/"F","BMH2.FNT"))),sprite_(data),window_(load_vga(asset_path(data/"KB","56.VGA"))),buttons_(load_vga(asset_path(data/"KB","0.VGA"))),texts_(load_textd(asset_path(data,"TEXTD.BLK"))){if(state_.underlay.width!=320||state_.underlay.height!=240)throw FormatError("alltime: invalid parent frame");}
Image AllTimeTableScreen::render()const{Image out=state_.underlay;
 if(state_.has_data){int spacing=state_.rows.size()<15?8:state_.rows.size()>19?6:7;for(size_t i=0;i<state_.rows.size();++i){const auto&r=state_.rows[i];int y=38+int(i)*spacing;auto color=state_.club_name_colors[size_t(r.team)];
  std::string identity=source_number(uint32_t(i+1),0xc2)+exe_.cstring(::bmh::layout::AlltimeTable_Render_ExecutableField2)+state_.club_names[size_t(r.team)];
  font_.draw(out,2,y-font_.baseline,identity,color);
  const auto&record=state_.records[size_t(r.team)];std::string stats;
  stats+=source_number(uint32_t(r.played),0xc4)+exe_.cstring(::bmh::layout::AlltimeTable_Render_ExecutableField);
  stats+=source_number(record.wins,0x44)+exe_.cstring(::bmh::layout::AlltimeTable_Render_ExecutableField);
  stats+=source_number(record.draws,0x44)+exe_.cstring(::bmh::layout::AlltimeTable_Render_ExecutableField);
  stats+=source_number(record.losses,0x44)+exe_.cstring(::bmh::layout::AlltimeTable_Render_ExecutableField)+exe_.cstring(::bmh::layout::AlltimeTable_Render_ExecutableField3);
  if(state_.relative){
   stats+=source_number(r.points/10,7)+exe_.cstring(::bmh::layout::AlltimeTable_Render_ExecutableField4)+source_number(r.points%10,1)+exe_.cstring(::bmh::layout::AlltimeTable_Render_ExecutableField5);
   stats+=source_number(r.goals_for/10,5)+exe_.cstring(::bmh::layout::AlltimeTable_Render_ExecutableField4)+source_number(r.goals_for%10,1)+exe_.cstring(::bmh::layout::AlltimeTable_Render_ExecutableField7);
   stats+=source_number(r.goals_against/10,3)+exe_.cstring(::bmh::layout::AlltimeTable_Render_ExecutableField4)+source_number(r.goals_against%10,1)+exe_.cstring(::bmh::layout::AlltimeTable_Render_ExecutableField8);
  }else{
   stats+=source_number(r.points,0x48)+exe_.cstring(::bmh::layout::AlltimeTable_Render_ExecutableField6);
   stats+=source_number(r.goals_for,0x45)+exe_.cstring(::bmh::layout::AlltimeTable_Render_ExecutableField7);
   stats+=source_number(r.goals_against,0x45)+exe_.cstring(::bmh::layout::AlltimeTable_Render_ExecutableField8);
  }
  font_.draw(out,0x67,y-font_.baseline,stats,color);
 }
 for(int i=0;i<6;++i){int x=12+(i%3)*99,y=177+(i/3)*12;auto label=i<5?state_.nation_names[size_t(i)]:original_to_utf8(texts_[state_.relative?22:21]);button(out,buttons_,sprite_,exe_,x,y,label,i<5?(state_.mask&(1U<<i))!=0:state_.relative);}}return out;}
bool AllTimeTableScreen::pointer_down(int x,int y,bool right){if(pending_)return false;if(right){pending_=AllTimeRequest{AllTimeRequestKind::close,state_.owner,state_.nation,state_.relative};return true;}if(!state_.has_data)return false;pressed_=-1;for(int i=0;i<6;++i){int bx=12+(i%3)*99,by=177+(i/3)*12;if(x>=bx&&x<=bx+96&&y>=by&&y<=by+10){pressed_=i;return true;}}return false;}
bool AllTimeTableScreen::release(int x,int y){if(pending_)return false;int i=pressed_;pressed_=-1;if(i<0)return false;int bx=12+(i%3)*99,by=177+(i/3)*12;if(x<bx||x>bx+96||y<by||y>by+10)return true;if(i<5){state_.mask=uint8_t((state_.mask&0x20)^(1U<<i));if(i!=state_.nation)pending_=AllTimeRequest{AllTimeRequestKind::select_nation,state_.owner,i,state_.relative};state_.nation=i;}else{state_.mask=uint8_t((state_.mask&0x2f)^0x20);state_.relative=!state_.relative;pending_=AllTimeRequest{AllTimeRequestKind::toggle_relative,state_.owner,state_.nation,state_.relative};}return true;}
}
