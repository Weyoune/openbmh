// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "app/career_session.hpp"
#include "game/save.hpp"
#include "game/stadium_blocks.hpp"
#include "game/messages.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

using namespace bmh;

namespace {
constexpr size_t kRec = 0xb0c;
constexpr size_t kBlockBuild = 0x34;
constexpr size_t kBlockFlags = 0x44;
constexpr size_t kBlockCurrent = 0x4c;
constexpr size_t kBlockFuture = 0x54;
constexpr std::array<uint32_t, 8> kPrice{300000,375000,300000,350000,300000,375000,300000,350000};
constexpr std::array<uint8_t, 8> kWeeks{30,37,30,25,30,37,30,25};
constexpr std::array<uint32_t, 8> kMax{8000,18000,8000,14000,8000,18000,8000,14000};
const StadiumBlockExpansionConstants kConstants{kPrice,kWeeks,kMax};

void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
void put16(std::vector<uint8_t>& v,size_t at,uint16_t x) {
    v.at(at)=uint8_t(x);v.at(at+1)=uint8_t(x>>8);
}
void put32(std::vector<uint8_t>& v,size_t at,uint32_t x) {
    for(int i=0;i<4;++i)v.at(at+size_t(i))=uint8_t(x>>(i*8));
}
uint32_t get32(const std::vector<uint8_t>& v,size_t at) {
    return uint32_t(v.at(at))|uint32_t(v.at(at+1))<<8|uint32_t(v.at(at+2))<<16|uint32_t(v.at(at+3))<<24;
}
uint16_t get16(const std::vector<uint8_t>& v,size_t at) {
    return uint16_t(v.at(at))|uint16_t(v.at(at+1))<<8;
}
size_t rb(int manager,size_t offset) { return size_t(manager)*kRec+offset; }
GameState make_state(const GameState& original) {
    GameState g=original;
    const int manager=0;
    check(!g.manager_teams.empty()&&g.manager_teams[0]>=0,"fixture manager0 is live");
    g.level=1;
    for(size_t s=0;s<8;++s) {
        g.manager_records.at(rb(manager,kBlockBuild+s))=0;
        g.manager_records.at(rb(manager,kBlockFlags+s))=0;
        g.manager_records.at(rb(manager,kBlockCurrent+s))=0;
        g.manager_records.at(rb(manager,kBlockFuture+s))=0;
    }
    put16(g.manager_records,rb(manager,0x2ac),0);
    put32(g.manager_records,rb(manager,0x314),100000000);
    put32(g.manager_records,rb(manager,0x31c),0);
    put32(g.manager_records,rb(manager,0x874),0);
    put32(g.manager_records,rb(manager,0x8a0),0);
    g.manager_records.at(rb(manager,0x171))=0xa5; // Source sponsor share must survive sponsor=false.
    return g;
}
auto quote(GameState& g, int slot, uint32_t total, uint8_t features=0, int percent=100) {
    return quote_stadium_block_expansion(g,0,static_cast<StadiumBlockSlot>(slot),total,features,percent,kConstants);
}
StandBlockOffer resolved_offer(uint32_t price,uint8_t days,bool yes,uint32_t seed,WatcomRandom& rand) {
    rand.srand(seed);
    StandBlockOffer offer=prepare_stand_block_offer(price,days,rand);
    resolve_stand_block_offer(offer,yes,rand);
    return offer;
}
void native_equal(const GameState& a,const GameState& b,const char* message) {
    const auto left=save_stream(a),right=save_stream(b);
    if(left!=right) {
        const size_t n=std::min(left.size(),right.size());
        size_t first=0;while(first<n&&left[first]==right[first])++first;
        std::fprintf(stderr,"Native mismatch at offset %zu: actual=%u expected=%u sizes=%zu/%zu\n",
                     first,first<left.size()?unsigned(left[first]):999u,
                     first<right.size()?unsigned(right[first]):999u,left.size(),right.size());
    }
    check(left==right,message);
}
}

