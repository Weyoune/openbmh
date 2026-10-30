// SPDX-License-Identifier: GPL-3.0-only
#include "formats/badge_policy.hpp"
#include "formats/executable_layout.hpp"
#include "ui/balance_plan.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <sstream>

#include "game/messages.hpp"
#include "ui/manager.hpp"

namespace bmh::ui {
namespace {
constexpr uint32_t kTextBase=::bmh::layout::TextPointerTable;
constexpr std::string_view kKeys="0123456789DRCJN";
std::string_view at(const TextDb& t,uint32_t a){if(a<kTextBase||(a-kTextBase)%4)throw FormatError("balance: bad text address");return t[(a-kTextBase)/4];}
bool in(int x,int y,int x0,int y0,int x1,int y1){return x>=x0&&x<=x1&&y>=y0&&y<=y1;}
void copy(Image& o,const Image& s,int sx,int sy,int x,int y,int w,int h,int transparent=-1){
 if(sx<0||sy<0||x<0||y<0||w<0||h<0||sx+w>s.width||sy+h>s.height||x+w>o.width||y+h>o.height)throw FormatError("balance: sprite bounds "+std::to_string(sx)+","+std::to_string(sy)+" "+std::to_string(w)+"x"+std::to_string(h)+" source "+std::to_string(s.width)+"x"+std::to_string(s.height)+" dest "+std::to_string(o.width)+"x"+std::to_string(o.height));
 for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx){auto p=s.pixels[size_t(sy+yy)*size_t(s.width)+size_t(sx+xx)];if(transparent<0||p!=transparent)o.at(x+xx,y+yy)=p;}
}
void fill(Image& o,int x,int y,int w,int h,uint8_t c){for(int yy=std::max(y,0);yy<std::min(y+h,o.height);++yy)for(int xx=std::max(x,0);xx<std::min(x+w,o.width);++xx)o.at(xx,yy)=c;}
std::string calc7(uint32_t v){return format_number(GameState{},int32_t(v),7);}
std::string calc8(uint32_t v){return format_number(GameState{},int32_t(v),8);}
}

