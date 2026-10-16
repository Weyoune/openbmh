// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/transfer_market.hpp"

#include <cstdio>
#include <set>
#include <string>
#include "game/state.hpp"

using namespace bmh;
using namespace bmh::ui;
namespace {
int failures=0;
void check(bool ok,const char* message){if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);++failures;}}
void put32(std::vector<uint8_t>& v,size_t at,uint32_t n){for(int i=0;i<4;++i)v.at(at+size_t(i))=uint8_t(n>>(8*i));}
GameState fixture(){
    GameState g;g.manager_teams={1};g.manager_records.resize(5*0xb0c);put32(g.manager_records,0x314,900000);put32(g.manager_records,0x31c,500000);
    g.memory.add_block(::bmh::layout::ActiveManager,1);g.memory.set8(::bmh::layout::ActiveManager,0);
    g.memory.add_block(::bmh::layout::EditorList,40*0x58);
    for(int entry=0;entry<40;++entry){for(int bid=0;bid<4;++bid)g.memory.set16(::bmh::layout::EditorList+uint32_t(entry)*0x58+uint32_t(bid)*2,0xffff);g.memory.set16(::bmh::layout::EditorList+uint32_t(entry)*0x58+8,2);}
    g.memory.add_block(::bmh::layout::Savestate_Layout_ExecutableField3,2);g.memory.set16(::bmh::layout::Savestate_Layout_ExecutableField3,0x1f0);
    g.squad_slots.resize(3072,0x0fff);g.squad_slots[24]=0x8000;g.squad_slots[2928+4]=1;
    g.players={{"Squad",60u<<7|70u<<14|70u,0,uint32_t(23-16)<<21|1},{"Market",64u<<7|70u<<14|70u,0,uint32_t(25-16)<<21|3}};
    g.player_name_offsets={0,6};g.player_names={'S','q','u','a','d',0,'M','a','r','k','e','t',0};
    g.club_ids={0,6,13};g.club_names={'O','t','h','e','r',0,'B','a','y','e','r','n',0,'V','a','l','e','n','c','i','a',0};
    g.tactics.resize(5*0x840);return g;
}
TransferMarketState state(){return transfer_market_from_game(fixture(),4);}
TransferMarketPreview quote(const TransferMarketRequest& r){
    TransferMarketPreview q;q.manager_slot=r.manager_slot;q.runtime_club=r.runtime_club;q.revision=r.revision;
    q.source_slot=r.preview_source_slot;q.player=r.expected_player;q.seller=r.preview_seller;q.purchase=r.preview_is_purchase;q.loan=r.preview_is_loan;
    q.delta=-120000;q.projected_balance=780000;return q;
}
std::vector<int> visible(const TransferMarketScreen& screen){
    const auto json=screen.state_json();const std::string prefix="\"visible_market_entries\":[";const auto start=json.find(prefix)+prefix.size();
    std::vector<int> rows;size_t at=start;
    while(at<json.size()&&json[at]!=']'){size_t end=at;while(end<json.size()&&json[end]!=','&&json[end]!=']')++end;rows.push_back(std::stoi(json.substr(at,end-at)));at=end;if(json[at]==',')++at;}
    return rows;
}
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    try{
        auto g=fixture();const auto slots=g.squad_slots;const auto attributes=g.players;const auto records=g.manager_records,tactics=g.tactics;
        auto s=transfer_market_from_game(g,4);
        check(s.manager_slot==0&&s.runtime_club==1&&s.items.size()==1&&s.items[0].source_slot==2932,"Native projection preserves absolute pool identity");
        check(s.roster.players.size()==24&&s.squad.size()==1&&s.roster.players[0].displayed_slot==0x8000,"Source roster retains all physical24 rows instead of fixed16 truncation");
        check(g.squad_slots==slots&&g.manager_records==records&&g.tactics==tactics&&g.players[0].w35c==attributes[0].w35c,"pure projection preserves Native inputs");
        TransferMarketScreen screen(argv[1],s);const auto initial=screen.render();
        check(initial.width==320&&initial.height==240,"Original overview is320x240");
        const auto frame=load_vga(asset_path(std::filesystem::path(argv[1])/"KB","129.VGA"));
        const auto pane=load_vga(asset_path(std::filesystem::path(argv[1])/"KB","56.VGA"));
        check(initial.pixels[150*320+120]==frame.pixels[150*320+120],"left body keeps KB129 rather than global KB56 replacement");
        check(initial.pixels[21*320+185]==pane.pixels[1*318+184]&&initial.pixels[20*320+184]==0x46&&initial.pixels[153*320+184]==0x40,"format field right pane source offsets and border colors");
        screen.click(10,219);check(screen.state_json().find("\"data_page\":\"B\"")!=std::string::npos&&screen.render().pixels!=initial.pixels,"SourceDataB button changes owned roster statistics");
        screen.click(65,219);check(screen.state_json().find("\"source_view_flags\":2545")!=std::string::npos,"Komplett toggles only Sourcebit0 after SALE entrymask");
        check(screen.render().pixels[110*320+185]==frame.pixels[110*320+185],"Komplett restores full original roster background");
        screen.click(65,219);
        auto request=screen.move(210,28);
        check(request.kind==TransferMarketRequestKind::preview_quote&&request.preview_source_slot==2932&&request.expected_player==1&&request.preview_is_purchase&&!request.preview_is_loan,"selection requests typed format field quote without price heuristic");
        check(screen.key("ENTER").kind==TransferMarketRequestKind::none,"outstanding owner quote suppresses duplicate requests");
        auto invalid=quote(request);invalid.player=0;bool rejected=false;try{screen.acknowledge_preview(s,invalid);}catch(const FormatError&){rejected=true;}
        check(rejected&&screen.pending()&&screen.state().revision==4,"mismatched quote leaves pending identity and state intact");
        screen.acknowledge_preview(s,quote(request));check(screen.click(210,28).kind==TransferMarketRequestKind::none,"click marks the already quoted row without a second quote");check(!screen.pending()&&screen.render().pixels!=initial.pixels,"validated owner preview renders signed delta and projected balance");
        request=screen.click(200,206);check(request.kind==TransferMarketRequestKind::open_offer&&request.bid.source_slot==2932&&request.bid.fee==0&&request.bid.annual_salary==0,"new offer delegates Original1b556 initialization rather than synthesizing UI defaults");
        check(!screen.text("1000"),"overview never renders or edits old KB23 free-form offer");
        screen.acknowledge_unchanged();request=screen.click(20,32);
        check(request.kind==TransferMarketRequestKind::preview_quote&&!request.preview_is_purchase&&request.preview_source_slot==24,"own roster preview uses actual squad source slot");
        screen.acknowledge_preview(s,quote(request));request=screen.key("V");
        check(request.kind==TransferMarketRequestKind::list_player&&request.squad_row==0&&request.expected_player==0,"existing listing request retains physical row stale-validation boundary");
        screen.reject_action("Für Müller: Aktion offen");check(screen.state_json().find("Für Müller")!=std::string::npos,"host feedback remains UTF8");

        auto all=s;all.items.clear();for(int n=0;n<40;++n){auto item=s.items[0];item.list_entry=n;item.source_slot=2928+n;item.player=100+n;item.name="P"+std::to_string(n);item.position=n%4;item.strength=60+n%7;all.items.push_back(item);}
        TransferMarketScreen pages(argv[1],all);const auto first=visible(pages);std::set<int> reached;
        for(int page=0;page<7;++page){const auto rows=visible(pages);check(rows.size()==size_t(page==6?4:6),"six original rows per page with final four");reached.insert(rows.begin(),rows.end());pages.click(210,45,true);}
        check(reached.size()==40&&visible(pages)==first&&!pages.close_requested(),"all40 ordinary entries are reachable by Source right-click page and wrap");
        pages.click(232,158);const auto no_keeper=visible(pages);bool excludes_keeper=true;for(int n:no_keeper)excludes_keeper&=n%4!=0;check(excludes_keeper,"Source TOR filter removes keeper entries");
        pages.click(189,158);check(visible(pages)==first,"Source ALLE resets all positions and first page");
        pages.click(189,180);check(pages.state_json().find("\"source_view_flags\":4080")!=std::string::npos,"Aktive/Team swaps both Sourcebits9/10");
        auto ties=s;ties.items.assign(4,s.items[0]);for(int n=0;n<4;++n)ties.items[size_t(n)].list_entry=n;
        TransferMarketScreen equal(argv[1],ties);check(visible(equal)==std::vector<int>({2,3,0,1}),"format field equal-key swaps retain Original page order");

        auto own=s;TransferBidRequest bid;bid.fee=123456;bid.annual_salary=456789;bid.contract_years=4;for(size_t n=0;n<7;++n)bid.bonuses[n]=uint8_t(n*9+1);own.items[0].own_bid=bid;own.items[0].own_bid_pending=true;
        TransferMarketScreen edit(argv[1],own);request=edit.click(210,28);edit.acknowledge_preview(own,quote(request));request=edit.key("B");
        check(request.kind==TransferMarketRequestKind::open_offer&&request.bid.fee==bid.fee&&request.bid.annual_salary==bid.annual_salary&&request.bid.contract_years==4&&request.bid.bonuses==bid.bonuses,"open own bid preserves all7 saved bonuses and complete original terms");
        TransferMarketScreen toggle(argv[1],s);toggle.click(260,206);request=toggle.move(210,28);check(request.preview_is_loan,"unmarked Sourceaction swaps SALE/LOAN display flags for following preview");
        auto long_roster=s;long_roster.squad.clear();for(int row=0;row<24;++row){auto& p=long_roster.roster.players[size_t(row)];p.player_id=row;p.physical_row=row;p.rendered_row=row;p.original_name="P";p.attributes=s.roster.players[0].attributes;p.displayed_slot=uint16_t((row<11?0x8000:0x4000)|row);long_roster.squad.push_back({row,row,p.displayed_slot,"P",23,1,70,false});}
        TransferMarketScreen full_roster(argv[1],long_roster);request=full_roster.click(20,198);
        check(request.kind==TransferMarketRequestKind::preview_quote&&request.squad_row==23&&request.preview_source_slot==47,"24th physical roster player remains selectable with Sourceflag-group gap");
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());++failures;}
    return failures?1:0;
}
