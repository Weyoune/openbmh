// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/player_card.hpp"

#include <algorithm>
#include <functional>
#include <iostream>

#include "formats/fbm.hpp"
#include "formats/savestate.hpp"
#include "game/state.hpp"

using namespace bmh;
using namespace bmh::ui;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void rejected(const std::function<void()>& action){try{action();}catch(const FormatError&){return;}throw std::runtime_error("expected rejected player card");}
GameState fixture(const LeImage& exe) {
    GameState g;g.memory.add_block(::bmh::layout::ActiveManager,1);g.memory.add_block(::bmh::layout::CareerSession_AnyOf_ExecutableField,8);g.memory.add_block(::bmh::layout::LeaguePresentationOptions,0xb4);
    g.memory.add_block(::bmh::layout::ClubShortNameOffsets,exe.u16(::bmh::layout::CareerSession_DataDirectory_RuntimeField2));g.memory.add_block(::bmh::layout::Career_RemoveJoinLoans_RuntimeField,800);g.memory.add_block(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField,200);g.memory.add_block(::bmh::layout::OfferSlots,16);
    for(int i=0;i<4;++i)g.memory.set32(::bmh::layout::OfferSlots+i*4,0xffffffff);
    g.memory.set8(::bmh::layout::ClubShortNameOffsets+12,0x02);g.memory.set16(::bmh::layout::CareerSession_AnyOf_ExecutableField,0x16);
    g.manager_teams={12};g.manager_options.resize(20);g.squad_slots.resize(3096,0xfff);g.squad_slots[12*24]=0x8001;
    g.tactics.resize(0x2940);g.players.resize(2);g.player_cards.resize(2);g.player_bonus.resize(2);
    g.player_name_offsets={0,0};g.player_names={'N','E','U','E','R',0};g.club_ids.resize(419);g.club_heap_78.resize(838);
    g.club_names={'F','C','B',0};g.players[1].w35c=51768368;g.players[1].w368=14256;g.players[1].w364=14680064;
    g.tactics[0xc]=24;g.tactics[0x50]=0xac;g.tactics[0x51]=0xee; // 61100 LE
    return g;
}
}
int main(int argc,char**argv) {
    try {
        if(argc<2)throw std::runtime_error("original data directory required");
        const std::filesystem::path data=argv[1];const auto exe=load_le(asset_path(data,"BMH.exe"));
        auto g=fixture(exe);const auto board=g.tactics,names=g.player_names;const auto squads=g.squad_slots;
        auto state=player_card_from_game(g,exe,0,19);
        check(state.row==0&&state.lineup.players[0].player_id==1&&state.lineup.revision==19,"authoritative player identity and revision");
        check(state.club_short_name=="FCB"&&state.nationality_name=="FCB","original packed club-name span and nationality pool");
        check(state.lineup.source=="engine_projection"&&state.unsupported_record.empty(),"ordinary player projection");
        check(player_card_market_value(state.lineup.players[0].attributes,exe)==2790000,"original NEUER market value, 500-point correction and 125 percent rounding");
        Player cheap;cheap.w364=1;check(!player_card_market_value(cheap,exe),"random low-value original branch stays unknown");
        PlayerCardScreen card(data,state);
        const auto before=card.render().pixels;
        auto burkardt=state;burkardt.lineup.players[0].name="BURKARDT";
        check(PlayerCardScreen(data,burkardt).render().pixels.size()==76800,
              "Source packed portrait feature at243+27 in269px KB29 rows remains renderable");
        check(card.render().pixels==before,"render is deterministic");
        check(g.tactics==board&&g.player_names==names&&g.squad_slots==squads,"projection/render never mutate authoritative data");
        g.player_names[0]='X';g.tactics[0xc]=1;
        check(card.state().lineup.players[0].name=="NEUER"&&card.state().lineup.players[0].board[0xc]==24,"projection owns player names and board");
        check(!card.click(20,100)&&!card.close_requested(),"status/cash click does not perform a financial action");
        check(card.click(100,215)&&!card.close_requested()&&card.state_json().find("player_statistics")!=std::string::npos,"unsupported statistics remains explicitly visible");
        check(card.render().pixels==before,"unsupported statistics cannot alter card state");
        check(card.click(20,215)&&card.close_requested(),"native back button closes read-only dialog");
        check(!card.click(20,215),"closed card cannot perform repeated navigation");
        PlayerCardScreen right(data,state);check(right.click(200,100,true)&&right.close_requested(),"right mouse closes dialog");
        PlayerCardScreen escape(data,state);check(escape.key("ESC")&&escape.close_requested(),"keyboard back path");
        // Monthly history is optional: an incomplete engine projection cannot
        // invent a count or chart preferences from the executable's defaults.
        g.manager_records.resize(5*0xb0c);g.manager_records[0x286]=3;
        g.memory.add_block(::bmh::layout::Flags,12);g.memory.set8(::bmh::layout::FixturePlayerCardFlagByte,0x41);
        g.tactics[0x19]=0;g.tactics[0x1a]=100;g.tactics[0x1b]=255;
        g.tactics[0x25]=40;g.tactics[0x26]=40;g.tactics[0x27]=40;
        g.tactics[0x31]=0;g.tactics[0x32]=0;g.tactics[0x33]=0;
        auto history=player_card_from_game(g,exe,0,22);
        check(history.statistics&&history.statistics->months==3&&history.statistics->display_flags==0x41,"typed manager history count and saved display preference");
        const auto absolute=player_statistics_points(history,exe,0,true);
        check(absolute==std::vector<std::pair<int,int>>{{13,136},{30,104},{48,53}},"known absolute byte-to-plot coordinates, including clamp bounds");
        const auto flat=player_statistics_points(history,exe,1,false);
        check(flat==std::vector<std::pair<int,int>>{{13,95},{30,95},{48,95}},"constant relative history occupies the original 50 percent line");
        auto shifted=history;shifted.lineup.players[0].board[0x19]=50;
        shifted.lineup.players[0].board[0x1a]=100;shifted.lineup.players[0].board[0x1b]=150;
        check(player_statistics_points(shifted,exe,0,false)==std::vector<std::pair<int,int>>{{13,136},{30,95},{48,53}},"relative nonzero minimum is captured before rescaling samples");
        auto total=history;total.statistics->months=1;
        total.lineup.players[0].board[0x19]=total.lineup.players[0].board[0x25]=total.lineup.players[0].board[0x31]=255;
        check(player_statistics_points(total,exe,3,true)==std::vector<std::pair<int,int>>{{13,53}},"total development sums all three bytes with 765 scale");
        auto full=history;full.statistics->months=12;
        check(player_statistics_points(full,exe,0,true).back().first==205,"twelve months use original seventeen-column spacing");
        auto empty=history;empty.statistics->months=0;
        check(player_statistics_points(empty,exe,0,false).empty(),"no monthly history does not create a fake point");
        rejected([&]{auto bad=history;bad.statistics->months=13;PlayerCardScreen x(data,bad);});
        rejected([&]{player_statistics_points(history,exe,4,true);});
        rejected([&]{player_statistics_points(state,exe,0,true);});
        PlayerCardScreen statistics(data,history);const auto card_frame=statistics.render().pixels;
        check(statistics.click(100,215)&&statistics.in_statistics()&&!statistics.close_requested(),"statistics opens locally over the card");
        const auto chart_frame=statistics.render().pixels;
        check(statistics.render().pixels==chart_frame,"statistics rendering is deterministic");
        check(statistics.click(165,147)&&statistics.state_json().find("\"statistics_display_flags\":67")!=std::string::npos,"strength curve toggles independently");
        check(statistics.click(275,157)&&statistics.state_json().find("\"statistics_display_flags\":131")!=std::string::npos,"ABS/REL changes both complementary original mode bits");
        check(!statistics.click(275,152),"gap between the nine-pixel mode buttons is inactive");
        check(!statistics.click(100,215)&&statistics.in_statistics(),"underlying card controls cannot close active statistics");
        PlayerCardScreen independent=statistics;
        check(statistics.key("ESC")&&!statistics.in_statistics()&&!statistics.close_requested(),"ESC returns one modal level to the card");
        const auto restored=statistics.render().pixels;
        check(std::equal(card_frame.begin(),card_frame.begin()+198*320,restored.begin()),"statistics back restores card data; native lower redraw edges are retained");
        check(independent.in_statistics(),"copied chart navigation remains independent");
        check(independent.click(25,190)&&!independent.in_statistics(),"statistics own back button returns to info");
        check(statistics.click(100,215)&&statistics.in_statistics(),"statistics can be reopened");
        check(statistics.click(100,100,true)&&!statistics.in_statistics()&&!statistics.close_requested(),"RMT in chart returns to card");
        check(statistics.state().statistics->display_flags==0x41&&g.memory.u8(::bmh::layout::FixturePlayerCardFlagByte)==0x41,"view toggles preserve projected and engine preferences");
        rejected([&]{player_card_from_game(g,exe,1,20);});
        auto invalid=state;invalid.lineup.players[0].player_id=2;rejected([&]{PlayerCardScreen x(data,invalid);});
        rejected([&]{PlayerCardScreen x(data,state,Image(1,1));});
        g.memory.set32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField,1|(15<<12)|(12<<22));g.memory.set8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField,7);
        const auto loan_word=g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField);const auto loan_months=g.memory.u8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField);
        auto loan=player_card_from_game(g,exe,0,20);
        check(loan.unsupported_record.empty()&&loan.loan&&loan.loan->owner==15&&loan.loan->borrower==12&&loan.loan->months==7,
              "ordinary borrower card owns both format field clubs and the remaining duration");
        PlayerCardScreen loan_card(data,loan);
        auto ordinary_loan=loan;ordinary_loan.loan.reset();
        const auto loan_pixels=loan_card.render().pixels,ordinary_pixels=PlayerCardScreen(data,ordinary_loan).render().pixels;
        check(!std::equal(loan_pixels.begin()+52*320,loan_pixels.begin()+70*320,ordinary_pixels.begin()+52*320),
              "Source loan header replaces the ordinary contract header");
        bool second_club=false;
        for(int y=136;y<=185;++y)for(int x=254;x<=296;++x)
            second_club|=loan_pixels[size_t(y)*320+x]!=ordinary_pixels[size_t(y)*320+x];
        check(second_club,"Source second club occupies the original lower crest pane");
        check(loan_card.click(100,215)&&loan_card.in_statistics(),"ordinary borrowed squad board retains Source player statistics");
        check(loan_card.click(100,100,true)&&loan_card.click(20,215)&&loan_card.close_requested(),"loan card returns through statistics and its own back button");
        check(g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField)==loan_word&&g.memory.u8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField)==loan_months,"loan card display and navigation preserve Native loan metadata");
        g.memory.set32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField,1|(12<<12)|(15<<22));
        auto wrong_borrower=player_card_from_game(g,exe,0,20);
        check(wrong_borrower.unsupported_record=="loan_record","lender snapshot cannot masquerade as an ordinary borrower row");
        rejected([&]{PlayerCardScreen x(data,wrong_borrower);});
        g.memory.set32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField,0);g.memory.set32(::bmh::layout::OfferSlots,1<<10);
        auto offer=player_card_from_game(g,exe,0,21);check(offer.unsupported_record=="negotiation_offer","negotiation context remains explicit");
        rejected([&]{PlayerCardScreen x(data,offer);});
        if(argc>2&&!std::string(argv[2]).empty()){
            const auto bytes=read_file(argv[2]);const auto container=load_savegame(argv[2]);
            check(container.checksum_ok&&container.version=="V2.24","reference container validation");
            SaveState save(container.stream,{exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4)});
            auto original=player_card_from_save(save,exe,0,7);
            check(original.lineup.players[0].name=="NEUER"&&original.club_short_name=="M]NCHEN"&&original.nationality_name=="DEUTSCHLAND","original player/club/nationality identity");
            check(original.lineup.players[0].board[0xc]==24&&player_card_market_value(original.lineup.players[0].attributes,exe)==2790000,"original contract and observed market value");
            PlayerCardScreen reference(data,original);check(reference.render().pixels.size()==76800,"original assets reconstruct player card");
            check(original.statistics&&original.statistics->months==0&&original.statistics->display_flags==0x40,"original July save has no invented monthly history");
            check(reference.click(100,215)&&reference.in_statistics()&&reference.render().pixels.size()==76800,"original statistics frame reconstructed from original assets");
            check(read_file(argv[2])==bytes,"original reference save unchanged");
        }
        std::cout<<"Player card owned projection, market value, read-only navigation and original rendering checks passed\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
