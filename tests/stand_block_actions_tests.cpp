// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "game/desk_actions.hpp"

using namespace bmh;

namespace {
void check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
uint32_t u32(const std::vector<uint8_t>& v, size_t at) {
    return uint32_t(v.at(at)) | uint32_t(v.at(at+1))<<8 | uint32_t(v.at(at+2))<<16 | uint32_t(v.at(at+3))<<24;
}
void set32(std::vector<uint8_t>& v, size_t at, uint32_t value) {
    for (int i=0;i<4;++i) v.at(at+size_t(i))=uint8_t(value>>(8*i));
}
// Authored synthetic investment data: deliberately not an executable-table copy.
constexpr StadiumInvestmentConstants constants = [] {
    StadiumInvestmentConstants values{};
    for (size_t i = 0; i < 8; ++i) {
        values.price[i] = uint32_t(150000 + 50000 * i);
        values.weeks[i] = uint8_t(12 + i);
    }
    return values;
}();
GameState state_with_funds(uint32_t money) {
    GameState g; g.level=1;g.manager_teams.push_back(0);
    g.manager_records.assign(0xb0c,0x5a);
    set32(g.manager_records,0x314,money);set32(g.manager_records,0x31c,0);
    set32(g.manager_records,0x324,0x12345678);set32(g.manager_records,0x8a0,0);
    g.manager_records[0x290]=g.manager_records[0x291]=g.manager_records[0x171]=0;
    for (size_t c=0;c<8;++c) {
        g.manager_records[0x2c+c]=0;g.manager_records[0x3c+c]=(c==2||c==3)?1:0;
    }
    g.memory.add_block(::bmh::layout::CareerSession_Invalid_RuntimeField2,4);g.memory.set32(::bmh::layout::CareerSession_Invalid_RuntimeField2,0x12345678);
    return g;
}
StadiumInvestment choice(int n) {return StadiumInvestment(n);}
auto options(const GameState& g,int c,int percent=100) {
    return stadium_investment_options_source(g,0,choice(c),percent,constants);
}
StandBlockOffer no_tip(const StadiumInvestmentOption& option,WatcomRandom& rand) {
    auto offer=prepare_stand_block_offer(option.base_price,option.base_weeks,rand);
    check(!resolve_stand_block_offer(offer,false,rand),"NO tip leaves base terms and still resolves");
    return offer;
}
bool build(GameState& g,int c,const StadiumInvestmentOption& option,const StandBlockOffer& offer,
           WatcomRandom& rand,int percent=100,const ConstructionNoticeHook& notice={}) {
    return build_stadium_investment_source(g,0,choice(c),percent,constants,option,offer,rand,notice);
}
void neutral_invalid(GameState g,int c,StadiumInvestmentOption option,StandBlockOffer offer,const char* message) {
    const auto before=g.manager_records;WatcomRandom rand(91);
    check(!build(g,c,option,offer,rand),message);
    check(g.manager_records==before&&rand.calls()==0&&rand.seed()==91,
          "invalid/stale investment is rejected before payment or sponsor RNG");
}
}