int main(int argc,char** argv) {
    try {
        if(argc!=3) throw std::runtime_error("usage: stadium_block_expansion_tests <data-dir> <WEEK135.FBM>");
        app::CareerSession source(argv[1],0x1789abcdu,argv[2]);
        const GameState fixture=source.state();
        const int manager=0;
        const int club=fixture.manager_teams.at(size_t(manager));
        check(club==20,"approved fixture manager0 is club20");

        // All eight choices are separate Source slots, with exact maximum,
        // price-per-thousand and fixed-duration tables.
        {
            auto g=make_state(fixture);
            for(int s=0;s<8;++s) {
                auto q=quote(g,s,1000);
                check(q&&q->price_per_1000==kPrice[size_t(s)]&&q->base_price==kPrice[size_t(s)]&&
                      q->base_weeks==kWeeks[size_t(s)]&&q->max_places==kMax[size_t(s)],
                      "A-H quote mapping matches Original price/week/max tables");
                auto maxq=quote(g,s,kMax[size_t(s)]);
                check(maxq&&maxq->base_price==kPrice[size_t(s)]*(kMax[size_t(s)]/1000),
                      "A-H maximum target uses total-place price units");
            }
            check(!quote(g,8,1000)&&!quote(g,-1,1000),"foreign block selectors are rejected");
        }

        // Source order: floor new places to thousands; existing seat feature
        // charges only D while a new roof charges the full TOTAL T.
        {
            auto g=make_state(fixture);
            g.manager_records.at(rb(0,kBlockCurrent+1))=10;
            g.manager_records.at(rb(0,kBlockFlags+1))=1;
            auto q=quote(g,1,15333,3,95);
            check(q&&q->new_places==5333&&q->new_places_cost==1875000&&q->seats_cost==843750&&
                  q->roof_cost==1687500&&q->undiscounted_cost==4406250&&q->base_price==4185937,
                  "non-multiple total preserves Source truncation and feature-base order");
            check(!quote(g,1,15333,2,95),"existing seat feature cannot be switched off");
            g.manager_records.at(rb(0,kBlockFlags+1))=0;
            auto retrofit=quote(g,1,10000,3,100);
            check(retrofit&&retrofit->new_places==0&&retrofit->new_places_cost==0&&
                  retrofit->seats_cost==1687500&&retrofit->roof_cost==1125000,
                  "feature retrofit at zero added capacity uses total-place basis");
            g.manager_records.at(rb(0,kBlockFlags+1))=3;
            auto zero=quote(g,1,10000,3,100);
            check(zero&&zero->base_price==0,"unchanged zero-addition plan remains a valid preview");
        }

        // Pure/stale/busy/owner/bounds guards preserve the complete Native and RNGs.
        {
            auto g=make_state(fixture);auto q=quote(g,0,1000);check(q.has_value(),"normal A quote exists");
            WatcomRandom w(123);GameRandom game(987);
            auto stale=*q;
            g.manager_records.at(rb(0,kBlockFuture))=1;
            auto offer=resolved_offer(stale.base_price,stale.base_weeks,false,44,w);
            const auto before_commit=save_stream(g);const auto calls=w.calls();const auto seed=w.seed();
            check(!build_stadium_block_expansion(g,0,StadiumBlockSlot::A,kConstants,stale,offer,w),
                  "future-capacity change invalidates an otherwise matching quote");
            check(save_stream(g)==before_commit&&w.calls()==calls&&w.seed()==seed&&game.calls()==0&&game.seed()==987,
                  "stale quote has no Native or RNG effects");
            g=make_state(fixture);g.manager_records.at(rb(0,kBlockBuild))=1;
            check(!quote(g,0,1000),"busy A block has no quote");
            g=make_state(fixture);g.manager_records.at(rb(0,kBlockFlags))=0x80;
            check(!quote(g,0,1000),"unsupported idle flag bits are rejected");
            g=make_state(fixture);g.manager_teams[0]=21;
            check(!quote(g,0,1000),"wrong stable manager club is rejected");
        }

        // Zero price can be previewed but never creates a block or consumes tip RNG.
        {
            auto g=make_state(fixture);auto q=quote(g,0,0,0,100);check(q&&q->base_price==0,"zero price preview exists");
            StandBlockOffer empty;WatcomRandom w(55);const auto before=save_stream(g);
            check(!build_stadium_block_expansion(g,0,StadiumBlockSlot::A,kConstants,*q,empty,w),
                  "zero price does not construct without an offer");
            check(save_stream(g)==before&&w.seed()==55&&w.calls()==0,"zero price leaves Native and W unchanged");
        }

        // Declined tip payment writes only construction fields, amount/statistics,
        // and the derived usable-capacity byte; sponsor share remains unchanged.
        {
            auto g=make_state(fixture);auto q=quote(g,0,1000);check(q&&q->base_price==300000,"A base quote");
            WatcomRandom w(0x12345678),expected_w(0x12345678);
            auto offer=prepare_stand_block_offer(q->base_price,q->base_weeks,w);
            expected_w.range(6,14);check(!resolve_stand_block_offer(offer,false,w),"NO resolves without shortening");
            const auto seed_after_tip=w.seed();const auto calls_after_tip=w.calls();
            GameState expected=g;
            put32(expected.manager_records,rb(0,0x314),100000000-300000);
            put32(expected.manager_records,rb(0,0x8a0),300000);
            expected.manager_records.at(rb(0,kBlockBuild))=30;
            expected.manager_records.at(rb(0,kBlockFlags))=0;
            expected.manager_records.at(rb(0,kBlockFuture))=1;
            expected.manager_records.at(rb(0,0x5c))=0;
            check(build_stadium_block_expansion(g,0,StadiumBlockSlot::A,kConstants,*q,offer,w),
                  "declined tip still accepts Original block construction");
            native_equal(g,expected,"full Native stream matches the exact A construction/payment write set");
            check(w.seed()==seed_after_tip&&w.calls()==calls_after_tip&&expected_w.seed()==seed_after_tip&&
                  g.manager_records.at(rb(0,0x171))==0xa5,"commit adds no RNG and leaves sponsor share intact");
            check(g.manager_records.at(rb(0,kBlockCurrent))==0&&g.manager_records.at(rb(0,kBlockFuture))==1,
                  "current places stay unchanged until weekly completion");
        }

        // Accepted tip uses the already-resolved quote/offer and adds no commit draw.
        {
            auto g=make_state(fixture);auto q=quote(g,1,2000,0,100);check(q&&q->base_price==750000,"B quote");
            WatcomRandom w(0x87654321),expected_w(0x87654321);
            auto offer=prepare_stand_block_offer(q->base_price,q->base_weeks,w);
            const int markup=expected_w.range(6,14),shortening=expected_w.range(37>>3,37>>1);
            check(resolve_stand_block_offer(offer,true,w),"YES offer resolved once");
            const auto seed_after=w.seed();const auto calls_after=w.calls();
            GameState expected=g;const uint32_t paid=uint32_t(uint64_t(q->base_price)*uint32_t(100+markup)/100);
            put32(expected.manager_records,rb(0,0x314),100000000-paid);
            put32(expected.manager_records,rb(0,0x8a0),paid);
            expected.manager_records.at(rb(0,kBlockBuild+1))=uint8_t(37-shortening);
            expected.manager_records.at(rb(0,kBlockFuture+1))=2;
            expected.manager_records.at(rb(0,0x5c))=0;
            check(build_stadium_block_expansion(g,0,StadiumBlockSlot::B,kConstants,*q,offer,w),
                  "accepted tip commits B with exact price and duration");
            native_equal(g,expected,"full Native stream matches accepted-tip A-H fields exactly");
            check(w.seed()==seed_after&&w.calls()==calls_after&&g.manager_records.at(rb(0,0x171))==0xa5,
                  "accepted tip commit adds no draw and never changes sponsor share");
        }

        // Source computes base_price * (100 + tip_percent) in a signed 32-bit
        // intermediate before dividing. This otherwise-valid B quote has a
        // representable final YES price, but its accepted-tip intermediate
        // overflows. Reject only the YES commit and preserve the fully resolved
        // offer's RNG state and the entire Native stream.
        {
            auto g=make_state(fixture);
            auto q=quote(g,1,18000,3,180);
            check(q&&q->new_places_cost==6750000&&q->seats_cost==3037500&&
                  q->roof_cost==2025000&&q->base_price==21262500,
                  "large B quote remains within quote arithmetic bounds");
            WatcomRandom w(0x31415926);
            GameRandom game(0x27182818);
            auto offer=prepare_stand_block_offer(q->base_price,q->base_weeks,w);
            check(resolve_stand_block_offer(offer,true,w)&&offer.percent>=6&&offer.percent<=14&&
                  uint64_t(q->base_price)*uint32_t(100+offer.percent)>uint64_t(INT32_MAX)&&
                  offer.price==uint32_t(uint64_t(q->base_price)*uint32_t(100+offer.percent)/100),
                  "accepted large-tip fixture overflows Source intermediate but final amount is representable");
            const auto before=save_stream(g);
            const auto seed_after_offer=w.seed();
            const auto calls_after_offer=w.calls();
            const auto game_seed=game.seed();
            const auto game_calls=game.calls();
            check(!build_stadium_block_expansion(g,0,StadiumBlockSlot::B,kConstants,*q,offer,w),
                  "YES with signed Source tip-product overflow is refused before payment");
            check(save_stream(g)==before&&w.seed()==seed_after_offer&&w.calls()==calls_after_offer&&
                  game.seed()==game_seed&&game.calls()==game_calls,
                  "overflow rejection leaves full Native and both RNG streams unchanged after offer resolution");
        }

        // The overflow rule is specific to Source's accepted-tip multiplication.
        // The identical large quote with NO still commits the original base price.
        {
            auto g=make_state(fixture);auto q=quote(g,1,18000,3,180);
            check(q&&q->base_price==21262500,"large B decline control has same quote as YES guard");
            WatcomRandom w(0x31415926),expected_w(0x31415926);
            auto offer=prepare_stand_block_offer(q->base_price,q->base_weeks,w);
            expected_w.range(6,14);
            check(!resolve_stand_block_offer(offer,false,w)&&
                  offer.decision==StandBlockOfferDecision::Declined&&offer.price==q->base_price&&
                  offer.construction_days==q->base_weeks,"NO declines the markup and duration change");
            const auto seed_after=w.seed();const auto calls_after=w.calls();
            GameState expected=g;
            put32(expected.manager_records,rb(0,0x314),100000000-q->base_price);
            put32(expected.manager_records,rb(0,0x8a0),q->base_price);
            expected.manager_records.at(rb(0,kBlockBuild+1))=q->base_weeks;
            expected.manager_records.at(rb(0,kBlockFlags+1))=0x30;
            expected.manager_records.at(rb(0,kBlockFuture+1))=18;
            expected.manager_records.at(rb(0,0x5c))=0;
            check(build_stadium_block_expansion(g,0,StadiumBlockSlot::B,kConstants,*q,offer,w),
                  "same large quote accepts NO and commits at base price");
            native_equal(g,expected,"large NO control matches complete Source Native write set");
            check(w.seed()==seed_after&&w.calls()==calls_after&&expected_w.seed()==seed_after&&
                  expected_w.calls()==calls_after,"large NO commit adds no W draw");
        }

        // Manual TOTAL places may be nonmultiples of 1000. Existing seats are
        // charged only on new places, roof on TOTAL, and current +4c stays put.
        {
            auto g=make_state(fixture);
            g.manager_records.at(rb(0,kBlockFlags+1))=1;
            g.manager_records.at(rb(0,kBlockCurrent+1))=10;
            auto q=quote(g,1,15333,3,95);
            check(q&&q->new_places==5333&&q->new_places_cost==1875000&&
                  q->seats_cost==843750&&q->roof_cost==1687500&&q->base_price==4185937,
                  "B seat retrofit preserves Source component truncation for total15333");
            WatcomRandom w(0xabcdef01),expected_w(0xabcdef01);
            auto offer=prepare_stand_block_offer(q->base_price,q->base_weeks,w);
            expected_w.range(6,14);
            check(!resolve_stand_block_offer(offer,false,w)&&
                  offer.decision==StandBlockOfferDecision::Declined&&offer.price==4185937&&
                  offer.construction_days==37,"nonmultiple retrofit takes NO at exact base price");
            const auto seed_after=w.seed();const auto calls_after=w.calls();
            GameState expected=g;
            put32(expected.manager_records,rb(0,0x314),100000000-4185937);
            put32(expected.manager_records,rb(0,0x8a0),4185937);
            expected.manager_records.at(rb(0,kBlockBuild+1))=37;
            expected.manager_records.at(rb(0,kBlockFlags+1))=0x31;
            expected.manager_records.at(rb(0,kBlockCurrent+1))=10;
            expected.manager_records.at(rb(0,kBlockFuture+1))=15;
            expected.manager_records.at(rb(0,0x5c))=0;
            check(build_stadium_block_expansion(g,0,StadiumBlockSlot::B,kConstants,*q,offer,w),
                  "nonmultiple seat retrofit commits successful NO build");
            native_equal(g,expected,"15333 retrofit matches complete Native write set exactly");
            check(w.seed()==seed_after&&w.calls()==calls_after&&expected_w.seed()==seed_after&&
                  expected_w.calls()==calls_after,"15333 retrofit commit adds no W draw");
        }

        // Failed funds after a declined tip preserve the already consumed prompt draw,
        // but do not change any Native field or perform compensation.
        {
            auto g=make_state(fixture);put32(g.manager_records,rb(0,0x314),0);
            auto q=quote(g,0,1000);WatcomRandom w(70);auto offer=prepare_stand_block_offer(q->base_price,q->base_weeks,w);
            resolve_stand_block_offer(offer,false,w);const auto before=save_stream(g);const auto seed=w.seed();const auto calls=w.calls();
            check(!build_stadium_block_expansion(g,0,StadiumBlockSlot::A,kConstants,*q,offer,w),
                  "funds refusal does not construct the block");
            check(save_stream(g)==before&&w.seed()==seed&&w.calls()==calls,
                  "funds refusal leaves full Native and retains only the pre-commit tip draw");
        }

        // Immediate ticket compensation: notice sees the paid construction state,
        // computed usable places and tickets before the forced debit/statistics.
        {
            auto g=make_state(fixture);put16(g.manager_records,rb(0,0x2ac),1500);
            check(g.league_rounds.size()>1,"fixture has Source league round table");g.league_rounds[1]=2;
            auto q=quote(g,0,1000);WatcomRandom w(2);auto offer=resolved_offer(q->base_price,q->base_weeks,false,2,w);
            bool called=false;
            const auto notice=[&](const GameState& at_notice,const SeasonTicketCompensationNotice& n) {
                called=true;
                check(n.manager==0&&n.missing_tickets==1500&&n.usable_places==0&&n.remaining_weeks==15&&
                      n.signed_amount==405000,"compensation payload is Source-exact for league1/r15");
                check(at_notice.manager_records.at(rb(0,kBlockBuild))==30&&
                      at_notice.manager_records.at(rb(0,kBlockFuture))==1&&
                      get16(at_notice.manager_records,rb(0,0x2ac))==1500&&
                      get32(at_notice.manager_records,rb(0,0x314))==100000000-300000&&
                      get32(at_notice.manager_records,rb(0,0x8a0))==300000,
                      "notice runs after payment/construction setup and before forced compensation booking");
            };
            GameState expected=g;
            put32(expected.manager_records,rb(0,0x314),100000000-705000);
            put32(expected.manager_records,rb(0,0x8a0),705000);
            expected.manager_records.at(rb(0,kBlockBuild))=30;
            expected.manager_records.at(rb(0,kBlockFuture))=1;
            expected.manager_records.at(rb(0,0x5c))=0;
            put16(expected.manager_records,rb(0,0x2ac),0);
            check(build_stadium_block_expansion(g,0,StadiumBlockSlot::A,kConstants,*q,offer,w,{},notice)&&called,
                  "successful block invokes immediate source compensation hook");
            native_equal(g,expected,"full Native stream includes forced refund, ticket cap and expense statistics");
        }

        // Underbooked block has no compensation notice. Level zero still commits but
        // account/statistics are bypassed by the existing money routine.
        {
            auto g=make_state(fixture);g.manager_records.at(rb(0,kBlockCurrent+1))=2;
            put16(g.manager_records,rb(0,0x2ac),1500);auto q=quote(g,0,1000);
            WatcomRandom w(81);auto offer=resolved_offer(q->base_price,q->base_weeks,false,81,w);int notices=0;
            check(build_stadium_block_expansion(g,0,StadiumBlockSlot::A,kConstants,*q,offer,w,{},
                  [&](const GameState&,const SeasonTicketCompensationNotice&){++notices;}),"underbooked accepted block builds");
            check(notices==0&&g.manager_records.at(rb(0,0x5c))==2&&get16(g.manager_records,rb(0,0x2ac))==1500,
                  "existing usable 2000 places suppress compensation for 1500 tickets");
            g=make_state(fixture);g.level=0;put16(g.manager_records,rb(0,0x2ac),1500);q=quote(g,0,1000);
            w.srand(82);offer=resolved_offer(q->base_price,q->base_weeks,false,82,w);
            const auto cash=get32(g.manager_records,rb(0,0x314)),stats=get32(g.manager_records,rb(0,0x8a0));
            check(build_stadium_block_expansion(g,0,StadiumBlockSlot::A,kConstants,*q,offer,w),
                  "level-zero Source manager may build despite bypassed account");
            check(get32(g.manager_records,rb(0,0x314))==cash&&get32(g.manager_records,rb(0,0x8a0))==stats&&
                  get16(g.manager_records,rb(0,0x2ac))==0,"level zero bypasses money/statistics but still caps tickets");
        }

        // Source notice exceptions propagate. An App checkpoint around the call can
        // restore the entire state and RNG before allowing the same quote to retry.
        {
            auto g=make_state(fixture);put16(g.manager_records,rb(0,0x2ac),1500);g.league_rounds[1]=2;
            auto q=quote(g,0,1000);WatcomRandom w(91);auto offer=resolved_offer(q->base_price,q->base_weeks,false,91,w);
            const GameState checkpoint=g;const WatcomRandom rand_checkpoint=w;bool threw=false;
            try {
                build_stadium_block_expansion(g,0,StadiumBlockSlot::A,kConstants,*q,offer,w,{},
                    [](const GameState&,const SeasonTicketCompensationNotice&){throw std::runtime_error("notice failure");});
            } catch(const std::runtime_error&) { threw=true; }
            check(threw,"compensation presenter exception propagates to transaction owner");
            g=checkpoint;w=rand_checkpoint;
            check(save_stream(g)==save_stream(checkpoint)&&w.seed()==rand_checkpoint.seed()&&w.calls()==rand_checkpoint.calls(),
                  "full checkpoint rollback after presenter failure is byte- and RNG-exact");
            check(build_stadium_block_expansion(g,0,StadiumBlockSlot::A,kConstants,*q,offer,w),
                  "restored compensation transaction can retry");
        }

        // Legacy and source compensation both book statistics, including the
        // Original negative-rest-week income case (no lower clamp).
        {
            auto g=make_state(fixture);g.league_rounds[1]=20;put16(g.manager_records,rb(0,0x2ac),100);
            int noticed=0;
            season_ticket_compensation_source(g,0,[&](const GameState&,const SeasonTicketCompensationNotice& n){
                ++noticed;check(n.missing_tickets==100&&n.remaining_weeks==-3&&n.signed_amount==-5400,
                                "negative weeks preserve Source signed income amount");
            });
            check(noticed==1&&get32(g.manager_records,rb(0,0x314))==100005400&&
                  get32(g.manager_records,rb(0,0x874))==5400&&get32(g.manager_records,rb(0,0x8a0))==0&&
                  get16(g.manager_records,rb(0,0x2ac))==0,
                  "forced negative compensation updates money/income/ticket cap exactly");
            auto legacy=make_state(fixture);legacy.league_rounds[1]=2;put16(legacy.manager_records,rb(0,0x2ac),100);
            season_ticket_compensation(legacy,0);
            check(get32(legacy.manager_records,rb(0,0x8a0))==100*15*18,
                  "legacy ABI delegates through statistics-enabled Source compensation");
        }

        // A real weekly construction countdown of one completes A, shifts pending
        // features to current flags, installs capacity and performs its Source W draw.
        {
            auto g=make_state(fixture);
            for(size_t i=0;i<0x1e;++i)g.manager_records.at(rb(0,0x1e + i))=0;
            g.manager_records.at(rb(0,kBlockBuild))=1;
            g.manager_records.at(rb(0,kBlockFlags))=0x31;
            g.manager_records.at(rb(0,kBlockFuture))=9;
            if(!g.memory.has(::bmh::layout::Savestate_Layout_ExecutableField))g.memory.add_block(::bmh::layout::Savestate_Layout_ExecutableField,1);
            g.memory.set8(::bmh::layout::Savestate_Layout_ExecutableField,0);
            GameRandom game(0x10203040);WatcomRandom rand(0x55667788),expected(0x55667788);
            expected.range(0,1);expected.range(30,50);
            MinuteReport report;
            facility_ageing(g,0,game,rand,report);
            check(g.manager_records.at(rb(0,kBlockBuild))==0&&g.manager_records.at(rb(0,kBlockCurrent))==9&&
                  g.manager_records.at(rb(0,kBlockFuture))==0&&g.manager_records.at(rb(0,kBlockFlags))==3,
                  "weekly completion installs capacity and shifts high feature bits to low current flags");
            check(rand.calls()==2&&rand.seed()==expected.seed()&&game.calls()==0&&game.seed()==0x10203040,
                  "real facility_ageing uses the full timer plus Source block-completion W sequence");
        }

        // legacy routine chooses the closed occupied block independently of the
        // damaged health object. Exercise every A-H capacity with different
        // object/block indices: the mailbox label must name the same block
        // whose capacity is cleared. This is a controlled Source branch,
        // rather than evidence that a captured Original save hit that event.
        for (int closed = 0; closed < 8; ++closed) {
            auto g = make_state(fixture);
            g.level = 1;
            g.manager_records.at(rb(0, 0x178)) = 0;
            g.manager_records.at(rb(0, 0x210)) = 0;
            g.manager_records.at(rb(0, 0x42)) = 0;
            for (size_t i = 0; i < 0x1e; ++i) {
                g.manager_records.at(rb(0, i)) = 1;
                g.manager_records.at(rb(0, 0x1e + i)) = 0;
            }
            for (size_t i = 0; i < 8; ++i)
                g.manager_records.at(rb(0, kBlockCurrent + i)) = uint8_t(i + 1);
            g.manager_records.at(rb(0, 0x5c)) = 36;
            put16(g.manager_records, rb(0, 0x2ac), 0);
            std::fill(g.mailbox.begin(), g.mailbox.end(), uint8_t(0));
            const int damaged_object = 22 + (closed + 1) % 8;
            int32_t game_seed = -1;
            for (int32_t seed = 0; seed < 250000; ++seed) {
                GameRandom probe(seed);
                if (probe.range(0, 29) == damaged_object) { game_seed = seed; break; }
            }
            check(game_seed >= 0, "closure fixture finds independent damaged object G seed");
            uint32_t watcom_seed = 0;
            for (uint32_t seed = 1; seed < 100000; ++seed) {
                WatcomRandom probe(seed);
                if (probe.range(0, 175) >= 1) continue;
                probe.range(1, 3); probe.range(0, 1); probe.range(1, 3);
                if (probe.range(0, 7) == closed) { watcom_seed = seed; break; }
            }
            check(watcom_seed != 0, "closure fixture finds valid difficulty1 branch W seed");
            GameRandom game(game_seed), expected_game(game_seed);
            WatcomRandom rand(watcom_seed), expected_rand(watcom_seed);
            check(expected_game.range(0, 29) == damaged_object &&
                  expected_rand.range(0, 175) == 0,
                  "closure oracle independently accepts Source damage gates");
            expected_rand.range(1, 3); expected_rand.range(0, 1); expected_rand.range(1, 3);
            check(expected_rand.range(0, 7) == closed && damaged_object - 22 != closed,
                  "closure oracle keeps independently selected indices distinct");
            auto expected = g;
            expected.manager_records.at(rb(0, size_t(damaged_object))) = 0;
            expected.manager_records.at(rb(0, kBlockCurrent + size_t(closed))) = 0;
            expected.manager_records.at(rb(0, 0x5c)) = uint8_t(36 - closed - 1);
            post_composed(expected, 0, message_text(expected, ::bmh::layout::Weekly_SeasonTicketCompensation_TextPointer4) +
                message_text(expected, ::bmh::layout::Stadium_RenderPanorama_TextPointer + uint32_t(closed + 22) * 4) +
                message_text(expected, ::bmh::layout::Weekly_SeasonTicketCompensation_TextPointer5));
            facility_damage(g, 0, game, rand);
            native_equal(g, expected, "closed A-H capacity and announced mailbox block match exact Source Native");
            check(game.seed() == expected_game.seed() && game.calls() == expected_game.calls() &&
                  rand.seed() == expected_rand.seed() && rand.calls() == expected_rand.calls(),
                  "all-eight closure regression retains complete Source W/G sequence");
        }

        std::puts("stadium_block_expansion_tests: all checks passed");
        return 0;
    } catch(const std::exception& e) {
        std::fprintf(stderr,"stadium_block_expansion_tests: %s\n",e.what());
        return 1;
    }
}