BalanceState balance_from_game(const GameState& g,const BalancePlanDraft& d,const LeImage& exe,BalanceIdentity id,const Image& underlay){
 if(d.manager<0||d.manager>3||id.manager_slot!=d.manager)throw FormatError("balance: manager identity mismatch");
 BalanceState s;s.identity=id;s.draft=d;s.underlay=underlay;s.club_name=std::string(g.club_name(g.manager_teams.at(size_t(d.manager))));
 s.currency_numerator=g.memory.has(::bmh::layout::CurrencyMultiplier)?g.memory.u32(::bmh::layout::CurrencyMultiplier):exe.u32(::bmh::layout::CurrencyMultiplier);s.currency_denominator=g.memory.has(::bmh::layout::CurrencyDivisor)?g.memory.u32(::bmh::layout::CurrencyDivisor):exe.u32(::bmh::layout::CurrencyDivisor);
 for(int row=0;row<kBalanceRows;++row){
  auto& p=s.calculators[size_t(row)];const int32_t row_max=balance_row_maximum(d,row);p.supported=row_max>=0;p.minimum=0;p.maximum=row_max<0?0:uint32_t(row_max);p.initial=uint32_t(std::clamp<int64_t>(d.rows[size_t(row)],0,p.maximum));p.source_text_address=::bmh::layout::BalancePlan_BalanceFromGame_ExecutableField;
  uint32_t first=p.maximum/10,step=p.maximum/5;
  int64_t first_signed=first;uint32_t advance=step;
  if(row==20){
   const size_t rec=size_t(d.manager)*0xb0c;const uint32_t attendance=rec+0x5c<g.manager_records.size()?g.manager_records[rec+0x5c]:0;
   if(d.rows[20]==0){first_signed=int64_t(attendance)*400;advance=attendance*100;}
   else {const uint32_t value=p.initial;if(value<10){
     // The Original helper divides by zero while deriving its decimal step for 1..9.
     // Keep the true 0..100000 editor range, but do not invent six presets.
     first_signed=int64_t(p.maximum)+1;advance=0;
    }else{uint32_t base=(value/1000)*1000,scale=1000,delta=scale*((value/scale)/10);while(delta==0&&scale>1){scale/=10;delta=scale*((value/scale)/10);}first_signed=int64_t(base)-int64_t(3)*delta;advance=delta;}}
  }
  for(size_t i=0;i<6;++i){first_signed=std::max<int64_t>(0,first_signed);if(uint64_t(first_signed)<=p.maximum)p.presets[i]=uint32_t(first_signed);if(uint64_t(first_signed)>p.maximum)p.presets[i].reset();first_signed+=advance;}
  p.presets[6]=0;p.presets[7]=p.maximum;
 }
 const size_t rec=size_t(d.manager)*0xb0c;
 for(size_t n=0;n<30&&rec+0xa8+n<g.manager_records.size();++n){char c=char(g.manager_records[rec+0xa8+n]);if(!c)break;s.read_only_notice.manager_name.push_back(c);s.close_notice.manager_name.push_back(c);}
 s.read_only_notice.kind=TaxDialogKind::acknowledgement;s.read_only_notice.source_manager_flags=uint8_t(d.manager)|0x40;s.read_only_notice.body=message_text(g,::bmh::layout::BalancePlan_BalanceFromGame_TextPointer);
 s.close_notice.kind=TaxDialogKind::acknowledgement;s.close_notice.source_manager_flags=uint8_t(d.manager)|0x40;s.close_notice.body=message_text(g,::bmh::layout::BalancePlan_BalanceFromGame_TextPointer2);
 const int portrait=rec+0x221<g.manager_records.size()?g.manager_records[rec+0x221]:0;
 if(portrait>14)throw FormatError("balance: invalid Source manager portrait");
 const int team=g.manager_teams[size_t(d.manager)];
 const bool fallback=original_badge_fallback(team);
 int badge=0;if(!fallback){if(team<0||size_t(team)*2+2>g.club_heap_78.size())throw FormatError("balance: missing Source badge selector");badge=(int(g.club_heap_78[size_t(team)*2])|int(g.club_heap_78[size_t(team)*2+1])<<8)&0x1ff;}
 const uint8_t compact_dialog_frame=g.memory.has(::bmh::layout::CompactDialogFrame)?g.memory.u8(::bmh::layout::CompactDialogFrame):exe.u8(::bmh::layout::CompactDialogFrame);
 for(auto* projection:{&s.read_only_notice,&s.close_notice}){projection->portrait=portrait;projection->badge_fallback=fallback;projection->badge_sprite=badge;projection->compact_dialog_frame=compact_dialog_frame!=0;}
 const uint8_t notice_gate_disabled=g.memory.has(::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField2)?g.memory.u8(::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField2):exe.u8(::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField2);
 const uint8_t presentation_blocked=g.memory.has(::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField)?g.memory.u8(::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField):exe.u8(::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField);
 s.read_only_notice_enabled=notice_gate_disabled==0&&(compact_dialog_frame!=0||presentation_blocked==0);
 return s;
}