int main() {
    {
        WatcomRandom rand(0x12345678),expected(0x12345678);
        const int percent=expected.range(6,14);
        auto offer=prepare_stand_block_offer(100000,6,rand);
        check(rand.calls()==1&&rand.seed()==expected.seed(),"tip prompt draws W(6,14) exactly once");
        check(offer.percent==percent&&offer.offered_price==uint32_t(100000*(100+percent)/100),
              "tip prompt projects exact percent and price without paying");
        check(!resolve_stand_block_offer(offer,false,rand)&&offer.decision==StandBlockOfferDecision::Declined&&
              offer.price==100000&&offer.construction_days==6&&rand.calls()==1,
              "NO tip keeps base price/weeks and consumes no shortening draw");
        const auto seed=rand.seed();
        check(!resolve_stand_block_offer(offer,true,rand)&&rand.calls()==1&&rand.seed()==seed,
              "replayed tip answer cannot consume RNG");
    }
    {
        WatcomRandom rand(0x87654321),expected(0x87654321);
        const int percent=expected.range(6,14),reduction=expected.range(6>>3,6>>1);
        auto offer=prepare_stand_block_offer(200000,6,rand);
        check(resolve_stand_block_offer(offer,true,rand)&&rand.calls()==2&&rand.seed()==expected.seed(),
              "YES tip adds exactly Source W(weeks>>3,weeks>>1)");
        check(offer.price==uint32_t(200000*(100+percent)/100)&&offer.construction_days==6-reduction,
              "YES tip applies markup and exact shortening");
    }
    {
        GameState g=state_with_funds(20000000);const auto before=g.manager_records;
        for(int c=0;c<8;++c) {
            const auto projected=options(g,c,90);
            check(projected.size()==size_t(c==2||c==3?1:3),"all eight choices expose Source-valid option count");
            for(size_t o=0;o<projected.size();++o) {
                const auto& q=projected[o];
                check(q.current_status==g.manager_records[0x3c+size_t(c)]&&q.increment==o+1&&
                      q.target_level==q.current_level+o+1&&q.base_price==constants.price[size_t(c)]*(o+1)*90/100&&
                      q.base_weeks==constants.weeks[size_t(c)],"Source facility price/duration/relative level mapping");
            }
        }
        check(options(g,0).front().base_price==constants.price[0]&&options(g,0).front().base_weeks==constants.weeks[0],
              "first facility uses the supplied synthetic price and duration");
        check(options(g,6).front().base_price==constants.price[6]&&options(g,6).front().base_weeks==constants.weeks[6]&&
              options(g,7).front().base_price==constants.price[7]&&options(g,7).front().base_weeks==constants.weeks[7],
              "missing Condition/Safety choices use literal Original tables");
        check(g.manager_records==before,"option projection is Native-pure");
        for(int c:{0,1,4,5,6,7})for(int level=0;level<=4;++level) {
            g.manager_records[0x3c+size_t(c)]=uint8_t(level);
            const auto p=options(g,c);
            check(p.size()==size_t(std::min(3,4-level)),"normal choice stops at level4");
            if(!p.empty())check(p.back().target_level==level+int(p.size()),"normal options are increments from current level");
        }
        for(int c:{2,3}) {
            g.manager_records[0x3c+size_t(c)]=2;
            check(options(g,c).empty(),"heating/VIP terminal level2 has no construction choice");
            g.manager_records[0x3c+size_t(c)]=1;
            check(options(g,c).size()==1&&options(g,c).front().target_level==2,
                  "heating/VIP have one upgrade1to2, never three levels");
            g.manager_records[0x3c+size_t(c)]=0;
            check(options(g,c).size()==1&&options(g,c).front().increment==1&&
                  options(g,c).front().target_level==1,"special raw0 exposes exactly one fixed base tier");
        }
    }
    {
        GameState g=state_with_funds(20000000);const auto before=g.manager_records;
        for(int c=0;c<8;++c) {
            auto state=g;const auto option=options(state,c).front();WatcomRandom rand(44),expected(44);
            expected.range(6,14);auto offer=no_tip(option,rand);
            auto want=before;
            set32(want,0x314,20000000-option.base_price);set32(want,0x8a0,option.base_price);
            want[0x2c+size_t(c)]=option.base_weeks;
            const uint8_t old=want[0x3c+size_t(c)];want[0x3c+size_t(c)]=uint8_t(old|uint8_t((old+1)<<4));
            check(build(state,c,option,offer,rand),"NO tip still pays and constructs every Facility/Maintain choice");
            check(state.manager_records==want,"accepted Native footprint only money/statistics/facility weeks+status");
            check(rand.calls()==1&&rand.seed()==expected.seed(),"NO tip construction adds no random draw without sponsor");
            const auto committed=state.manager_records;
            check(!build(state,c,option,offer,rand)&&state.manager_records==committed&&rand.calls()==1,
                  "replayed accepted investment cannot repeat payment or construction");
        }
    }
    {
        GameState g=state_with_funds(20000000);
        const auto old_option=options(g,0).front();WatcomRandom rand(17);
        auto offer=no_tip(old_option,rand);g.manager_records[0x3c]=0x09;
        const auto before=g.manager_records;const auto seed=rand.seed();const auto calls=rand.calls();
        check(options(g,0).empty(),"Source full raw0x09 counter offers no ordinary tiers");
        check(!build(g,0,old_option,offer,rand)&&g.manager_records==before&&
              rand.seed()==seed&&rand.calls()==calls,"ordinary rawbit3 cannot accept an invented masked tier");
        for(int c:{2,3})for(int status:{0,3,7,9,10,15}) {
            g=state_with_funds(20000000);g.manager_records[0x3c+size_t(c)]=uint8_t(status);
            const auto projected=options(g,c);
            if((status&7)==2) {check(projected.empty(),"special masked2 is terminal including rawbit3");continue;}
            check(projected.size()==1&&projected.front().increment==1&&
                  projected.front().target_level==((status+1)&7),"special literal nonterminal branch offers one tier");
            const auto option=projected.front();WatcomRandom special_rand(17);auto tip=no_tip(option,special_rand);
            check(build(g,c,option,tip,special_rand)&&g.manager_records[0x3c+size_t(c)]==
                  uint8_t(status|uint8_t((status+1)<<4)),"special Source raw increment and byte-narrowed flags are retained");
        }
    }
    {
        GameState g=state_with_funds(20000000);g.manager_records[0x3c+6]=2;
        const auto option=options(g,6,93).at(1);WatcomRandom rand(0x89abcdef),expected(0x89abcdef);
        const int tip=expected.range(6,14),shortening=expected.range(constants.weeks[6]>>3,constants.weeks[6]>>1);
        auto offer=prepare_stand_block_offer(option.base_price,option.base_weeks,rand);
        check(resolve_stand_block_offer(offer,true,rand),"YES tip resolves once");
        check(build(g,6,option,offer,rand,93)&&g.manager_records[0x32]==constants.weeks[6]-shortening&&g.manager_records[0x42]==0x42,
              "Condition relative two-level upgrade builds target4 with shortened duration");
        const uint32_t paid=uint32_t(uint64_t(constants.price[6]*2*93/100)*(100+tip)/100);
        check(u32(g.manager_records,0x314)==20000000-paid&&u32(g.manager_records,0x8a0)==paid&&
              rand.calls()==2&&rand.seed()==expected.seed(),"YES tip exact debit/statistics and final W seed");
    }
    {
        GameState g=state_with_funds(20000000);g.manager_records[0x8b4]=0xff;g.manager_records[0x8b5]=0x9b;
        GameRandom game(7),expected(7);expected.set_seed(0x12345678);
        const int want=100-expected.range(-256,-251); // int16(0x9bff)=-25601 -> /100=-256
        const int percent=construction_price_percent(g,0,game);
        check(percent==want&&percent>100&&game.seed()==expected.seed()&&game.calls()==expected.calls(),
              "Original negative signed rating produces discount percentage above100 with exact G draw");
        const auto before_g=game.seed();const auto before_calls=game.calls();
        const auto option=options(g,0,percent).front();WatcomRandom rand(29);auto offer=no_tip(option,rand);
        check(option.base_price==uint32_t(constants.price[0]*percent/100)&&build(g,0,option,offer,rand,percent),
              "authentic percentage above100 remains supported in projection and acceptance");
        check(game.seed()==before_g&&game.calls()==before_calls,"pure options/payment do not draw the separate G generator");
        check(options(state_with_funds(20000000),7,427).back().base_price==uint32_t(uint64_t(constants.price[7])*3*427/100),
              "positive int16-derived Source percentage upper boundary427 is supported");
    }
    {
        GameState g=state_with_funds(20000000);const auto option=options(g,4).front();WatcomRandom quote(1);
        const auto valid=no_tip(option,quote);
        auto bad_option=option;--bad_option.base_price;auto bad_offer=valid;
        --bad_offer.base_price;--bad_offer.price;
        bad_offer.offered_price=uint32_t(uint64_t(bad_offer.base_price)*(100+bad_offer.percent)/100);
        neutral_invalid(g,4,bad_option,bad_offer,"internally consistent cheap forged quote is rejected against Original price");
        bad_option=option;++bad_option.target_level;
        neutral_invalid(g,4,bad_option,valid,"forged target level is not an owned option");
        bad_option=option;++bad_option.base_weeks;
        neutral_invalid(g,4,bad_option,valid,"forged duration is not an owned option");
        bad_offer=valid;bad_offer.decision=StandBlockOfferDecision::Pending;
        neutral_invalid(g,4,option,bad_offer,"unresolved tip cannot pay");
        bad_offer=valid;bad_offer.decision=StandBlockOfferDecision::Accepted;
        bad_offer.price=bad_offer.offered_price;bad_offer.construction_days=option.base_weeks;
        // Use a longer Original duration whose minimum shortening is nonzero.
        const auto long_option=options(g,0).front();WatcomRandom long_quote(3);bad_offer=no_tip(long_option,long_quote);
        bad_offer.decision=StandBlockOfferDecision::Accepted;bad_offer.price=bad_offer.offered_price;
        neutral_invalid(g,0,long_option,bad_offer,"YES tip with no required shortening is forged");
        auto changed=g;changed.manager_records[0x40]=1;
        neutral_invalid(changed,4,option,valid,"changed current level makes the old quote stale");
        changed=g;changed.manager_records[0x40]=8;
        neutral_invalid(changed,4,option,valid,"changed raw bit3 makes the old quote stale");
        changed=g;changed.manager_records[0x30]=1;
        neutral_invalid(changed,4,option,valid,"construction already active makes the quote stale");
        changed=g;changed.manager_records[0x40]=4;
        neutral_invalid(changed,4,option,valid,"terminal level rejects an old quote");
        const auto special=options(g,2).front();auto forged=special;++forged.increment;++forged.target_level;
        WatcomRandom special_quote(7);const auto special_offer=no_tip(special,special_quote);
        neutral_invalid(g,2,forged,special_offer,"heating never accepts a second tier option");
        for(int c:{8,9,255})neutral_invalid(g,c,option,valid,"stand capacity source IDs cannot use facility acceptance");
        changed=g;changed.manager_teams[0]=1;
        neutral_invalid(changed,4,option,valid,"manager club identity must match the current Native record");
        WatcomRandom rand(91);const auto before=g.manager_records;
        check(!build_stadium_investment_source(g,-1,choice(4),100,constants,option,valid,rand)&&
              !build_stadium_investment_source(g,1,choice(4),100,constants,option,valid,rand)&&
              g.manager_records==before&&rand.calls()==0,"invalid owner is neutral before any payment");
        auto malformed=constants;malformed.price[4]=std::numeric_limits<uint32_t>::max();
        check(!build_stadium_investment_source(g,0,choice(4),100,malformed,option,valid,rand)&&
              g.manager_records==before&&rand.calls()==0,"malformed table arithmetic is rejected before payment");
    }
    {
        // Deterministic sponsor branch: price350000 < 351000 threshold for share4.
        GameState g=state_with_funds(20000000);g.manager_records[0x171]=4;
        const auto option=options(g,4).front();WatcomRandom rand(53),expected(53);expected.range(6,14);
        auto offer=no_tip(option,rand);const auto before=g.manager_records;auto want=before;
        set32(want,0x314,20000000-245000);set32(want,0x8a0,245000);want[0x171]=0;
        want[0x30]=4;want[0x40]=0x10;
        check(build(g,4,option,offer,rand)&&g.manager_records==want&&rand.seed()==expected.seed()&&rand.calls()==1,
              "Source sponsor deterministic subsidy consumes share and no extra W draw");
    }
    {
        GameState g=state_with_funds(20000000);g.manager_records[0x171]=1;
        const auto option=options(g,4).front();WatcomRandom rand(65),expected(65);
        expected.range(6,14);const uint32_t subsidy=350000*uint32_t(expected.range(2,4))*5/100;
        auto offer=no_tip(option,rand);
        check(build(g,4,option,offer,rand)&&u32(g.manager_records,0x314)==20000000-350000+subsidy&&
              u32(g.manager_records,0x8a0)==350000-subsidy&&g.manager_records[0x171]==0&&
              rand.seed()==expected.seed()&&rand.calls()==2,"Source sponsor random subsidy follows the tip draw once");
    }
    {
        GameState g=state_with_funds(0);g.manager_records[0x171]=1;
        const auto option=options(g,4).front();WatcomRandom rand(77);auto offer=no_tip(option,rand);
        const auto before=g.manager_records;const auto seed=rand.seed();
        check(!build(g,4,option,offer,rand)&&g.manager_records==before&&rand.seed()==seed&&rand.calls()==1,
              "cash-ineligible sponsor/payment refusal leaves Native and adds no draw");
    }
    {
        // legacy routine clears eligible share before68d06's signed balance+credit
        // check. Negative credit forces a genuine refusal after sponsor effects.
        GameState g=state_with_funds(1000000);g.manager_records[0x171]=1;
        set32(g.manager_records,0x31c,uint32_t(-1000000));
        const auto option=options(g,4).front();WatcomRandom rand(81),expected(81);
        expected.range(6,14);expected.range(2,4);auto offer=no_tip(option,rand);
        auto want=g.manager_records;want[0x171]=0;
        check(!build(g,4,option,offer,rand)&&g.manager_records==want&&
              rand.seed()==expected.seed()&&rand.calls()==2,
              "genuine Source payment refusal preserves preceding sponsor consumption/draw without construction");
    }
    {
        GameState g=state_with_funds(20000000);g.manager_records[0x171]=4;
        const auto option=options(g,4).front();WatcomRandom rand(53);const auto offer=no_tip(option,rand);
        auto unpresented=g;auto unpresented_rand=rand;const auto before=g.manager_records;
        int calls=0;
        const ConstructionNoticeHook notice=[&](const GameState& current,const ConstructionNotice& event) {
            ++calls;
            check(event.manager==0&&event.kind==ConstructionNoticeKind::SponsorContribution&&
                  event.gross_price==350000&&event.subsidy==105000&&event.percent==30,
                  "contribution notice carries exact Source gross/subsidy/percent body inputs");
            check(current.manager_records==before&&current.manager_records[0x171]==4&&rand.calls()==1,
                  "sponsor notice runs before share clearing/payment/construction and adds no W draw");
        };
        check(build(g,4,option,offer,rand,100,notice)&&calls==1&&g.manager_records[0x171]==0&&
              u32(g.manager_records,0x314)==19755000&&g.manager_records[0x30]==constants.weeks[4],
              "share is cleared only after sponsor presenter returns, then payment and construction proceed");
        check(build(unpresented,4,option,offer,unpresented_rand)&&g.manager_records==unpresented.manager_records&&
              rand.seed()==unpresented_rand.seed()&&rand.calls()==unpresented_rand.calls(),
              "presented contribution has identical Native/RNG outcome to unchanged unpresented callers");
    }
    {
        GameState g=state_with_funds(1000000);g.manager_records[0x171]=1;
        set32(g.manager_records,0x31c,uint32_t(-1000000));
        const auto option=options(g,4).front();WatcomRandom rand(81);const auto offer=no_tip(option,rand);
        auto unpresented=g;auto unpresented_rand=rand;auto expected_rand=rand;
        const int percent=expected_rand.range(2,4)*5;const uint32_t subsidy=350000*uint32_t(percent)/100;
        const auto before=g.manager_records;std::vector<ConstructionNoticeKind> order;
        const ConstructionNoticeHook notice=[&](const GameState& current,const ConstructionNotice& event) {
            order.push_back(event.kind);
            check(event.manager==0&&event.gross_price==350000&&event.subsidy==subsidy&&event.percent==percent&&
                  rand.calls()==2&&rand.seed()==expected_rand.seed(),
                  "exploited/funds notices carry same exact numeric inputs and one sponsor W(2,4) draw");
            auto wanted=before;
            if(order.size()==1) {
                check(event.kind==ConstructionNoticeKind::SponsorExploited,
                      "exploited sponsor notice precedes genuine funds refusal");
            } else {
                wanted[0x171]=0;
                check(order.size()==2&&event.kind==ConstructionNoticeKind::InsufficientFunds,
                      "funds notice is second after share clearing and payment rejection");
            }
            check(current.manager_records==wanted,"each callback observes its exact Source Native checkpoint");
        };
        check(!build(g,4,option,offer,rand,100,notice)&&order.size()==2,
              "genuine negative-credit refusal presents sponsor then funds once without construction");
        check(!build(unpresented,4,option,offer,unpresented_rand)&&g.manager_records==unpresented.manager_records&&
              rand.seed()==unpresented_rand.seed()&&rand.calls()==unpresented_rand.calls(),
              "presented funds refusal retains exact unpresented sponsor/RNG effects");
    }
    {
        GameState g=state_with_funds(0);g.manager_records[0x171]=1;
        const auto option=options(g,4).front();WatcomRandom rand(77);const auto offer=no_tip(option,rand);
        const auto before=g.manager_records;const auto seed=rand.seed();int calls=0;
        const ConstructionNoticeHook notice=[&](const GameState& current,const ConstructionNotice& event) {
            ++calls;
            check(event.manager==0&&event.kind==ConstructionNoticeKind::InsufficientFunds&&
                  event.gross_price==350000&&event.subsidy==0&&event.percent==0&&
                  current.manager_records==before,
                  "cash-ineligible sponsor produces only funds notice with no invented subsidy");
        };
        check(!build(g,4,option,offer,rand,100,notice)&&calls==1&&g.manager_records==before&&
              rand.calls()==1&&rand.seed()==seed,"ordinary funds refusal has no payment/construction/sponsor draw");
        g.level=0;calls=0;
        check(pay_construction(g,0,350000,true,rand,notice)&&calls==0&&g.manager_records==before,
              "Source free difficulty does not invent a funds notice on an accepted free payment");
    }
    {
        GameState g=state_with_funds(20000000);g.manager_records[0x171]=4;
        const auto option=options(g,4).front();WatcomRandom rand(53);const auto offer=no_tip(option,rand);
        const auto before=g.manager_records;const auto seed=rand.seed();bool threw=false;
        try {
            build(g,4,option,offer,rand,100,[&](const GameState& current,const ConstructionNotice& event) {
                check(current.manager_records==before&&event.kind==ConstructionNoticeKind::SponsorContribution,
                      "throwing sponsor presenter sees uncleared share and no money write");
                throw FormatError("sponsor presenter failed");
            });
        } catch(const FormatError& e) {threw=std::string(e.what())=="sponsor presenter failed";}
        check(threw&&g.manager_records==before&&rand.seed()==seed&&rand.calls()==1,
              "presenter FormatError escapes build validation catch rather than becoming consumed refusal");
    }
    {
        GameState g=state_with_funds(1000000);g.manager_records[0x171]=1;
        set32(g.manager_records,0x31c,uint32_t(-1000000));
        const auto option=options(g,4).front();WatcomRandom rand(81);const auto offer=no_tip(option,rand);
        const auto before=g.manager_records;int calls=0;bool threw=false;
        try {
            build(g,4,option,offer,rand,100,[&](const GameState&,const ConstructionNotice& event) {
                ++calls;
                if(event.kind==ConstructionNoticeKind::InsufficientFunds)throw FormatError("funds presenter failed");
            });
        } catch(const FormatError& e) {threw=std::string(e.what())=="funds presenter failed";}
        auto wanted=before;wanted[0x171]=0;
        check(threw&&calls==2&&g.manager_records==wanted&&rand.calls()==2,
              "funds presenter exception propagates with prior Source effects for whole App checkpoint rollback");
    }
    {
        GameState g=state_with_funds(20000000);g.manager_records[0x171]=1;
        const auto option=options(g,4).front();WatcomRandom rand(65);const auto offer=no_tip(option,rand);
        const auto before=g.manager_records;auto expected=rand;expected.range(2,4);bool threw=false;
        try {
            build(g,4,option,offer,rand,100,[&](const GameState&,const ConstructionNotice&) {
                throw std::runtime_error("notice transport failed");
            });
        } catch(const std::runtime_error& e) {threw=std::string(e.what())=="notice transport failed";}
        check(threw&&g.manager_records==before&&rand.calls()==2&&rand.seed()==expected.seed(),
              "ordinary presenter exception leaves share unconsumed and propagates the already drawn sponsor roll");
        auto forged=option;++forged.base_price;const auto before_seed=rand.seed();const auto before_calls=rand.calls();
        int notices=0;
        check(!build(g,4,forged,offer,rand,100,[&](const GameState&,const ConstructionNotice&){++notices;})&&
              notices==0&&g.manager_records==before&&rand.seed()==before_seed&&rand.calls()==before_calls,
              "forged option is rejected before notice, payment and either random generator");
    }
    {
        // Periphery uses the same legacy routine notice before clearing the sponsor,
        // booking money or starting format field[object*5] construction days.
        auto g=state_with_funds(1000000); g.manager_records[0x24]=0; g.manager_records[0x171]=4;
        auto silent=g; WatcomRandom rand(53), silent_rand(53); const auto before=g.manager_records;
        int notices=0;
        check(build_periphery(g,0,6,280000,rand,[&](const GameState& current,const ConstructionNotice& event) {
            ++notices;
            check(current.manager_records==before && event.kind==ConstructionNoticeKind::SponsorContribution &&
                  event.gross_price==280000 && event.subsidy==84000 && event.percent==30,
                  "periphery contribution notice precedes sponsor clearing/payment/construction");
        }) && notices==1 && g.manager_records[0x24]==6 && g.manager_records[0x171]==0 &&
            u32(g.manager_records,0x314)==804000 && rand.calls()==0,
            "periphery contribution keeps Source net price and six construction days");
        check(build_periphery(silent,0,6,280000,silent_rand) && g.manager_records==silent.manager_records &&
              rand.seed()==silent_rand.seed() && rand.calls()==silent_rand.calls(),
              "notice forwarding preserves complete old periphery Native/RNG result");
    }
    {
        auto g=state_with_funds(1000000); g.manager_records[0x20]=0; g.manager_records[0x171]=1;
        set32(g.manager_records,0x31c,uint32_t(-1000000));
        auto silent=g; WatcomRandom rand(81), silent_rand(81), expected(81);
        const int percent=expected.range(2,4)*5; const auto before=g.manager_records;
        std::vector<ConstructionNoticeKind> order;
        check(!build_periphery(g,0,2,1000000,rand,[&](const GameState& current,const ConstructionNotice& event) {
            order.push_back(event.kind);
            auto wanted=before;
            if(event.kind==ConstructionNoticeKind::InsufficientFunds) wanted[0x171]=0;
            check(current.manager_records==wanted && event.percent==percent &&
                  rand.seed()==expected.seed() && rand.calls()==1,
                  "periphery notices preserve Source roll and sponsor-before-funds record order");
        }) && order==std::vector<ConstructionNoticeKind>{ConstructionNoticeKind::SponsorExploited,
                ConstructionNoticeKind::InsufficientFunds} && g.manager_records[0x20]==0,
            "genuine periphery refusal presents sponsor then funds without starting construction");
        check(!build_periphery(silent,0,2,1000000,silent_rand) && g.manager_records==silent.manager_records &&
              rand.seed()==silent_rand.seed() && rand.calls()==silent_rand.calls(),
              "refusal notice forwarding preserves old periphery Native/RNG result");
    }
    std::puts("stadium investments: Original eight choices, prices/levels, NO/YES tip, sponsor/refusal and owned validation OK");
    return 0;
}
