// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <vector>
#include "formats/fbm.hpp"
#include "formats/savestate.hpp"
#include "formats/textd.hpp"
#include "game/save.hpp"
#include "game/season.hpp"
#include "game/messages.hpp"

using namespace bmh;
void check(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
void put(GameState& g, size_t at, uint32_t v) {
    for (size_t i=0;i<4;++i)g.manager_records.at(at+i)=uint8_t(v>>(8*i));
}
uint32_t get(const GameState& g,size_t at) {
    uint32_t v=0;for(size_t i=0;i<4;++i)v|=uint32_t(g.manager_records.at(at+i))<<(8*i);return v;
}
int main(int argc,char**argv) {try {
    check(argc==3,"usage: season_tax_probe DATA GAMEND.FBM");
    if (!*argv[1]) { std::puts("season tax tests: skipped (Original data directory not configured)"); return 77; }
    const auto save=load_savegame(argv[2]);check(save.checksum_ok,"fixture checksum");
    const SaveState raw(save.stream);auto original=GameState::from_save(raw);
    const auto texts=load_textd(std::filesystem::path(argv[1])/"TEXTD.BLK");
    original.texts=&texts;
    put(original,0x314,9503749);put(original,0x328,0);put(original,0x32c,0);
    original.manager_records.at(0x26f)=50;
    for(size_t i=0;i<60;++i)original.manager_ratings.at(i)=120;
    for(int branch=0;branch<3;++branch) {
        auto g=original;auto before=g.mailbox;GameRandom game(branch==2?225000:1);WatcomRandom rand(1);
        std::vector<SeasonTaxEventKind> order;bool question=false;
        SeasonTaxHooks hooks;
        hooks.observe=[&](const GameState& state,const SeasonTaxEvent& event) {
            order.push_back(event.kind);
            check(get(state,0x314)==9503749&&get(state,0x328)==0&&get(state,0x32c)==0,
                  "ACK/animation/outcome must precede debit and carry storage");
            check(event.profit==5903700&&event.legal_tax==2801850&&event.manager_slot==0,
                  "Original GameEnd profit/tax identity");
            if(event.kind==SeasonTaxEventKind::notice) {
                check(event.text==message_text(state,::bmh::layout::Season_SeasonApplyProfitTax_TextPointer)+format_amount(state,5903700)+
                    message_text(state,::bmh::layout::Season_SeasonApplyProfitTax_TextPointer2)+format_amount(state,2801850)+").",
                    "notice uses TEXTD875/876 and exact format field literal");
            }
            if(event.kind==SeasonTaxEventKind::animation)
                check(event.animation_variant==(branch==2?4:2),"Source animation variant");
        };
        hooks.choose=[&](const GameState& state,const SeasonTaxEvent& event) {
            check(order.size()==1&&order[0]==SeasonTaxEventKind::notice,"ACK before question");
            check(event.kind==SeasonTaxEventKind::question&&event.text==message_text(state,::bmh::layout::Season_SeasonApplyProfitTax_TextPointer3),
                  "Source question TEXTD877");
            check(get(state,0x314)==9503749&&get(state,0x328)==0&&get(state,0x32c)==0,
                  "question before booking");
            question=true;return branch!=0;
        };
        season_apply_profit_tax(g,0,5903700,game,rand,hooks);
        check(question&&g.mailbox==before,"Original dialog is not mailbox delivery");
        check(get(g,0x32c)==5903700,"taxable carry");
        if(branch==0) {
            check(get(g,0x328)==2801850&&get(g,0x314)==6701899,"NO tax debit matches post3 before tickets");
            check(game.calls()==0&&rand.calls()==0&&game.seed()==1&&rand.seed()==1,"NO consumes no random draws");
            check(g.manager_records.at(0x26f)==50&&g.manager_ratings==original.manager_ratings,
                  "NO leaves board patience and ratings unchanged");
            check(order.size()==1,"NO has no YES animation or result dialog");
        } else {
            check(order.size()==3&&order[1]==SeasonTaxEventKind::animation&&order[2]==SeasonTaxEventKind::outcome,
                  "YES Source event order");
            if(branch==1) {
                check(get(g,0x328)==1681110&&get(g,0x314)==7822639,"successful adjustment uses Source60percent");
                check(game.calls()==2&&rand.calls()==1&&game.seed()==3095&&rand.seed()==1103527590,
                      "success draws Game1..10,Watcom1..3,Game6..9");
                check(g.manager_records.at(0x26f)==50&&g.manager_ratings==original.manager_ratings,
                      "success has no penalty");
            } else {
                check(get(g,0x328)==2801850&&get(g,0x314)==6501899,"caught branch pays legal tax plus200000 fine");
                check(game.calls()==1&&rand.calls()==1&&game.seed()==3600167&&rand.seed()==1103527590,
                      "caught branch does not draw success animation or tax multiplier");
                check(g.manager_records.at(0x26f)==40,"caught board patience minus10");
                for(size_t i=0;i<60;++i)check(g.manager_ratings[i]==(i>=40&&i<50?0:120),
                    "only competition4 ten ratings cleared");
            }
        }
        std::printf("branch=%s profit=5903700 paid=%u cash=%u draws=%llu/%llu OK\n",
            branch==0?"NO":branch==1?"YES-success":"YES-caught",get(g,0x328),get(g,0x314),
            (unsigned long long)game.calls(),(unsigned long long)rand.calls());
    }
    auto g=original;GameRandom game(1);WatcomRandom rand(1);
    season_apply_profit_tax(g,0,5903700,game,rand);
    check(get(g,0x314)==6701899&&game.calls()==0&&rand.calls()==0&&g.mailbox==original.mailbox,
          "headless path uses identical NO without posting dialog to mailbox");
    const auto before=save_stream(g);season_apply_profit_tax(g,0,100000,game,rand);
    check(save_stream(g)==before&&game.calls()==0&&rand.calls()==0,"strict tax threshold no-op");
    std::puts("headless-NO and threshold OK");
    // format field jumps into the annual loop. Its fine local is initialised
    // only once; a caught first manager's fine is charged to later managers too.
    auto two = original;
    two.manager_teams.push_back(2);
    constexpr size_t next = 0xb0c;
    put(two,next+0x314,9503749);put(two,next+0x328,0);put(two,next+0x32c,0);
    two.manager_records.at(next+0x26f)=50;
    SeasonTaxState shared;
    SeasonTaxHooks yes;yes.choose=[](const GameState&,const SeasonTaxEvent&){return true;};
    GameRandom tg(225000);WatcomRandom tw(1);
    season_apply_profit_tax(two,0,5903700,tg,tw,yes,&shared);
    check(shared.carried_fine==200000,"caught fine survives Source annual loop continuation");
    const auto game_calls=tg.calls(),watcom_calls=tw.calls();
    season_apply_profit_tax(two,1,100000,tg,tw,{},&shared);
    check(shared.carried_fine==200000,"nontaxable manager does not clear Source fine local");
    season_apply_profit_tax(two,1,5903700,tg,tw,{},&shared);
    check(get(two,next+0x328)==2801850&&get(two,next+0x314)==6501899&&
          two.manager_records.at(next+0x26f)==50&&tg.calls()==game_calls&&tw.calls()==watcom_calls,
          "later NO pays carried fine without a new draw or board penalty");
    std::puts("multi-manager Source fine carry OK");
    auto novice=original;novice.level=0;GameRandom nrg(1);WatcomRandom nrw(1);
    season_apply_profit_tax(novice,0,5903700,nrg,nrw);
    check(get(novice,0x314)==9503749&&get(novice,0x328)==2801850&&get(novice,0x32c)==5903700,
          "difficulty0 Source money guard still stores annual tax carries");
    auto low=original;low.manager_records.at(0x26f)=4;GameRandom lrg(225000);WatcomRandom lrw(1);
    season_apply_profit_tax(low,0,5903700,lrg,lrw,yes);
    check(low.manager_records.at(0x26f)==0,"caught Source board penalty clamps atzero");
    std::puts("difficulty0 and board clamp OK");
}catch(const std::exception&e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