BalanceScreen::BalanceScreen(const std::filesystem::path& d,BalanceState s):state_(std::move(s)),data_(d),exe_(load_le(asset_path(d,"BMH.exe"))),texts_(load_textd(asset_path(d,"TEXTD.BLK"))),body_(load_font(asset_path(d/"F","BMH3.FNT"))),heading_(load_font(asset_path(d/"F","BMH2.FNT"))),calculator_art_(load_vga(asset_path(d/"KB","6.VGA"))),button_art_(load_vga(asset_path(d/"KB","0.VGA"))),panel_art_(load_vga(asset_path(d/"KB","56.VGA"))),frame_art_(load_vga(asset_path(d/"KB","129.VGA"))),sprite_(d){
 if(state_.identity.manager_slot<0||state_.identity.manager_slot>3||state_.identity.manager_slot!=state_.draft.manager||state_.underlay.width!=320||state_.underlay.height!=240||calculator_art_.width<113||calculator_art_.height<150)throw FormatError("balance: incomplete Source projection");
 state_.calculator_x=std::clamp(state_.calculator_x,0,207);state_.calculator_y=std::clamp(state_.calculator_y,0,104);
 format_state_.memory.add_block(::bmh::layout::CurrencyMultiplier,8);format_state_.memory.set32(::bmh::layout::CurrencyMultiplier,state_.currency_numerator);format_state_.memory.set32(::bmh::layout::CurrencyDivisor,state_.currency_denominator);
 close_notice_visible_=state_.pending_close;(void)render();
}
void BalanceScreen::draw_panels(Image& o)const{
 if(!state_.cached_backdrop&&!state_.decorated){fill(o,0,4,320,13,0x44);fill(o,1,5,318,11,0x39);fill(o,0,228,320,10,0x44);fill(o,1,229,318,8,0x39);}
 for(auto r:{std::array<int,4>{0,28,159,153},std::array<int,4>{161,28,319,153}}){
  if(state_.original_panel)copy(o,panel_art_,r[0],9,r[0]+1,r[1]+1,r[2]-r[0]-1,r[3]-r[1]-1);
  else fill(o,r[0],r[1],r[2]-r[0]+1,r[3]-r[1]+1,0x42);
  fill(o,r[0],r[1],r[2]-r[0]+1,1,0x46);fill(o,r[0],r[1],1,r[3]-r[1]+1,0x46);fill(o,r[0],r[3],r[2]-r[0]+1,1,0x40);fill(o,r[2],r[1],1,r[3]-r[1]+1,0x40);
 }
 const auto title=original_to_utf8(at(texts_,::bmh::layout::BalancePlan_Fill_TextPointer));sprite_.draw(o,172+55-sprite_.text_width(title)/2,13,title);sprite_.draw(o,5,235,original_to_utf8(state_.club_name));
}
void BalanceScreen::draw_rows(Image& o)const{
 auto text=[&](int x,int y,std::string_view v,uint8_t c){int top=y-body_.baseline,w=body_.text_width(v);if(v.empty()||x<0||x+w>o.width||top<0||top+body_.height>o.height)return;Image mask(std::max(1,w),body_.height);body_.draw(mask,0,0,v,1);int index=0;while(index<32&&exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2+uint32_t(index))!=c)++index;for(int yy=0;yy<body_.height;++yy)for(int xx=0;xx<w;++xx)if(mask.at(xx,yy))o.at(x+xx,top+yy)=state_.gradient&&index<32?exe_.u8(::bmh::layout::Advertising_Text_RuntimeField+uint32_t(index*6+yy)):c;};
 const uint8_t colors[2]={exe_.u8(::bmh::layout::BalancePlan_DrawRows_RuntimeField),exe_.u8(::bmh::layout::BalancePlan_DrawRows_RuntimeField2)};
 for(int c=0;c<2;++c){auto label=at(texts_,::bmh::layout::BalancePlan_DrawRows_TextPointer3+uint32_t(c)*4);int x=50+c*160,w=heading_.text_width(label);if(x+w<=o.width)heading_.draw(o,x,37-heading_.baseline,label,colors[c]);}
 double total=0;
 for(int row=0;row<18;++row){const bool exp=row>=7;const int i=exp?row-7:row,x=exp?165:3,vx=exp?262:100,y=44+i*9;const auto label=at(texts_,::bmh::layout::BalancePlan_DrawRows_TextPointer5+uint32_t(row)*4);text(x,y,label,colors[exp]);const int32_t value=std::max(0,state_.draft.rows[size_t(row)]);auto val=source_money(value);text(vx,y,val,colors[exp]);total+=double(value)*(exp?-1.0:1.0);}
 const double dep_display=-double(state_.draft.depreciation);const bool dep_right=state_.draft.depreciation>0;auto dep=source_money(int32_t(std::clamp<double>(std::abs(dep_display),0,99999999)));text(dep_right?165:3,143,at(texts_,::bmh::layout::BalancePlan_DrawRows_TextPointer6),colors[dep_right?1:0]);text(dep_right?262:100,143,dep,colors[dep_right?1:0]);total=state_.draft.total;
 const bool profit=total>=0;auto result=source_money(int32_t(std::clamp<double>(std::abs(total),0,99999999)));text(profit?165:3,152,at(texts_,profit?::bmh::layout::BalancePlan_DrawRows_TextPointer8: ::bmh::layout::BalancePlan_DrawRows_TextPointer7),colors[profit?0:1]);text(profit?262:100,152,result,colors[profit?0:1]);
 text(12,162,exe_.cstring(::bmh::layout::BalancePlan_DrawRows_TextPointer),0x40);text(10,160,exe_.cstring(::bmh::layout::BalancePlan_DrawRows_TextPointer),0x5f);
 constexpr std::array<int,3> suffix_id{15,16,17};
 for(int i=0;i<6;++i){int row=18+i,y=160+i*9;auto label=at(texts_,::bmh::layout::BalancePlan_DrawRows_TextPointer4+uint32_t(i)*4);text(22,y+2,label,0x40);text(20,y,label,0x5f);const int selector=exe_.u8(::bmh::layout::BalancePlan_DrawRows_TextPointer2+uint32_t(i));auto val=format_number(GameState{},state_.draft.rows[size_t(row)],0x89)+std::string(texts_[size_t(suffix_id[std::clamp(selector,0,2)])]);text(192,y+2,val,0x40);text(190,y,val,0x5f);}
}
std::string BalanceScreen::source_money(int32_t value)const{return format_number(format_state_,value,0x188);}
BalanceRequest BalanceScreen::request(BalanceAction kind,int row,int32_t value)const{BalanceRequest r;r.identity=state_.identity;r.kind=kind;r.row=row;r.value=value;return r;}
void BalanceScreen::draw_buttons(Image& o)const{
 auto button=[&](int x,int width,std::string_view raw,int id){const bool down=pressed_main_button_==id&&main_button_armed_;int y=214,dx=x+(down?2:0),dy=y+(down?2:0),sy=down?44:74;if(down){fill(o,x,y,width+1,2,o.at(x,y+10));fill(o,x,y,2,11,o.at(x,y+10));}else{fill(o,x+2,y+10,width+1,2,0x40);fill(o,x+width,y+2,2,10,0x40);}copy(o,button_art_,24,sy,dx,dy,31,10);int used=31;while(width-used>20){int n=std::min(8,width-used-20);copy(o,button_art_,24,sy+10,dx+used,dy,n,10);used+=n;}int tail=width-used;copy(o,button_art_,45-tail,sy+20,dx+used,dy,tail,10);auto label=original_to_utf8(raw);sprite_.draw(o,dx+width/2-sprite_.text_width(label)/2,dy+7,label,exe_.u8(::bmh::layout::Advertising_Button_RuntimeField + uint32_t(down)));};
 button(170,63,at(texts_,::bmh::layout::BalancePlan_DrawButtons_TextPointer+uint32_t(state_.draft.view)*4),0);button(245,63,at(texts_,::bmh::layout::BalancePlan_DrawButtons_TextPointer2),1);
}
void BalanceScreen::draw_calculator(Image& o)const{
 if(!calculator_open_)return;const int cx=state_.calculator_x,cy=state_.calculator_y;copy(o,calculator_art_,0,0,cx,cy,113,136);
 fill(o,cx+113,cy+2,2,136,0x40);fill(o,cx+2,cy+136,113,2,0x40);
 auto draw=[&](int x,int baseline,std::string_view raw,uint8_t color,bool gradient=true){const int w=body_.text_width(raw),top=baseline-body_.baseline;if(x<0||top<0||x+w>o.width||top+body_.height>o.height)return;if(!gradient||!state_.gradient){body_.draw(o,x,top,raw,color);return;}Image mask(std::max(1,w),body_.height);body_.draw(mask,0,0,raw,1);int idx=0;while(idx<32&&exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2+uint32_t(idx))!=color)++idx;for(int yy=0;yy<body_.height;++yy)for(int xx=0;xx<w;++xx)if(mask.at(xx,yy))o.at(x+xx,top+yy)=idx<32?exe_.u8(::bmh::layout::Advertising_Text_RuntimeField+uint32_t(idx*6+yy)):color;};
 std::string title(at(texts_,active_profile_.source_text_address));draw(cx+8+(105-8)/2-body_.text_width(title)/2,cy+14,title,0x62);
 const bool flash=current_milliseconds_<calculator_flash_until_;
 for(int i=0;i<15;++i){int x=cx+exe_.u16(::bmh::layout::Advertising_Click_RuntimeField+uint32_t(i)*2),y=cy+exe_.u8(::bmh::layout::Advertising_Click_RuntimeField2+uint32_t(i));const bool down=pressed_calculator_control_==i||(flash&&flash_calculator_control_==i);copy(o,calculator_art_,down?15:0,136,x+(down?1:0),y+(down?1:0),down?14:15,down?13:14);draw(x+(down?6:5),y+(down?10:9),kKeys.substr(size_t(i),1),0x62,false);}
 for(size_t i=0;i<8;++i)if(active_profile_.presets[i]){int x=cx+exe_.u16(::bmh::layout::Advertising_Click_RuntimeField+uint32_t(i+15)*2),y=cy+exe_.u8(::bmh::layout::Advertising_Click_RuntimeField2+uint32_t(i+15));auto t=calc7(*active_profile_.presets[i]);draw(x+47-body_.text_width(t),y+12,t,0x62);}
 auto value=calculator_value_edited_?calc8(calculator_value_):calc7(calculator_value_);const int value_x=cx+exe_.u16(::bmh::layout::Advertising_Click_RuntimeField+23*2),value_y=cy+exe_.u8(::bmh::layout::Advertising_Click_RuntimeField2+23);draw(value_x+47-body_.text_width(value),value_y+12,value,0x00);
}
void BalanceScreen::draw_notice(Image& o)const{
 if(!notice_visible_&&!close_notice_visible_)return;
 const auto& p=close_notice_visible_?state_.close_notice:state_.read_only_notice;
 GameEndTaxDialog dialog(data_,p,GameEndRenderOptions{!state_.decorated,state_.gradient},o);o=dialog.render();
}
Image BalanceScreen::render()const{Image out=state_.cached_backdrop?state_.underlay:(state_.decorated?frame_art_:Image(320,240));draw_panels(out);draw_rows(out);draw_buttons(out);draw_calculator(out);draw_notice(out);return out;}
void BalanceScreen::open_calculator(int row){
 if(row<0||row>=22)return;
 const bool editable=(row<=2)||(row>=4&&row<=17)||(row>=19&&row<=21);
 if(!editable)return;
 if(state_.draft.view==BalanceView::so_far){notice_visible_=state_.read_only_notice_enabled;notice_consumed_=false;notice_clock_started_=false;return;}
 active_row_=row;active_profile_=state_.calculators[size_t(row)];if(!active_profile_.supported)return;
 calculator_value_=active_profile_.initial;numeric_text_=std::to_string(calculator_value_);calculator_value_edited_=false;calculator_open_=true;
}
bool BalanceScreen::commit_calculator(bool accepted){
 if(!calculator_open_)return false;const int row=active_row_;calculator_open_=false;numeric_text_.clear();
 if(accepted)pending_=request(BalanceAction::edit_row,row,int32_t(std::min<uint32_t>(calculator_value_,active_profile_.maximum)));
 return true;
}
bool BalanceScreen::handle_calculator_key(std::string_view key){
 if(key.size()==1&&key[0]>='0'&&key[0]<='9')return text(key);
 if(key=="D"){calculator_value_/=10;numeric_text_=std::to_string(calculator_value_);calculator_value_edited_=true;return true;}
 if(key=="R"){calculator_value_=active_profile_.initial;numeric_text_=std::to_string(calculator_value_);calculator_value_edited_=true;return true;}
 if(key=="C"){calculator_value_=0;numeric_text_="0";calculator_value_edited_=true;return true;}
 if(key=="J"){calculator_deferred_accept_=true;return true;}
 if(key=="N"){calculator_open_=false;numeric_text_.clear();calculator_cancel_waiting_=true;calculator_cancel_started_=false;return true;}
 return false;
}
bool BalanceScreen::pointer_down(int x,int y,bool right){
 if(pending_||calculator_cancel_waiting_||(flash_calculator_control_>=0&&current_milliseconds_<calculator_flash_until_))return false;pointer_x_=x;pointer_y_=y;
 if(close_notice_visible_){close_notice_visible_=false;if(state_.pending_close)pending_=request(BalanceAction::confirm_close);return true;}
 if(notice_visible_){notice_visible_=false;notice_consumed_=true;return true;}
 if(calculator_open_){
  if(right)return commit_calculator(false);
  const int cx=state_.calculator_x,cy=state_.calculator_y;
  if(in(x,y,cx,cy,cx+113,cy+16)){dragging_calculator_=true;drag_offset_x_=x-cx;drag_offset_y_=y-cy;return true;}
  for(int i=0;i<15;++i){int x0=cx+exe_.u16(::bmh::layout::Advertising_Click_RuntimeField+uint32_t(i)*2),y0=cy+exe_.u8(::bmh::layout::Advertising_Click_RuntimeField2+uint32_t(i));if(!in(x,y,x0,y0,x0+16,y0+14))continue;const bool acted=handle_calculator_key(kKeys.substr(size_t(i),1));if(i!=14){pressed_calculator_control_=i;flash_calculator_control_=i;calculator_flash_until_=current_milliseconds_+80;}return acted;}
  for(size_t i=0;i<8;++i)if(active_profile_.presets[i]){int x0=cx+exe_.u16(::bmh::layout::Advertising_Click_RuntimeField+uint32_t(i+15)*2),y0=cy+exe_.u8(::bmh::layout::Advertising_Click_RuntimeField2+uint32_t(i+15));if(in(x,y,x0,y0,x0+53,y0+14)){calculator_value_=*active_profile_.presets[i];numeric_text_=std::to_string(calculator_value_);calculator_value_edited_=true;return true;}}
  return true;
 }
 if(right){pending_=request(BalanceAction::close);return true;}
 if(in(x,y,170,214,233,224)){pressed_main_button_=0;main_button_armed_=true;return true;}
 if(in(x,y,245,214,308,224)){pressed_main_button_=1;main_button_armed_=true;return true;}
 for(int row=0;row<22;++row){int x0=0,x1=160,y0=38,y1=0;if(row<7)y0+=row*9,y1=y0+7;else if(row<18){x0=160;x1=320;y0+= (row-7)*9;y1=y0+7;}else{x1=320;y0=154+(row-18)*9;y1=y0+7;}if(in(x,y,x0,y0,x1,y1)){open_calculator(row);return true;}}
 return false;
}
bool BalanceScreen::release(int x,int y){if(pointer_x_<0)return false;bool handled=dragging_calculator_||pressed_calculator_control_>=0||pressed_main_button_>=0;if(dragging_calculator_){dragging_calculator_=false;}else if(pressed_calculator_control_>=0){pressed_calculator_control_=-1;}else if(pressed_main_button_>=0){const int id=pressed_main_button_;const bool activate=main_button_armed_&&in(x,y,id==0?170:245,214,id==0?233:308,224);pressed_main_button_=-1;main_button_armed_=false;if(activate)pending_=request(id==0?BalanceAction::cycle_view:BalanceAction::take_actuals);}else if(calculator_open_){/* no release action for unpressed calculator area */}pointer_x_=pointer_y_=-1;return handled;}
bool BalanceScreen::move(int x,int y){if(pointer_x_<0)return false;if(dragging_calculator_){state_.calculator_x=std::clamp(x-drag_offset_x_,0,207);state_.calculator_y=std::clamp(y-drag_offset_y_,0,104);}if(pressed_main_button_>=0&&main_button_armed_&&!in(x,y,pressed_main_button_==0?170:245,214,pressed_main_button_==0?233:308,224))main_button_armed_=false;pointer_x_=x;pointer_y_=y;return true;}
bool BalanceScreen::key(std::string_view k){
 if(pending_||calculator_cancel_waiting_||(flash_calculator_control_>=0&&current_milliseconds_<calculator_flash_until_))return false;
 if(close_notice_visible_){if(k!="ESC"&&k!="ESCAPE")return false;close_notice_visible_=false;if(state_.pending_close)pending_=request(BalanceAction::confirm_close);return true;}
 if(notice_visible_){if(k!="ESC"&&k!="ESCAPE")return false;notice_visible_=false;notice_consumed_=true;return true;}
 if(calculator_open_){if(k.size()==1&&k[0]>='0'&&k[0]<='9')return text(k);if(k=="BACKSPACE"){calculator_value_/=10;numeric_text_=std::to_string(calculator_value_);calculator_value_edited_=true;pressed_calculator_control_=flash_calculator_control_=10;calculator_flash_until_=current_milliseconds_+80;return true;}if(k=="SPACE"){calculator_value_=0;numeric_text_="0";calculator_value_edited_=true;pressed_calculator_control_=flash_calculator_control_=12;calculator_flash_until_=current_milliseconds_+80;return true;}if(k=="ENTER"||k=="RETURN"){pressed_calculator_control_=flash_calculator_control_=13;calculator_flash_until_=current_milliseconds_+80;calculator_deferred_accept_=true;return true;}if(k=="ESC"||k=="ESCAPE")return commit_calculator(false);return false;}
 if(k=="ESC"||k=="ESCAPE"){pending_=request(BalanceAction::close);return true;}
 return false;
}
bool BalanceScreen::process_timer_expired(){
 // BalanceFromGame2 event8 closes through the existing typed draft/warning owner.
 // BalanceFromGame returns ffffffff first; its N-key wait, held buttons and pending
 // edits keep their own completion. PresentManagerDialog notices have their own wait.
 if(pending_||notice_open()||calculator_cancel_waiting_||dragging_calculator_||
    pressed_main_button_>=0||pressed_calculator_control_>=0||
    (flash_calculator_control_>=0&&current_milliseconds_<calculator_flash_until_))return false;
 if(calculator_open_)return commit_calculator(false);
 pending_=request(BalanceAction::close);return true;
}
bool BalanceScreen::text(std::string_view s){
 if(!calculator_open_||pending_||calculator_cancel_waiting_||(flash_calculator_control_>=0&&current_milliseconds_<calculator_flash_until_)||s.empty())return false;for(char c:s)if(c<'0'||c>'9')return false;
 std::string candidate=(numeric_text_=="0"?std::string{}:numeric_text_)+std::string(s);if(candidate.size()>10)return false;uint64_t v=0;auto [end,ec]=std::from_chars(candidate.data(),candidate.data()+candidate.size(),v);if(ec!=std::errc{}||end!=candidate.data()+candidate.size())return false;
 calculator_value_=uint32_t(std::min<uint64_t>(v,active_profile_.maximum));numeric_text_=std::to_string(calculator_value_);calculator_value_edited_=true;const int digit=s.back()-'0';pressed_calculator_control_=flash_calculator_control_=digit;calculator_flash_until_=current_milliseconds_+80;return true;
}
bool BalanceScreen::tick_source_milliseconds(uint64_t now){
 current_milliseconds_=now;bool changed=false;
 if(flash_calculator_control_>=0&&now>=calculator_flash_until_){flash_calculator_control_=-1;pressed_calculator_control_=-1;changed=true;if(calculator_deferred_accept_){calculator_deferred_accept_=false;changed=commit_calculator(true)||changed;}}
 if(calculator_cancel_waiting_&&calculator_cancel_started_&&now>=calculator_cancel_until_){calculator_cancel_waiting_=false;calculator_cancel_started_=false;changed=true;}
 if(!close_notice_visible_&&!notice_visible_)return changed;if(!notice_clock_started_)return changed;
 // PresentManagerDialog waits 70 times for 3 Original ticks; at 50 Hz this is 4.2 s.
 // The owner supplies a monotonic millisecond clock to retain that fraction.
 if(now<notice_started_||now-notice_started_<4200)return changed;
 if(close_notice_visible_){close_notice_visible_=false;if(state_.pending_close)pending_=request(BalanceAction::confirm_close);}
 else {notice_visible_=false;notice_consumed_=true;}
 return true;
}
void BalanceScreen::mark_presented_source_milliseconds(uint64_t now){
 current_milliseconds_=now;
 if(calculator_cancel_waiting_&&!calculator_cancel_started_){calculator_cancel_started_=true;calculator_cancel_until_=now+1000;}
 if(!notice_clock_started_&&(notice_visible_||close_notice_visible_)){notice_started_=now;notice_clock_started_=true;}
}
void BalanceScreen::reject_action(std::string){const bool retry_close=pending_&&pending_->kind==BalanceAction::confirm_close;pending_.reset();if(retry_close){close_notice_visible_=true;notice_clock_started_=false;return;}calculator_open_=false;notice_visible_=false;}
bool BalanceScreen::calculator_open()const{return calculator_open_;}
bool BalanceScreen::notice_open()const{return notice_visible_||close_notice_visible_;}
std::string BalanceScreen::state_json()const{
 std::ostringstream o;o<<"{\"manager_slot\":"<<state_.identity.manager_slot<<",\"runtime_club\":"<<state_.identity.runtime_club<<",\"revision\":"<<state_.identity.revision<<",\"scope\":"<<state_.identity.scope<<",\"sequence\":"<<state_.identity.sequence<<",\"view\":"<<int(state_.draft.view)<<",\"calculator_x\":"<<state_.calculator_x<<",\"calculator_y\":"<<state_.calculator_y<<",\"calculator_open\":"<<(calculator_open_?"true":"false")<<",\"notice_open\":"<<(notice_open()?"true":"false")<<",\"pending_close\":"<<(state_.pending_close?"true":"false")<<",\"pending\":";
 if(!pending_)o<<"null";else{o<<"{\"kind\":"<<int(pending_->kind)<<",\"row\":"<<pending_->row<<",\"value\":"<<pending_->value<<"}";}o<<'}';return o.str();
}
} // namespace bmh::ui
