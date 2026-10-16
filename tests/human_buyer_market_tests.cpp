// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "formats/fbm.hpp"
#include "formats/savestate.hpp"
#include "formats/textd.hpp"
#include "game/desk_actions.hpp"
#include "game/human_buyer_sale.hpp"
#include "game/lineup.hpp"
#include "game/matchday.hpp"
#include "game/messages.hpp"
#include "game/month.hpp"
#include "game/national.hpp"
#include "game/prediction.hpp"
#include "game/save.hpp"

using namespace bmh;
namespace {
constexpr uint32_t list_start = ::bmh::layout::EditorList;
constexpr size_t pool_start = 0xb70, record_size = 0xb0c, board_size = 0x840, row_size = 0x58;
void check(bool yes, const std::string& why) { if (!yes) throw std::runtime_error(why); }
uint32_t rd(const std::vector<uint8_t>& b, size_t at) {
    uint32_t n = 0; for (size_t i = 0; i < 4; ++i) n |= uint32_t(b.at(at+i)) << (8*i); return n;
}
void wr(std::vector<uint8_t>& b, size_t at, uint32_t n) {
    for (size_t i = 0; i < 4; ++i) b.at(at+i) = uint8_t(n >> (8*i));
}
void optional_byte(GameState& g, uint32_t at, uint8_t n) {
    if (g.memory.has(at)) g.memory.set8(at,n);
}
size_t find_player(const GameState& g, int team, int player) {
    const auto r = squad_range(team);
    for (size_t n = 0; n < r.count; ++n)
        if ((g.squad_slots.at(r.first+n)&0xfff)==player) return r.first+n;
    throw std::runtime_error("expected concrete source squad owner");
}
bool contains(const GameState& g, int team, int player) {
    const auto r = squad_range(team);
    for (size_t n = 0; n < r.count; ++n)
        if ((g.squad_slots.at(r.first+n)&0xfff)==player) return true;
    return false;
}
struct Boundary {
    std::string point, text;
    int manager=-1, entry=-1, raw=-1;
    uint32_t address=0; uint16_t flags=0;
    Bytes native;
    uint32_t w_seed=0; int32_t g_seed=0;
    uint64_t w_calls=0, g_calls=0;
    int active=-1, bdb7e=-1, bdb8c=-1, bdb93=-1;
    bool suppressed=false;
};
Boundary capture(const GameState& g, const WatcomRandom& w, const GameRandom& game,
                 std::string point, int entry, int manager=-1,
                 uint32_t address=0, std::string text={}, uint16_t flags=0) {
    Boundary b;
    b.point=std::move(point); b.entry=entry; b.manager=manager; b.address=address;
    b.text=std::move(text); b.flags=flags;
    b.w_seed=w.seed(); b.w_calls=w.calls(); b.g_seed=game.seed(); b.g_calls=game.calls();
    b.active=g.memory.u8(::bmh::layout::ActiveManager); b.suppressed=g.transfer_notice_suppressed;
    const auto transient=[&](uint32_t at){return g.memory.has(at)?int(g.memory.u8(at)):-1;};
    b.bdb7e=transient(::bmh::layout::FinanceStatisticsEnabled); b.bdb8c=transient(::bmh::layout::DeskActions_EditorFail_RuntimeField2); b.bdb93=transient(::bmh::layout::Weekly_PendingSettleHumanBuyer_RuntimeField);
    b.native=save_stream(g); // Native only: no encryption and no clock/RNG draw.
    check(w.seed()==b.w_seed&&w.calls()==b.w_calls&&game.seed()==b.g_seed&&game.calls()==b.g_calls,
          "capture must not consume either RNG stream");
    return b;
}
void compare(const Boundary& actual, const Boundary& expected, const std::string& label) {
    check(actual.point==expected.point&&actual.entry==expected.entry&&actual.raw==expected.raw&&
          actual.manager==expected.manager&&actual.address==expected.address&&actual.flags==expected.flags&&
          actual.text==expected.text, label+": Source callback identity/text/flags");
    check(actual.native==expected.native, label+": full Native checkpoint");
    check(actual.w_seed==expected.w_seed&&actual.w_calls==expected.w_calls&&
          actual.g_seed==expected.g_seed&&actual.g_calls==expected.g_calls,
          label+": W/G checkpoint");
    check(actual.active==expected.active&&actual.bdb7e==expected.bdb7e&&
          actual.bdb8c==expected.bdb8c&&actual.bdb93==expected.bdb93&&actual.suppressed==expected.suppressed,
          label+": runtime/checkpoint ownership");
}

// Independent composition of SettleHumanBuyerSale -> PendingOfferAnswer -> 42707 -> ReleasePlayer/PoolString.
// Only reviewed small primitives are shared: book_money, new_board_entry,
// market_value, expected_bonus and auto_lineup. Never call production settlement,
// move_player, remove_from_seller, pending adapters or source history helpers here.
// Scope: declared ordinary 0..39 SALE control, valid 24-word human buyer,
// explicit observed allocator gate; no loan, targeted entry or runtime-PC claim.
void source_history(GameState& g, int manager, int other, int player, uint32_t fee, bool buyer) {
    const size_t base=size_t(manager)*record_size;
    for (size_t n=29;n>0;--n) {
        wr(g.manager_records,base+0x760+n*4,rd(g.manager_records,base+0x760+(n-1)*4));
        wr(g.manager_records,base+0x7d8+n*4,rd(g.manager_records,base+0x7d8+(n-1)*4));
    }
    wr(g.manager_records,base+0x760,g.date*4u+uint32_t(buyer)+(fee/10000u)*0x80000u);
    wr(g.manager_records,base+0x7d8,uint32_t(g.player_name_offsets.at(size_t(player)))+
       (other==0x0fff?0xffffu:uint32_t(g.club_ids.at(size_t(other))))*0x10000u);
    auto& n=g.manager_records.at(base+0x21d); if(n<25) ++n;
}
void source_remove(GameState& g, int seller, int seller_manager, int player) {
    if(seller==0x0fff) {
        // format field free-agent branch covers the entire free-player pool,
        // including aliases beyond the 44 ordinary/special list slots.
        for(size_t at=pool_start;at<3072;++at)
            if((g.squad_slots.at(at)&0xfff)==player) g.squad_slots.at(at)=0x0fff;
        return;
    }
    const auto r=squad_range(seller); const size_t found=find_player(g,seller,player);
    // ReleasePlayer removes every matching pool occurrence, including special rows,
    // independently of each list's retained bidder words.
    for(size_t at=pool_start;at<pool_start+44;++at)
        if((g.squad_slots.at(at)&0xfff)==player) g.squad_slots.at(at)=0x0fff;
    for(size_t at=found;at+1<r.first+r.count;++at) g.squad_slots.at(at)=g.squad_slots.at(at+1);
    g.squad_slots.at(r.first+r.count-1)=0x0fff;
    if(seller_manager>=0&&found-r.first!=23) {
        const size_t row=found-r.first, start=size_t(seller_manager)*board_size+row*row_size;
        std::memmove(g.tactics.data()+start,g.tactics.data()+start+row_size,(23-row)*row_size);
    }
    auto_lineup(g,seller,100); // embedded ReleasePlayer call, followed by caller's second seller call.
}
struct SourceResult {
    HumanBuyerSaleStatus status=HumanBuyerSaleStatus::unsupported;
    uint32_t adjusted=0;
    std::vector<Boundary> observations;
};
SourceResult source_settle(GameState& g, const HumanBuyerSaleRequest& request,
                          HumanBuyerSaleAllocatorGate gate, WatcomRandom& w,
                          GameRandom& game, bool direct_ack, bool paid_checkpoint) {
    SourceResult result;
    const uint32_t list=request.list_address;
    const int player=g.squad_slots.at(request.source_pool_slot)&0xfff;
    const int seller=g.memory.u16(list+8)&0x7fff;
    const int buyer=g.memory.u16(list+uint32_t(request.bid_slot)*2);
    const int seller_manager=g.manager_of(seller), buyer_manager=g.manager_of(buyer);
    const uint32_t fee=g.memory.u32(list+0x1c+uint32_t(request.bid_slot)*4);
    const uint32_t salary=g.memory.u32(list+0x0c+uint32_t(request.bid_slot)*4);
    const uint8_t term=g.memory.u8(list+0x34+uint32_t(request.bid_slot));
    check(request.list_entry>=0&&request.list_entry<40&&request.raw_offer_slot==-1&&
          request.list_address==list_start+uint32_t(request.list_entry)*row_size&&
          request.source_pool_slot==pool_start+size_t(request.list_entry),"oracle only ordinary explicit coordinates");
    check(gate!=HumanBuyerSaleAllocatorGate::unsupported&&buyer_manager>=0&&
          squad_range(buyer).count==24&&term<=127,"oracle requires supported Source scope");
    const size_t seller_slot=seller==0x0fff?0:find_player(g,seller,player);
    const auto notice=[&](int manager,uint32_t at,std::string text={},uint16_t flags=0){
        result.observations.push_back(capture(g,w,game,"notice",request.list_entry,manager,at,
            text.empty()?message_text(g,at):std::move(text),flags));
    };
    if(direct_ack) notice(g.memory.u8(::bmh::layout::ActiveManager),::bmh::layout::DeskActions_SubmitTransferBidImpl_ExecutableField,{},0x80);
    const size_t br=size_t(buyer_manager)*record_size;
    result.adjusted=fee;
    const uint8_t subsidy=g.manager_records.at(br+0x170);
    if(fee<=rd(g.manager_records,br+0x314)&&subsidy!=0&&
       (subsidy&3)==(g.players.at(size_t(player)).w364&3)) {
        uint32_t league=g.manager_records.at(br+0x210); if(league>2) --league;
        const uint32_t limit=(((uint32_t(subsidy)>>2)&7u)*50000u+150000u)*(4u-league);
        if(fee<limit) {
            const uint32_t discount=(((uint32_t(subsidy)>>5)+3u)*fee*10u)/100u;
            notice(buyer_manager,::bmh::layout::PendingOffer_ConstructionNoticeDialog_TextPointer,message_text(g,::bmh::layout::PendingOffer_ConstructionNoticeDialog_TextPointer)+format_amount(g,int32_t(discount))+
                   message_text(g,::bmh::layout::Weekly_Fail_TextPointer)+format_amount(g,int32_t(fee))+").");
            result.adjusted-=discount;
        } else notice(buyer_manager,::bmh::layout::PendingOffer_FormatError_ExecutableField3);
        g.manager_records.at(br+0x170)=0;
    }
    const uint8_t old_active=g.memory.u8(::bmh::layout::ActiveManager);
    optional_byte(g,::bmh::layout::FinanceStatisticsEnabled,1);
    if(!book_money(g,buyer_manager,int32_t(result.adjusted),false,true)) {
        g.memory.set8(::bmh::layout::ActiveManager,uint8_t(buyer_manager)); notice(buyer_manager,::bmh::layout::Advertising_FormatError_TextPointer18);
        if(g.level!=0) optional_byte(g,::bmh::layout::FinanceStatisticsEnabled,0);
        g.memory.set8(::bmh::layout::ActiveManager,old_active);
        result.status=HumanBuyerSaleStatus::funds_refused; return result;
    }
    if(g.level!=0) optional_byte(g,::bmh::layout::FinanceStatisticsEnabled,0);
    wr(g.manager_records,br+0x330,rd(g.manager_records,br+0x330)+result.adjusted);
    if(seller_manager>=0) {
        const size_t sr=size_t(seller_manager)*record_size;
        optional_byte(g,::bmh::layout::FinanceStatisticsEnabled,1);
        book_money(g,seller_manager,int32_t(0u-result.adjusted),true,true);
        if(g.level!=0) optional_byte(g,::bmh::layout::FinanceStatisticsEnabled,0);
        wr(g.manager_records,sr+0x330,rd(g.manager_records,sr+0x330)-result.adjusted);
    }
    if(paid_checkpoint) result.observations.push_back(capture(g,w,game,"paid",request.list_entry));
    if(gate==HumanBuyerSaleAllocatorGate::blocked) {
        result.status=HumanBuyerSaleStatus::allocator_blocked; return result;
    }
    const auto buyer_range=squad_range(buyer);
    size_t destination=buyer_range.first;
    // format field scans exactly 24 FULL words, not masked IDs or squad_count.
    while(destination<buyer_range.first+24&&g.squad_slots.at(destination)!=0x0fff) ++destination;
    if(destination==buyer_range.first+24) {
        notice(g.memory.u8(::bmh::layout::ActiveManager),::bmh::layout::PendingOffer_FormatError_ExecutableField);
        result.status=HumanBuyerSaleStatus::no_room; return result;
    }
    const size_t row=destination-buyer_range.first;
    const size_t board=size_t(buyer_manager)*board_size+row*row_size;
    g.squad_slots.at(destination)=g.squad_slots.at(request.source_pool_slot);
    new_board_entry(g,buyer_manager,int(row),int(destination),w);
    if(seller_manager>=0) {
        const size_t seller_board=size_t(seller_manager)*board_size+
            (seller_slot-squad_range(seller).first)*row_size;
        std::copy_n(g.tactics.begin()+std::ptrdiff_t(seller_board),row_size,
                    g.tactics.begin()+std::ptrdiff_t(board));
    }
    wr(g.tactics,board+0x50,salary);
    uint32_t reference_fee=0,reference_salary=0;
    market_value(g,int(request.source_pool_slot),0,100,reference_fee,reference_salary,term&127,0,w);
    check(reference_salary!=0,"oracle excludes Original divide-by-zero boundary");
    for(size_t n=0;n<7;++n)
        g.tactics.at(board+0x12+n)=g.memory.u8(list+0x38+uint32_t(request.bid_slot)*7+uint32_t(n));
    const uint32_t bonus=expected_bonus(g,buyer_manager,0xff,int(row),player,true);
    // EXE4292c..42949: wrap32 IMUL, signed bonus IDIV, unsigned final DIV,
    // then signed legacy routine clamp. No 64-bit widening or signed final divide.
    const int32_t part=int32_t(bonus*80u)/100;
    const uint32_t numerator=(salary+uint32_t(part))*100u;
    g.tactics.at(board+0x0d)=uint8_t(std::clamp(int32_t(numerator/reference_salary),0,255));
    wr(g.tactics,board+0x54,fee); g.tactics.at(board+0x0c)=term;
    if(seller_manager>=0) source_history(g,seller_manager,buyer,player,fee,false);
    source_history(g,buyer_manager,seller,player,fee,true);
    source_remove(g,seller,seller_manager,player);
    if(seller!=0x0fff) auto_lineup(g,seller,100);
    auto_lineup(g,buyer,100);
    result.status=HumanBuyerSaleStatus::moved;
    return result;
}

struct Control {
    HumanBuyerSaleRequest request;
    int player=-1, seller=-1, buyer=-1, seller_manager=-1, buyer_manager=1, destination=-1;
    uint32_t fee=250000,salary=70000;
    uint8_t term=24;
};
Control make_control(GameState& g, bool human_seller=false, int first_free=-1,
                     bool full=false, int bid=0, uint32_t fee=250000, uint32_t salary=70000,
                     bool formed_bid=true, int selected_seller=-1, int selected_player=-1) {
    check(g.manager_teams.size()==2&&g.manager_teams[0]==1&&g.manager_teams[1]==54,
          "unchanged genuine 2PNWK001 manager identity");
    Control c; c.buyer=g.manager_teams[1]; c.fee=fee; c.salary=salary;
    WatcomRandom listing_w(37); MinuteReport listing_report;
    int entry=-1;
    if(human_seller) {
        const auto listing=list_manager_player(g,0,5,listing_w,listing_report);
        check(listing.status==TransferActionStatus::accepted&&!listing_report.unsupported,
              "actual Source human listing control");
        c.player=listing.player; c.seller=g.manager_teams[0]; c.seller_manager=0; entry=listing.list_entry;
    } else {
        // Genuine fixture has only free agents on its original ordinary list.
        // Generate an AI club listing through the existing reviewed Source
        // primitive, preserving real unique players; no special-row promotion.
        const int first_team=selected_seller<0?0:selected_seller;
        const int last_team=selected_seller<0?90:selected_seller+1;
        for(int team=first_team;team<last_team&&entry<0;++team) {
            if(g.manager_of(team)>=0) continue;
            const auto r=squad_range(team);
            for(size_t row=selected_player<0?(r.count==16?11:16):0;row<r.count&&entry<0;++row) {
                const int player=g.squad_slots.at(r.first+row)&0xfff;
                if(player==0xfff||(selected_player>=0&&player!=selected_player)) continue;
                auto candidate=g; WatcomRandom candidate_w(37); MinuteReport candidate_report;
                if(!list_transfer_player(candidate,int(r.first+row),candidate_w,candidate_report)||candidate_report.unsupported)
                    continue;
                for(int n=0;n<40;++n) if((candidate.squad_slots.at(pool_start+size_t(n))&0xfff)==player){entry=n;break;}
                if(entry>=0){g=std::move(candidate);c.seller=team;c.player=player;listing_w=candidate_w;}
            }
        }
        check(entry>=0&&c.seller>=0&&g.manager_of(c.seller)<0,"actual Source AI listing control");
    }
    check(entry>=0&&entry<40,"control remains ordinary list 0..39");
    const uint32_t list=list_start+uint32_t(entry)*row_size;
    c.request.list_entry=entry; c.request.bid_slot=bid; c.request.list_address=list;
    c.request.source_pool_slot=pool_start+size_t(entry);
    c.request.expected_seller=c.seller; c.request.expected_buyer=c.buyer;
    c.request.expected_active_manager=1; c.request.raw_offer_slot=-1;
    const auto buyer_range=squad_range(c.buyer);
    check(buyer_range.count==24,"genuine ordinary 24-slot buyer");
    size_t original_hole=24;
    for(size_t n=0;n<24;++n) if(g.squad_slots.at(buyer_range.first+n)==0x0fff){original_hole=n;break;}
    check(original_hole<24,"genuine fixture buyer capacity");
    if(full) {
        // 0xffff has a masked empty ID but is NOT the allocator's full 0x0fff.
        // This exact distinction creates no duplicate player IDs.
        for(size_t n=0;n<24;++n) if(g.squad_slots.at(buyer_range.first+n)==0x0fff)
            g.squad_slots.at(buyer_range.first+n)=0xffff;
    } else {
        const size_t target=first_free<0?original_hole:size_t(first_free);
        check(target<24,"declared allocator destination");
        if(target!=original_hole) {
            std::swap(g.squad_slots.at(buyer_range.first+target),g.squad_slots.at(buyer_range.first+original_hole));
            for(size_t n=0;n<row_size;++n)
                std::swap(g.tactics.at(board_size+target*row_size+n),g.tactics.at(board_size+original_hole*row_size+n));
        }
        for(size_t n=0;n<target;++n) if(g.squad_slots.at(buyer_range.first+n)==0x0fff)
            g.squad_slots.at(buyer_range.first+n)=0xffff;
        c.destination=int(target); g.tactics.at(board_size+target*row_size+6)=0x73;
    }
    if(human_seller) {
        const size_t seller_row=find_player(g,c.seller,c.player)-squad_range(c.seller).first;
        g.tactics.at(seller_row*row_size+6)=0x6d;
    }
    g.memory.set8(::bmh::layout::ActiveManager,1);
    g.level=1; // declared scalar control: actual debit/refusal rather than free-play no-op.
    wr(g.manager_records,record_size+0x314,10000000);
    wr(g.manager_records,record_size+0x31c,0);
    g.manager_records.at(record_size+0x170)=0;
    if(formed_bid) {
        g.memory.set16(list+uint32_t(bid)*2,uint16_t(c.buyer));
        g.memory.set32(list+0x1c+uint32_t(bid)*4,c.fee);
        g.memory.set32(list+0x0c+uint32_t(bid)*4,c.salary);
        g.memory.set8(list+0x34+uint32_t(bid),c.term);
        for(uint32_t n=0;n<7;++n) g.memory.set8(list+0x38+uint32_t(bid)*7+n,uint8_t(n+3));
    }
    return c;
}
Control make_reverse_human_control(GameState& g) {
    // Same untouched2PN manager/player ownership; Source listing by manager1
    // sells his actual row5 to manager0. No AI/free-player relabelling.
    Control c;c.seller_manager=1;c.seller=g.manager_teams.at(1);
    c.buyer_manager=0;c.buyer=g.manager_teams.at(0);
    WatcomRandom listing_w(37);MinuteReport listing_report;
    const auto listed=list_manager_player(g,1,5,listing_w,listing_report);
    check(listed.status==TransferActionStatus::accepted&&!listing_report.unsupported,
          "reverse actual Source human listing control");
    c.player=listed.player;
    c.request.list_entry=listed.list_entry;c.request.bid_slot=0;
    c.request.list_address=list_start+uint32_t(listed.list_entry)*row_size;
    c.request.source_pool_slot=pool_start+size_t(listed.list_entry);
    c.request.expected_seller=c.seller;c.request.expected_buyer=c.buyer;
    c.request.expected_active_manager=0;c.request.raw_offer_slot=-1;
    const auto range=squad_range(c.buyer);
    for(size_t row=0;row<24;++row)if(g.squad_slots.at(range.first+row)==0x0fff){c.destination=int(row);break;}
    check(c.destination>=0,"reverse genuine human buyer capacity");
    g.tactics.at(size_t(c.destination)*row_size+6)=0x73;
    const size_t seller_row=find_player(g,c.seller,c.player)-squad_range(c.seller).first;
    g.tactics.at(board_size+seller_row*row_size+6)=0x6d;
    g.memory.set8(::bmh::layout::ActiveManager,0);g.level=1;
    wr(g.manager_records,0x314,10000000);wr(g.manager_records,0x31c,0);
    g.manager_records.at(0x170)=0;
    return c;
}
Control make_free_control(GameState& g,bool formed_bid=true) {
    Control c; c.seller=0x0fff; c.seller_manager=-1;
    c.buyer_manager=g.memory.u8(::bmh::layout::ActiveManager); c.buyer=g.manager_teams.at(size_t(c.buyer_manager));
    int entry=-1;
    for(int n=0;n<40;++n) {
        const uint32_t list=list_start+uint32_t(n)*row_size;
        const int player=g.squad_slots.at(pool_start+size_t(n))&0xfff;
        if(player!=0xfff&&g.memory.u16(list+8)==0x0fff&&g.memory.u16(list)==0xffff){entry=n;c.player=player;break;}
    }
    check(entry>=0,"untouched genuine Native input contains a canonical free-agent ordinary listing");
    const uint32_t list=list_start+uint32_t(entry)*row_size;
    c.request.list_entry=entry; c.request.bid_slot=0; c.request.list_address=list;
    c.request.source_pool_slot=pool_start+size_t(entry);
    c.request.expected_seller=0x0fff;c.request.expected_buyer=c.buyer;
    c.request.expected_active_manager=c.buyer_manager;c.request.raw_offer_slot=-1;
    c.fee=((g.memory.u32(list+0x2c)/10000u)*12500u)*2u;
    c.salary=((100u*g.memory.u32(list+0x30))/10000u)*200u;c.term=12;
    check(c.fee>0&&c.salary>0&&rd(g.manager_records,size_t(c.buyer_manager)*record_size+0x314)>c.fee,
          "genuine free-agent quote and buyer allowance support ordinary purchase");
    check(g.manager_records.at(size_t(c.buyer_manager)*record_size+0x170)==0,
          "genuine Native free-agent buyer has no subsidy in this scenario");
    for(int team=0;team<138;++team) check(!contains(g,team,c.player),"genuine free agent is not an owned AI/human squad player");
    const auto range=squad_range(c.buyer);
    for(size_t row=0;row<24;++row) if(g.squad_slots.at(range.first+row)==0xfff){c.destination=int(row);break;}
    check(c.destination>=0,"genuine free-agent buyer has allocator capacity");
    // Opaque marker is a declared allocator-board control, separate from the
    // untouched Native free-agent identity/listing and App-flow fixture below.
    g.tactics.at(size_t(c.buyer_manager)*board_size+size_t(c.destination)*row_size+6)=0x73;
    if(formed_bid) {
        g.memory.set16(list,uint16_t(c.buyer)); g.memory.set32(list+0x1c,c.fee);
        g.memory.set32(list+0x0c,c.salary);g.memory.set8(list+0x34,c.term);
        for(uint32_t n=0;n<7;++n) g.memory.set8(list+0x38+n,uint8_t(n+3));
    }
    return c;
}
HumanBuyerSaleHooks observing_hooks(HumanBuyerSaleAllocatorGate gate,
    std::vector<Boundary>& observations, WatcomRandom& w, GameRandom& game,
    bool paid_checkpoint) {
    HumanBuyerSaleHooks h; h.allocator_gate=gate;
    h.notice=[&](const GameState& g,const HumanBuyerSaleNotice& n){
        auto b=capture(g,w,game,"notice",n.list_entry,n.manager,n.source_text_address,n.original_message,n.source_flags);
        b.raw=n.raw_offer_slot; observations.push_back(std::move(b));
    };
    if(paid_checkpoint) h.after_paid_accounts=[&](GameState& g,const HumanBuyerSalePaid& paid){
        check(paid.raw_offer_slot==-1&&paid.buyer_manager>=0&&paid.buyer_manager<4&&paid.bid_slot>=0&&paid.bid_slot<4,
              "shared paid callback ordinary source identity");
        observations.push_back(capture(g,w,game,"paid",paid.list_entry));
    };
    return h;
}
void check_bids(const GameState& after,const GameState& before,uint32_t list) {
    for(uint32_t n=0;n<4;++n)
        check(after.memory.u16(list+n*2)==before.memory.u16(list+n*2),"shared/direct settlement preserves all four bid words");
}
void settlement_case(GameState input,const Control& c,HumanBuyerSaleAllocatorGate gate,
                     HumanBuyerSaleStatus wanted,const std::string& label) {
    auto source=input,product=input;
    WatcomRandom source_w(0x13579),product_w(0x13579); GameRandom source_g(73),product_g(73);
    const auto expected=source_settle(source,c.request,gate,source_w,source_g,false,true);
    std::vector<Boundary> observed; auto hooks=observing_hooks(gate,observed,product_w,product_g,true);
    MinuteReport report;
    const auto actual=settle_human_buyer_sale(product,c.request,hooks,product_w,report);
    check(!report.unsupported&&actual.status==wanted&&expected.status==wanted,label+": actual/Source outcome");
    check(actual.player==c.player&&actual.seller==c.seller&&actual.buyer==c.buyer&&
          actual.seller_manager==c.seller_manager&&actual.buyer_manager==c.buyer_manager&&
          actual.raw_fee==c.fee&&actual.adjusted_fee==expected.adjusted,label+": result identity/raw/adjusted fee");
    compare(capture(product,product_w,product_g,"final",c.request.list_entry),
            capture(source,source_w,source_g,"final",c.request.list_entry),label);
    check(observed.size()==expected.observations.size(),label+": notice/paid checkpoint count");
    for(size_t n=0;n<observed.size();++n) compare(observed[n],expected.observations[n],label+" checkpoint "+std::to_string(n));
    check_bids(product,input,c.request.list_address);
    check(product_g.seed()==73&&product_g.calls()==0,label+": Source settlement never consumes G");
    if(wanted==HumanBuyerSaleStatus::moved) {
        check(contains(product,c.buyer,c.player)&&!contains(product,c.seller,c.player),label+": actual ownership move");
        check((product.squad_slots.at(find_player(product,c.buyer,c.player))&0x2000)==
              (input.squad_slots.at(c.request.source_pool_slot)&0x2000),label+": full pool word flag follows player");
        const size_t row=find_player(product,c.buyer,c.player)-squad_range(c.buyer).first;
        const size_t board=size_t(c.buyer_manager)*board_size+row*row_size;
        check(product.tactics.at(board+6)==(c.seller_manager<0?0x73:0x6d),
              label+": AI keeps initialized buyer board, human inherits seller board");
        check(rd(product.tactics,board+0x54)==c.fee&&rd(product.tactics,board+0x50)==c.salary&&
              product.tactics.at(board+0xc)==c.term,label+": raw fee/salary/sale term survive lineup");
        check(product.squad_slots.at(c.request.source_pool_slot)==0x0fff,label+": Source success removes ordinary pool");
        if(c.seller==0x0fff) {
            const size_t base=size_t(c.buyer_manager)*record_size;
            check(rd(product.manager_records,base+0x7d8)==uint32_t(product.player_name_offsets.at(size_t(c.player)))+0xffff0000u,
                  label+": format field free-agent counterpart is FFFF sentinel");
            for(size_t at=pool_start;at<3072;++at) {
                check((product.squad_slots.at(at)&0xfff)!=c.player,label+": all free-pool aliases removed through final slot3071");
                if((input.squad_slots.at(at)&0xfff)!=c.player)
                    check(product.squad_slots.at(at)==input.squad_slots.at(at),label+": unrelated free-pool words retained");
            }
        }
        if(c.seller_manager<0) {
            check(std::equal(product.manager_records.begin()+std::ptrdiff_t(size_t(1-c.buyer_manager)*record_size),
                             product.manager_records.begin()+std::ptrdiff_t(size_t(2-c.buyer_manager)*record_size),
                             input.manager_records.begin()+std::ptrdiff_t(size_t(1-c.buyer_manager)*record_size)),label+": unrelated human manager gets no AI seller accounting/history");
        }
    } else {
        check(product.squad_slots.at(c.request.source_pool_slot)==input.squad_slots.at(c.request.source_pool_slot)&&
              product.squad_slots==input.squad_slots&&product.tactics==input.tactics,
              label+": expected non-move retains pool, squads and boards");
        check(product_w.calls()==0,label+": expected refusal/blocked/no-room has no allocator W draws");
        if(wanted!=HumanBuyerSaleStatus::funds_refused)
            check(rd(product.manager_records,size_t(c.buyer_manager)*record_size+0x330)==rd(input.manager_records,size_t(c.buyer_manager)*record_size+0x330)+expected.adjusted,
                  label+": allocator refusal retains already-paid ledger");
    }
    std::printf("PASS %s W=%llu G=%llu checkpoints=%zu\n",label.c_str(),
                static_cast<unsigned long long>(product_w.calls()),static_cast<unsigned long long>(product_g.calls()),observed.size());
}

// Independent legacy routine qualifier for the genuine opening-season Native
// controls used below. No production rating/bonus/settlement API is an oracle.
// The independently composed settlement below remains its original contract.
struct SourcePcts { uint32_t fee=0,salary=0; };
SourcePcts source_direct_rating(const GameState& g,const HumanBuyerSaleRequest& request,WatcomRandom& w) {
    const uint32_t list=request.list_address,b=uint32_t(request.bid_slot);
    const uint8_t term=g.memory.u8(list+0x34+b);
    const uint8_t q=uint8_t((int(term&127)-1)/12);
    uint32_t fee_base=(g.memory.u32(list+0x2c)/10000u)*12500u;
    if(term&128)fee_base>>=2;
    const uint32_t salary_base=(((uint32_t(q)*20u+100u)*g.memory.u32(list+0x30))/10000u)*100u;
    check(fee_base!=0&&salary_base!=0,"independent direct Source denominators");
    SourcePcts result;
    result.fee=(g.memory.u32(list+0x1c+b*4)*100u)/fee_base;
    uint32_t salary=g.memory.u32(list+0x0c+b*4);
    const int bidder=int(int16_t(g.memory.u16(list+b*2)));
    int manager=-1;
    for(size_t m=0;m<g.manager_teams.size();++m)if(g.manager_teams[m]==bidder)manager=int(m);
    if(manager>=0) {
        // Source opening-day fallback rank11. Nonopening mode4 results have
        // separate full result-composition coverage in qualification_tests.
        check(bidder>=0&&bidder<90&&g.league_rounds.at(size_t(bidder/18))<2,
              "direct oracle explicitly bounded to genuine opening Native prediction fallback");
        const int player=g.squad_slots.at(request.source_pool_slot)&0xfff;
        const auto& p=g.players.at(size_t(player));
        int sum=0;
        for(size_t slot=size_t(bidder)*24;slot<size_t(bidder+1)*24;++slot) {
            const uint16_t word=g.squad_slots.at(slot);
            if(word==0x0fff||!(word&0x8000))continue;
            const auto& member=g.players.at(word&0xfff);
            sum+=int(member.w35c>>7&127)+int(member.w35c>>14&127);
        }
        const int average=std::max(1,std::clamp((sum/2)/11,0,99));
        const int strength=int(((p.w35c>>7&127)+(p.w35c>>14&127))>>1);
        const int gap=std::max(-1,int(int16_t(average-strength)));
        const int goals[]={0,2,4,8};
        const int times[]={std::max(0,goals[p.w364&3]-gap),34,std::max(0,25-gap*5)};
        uint32_t bonus=0;
        for(uint32_t k=0;k<3;++k) {
            const uint32_t rate=g.memory.u8(list+0x38+b*7+k);
            bonus+=(((rate*salary)/100u)*uint32_t(times[k]))/12u;
        }
        // In all five18-team leagues rank11 selects the staying-up prämie6
        // (18-relegation is14..16). All seven source bytes remain staged.
        const uint32_t season=(5u*uint32_t(g.memory.u8(list+0x38+b*7+6))*salary)/100u;
        bonus+=season/12u;
        salary+=bonus;
    }
    result.salary=(salary*100u)/salary_base;
    if(result.fee<uint32_t(w.range(90,100)))result.fee=0;
    if(result.salary<g.memory.u8(list+0x55))result.salary=0;
    return result;
}
int source_capacity_count(const GameState& g,int buyer) {
    int count=0;const auto range=squad_range(buyer);
    for(size_t at=range.first;at<range.first+range.count;++at)
        if((g.squad_slots.at(at)&0xfff)!=0xfff)++count;
    for(uint32_t n=0;n<200;++n) {
        const uint32_t loan=g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField+n*4);
        if(loan!=0&&int(loan>>12&0x3ff)==buyer)++count;
    }
    return count&255;
}

// Direct format field ordinary caller: editor values -> exact425ee -> mode
// salary threshold -> strict fee threshold -> DeskActions_SubmitTransferBidImpl_ExecutableField -> unchanged settlement.
// Full1b556 display/redraw/quote is owned by its UI caller, not manufactured here.
void direct_case(GameState input,const Control& c,HumanBuyerSaleAllocatorGate gate,
                 TransferActionStatus wanted,const std::string& label,
                 const std::vector<uint32_t>& wanted_notices={}, uint32_t wanted_adjusted=0,
                 bool cheap_fee_override=false) {
    TransferBidRequest bid; bid.manager=c.buyer_manager; bid.list_entry=c.request.list_entry; bid.player=c.player;
    bid.source_slot=int(c.request.source_pool_slot);
    bid.contract_years=c.seller==0x0fff?1:2;
    const uint32_t list=c.request.list_address;
    const uint32_t fee_base=(input.memory.u32(list+0x2c)/10000u)*12500u;
    const uint32_t salary_base=((uint32_t((bid.contract_years-1)*20+100)*input.memory.u32(list+0x30))/10000u)*100u;
    check(fee_base>0&&salary_base>0,"direct real listing denominator");
    bid.fee=int32_t(cheap_fee_override?(fee_base*90u)/100u:fee_base*2u);
    bid.annual_salary=int32_t(salary_base*2u);
    for(size_t n=0;n<7;++n) bid.bonuses[n]=uint32_t(n+3);
    // The previous pure-settlement FULLWORD control used FFFF. Real425ee
    // fitness must not read its flagged Player4095. This separately declared
    // qualification control uses nonlineup7FFF for masked-empty full words;
    // the1c7df exact0FFF allocator still has no room. No duplicate real player.
    const auto buyer_range=squad_range(c.buyer);
    for(size_t at=buyer_range.first;at<buyer_range.first+buyer_range.count;++at)
        if(input.squad_slots.at(at)==0xffff)input.squad_slots.at(at)=0x7fff;
    auto source=input,product=input; auto request=c.request;
    check(source_capacity_count(source,c.buyer)+int(int8_t(source.memory.u8(::bmh::layout::DeskActions_Unsupported_RuntimeField+uint32_t(c.buyer_manager))))!=23,
          "direct successful controls pass exact Source capacity equality gate");
    request.bid_slot=0;
    while(request.bid_slot<4&&source.memory.u16(list+uint32_t(request.bid_slot)*2)!=0xffff) ++request.bid_slot;
    check(request.bid_slot<4,"direct free raw bid slot");
    WatcomRandom source_w(0x13579),product_w(0x13579); GameRandom source_g(73),product_g(73);
    const uint32_t b=uint32_t(request.bid_slot);
    source.memory.set16(list+b*2,uint16_t(c.buyer));
    source.memory.set32(list+0x1c+b*4,uint32_t(bid.fee));
    source.memory.set32(list+0x0c+b*4,uint32_t(bid.annual_salary));
    source.memory.set8(list+0x34+b,uint8_t(bid.contract_years*12));
    for(uint32_t n=0;n<7;++n) source.memory.set8(list+0x38+b*7+n,uint8_t(bid.bonuses[n]));
    auto percentages=source_direct_rating(source,request,source_w);
    const int mode=source.memory.u8(::bmh::layout::GameMode);
    check(mode>=1&&mode<=3,"actual saved Source career mode");
    const uint32_t salary_threshold=mode==1?100u:uint32_t(source_w.range(mode==2?90:120,mode==2?110:190));
    check(percentages.salary>=salary_threshold,"direct successful Source salary gate");
    if(mode!=3)percentages.fee=200;
    check(uint32_t(source_w.range(92,150))<percentages.fee,"direct successful strict Source fee gate");
    const uint64_t qualification_calls=source_w.calls();
    check(qualification_calls==uint64_t(mode==1?2:3),"hard mode-dependent Source qualification draw count");
    const auto expected=source_settle(source,request,gate,source_w,source_g,true,false);
    std::vector<Boundary> observed; auto hooks=observing_hooks(gate,observed,product_w,product_g,false);
    hooks.direct_mode=HumanBuyerSaleDirectMode::ordinary_ai_purchase;
    MinuteReport report;
    const auto actual=submit_transfer_bid_source_human_buyer(product,bid,product_w,hooks,report);
    check(!report.unsupported&&actual.status==wanted,label+": direct typed result");
    compare(capture(product,product_w,product_g,"final",c.request.list_entry),
            capture(source,source_w,source_g,"final",c.request.list_entry),label);
    check(observed.size()==expected.observations.size()&&!observed.empty()&&observed[0].address==::bmh::layout::DeskActions_SubmitTransferBidImpl_ExecutableField,
          label+": caller ACK precedes shared account notices");
    for(size_t n=0;n<observed.size();++n) compare(observed[n],expected.observations[n],label+" checkpoint "+std::to_string(n));
    check_bids(product,source,list);
    // Compare the consumed settlement's actual hard Source outcome separately
    // from the full snapshot oracle: paid non-moves are expected partials,
    // never rollback/unsupported and never blanket list/bid consumption.
    const auto source_status = wanted==TransferActionStatus::accepted ? HumanBuyerSaleStatus::moved
        : wanted==TransferActionStatus::allocator_blocked ? HumanBuyerSaleStatus::allocator_blocked
        : wanted==TransferActionStatus::no_squad_room ? HumanBuyerSaleStatus::no_room
        : HumanBuyerSaleStatus::funds_refused;
    check(expected.status==source_status,label+": hard Source settlement outcome");
    if(!wanted_notices.empty()) {
        std::vector<uint32_t> addresses;
        for(const auto& n:observed) addresses.push_back(n.address);
        check(addresses==wanted_notices,label+": exact direct ACK/account-notice order");
    }
    if(wanted_adjusted!=0) check(expected.adjusted==wanted_adjusted,label+": exact adjusted amount");
    if(wanted_adjusted!=0&&wanted_adjusted!=uint32_t(bid.fee))
        check(product.manager_records.at(size_t(c.buyer_manager)*record_size+0x170)==0,
              label+": qualifying subsidy consumed even when later payment refuses");
    for(const auto& n:observed)
        check(n.manager==c.buyer_manager&&n.active==c.buyer_manager,label+": actual active buyer owns direct af304/subsidy/funds/no-room notice");
    for(uint32_t n=0;n<4;++n) if(int(n)!=request.bid_slot)
        check(product.memory.u16(list+n*2)==input.memory.u16(list+n*2),label+": every competing ordinary bid survives");
    check(product.memory.u16(list+b*2)==uint16_t(c.buyer)&&
          product.memory.u32(list+0x1c+b*4)==uint32_t(bid.fee)&&
          product.memory.u32(list+0x0c+b*4)==uint32_t(bid.annual_salary),
          label+": actual submitted ordinary buyer/raw fee/salary retained after settlement");
    const size_t br=size_t(c.buyer_manager)*record_size;
    if(wanted==TransferActionStatus::accepted) {
        check(contains(product,c.buyer,c.player)&&!contains(product,c.seller,c.player)&&
              product.squad_slots.at(c.request.source_pool_slot)==0x0fff,label+": format field actual move/pool removal");
        check(rd(product.manager_records,br+0x314)==rd(input.manager_records,br+0x314)-expected.adjusted&&
              rd(product.manager_records,br+0x330)==rd(input.manager_records,br+0x330)+expected.adjusted,
              label+": direct adjusted cash and transfer ledger");
        if(c.seller_manager>=0) {
            const size_t sr=size_t(c.seller_manager)*record_size;
            check(rd(product.manager_records,sr+0x314)==rd(input.manager_records,sr+0x314)+expected.adjusted&&
                  rd(product.manager_records,sr+0x330)==rd(input.manager_records,sr+0x330)-expected.adjusted,
                  label+": Source human seller adjusted credit and opposite ledger");
            const size_t old_row=find_player(input,c.seller,c.player)-squad_range(c.seller).first;
            const size_t new_row=find_player(product,c.buyer,c.player)-squad_range(c.buyer).first;
            check(product.tactics.at(size_t(c.buyer_manager)*board_size+new_row*row_size+6)==
                  input.tactics.at(size_t(c.seller_manager)*board_size+old_row*row_size+6),
                  label+": human seller board marker inherited after allocator initializer");
            check(rd(product.manager_records,sr+0x760)==product.date*4u+(uint32_t(bid.fee)/10000u)*0x80000u,
                  label+": human seller history uses raw sale fee and sale flag");
        }
        const size_t row=find_player(product,c.buyer,c.player)-squad_range(c.buyer).first;
        check(rd(product.tactics,size_t(c.buyer_manager)*board_size+row*row_size+0x54)==uint32_t(bid.fee)&&
              rd(product.manager_records,br+0x760)==product.date*4u+1u+(uint32_t(bid.fee)/10000u)*0x80000u,
              label+": direct board/history retain RAW fee despite subsidy");
    } else {
        check(product.squad_slots==input.squad_slots&&product.tactics==input.tactics&&
              product.squad_slots.at(c.request.source_pool_slot)==input.squad_slots.at(c.request.source_pool_slot),
              label+": expected direct non-move retains ordinary pool/squads/boards");
        check(product_w.calls()==qualification_calls&&product_g.calls()==0,
              label+": exact Source qualification W sequence, no allocator W or G");
        const bool funds=wanted==TransferActionStatus::insufficient_funds;
        check(rd(product.manager_records,br+0x314)==rd(input.manager_records,br+0x314)-(funds?0u:expected.adjusted)&&
              rd(product.manager_records,br+0x330)==rd(input.manager_records,br+0x330)+(funds?0u:expected.adjusted),
              label+": expected funds versus paid-allocator partial accounting");
        check(rd(product.manager_records,br+0x760)==rd(input.manager_records,br+0x760)&&
              rd(product.manager_records,br+0x7d8)==rd(input.manager_records,br+0x7d8),
              label+": non-move has no buyer transfer history");
    }
    for(const auto& n:observed) check(n.address!=::bmh::layout::PendingOffer_FormatError_ExecutableField2&&n.address!=::bmh::layout::Weekly_PendingSettle_TextPointer&&n.address!=::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField4,
                                    label+": direct caller has no pending buyer-success/severance notice");
    check(!product.memory.has(::bmh::layout::DeskActions_EditorFail_RuntimeField2)&&!product.memory.has(::bmh::layout::Weekly_PendingSettleHumanBuyer_RuntimeField)&&!product.memory.has(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2),
          label+": normal runtime proof does not invent Native BSS bytes");
    std::printf("PASS %s W=%llu notices=%zu\n",label.c_str(),static_cast<unsigned long long>(product_w.calls()),observed.size());
}
// Independently composed legacy routine branches that retain the ordinary bid.
// Editor staging is explicit and compared as a complete Native snapshot; no
// production qualification or settlement helper supplies the expected state.
enum class RetainedKind { salary_refused, fee_refused, existing_bid, capacity };
void retained_case(GameState input,const Control& c,int mode,RetainedKind kind,const std::string& label) {
    input.memory.set8(::bmh::layout::GameMode,uint8_t(mode));
    if(kind==RetainedKind::capacity) {
        const int missing=23-source_capacity_count(input,c.buyer);
        check(missing>=0&&missing<=3,"declared valid saved loan-counter capacity control");
        input.memory.set8(::bmh::layout::DeskActions_Unsupported_RuntimeField+uint32_t(c.buyer_manager),uint8_t(missing));
    }
    const uint32_t list=c.request.list_address;
    TransferBidRequest bid;bid.manager=c.buyer_manager;bid.list_entry=c.request.list_entry;bid.player=c.player;
    bid.source_slot=int(c.request.source_pool_slot);bid.contract_years=2;
    const uint32_t fee_base=(input.memory.u32(list+0x2c)/10000u)*12500u;
    const uint32_t salary_base=((120u*input.memory.u32(list+0x30))/10000u)*100u;
    check(fee_base&&salary_base,"retained real Source listing denominators");
    bid.fee=int32_t(kind==RetainedKind::fee_refused?(fee_base*90u)/100u:fee_base*2u);
    bid.annual_salary=int32_t(kind==RetainedKind::salary_refused?salary_base/2u:salary_base*2u);
    // All seven editor bytes are meaningful even in modes1/2. Salary-refusal
    // vectors have zero premiums so no extra projected pay rescues the offer.
    if(kind!=RetainedKind::salary_refused)
        bid.bonuses={11,22,33,44,55,66,77};
    if(kind==RetainedKind::salary_refused)input.memory.set8(list+0x55,100);
    auto request=c.request;request.bid_slot=0;
    if(kind==RetainedKind::existing_bid) {
        request.bid_slot=2;
        input.memory.set16(list+4,uint16_t(c.buyer));
        input.memory.set32(list+0x24,17);input.memory.set32(list+0x14,19);
        input.memory.set8(list+0x36,12);
        for(uint32_t n=0;n<7;++n)input.memory.set8(list+0x46+n,1);
    } else {
        while(request.bid_slot<4&&input.memory.u16(list+uint32_t(request.bid_slot)*2)!=0xffff)++request.bid_slot;
        check(request.bid_slot<4,"retained Source free bidder slot");
    }
    auto source=input,product=input;
    WatcomRandom source_w(0x13579),product_w(0x13579);GameRandom source_g(73),product_g(73);
    std::vector<Boundary> expected,observed;
    const auto notice=[&](uint32_t address,std::string text={},uint16_t flags=0x80) {
        expected.push_back(capture(source,source_w,source_g,"notice",request.list_entry,c.buyer_manager,
            address,text.empty()?message_text(source,address):std::move(text),flags));
    };
    const bool capacity=source_capacity_count(source,c.buyer)+int(int8_t(source.memory.u8(::bmh::layout::DeskActions_Unsupported_RuntimeField+uint32_t(c.buyer_manager))))==23;
    if(capacity) {
        check(kind==RetainedKind::capacity,"only capacity vector stops before editor writes");
        notice(::bmh::layout::DeskActions_Unsupported_ExecutableField,{},0x40);
    } else {
        const uint32_t b=uint32_t(request.bid_slot);
        source.memory.set16(list+b*2,uint16_t(c.buyer));
        source.memory.set32(list+0x1c+b*4,uint32_t(bid.fee));
        source.memory.set32(list+0x0c+b*4,uint32_t(bid.annual_salary));
        source.memory.set8(list+0x34+b,uint8_t(bid.contract_years*12));
        for(uint32_t n=0;n<7;++n)source.memory.set8(list+0x38+b*7+n,uint8_t(bid.bonuses[n]));
        if(kind!=RetainedKind::existing_bid) {
            auto pct=source_direct_rating(source,request,source_w);
            const uint32_t threshold=mode==1?100u:uint32_t(source_w.range(mode==2?90:120,mode==2?110:190));
            if(pct.salary<threshold) {
                check(kind==RetainedKind::salary_refused,"independent Source salary short circuit");
                notice(::bmh::layout::DeskActions_Unsupported_ExecutableField3,source.player_name(c.player)+message_text(source,::bmh::layout::DeskActions_Unsupported_ExecutableField3));
            } else {
                if(mode!=3)pct.fee=200;
                if(uint32_t(source_w.range(92,150))>=pct.fee) {
                    check(kind==RetainedKind::fee_refused,"independent strict Source fee failure");
                    const bool free=source.memory.u16(list+8)==0x0fff;
                    notice(free?::bmh::layout::DeskActions_Unsupported_ExecutableField2 : ::bmh::layout::DeskActions_Unsupported_TextPointer3,free?message_text(source,::bmh::layout::DeskActions_Unsupported_ExecutableField2):source.club_name(c.seller)+message_text(source,::bmh::layout::DeskActions_Unsupported_TextPointer3));
                } else {
                    check(false,"retained oracle must not invent a pending branch after a passed ordinary fee gate");
                }
            }
        }
    }
    auto hooks=observing_hooks(HumanBuyerSaleAllocatorGate::zero,observed,product_w,product_g,false);
    hooks.direct_mode=HumanBuyerSaleDirectMode::ordinary_ai_purchase;
    MinuteReport report;
    const auto result=submit_transfer_bid_source_human_buyer(product,bid,product_w,hooks,report);
    check(!report.unsupported&&result.status==(capacity?TransferActionStatus::no_squad_room:TransferActionStatus::pending),
          label+": expected retained typed result");
    compare(capture(product,product_w,product_g,"final",request.list_entry),
            capture(source,source_w,source_g,"final",request.list_entry),label);
    check(observed.size()==expected.size(),label+": exact Source notice count");
    for(size_t n=0;n<observed.size();++n)compare(observed[n],expected[n],label+" notice "+std::to_string(n));
    const uint64_t wanted_calls=kind==RetainedKind::capacity||kind==RetainedKind::existing_bid?0u:
        kind==RetainedKind::salary_refused?uint64_t(mode==1?1:2):uint64_t(mode==1?2:3);
    check(product_w.calls()==wanted_calls&&product_g.calls()==0,label+": hard Source short-circuit RNG count");
    check(product.squad_slots==input.squad_slots&&product.tactics==input.tactics&&
          product.manager_records==input.manager_records,label+": retained bid has no accounts/allocation/history/board mutation");
    for(uint32_t n=0;n<4;++n)if(capacity||int(n)!=request.bid_slot)
        for(uint32_t at:{n*2,n*2+1,0x0c+n*4,0x0d+n*4,0x0e + n*4,0x0f+n*4,
                         0x1c+n*4,0x1d+n*4,0x1e + n*4,0x1f+n*4,0x34+n})
            check(product.memory.u8(list+at)==input.memory.u8(list+at),label+": competing or capacity-blocked bid retained");
    if(!capacity) {
        const uint32_t b=uint32_t(request.bid_slot);
        check(product.memory.u16(list+b*2)==uint16_t(c.buyer)&&
              product.memory.u32(list+0x1c+b*4)==uint32_t(bid.fee)&&
              product.memory.u32(list+0x0c+b*4)==uint32_t(bid.annual_salary)&&
              product.memory.u8(list+0x34+b)==24,label+": exact posted fee/salary/months persist");
        for(uint32_t n=0;n<7;++n)
            check(product.memory.u8(list+0x38+b*7+n)==bid.bonuses[n],label+": all seven posted bonuses persist");
    }
    std::printf("PASS %s W=%llu notices=%zu\n",label.c_str(),static_cast<unsigned long long>(product_w.calls()),observed.size());
}
void unsupported_case(GameState input,HumanBuyerSaleRequest request,HumanBuyerSaleHooks hooks,const std::string& label) {
    const auto before=save_stream(input); const auto before_suppressed=input.transfer_notice_suppressed;
    WatcomRandom w(0x13579); size_t callbacks=0;
    hooks.notice=[&](const GameState&,const HumanBuyerSaleNotice&){++callbacks;};
    hooks.before_accounts=[&](const GameState&,const HumanBuyerSaleRequest&){++callbacks;};
    hooks.after_paid_accounts=[&](GameState&,const HumanBuyerSalePaid&){++callbacks;};
    MinuteReport report;
    const auto result=settle_human_buyer_sale(input,request,hooks,w,report);
    check(result.status==HumanBuyerSaleStatus::unsupported&&report.unsupported&&callbacks==0&&
          w.seed()==0x13579&&w.calls()==0&&save_stream(input)==before&&
          input.transfer_notice_suppressed==before_suppressed,label+": deterministic unsupported before mutation/presentation");
    // Transactional owner rollback belongs to actual CareerSession/App tests;
    // this is common deterministic preflight, not a synthetic owner test.
}

GameState pitch_fixture(GameState g) {
    constexpr int manager=0, team=1;
    g.memory.add_block(::bmh::layout::ActiveManager,1); g.memory.set8(::bmh::layout::ActiveManager,uint8_t(manager));
    g.memory.add_block(::bmh::layout::GameMode,1); g.memory.set8(::bmh::layout::GameMode,2);
    g.manager_teams.at(manager)=team;
    g.manager_records.at(size_t(manager)*record_size+0x290)=uint8_t(team);
    g.manager_records.at(size_t(manager)*record_size+0x291)=0;
    g.manager_records.at(size_t(manager)*record_size+0x218)=0;
    g.manager_records.at(size_t(manager)*record_size+0x288)=0;
    g.manager_options.at(size_t(manager)*5)=0;
    g.indoor=false; g.lineups_frozen=false; g.level=0;
    const size_t badge=size_t(team)*2;
    g.club_heap_78.at(badge+1)&=uint8_t(~0x02u);
    const auto range=squad_range(team);
    for(size_t row=0;row<11;++row) {
        auto& slot=g.squad_slots.at(range.first+row);
        slot=uint16_t((slot&0x0fff)|0x8000);
        const size_t id=slot&0x0fff;
        g.players.at(id).w364&=~(uint32_t(0x1f)<<16);
    }
    const size_t board=size_t(manager)*board_size;
    for(size_t row=0;row<24;++row) g.tactics.at(board+row*row_size+0x3f)=0xff;
    for(size_t row=0;row<11;++row) {
        g.tactics.at(board+row*row_size+0x41)=uint8_t(row);
        g.tactics.at(board+row*row_size+0x42)=4;
    }
    return g;
}

void pitch_engine_cases(const GameState& original) {
    constexpr int manager=0, team=1;
    const size_t record=size_t(manager)*record_size, board=size_t(manager)*board_size;
    const auto range=squad_range(team);
    // Controlled mode cross-product: the same manager owns independent
    // outdoor/indoor masks and formation tables, while the badge follows the
    // live runtime mode exactly as legacy routine does on a qualified entry.
    {
        auto g=pitch_fixture(original);
        g.indoor=true;
        g.club_heap_78.at(size_t(team)*2+1)|=0x02;
        g.manager_records.at(record+0x218)=0x80;
        g.manager_records.at(record+0x219)=0x01;
        const size_t indoor_table=size_t(manager)*0x160+0xb0+4*0x16;
        const size_t outdoor_table=size_t(manager)*0x160+4*0x16;
        for(size_t row=0;row<11;++row) {
            g.formations.at(indoor_table+row)=uint8_t(row+1);
            g.formations.at(indoor_table+11+row)=uint8_t(8-row%8);
        }
        const auto positions=std::array<uint8_t,22>{
            0,1,2,3,4,5,6,7,8,9,10, 4,4,4,4,4,4,4,4,4,4,4};
        auto expected=g;
        std::copy(positions.begin(),positions.end(),expected.formations.begin()+std::ptrdiff_t(indoor_table));
        expected.manager_records.at(record+0x219)|=0x10;
        expected.manager_options.at(0)=5; auto_lineup(expected,team,0xe4);
        check(save_pitch_formation_source(g,manager,team,4,g.manager_records.at(record+0x288),positions),
              "indoor own formation saves through manager*0x160+0xb0 and mask+0x219");
        check(save_stream(g)==save_stream(expected)&&g.manager_records.at(record+0x218)==0x80&&
              std::any_of(g.formations.begin()+std::ptrdiff_t(outdoor_table),
                          g.formations.begin()+std::ptrdiff_t(outdoor_table+22),
                          [](uint8_t v){return v!=0;}),
              "indoor own save/optimizer preserves outdoor mask and formation bytes");
        auto applied=pitch_fixture(original); applied.indoor=true;
        applied.club_heap_78.at(size_t(team)*2+1)|=0x02;
        applied.manager_records.at(record+0x218)=1; applied.manager_records.at(record+0x219)=1;
        std::copy(positions.begin(),positions.end(),applied.formations.begin()+std::ptrdiff_t(size_t(manager)*0x160+0xb0));
        auto apply_expected=applied; apply_expected.manager_options.at(0)=1; auto_lineup(apply_expected,team,0xe4);
        check(apply_pitch_formation_source(applied,manager,team,0,applied.manager_records.at(record+0x288))&&
              save_stream(applied)==save_stream(apply_expected)&&applied.manager_records.at(record+0x218)==1,
              "indoor preset selects its own mask/table and leaves outdoor availability intact");
        auto mismatched=pitch_fixture(original); mismatched.indoor=true;
        auto mismatch_expected=mismatched;
        mismatch_expected.manager_records.at(record+0x288)=1;
        check(set_pitch_automatic_weight_source(mismatched,manager,team,0,0,1)&&save_stream(mismatched)==save_stream(mismatch_expected),
              "Source-valid indoor assistant state accepts a weight action without badge normalization");
    }
    {
        auto indoor=pitch_fixture(original); indoor.indoor=true;
        indoor.club_heap_78.at(size_t(team)*2+1)|=0x02;
        const size_t candidate=board+5*row_size;
        indoor.tactics.at(candidate+0x41)=2; indoor.tactics.at(candidate+0x42)=5;
        // Seed a valid old pair and a 12th-row reference to the new Source partner.
        indoor.tactics.at(board+0x3f)=7; indoor.tactics.at(board+7*row_size+0x3f)=0;
        indoor.tactics.at(board+12*row_size+0x3f)=5;
        const uint16_t slot=indoor.squad_slots.at(range.first); const int id=slot&0x0fff;
        check(move_pitch_player_source(indoor,manager,team,0,id,slot,0,4,1,5),
              "indoor Source move accepts the adjacent free target");
        check(indoor.tactics.at(board+0x41)==1&&indoor.tactics.at(board+0x42)==5&&
              indoor.tactics.at(board+0x3f)==5&&indoor.tactics.at(candidate+0x3f)==0,
              "indoor move installs reciprocal partner rows");
        check(indoor.tactics.at(board+7*row_size+0x3f)==0xff&&
              indoor.tactics.at(board+12*row_size+0x3f)==0xff,
              "indoor move clears selected/new partner links across all 24 rows before reassignment");
        auto outdoor=pitch_fixture(original);
        outdoor.tactics.at(candidate+0x41)=2; outdoor.tactics.at(candidate+0x42)=5;
        const auto unchanged=save_stream(outdoor); const auto board_before=outdoor.tactics;
        const auto outdoor_slot=outdoor.squad_slots.at(range.first);
        check(!move_pitch_player_source(outdoor,manager,team,0,outdoor_slot&0x0fff,outdoor_slot,0,4,1,5)&&
              save_stream(outdoor)==unchanged&&outdoor.tactics==board_before,
              "the same adjacent move remains a neutral outdoor rejection");
    }
    {
        auto g=pitch_fixture(original);
        const uint8_t before=0xff;
        for(int field=0;field<3;++field) {
            g.manager_records.at(record+0x288)=before;
            auto expected_state=g;
            const unsigned shift=unsigned(field*2);
            const uint8_t expected=uint8_t(before&uint8_t(~(3u<<shift)));
            expected_state.manager_records.at(record+0x288)=expected;
            check(set_pitch_automatic_weight_source(g,manager,team,field,before,expected),
                  "pitch weight cycles selected 3 field to 0");
            check(g.manager_records.at(record+0x288)==expected&&save_stream(g)==save_stream(expected_state),
                  "pitch weight changes only selected field and preserves reserved bits");
        }
        const auto native=save_stream(g); const auto records=g.manager_records;
        check(!set_pitch_automatic_weight_source(g,manager,team,0,0xff,0xfe)&&
              save_stream(g)==native&&g.manager_records==records,"pitch weight stale byte refusal is neutral");
        g.memory.set8(::bmh::layout::GameMode,1);
        const auto mode_one=save_stream(g); const auto mode_records=g.manager_records;
        check(!set_pitch_automatic_weight_source(g,manager,team,0,g.manager_records.at(record+0x288),
              uint8_t(g.manager_records.at(record+0x288)+1))&&save_stream(g)==mode_one&&
              g.manager_records==mode_records,"pitch automatic weight refuses Source mode I neutrally");
    }
    {
        auto g=pitch_fixture(original);
        g.tactics.at(board+0x3f)=1; g.tactics.at(board+row_size+0x3f)=0;
        g.tactics.at(board+2*row_size+0x3f)=1;
        const auto chosen=g.squad_slots.at(range.first); const int id=chosen&0x0fff;
        check(move_pitch_player_source(g,manager,team,0,id,chosen,0,4,19,8),
              "pitch move accepts free boundary coordinate");
        check(g.tactics.at(board+0x41)==19&&g.tactics.at(board+0x42)==8&&
              g.manager_options.at(size_t(manager)*5)==0,"pitch free move sets coordinates and manual AA");
        check(g.tactics.at(board+row_size+0x3f)==0xff&&g.tactics.at(board+0x3f)==0xff,
              "pitch free move clears selected partner references");
        check(g.tactics.at(board+2*row_size+0x3f)==1,
              "pitch outdoor free move leaves unrelated references to old partner intact");
        auto injured_overlap=pitch_fixture(original);
        injured_overlap.players.at(injured_overlap.squad_slots.at(range.first+10)&0x0fff).w364|=(1u<<16);
        injured_overlap.tactics.at(board+10*row_size+0x41)=19;
        injured_overlap.tactics.at(board+10*row_size+0x42)=8;
        const auto overlap_slot=injured_overlap.squad_slots.at(range.first);
        check(move_pitch_player_source(injured_overlap,manager,team,0,overlap_slot&0x0fff,
              overlap_slot,0,4,19,8),"pitch fit player may move onto an injured row's coordinate");
        check(injured_overlap.tactics.at(board+0x41)==19&&injured_overlap.tactics.at(board+0x42)==8&&
              int8_t(injured_overlap.tactics.at(board+10*row_size+0x41))==19,
              "pitch injured overlap retains both Source coordinates");
        check(set_pitch_automatic_weight_source(injured_overlap,manager,team,0,0,1)&&
              injured_overlap.manager_records.at(record+0x288)==1,
              "pitch injured overlap remains valid for a subsequent Source action");
        auto stale=pitch_fixture(original); const auto stale_native=save_stream(stale);
        const auto stale_tactics=stale.tactics; const auto stale_options=stale.manager_options;
        check(!move_pitch_player_source(stale,manager,team,0,id,chosen,1,4,19,8)&&
              save_stream(stale)==stale_native&&stale.tactics==stale_tactics&&
              stale.manager_options==stale_options,"pitch stale old coordinate refusal is neutral");
        auto adjacent=pitch_fixture(original);
        adjacent.tactics.at(board+5*row_size+0x41)=1;
        adjacent.tactics.at(board+5*row_size+0x42)=5;
        const auto adjacent_before=save_stream(adjacent);
        const auto adjacent_tactics=adjacent.tactics;
        check(!move_pitch_player_source(adjacent,manager,team,0,id,chosen,0,4,1,5)&&
              save_stream(adjacent)==adjacent_before&&adjacent.tactics==adjacent_tactics,
              "pitch row-order move refuses earlier adjacent player before later exact occupant");
        auto exact_first=pitch_fixture(original);
        exact_first.tactics.at(board+row_size+0x41)=1; exact_first.tactics.at(board+row_size+0x42)=5;
        exact_first.tactics.at(board+5*row_size+0x41)=2; exact_first.tactics.at(board+5*row_size+0x42)=5;
        check(move_pitch_player_source(exact_first,manager,team,0,id,chosen,0,4,1,5)&&
              int8_t(exact_first.tactics.at(board+0x41))==1&&
              int8_t(exact_first.tactics.at(board+row_size+0x41))==0,
              "pitch row-order move takes earlier exact occupant before later adjacent player");
        auto occupied=pitch_fixture(original); const auto second=occupied.squad_slots.at(range.first+5)&0x0fff;
        occupied.tactics.at(board+4*row_size+0x42)=0; // avoid an earlier adjacent refusal
        check(move_pitch_player_source(occupied,manager,team,0,id,chosen,0,4,5,4),
              "pitch move accepts exact occupied target");
        check((occupied.squad_slots.at(range.first)&0x0fff)==id&&
              (occupied.squad_slots.at(range.first+5)&0x0fff)==second&&
              int8_t(occupied.tactics.at(board+0x41))==5&&
              int8_t(occupied.tactics.at(board+5*row_size+0x41))==0,
              "pitch occupied move swaps coordinates without players");
    }
    {
        auto g=pitch_fixture(original); const uint8_t weights=g.manager_records.at(record+0x288);
        g.manager_records.at(record+0x218)=1;
        auto expected=g; expected.manager_options.at(0)=1; auto_lineup(expected,team,0xe4);
        check(apply_pitch_formation_source(g,manager,team,0,weights),
              "pitch apply accepts available formation and runs Source optimizer");
        check(save_stream(g)==save_stream(expected),"pitch apply matches AA then optimizer Native result");
        auto unavailable=pitch_fixture(original); const auto unavailable_native=save_stream(unavailable);
        const auto unavailable_slots=unavailable.squad_slots;
        check(!apply_pitch_formation_source(unavailable,manager,team,0,
              unavailable.manager_records.at(record+0x288))&&save_stream(unavailable)==unavailable_native&&
              unavailable.squad_slots==unavailable_slots,"pitch apply refuses unavailable formation neutrally");
        auto detailed=pitch_fixture(original); detailed.level=6;
        detailed.manager_records.at(record+0x288)=4;
        detailed.manager_records.at(record+0x218)=1;
        auto detailed_expected=detailed; detailed_expected.manager_options.at(0)=1;
        auto_lineup(detailed_expected,team,0xe4);
        check(apply_pitch_formation_source(detailed,manager,team,0,4),
              "pitch apply accepts the high-level detailed optimizer");
        check(save_stream(detailed)==save_stream(detailed_expected)&&
              detailed.manager_options==detailed_expected.manager_options,
              "detailed pitch apply matches AA then optimizer Native result");
    }
    {
        auto g=pitch_fixture(original); std::array<uint8_t,22> positions{};
        const size_t table=size_t(manager)*0x160+4*0x16;
        for(size_t row=0;row<11;++row) { positions[row]=uint8_t(row); positions[11+row]=4; }
        auto expected=g; std::copy(positions.begin(),positions.end(),expected.formations.begin()+std::ptrdiff_t(table));
        expected.manager_records.at(record+0x218)|=uint8_t(1u<<4);
        expected.manager_options.at(0)=5; auto_lineup(expected,team,0xe4);
        check(save_pitch_formation_source(g,manager,team,4,g.manager_records.at(record+0x288),positions),
              "pitch save accepts exact current eleven into own slot 4");
        check(save_stream(g)==save_stream(expected),"pitch save matches 22-byte table/mask/AA/optimizer sequence");
        auto forged=pitch_fixture(original); auto bad=positions; bad[0]=19;
        const auto forged_native=save_stream(forged); const auto forged_formations=forged.formations;
        check(!save_pitch_formation_source(forged,manager,team,4,
              forged.manager_records.at(record+0x288),bad)&&save_stream(forged)==forged_native&&
              forged.formations==forged_formations,"pitch save rejects forged image neutrally");
        auto built_in=pitch_fixture(original); const auto built_native=save_stream(built_in);
        check(!save_pitch_formation_source(built_in,manager,team,0,
              built_in.manager_records.at(record+0x288),positions)&&save_stream(built_in)==built_native,
              "pitch save refuses non-owned built-in slot");
    }
    std::puts("PASS bounded Source pitch Engine actions (genuine Native base with explicit runtime gates)");
}
} // namespace

int main(int argc,char** argv) try {
    check(argc==3||argc==5,"usage: human_buyer_market_tests DATA_DIR genuine_2PNWK001.FBM [RESERVED.FBM FULL.FBM]");
    if(!*argv[1]) { std::puts("human buyer market tests: skipped (Original data not configured)"); return 77; }
    const auto fbm=load_savegame(argv[2]); auto texts=load_textd(std::filesystem::path(argv[1])/"TEXTD.BLK");
    auto original=GameState::from_save(SaveState(fbm.stream)); original.texts=&texts;
    // These are unchanged normal saves from the M139 CLI playthrough, not
    // artificial roster/counter controls. Original's equality lets the22-player
    // case bypass its reservation limit and eventually fill all24 physical rows.
    for(int a=3;a<argc;++a) {
        const auto file=load_savegame(argv[a]);
        check(file.checksum_ok&&file.version=="V2.24","natural capacity input checksum/version");
        auto g=GameState::from_save(SaveState(file.stream));g.texts=&texts;
        check(g.date==311&&g.memory.u8(::bmh::layout::ActiveManager)==0&&g.manager_teams.at(0)==1,
              "natural capacity input desk/date/owner");
        const auto range=squad_range(1);int active=0;
        for(size_t n=range.first;n<range.first+range.count;++n)
            if((g.squad_slots.at(n)&0x0fff)!=0x0fff)++active;
        check(active==(a==3?22:24)&&g.memory.u8(::bmh::layout::DeskActions_Unsupported_RuntimeField)==1&&
              (g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField)&0x0fff)==118&&
              ((g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField)>>12)&0x3ff)==1&&g.memory.u8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField)==2,
              "natural capacity input preserves outgoing loan and real roster");
        int entry=-1,player=-1;
        for(int e=0;e<40&&entry<0;++e) {
            const uint32_t row=list_start+uint32_t(e)*row_size;
            const int p=g.squad_slots.at(pool_start+size_t(e))&0x0fff;
            if(p==0x0fff||(g.memory.u16(row+8)&0x7fff)==1)continue;
            bool own=false,free=false;
            for(int b=0;b<4;++b) {
                own=own||g.memory.u16(row+uint32_t(b)*2)==1;
                free=free||g.memory.u16(row+uint32_t(b)*2)==0xffff;
            }
            if(!own&&free){entry=e;player=p;}
        }
        check(entry>=0,"natural capacity input has ordinary new bid candidate");
        const auto before=save_stream(g);WatcomRandom w(0x13579);MinuteReport report;int notices=0;
        TransferBidRequest bid;bid.manager=0;bid.list_entry=entry;bid.player=player;
        bid.source_slot=int(pool_start)+entry;bid.fee=100;bid.annual_salary=100;
        HumanBuyerSaleHooks hooks;hooks.allocator_gate=HumanBuyerSaleAllocatorGate::zero;
        hooks.direct_mode=HumanBuyerSaleDirectMode::ordinary_ai_purchase;
        hooks.notice=[&](const auto&,const HumanBuyerSaleNotice& notice){
            check(notice.manager==0&&notice.source_text_address==::bmh::layout::DeskActions_Unsupported_ExecutableField&&notice.source_flags==0x40,
                  "capacity overflow uses existing Source board refusal");++notices;
        };
        const auto result=submit_transfer_bid_source_human_buyer(g,bid,w,hooks,report);
        check(!report.unsupported&&result.status==TransferActionStatus::no_squad_room&&
              notices==1&&w.calls()==0&&w.seed()==0x13579&&save_stream(g)==before,
              "natural capacity overflow refuses before bid/account/loan/RNG changes");
        std::printf("PASS natural %d-player roster reserves loan118 without mutation or RNG\n",active);
    }
    check(original.manager_teams.size()==2&&original.manager_teams[0]==1&&original.manager_teams[1]==54&&
          original.memory.u8(::bmh::layout::ManagerCount)==2,"genuine Native 2PNWK001 identity/count");
    check(!original.memory.has(::bmh::layout::MatchEventsDisabled)&&!original.memory.has(::bmh::layout::DeskActions_EditorFail_RuntimeField2)&&!original.memory.has(::bmh::layout::Weekly_PendingSettleHumanBuyer_RuntimeField)&&
          !original.memory.has(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2),"genuine Native lacks unsaved Source runtime gates");
    pitch_engine_cases(original);
    {
        auto g=original; const auto c=make_free_control(g);
        const uint16_t source_word=uint16_t(g.squad_slots.at(c.request.source_pool_slot)|0x2000);
        g.squad_slots.at(c.request.source_pool_slot)=source_word;
        for(size_t at:{pool_start+39,pool_start+43,size_t(2972),size_t(3028),size_t(3071)})
            g.squad_slots.at(at)=source_word; // declared matching aliases, not AI-player relabelling
        settlement_case(g,c,HumanBuyerSaleAllocatorGate::zero,HumanBuyerSaleStatus::moved,
                        "genuine free-agent allocation/sentinel history/full-pool alias removal");
    }
    {
        auto g=original; const auto c=make_free_control(g,false);
        direct_case(g,c,HumanBuyerSaleAllocatorGate::zero,TransferActionStatus::accepted,
                    "genuine free-agent ordinary direct agreement/Source settlement",{::bmh::layout::DeskActions_SubmitTransferBidImpl_ExecutableField});
    }
    // Existing outgoing-offer loan checkbox/term survives editor submission,
    // direct qualification, fee booking, and AI-owner -> human-borrower expiry.
    {
        auto g=original;const auto c=make_control(g,false,-1,false,0,250000,70000,false);
        const uint32_t list=c.request.list_address;
        TransferBidRequest bid;bid.manager=c.buyer_manager;bid.list_entry=c.request.list_entry;
        bid.player=c.player;bid.source_slot=int(c.request.source_pool_slot);bid.contract_years=2;bid.loan=true;
        const uint32_t fee_base=(g.memory.u32(list+0x2c)/10000u)*0x30d4u;
        const uint32_t salary_base=((120u*g.memory.u32(list+0x30))/10000u)*100u;
        bid.fee=int32_t(fee_base*2u);bid.annual_salary=int32_t(salary_base*2u);
        for(size_t n=0;n<7;++n)bid.bonuses[n]=uint32_t(n+3);
        HumanBuyerSaleHooks hooks;hooks.allocator_gate=HumanBuyerSaleAllocatorGate::zero;
        hooks.direct_mode=HumanBuyerSaleDirectMode::ordinary_ai_purchase;
        std::vector<uint32_t> notices;hooks.notice=[&](const GameState&,const HumanBuyerSaleNotice& n){notices.push_back(n.source_text_address);};
        WatcomRandom w(0x13579);MinuteReport report;
        const auto result=submit_transfer_bid_source_human_buyer(g,bid,w,hooks,report);
        check(!report.unsupported&&result.status==TransferActionStatus::accepted,
              "AI-owner loan offer reaches the accepted human-buyer transaction route");
        int ledger=-1;for(int n=0;n<200&&ledger<0;++n)if(g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField+uint32_t(n)*4)!=0&&
            int(g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField+uint32_t(n)*4)&0xfff)==c.player)ledger=n;
        int row=-1;const auto buyer_range=squad_range(c.buyer);
        for(size_t n=0;n<buyer_range.count;++n)
            if((g.squad_slots.at(buyer_range.first+n)&0xfff)==c.player){row=int(n);break;}
        check(ledger>=0&&g.memory.u8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField+uint32_t(ledger))==24&&row>=0&&
              g.tactics.at(board_size+size_t(row)*row_size+0x0c)==0xff,
              "AI-owned loan writes 24-month return ledger and human borrower marker");
        for(int n=0;n<12;++n)check(g.memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField+uint32_t(n)*2)==0x0fff,
              "AI owner does not consume a human-owner board snapshot");
        const uint32_t buyer_history=rd(g.manager_records,record_size+0x760);
        check((buyer_history&3u)==3u,"buyer history marks both loan and buyer direction");
        auto loaded=GameState::from_save(SaveState(save_stream(g)));loaded.texts=&texts;
        loaded.memory.set8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField+uint32_t(ledger),1);
        int dialog_manager=-1;std::string dialog_text;
        loaded.loan_return_screen=[&](GameState&,int manager,const std::string& question){
            dialog_manager=manager;dialog_text=question;return 1;
        };
        bool return_editor_opened=false,return_offer_submitted=false;
        loaded.loan_return_offer_screen=[&](GameState& staged,GameRandom& editor_game,WatcomRandom& editor_rand,
                int manager,int source_slot,int list_entry,const HumanBuyerSaleHooks&)->int {
            const uint32_t row=::bmh::layout::EditorList+uint32_t(list_entry)*0x58;
            int bid_slot=-1;
            for(int i=0;i<4;++i)if(staged.memory.u16(row+uint32_t(i)*2)==0xffff){bid_slot=i;break;}
            TransferOfferEditorRequest request{manager,staged.manager_teams.at(size_t(manager)),list_entry,
                source_slot,int(staged.squad_slots.at(size_t(source_slot))&0x0fff),staged.memory.u16(row+8),bid_slot};
            request.loan_return_entry = true;
            auto editor=open_transfer_offer_editor_source(staged,request,editor_game,editor_rand);
            if(!editor.supported||!editor.editable)return -1;
            return_editor_opened=true;
            editor=edit_transfer_offer_editor_source(staged,editor,
                {TransferOfferEditorField::fee,staged.memory.u32(row+0x2c),true},editor_game,editor_rand);
            editor=edit_transfer_offer_editor_source(staged,editor,
                {TransferOfferEditorField::salary,staged.memory.u32(row+0x30),true},editor_game,editor_rand);
            HumanBuyerSaleHooks sale; sale.allocator_gate=HumanBuyerSaleAllocatorGate::zero;
            sale.direct_mode=HumanBuyerSaleDirectMode::ordinary_ai_purchase;
            sale.notice=[](const GameState&,const HumanBuyerSaleNotice&){};
            MinuteReport editor_report;
            const auto submitted=submit_transfer_offer_editor_source(staged,editor,editor_rand,sale,editor_report);
            return_offer_submitted=!editor_report.unsupported &&
                (submitted.action.status==TransferActionStatus::accepted ||
                 submitted.action.status==TransferActionStatus::pending);
            return return_offer_submitted?1:-1;
        };
        GameRandom return_game(73);WatcomRandom return_rand(1);MinuteReport return_report;
        month_change(loaded,return_rand,return_report,&return_game);
        check(!return_report.unsupported&&contains(loaded,c.seller,c.player)&&!contains(loaded,c.buyer,c.player)&&
              loaded.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField+uint32_t(ledger)*4)==0,
              "AI-owner loan survives save/load and returns from human borrower at expiry");
        check(dialog_manager==c.buyer_manager&&dialog_text.ends_with(message_text(loaded,::bmh::layout::Month_ReturnLoans_TextPointer6)),
              "human borrower receives exact format field question for AI-owned loan");
        check(return_editor_opened&&return_offer_submitted,
              "Source YES opens the actual legacy routine editor and submits through shared human-buyer settlement");
    }
    // Issue34: both ends of the format field sixteen-slot club range are
    // legitimate sellers. Use actual unique roster players/listing creation,
    // then the existing independently composed qualification/settlement oracle.
    for(int seller:{90,137}) {
        auto g=original;
        const auto c=make_control(g,false,-1,false,0,250000,70000,false,seller);
        check(c.seller==seller&&squad_range(seller).count==16,"Source16-slot seller identity");
        direct_case(g,c,HumanBuyerSaleAllocatorGate::zero,TransferActionStatus::accepted,
                    "Source16-slot seller "+std::to_string(seller)+" direct purchase",{::bmh::layout::DeskActions_SubmitTransferBidImpl_ExecutableField});
    }
    {
        // Issue34's named player is in the genuine SSC Neapel sixteen-slot
        // squad, not a club >=138. Season initialization reorders runtime
        // clubs: this unchanged 2PN input has Napoli126 and OLIVERA slot2738.
        // Generate only an ordinary listing through format field, then compare
        // the complete purchase against the independent Source composition.
        auto g=original;
        constexpr int seller=126,player=2012;
        check(g.club_name(seller)=="SSC NEAPEL"&&g.player_name(player)=="OLIVERA"&&
              find_player(g,seller,player)==2738&&squad_range(seller).count==16,
              "Issue34 genuine named OLIVERA/SSC Neapel runtime identity");
        const auto c=make_control(g,false,-1,false,0,6000000,200000,false,seller,player);
        check(c.seller==seller&&c.player==player&&g.memory.u16(c.request.list_address+8)==seller,
              "format field produces Napoli runtime seller, never a name-pool offset or higher metadata identity");
        direct_case(g,c,HumanBuyerSaleAllocatorGate::zero,TransferActionStatus::accepted,
                    "Issue34 OLIVERA/SSC Neapel ordinary full purchase",{::bmh::layout::DeskActions_SubmitTransferBidImpl_ExecutableField});
    }
    for(uint16_t seller:{uint16_t(138),uint16_t(0x8000|138),uint16_t(167),
                         uint16_t(168),uint16_t(356),uint16_t(0x165)}) {
        auto g=original;const auto c=make_free_control(g,false);
        g.memory.set16(c.request.list_address+8,seller);
        const auto before=save_stream(g);WatcomRandom w(1);MinuteReport report;size_t notices=0;
        HumanBuyerSaleHooks h;h.allocator_gate=HumanBuyerSaleAllocatorGate::zero;
        h.direct_mode=HumanBuyerSaleDirectMode::ordinary_ai_purchase;
        h.notice=[&](const auto&,const auto&){++notices;};
        TransferBidRequest bid;bid.manager=c.buyer_manager;bid.list_entry=c.request.list_entry;bid.player=c.player;
        bid.source_slot=int(c.request.source_pool_slot);
        bid.fee=int32_t(c.fee);bid.annual_salary=int32_t(c.salary);bid.contract_years=1;
        const auto result=submit_transfer_bid_source_human_buyer(g,bid,w,h,report);
        check(result.status==TransferActionStatus::unsupported&&report.unsupported&&notices==0&&
              w.seed()==1&&w.calls()==0&&save_stream(g)==before&&
              report.reason=="seller is outside the Original ordinary club squads (clubs 0..137 or free agents only)",
              "seller outside ordinary Source squads rejects before Native/bid/W/notice changes with clear reason");
        const TransferOfferEditorRequest editor_request{c.buyer_manager,c.buyer,c.request.list_entry,
            int(c.request.source_pool_slot),c.player,seller,0};
        GameRandom game(73);
        const auto editor=open_transfer_offer_editor_source(g,editor_request,game,w);
        check(!editor.supported&&editor.reason==report.reason&&save_stream(g)==before&&
              game.seed()==73&&game.calls()==0&&w.seed()==1&&w.calls()==0,
              "same clear ordinary-seller boundary at editor opening before Native/W/G changes");
    }
    for(uint16_t invalid:{uint16_t(0xffff),uint16_t(0x7fff)}) {
        auto g=original; const auto c=make_free_control(g);
        g.memory.set16(c.request.list_address+8,invalid);
        HumanBuyerSaleHooks h;h.allocator_gate=HumanBuyerSaleAllocatorGate::zero;
        unsupported_case(g,c.request,h,"free-agent seller must not normalize FFFF/7FFF");
        // Direct preflight must reject the same malformed seller before a bid
        // or qualification draw, not silently use the legacy mover.
        g=original;const auto direct=make_free_control(g,false);
        g.memory.set16(direct.request.list_address+8,invalid);
        const auto before=save_stream(g);WatcomRandom w(1);MinuteReport report;size_t notices=0;
        TransferBidRequest bid;bid.manager=direct.buyer_manager;bid.list_entry=direct.request.list_entry;bid.player=direct.player;
        bid.source_slot=int(direct.request.source_pool_slot);
        bid.fee=int32_t(direct.fee);bid.annual_salary=int32_t(direct.salary);bid.contract_years=1;
        h.direct_mode=HumanBuyerSaleDirectMode::ordinary_ai_purchase;
        h.notice=[&](const auto&,const auto&){++notices;};
        const auto result=submit_transfer_bid_source_human_buyer(g,bid,w,h,report);
        check(result.status==TransferActionStatus::unsupported&&report.unsupported&&notices==0&&
              w.calls()==0&&save_stream(g)==before,"invalid free-agent sentinel direct preflight leaves Native/RNG/bids unchanged");
    }
    for(int row:{0,1,19}) {
        auto g=original; const auto c=make_control(g,false,row);
        // Preserve a pool flag that Source transfers as a full word. Additional
        // ordinary/special occurrences prove ReleasePlayer's 44-slot matching cleanup.
        g.squad_slots.at(c.request.source_pool_slot)|=0x2000;
        g.squad_slots.at(pool_start+39)=g.squad_slots.at(c.request.source_pool_slot);
        g.squad_slots.at(pool_start+43)=g.squad_slots.at(c.request.source_pool_slot);
        settlement_case(g,c,HumanBuyerSaleAllocatorGate::zero,HumanBuyerSaleStatus::moved,
                        "AI sale allocator row "+std::to_string(row));
    }
    {
        auto g=original; const auto c=make_control(g,true,0,false,2);
        g.memory.set16(c.request.list_address,3); // unrelated real ordinary bidder must survive.
        settlement_case(g,c,HumanBuyerSaleAllocatorGate::zero,HumanBuyerSaleStatus::moved,"human seller shared nonzero bid2");
    }
    {
        auto g=original; const auto c=make_control(g,false,0,false,0,250000,0xf0000000u);
        settlement_case(g,c,HumanBuyerSaleAllocatorGate::zero,HumanBuyerSaleStatus::moved,"wrapped salary unsigned final divide");
    }
    for(int scenario=0;scenario<6;++scenario) {
        auto g=original; const bool full=scenario==5;
        const auto c=make_control(g,false,-1,full,0,scenario==1?750000u:250000u);
        if(scenario<=3) g.manager_records.at(record_size+0x170)=uint8_t(0x1c|(g.players.at(size_t(c.player)).w364&3));
        auto gate=HumanBuyerSaleAllocatorGate::zero; auto outcome=HumanBuyerSaleStatus::moved;
        std::string label="subsidy adjusted cash/raw history";
        if(scenario==1) {
            g.manager_records.at(record_size+0x170)=uint8_t(0x04|(g.players.at(size_t(c.player)).w364&3));
            g.manager_records.at(record_size+0x210)=3; // fee exceeds threshold; ACK consumes subsidy.
            label="subsidy threshold ACK without discount";
        } else if(scenario==2) {
            wr(g.manager_records,record_size+0x314,c.fee); wr(g.manager_records,record_size+0x31c,uint32_t(-200000));
            outcome=HumanBuyerSaleStatus::funds_refused; label="subsidy then funds refusal retains ordinary pool/bids";
        } else if(scenario==3) {
            g.manager_records.at(record_size+0x170)=uint8_t(0x1c|((g.players.at(size_t(c.player)).w364+1)&3));
            label="position mismatch keeps subsidy";
        } else if(scenario==4) {
            gate=HumanBuyerSaleAllocatorGate::blocked;outcome=HumanBuyerSaleStatus::allocator_blocked;
            label="paid allocator blocked retains ordinary pool/bids";
        } else if(scenario==5) {
            outcome=HumanBuyerSaleStatus::no_room; label="paid full-word no-room retains ordinary pool/bids";
        }
        settlement_case(g,c,gate,outcome,label);
    }
    {
        auto g=original; const auto c=make_control(g,true,-1,true);
        g.memory.set8(::bmh::layout::ActiveManager,0); auto request=c; request.request.expected_active_manager=0;
        settlement_case(g,request,HumanBuyerSaleAllocatorGate::zero,HumanBuyerSaleStatus::no_room,
                        "no-room notice uses current seller rather than buyer");
    }
    for(bool blocked:{false,true}) {
        auto g=original; const auto c=make_control(g,false,-1,false,0,250000,70000,false);
        g.memory.set16(c.request.list_address,3); // actual retained competing ordinary buyer.
        direct_case(g,c,blocked?HumanBuyerSaleAllocatorGate::blocked:HumanBuyerSaleAllocatorGate::zero,
                    blocked?TransferActionStatus::allocator_blocked:TransferActionStatus::accepted,
                    blocked?"direct paid blocked preserves all ordinary bids":"direct AI purchase normal ACK/shared Source settlement");
    }
    {
        auto g=original; const auto c=make_control(g,false,-1,true,0,250000,70000,false);
        g.memory.set16(c.request.list_address,3);
        direct_case(g,c,HumanBuyerSaleAllocatorGate::zero,TransferActionStatus::no_squad_room,
                    "direct paid FULLWORD no-room preserves ordinary pool/bids",{::bmh::layout::DeskActions_SubmitTransferBidImpl_ExecutableField,::bmh::layout::PendingOffer_FormatError_ExecutableField});
    }
    {
        auto g=original; const auto c=make_control(g,false,-1,false,0,250000,70000,false);
        g.memory.set16(c.request.list_address,3);
        // Declared scalar control of the real Source-generated ordinary listing:
        // its published qualification bases yield a 250000 bid, below the
        // matching position/tier subsidy threshold. No special pending row.
        g.memory.set32(c.request.list_address+0x2c,100000);
        g.memory.set32(c.request.list_address+0x30,10000);
        g.manager_records.at(record_size+0x170)=uint8_t(0x1c|(g.players.at(size_t(c.player)).w364&3));
        direct_case(g,c,HumanBuyerSaleAllocatorGate::zero,TransferActionStatus::accepted,
                    "direct subsidy ACK then adjusted payment/raw history",{::bmh::layout::DeskActions_SubmitTransferBidImpl_ExecutableField,::bmh::layout::PendingOffer_ConstructionNoticeDialog_TextPointer},175000);
    }
    for(bool subsidy:{false,true}) {
        auto g=original; const auto c=make_control(g,false,-1,false,0,250000,70000,false);
        g.memory.set16(c.request.list_address,3);
        g.memory.set32(c.request.list_address+0x2c,100000);
        g.memory.set32(c.request.list_address+0x30,10000);
        // Explicit Native financial scalar control, never a mutation by the
        // notice observer. The existing ordinary qualification's int64 board
        // allowance admits the bid. Original68d06 instead executes ADD32 and
        // a signed CMP/JL: data-field offset + 0x100 wraps to negative data-field offset,
        // so the later funds notice refuses this positive fee. This probes the
        // actual Source signed32 boundary despite the known upstream qualifier
        // approximation; it is not claimed as a naturally captured balance.
        wr(g.manager_records,record_size+0x314,0x7ffffff0u);
        wr(g.manager_records,record_size+0x31c,0x100u);
        if(subsidy) g.manager_records.at(record_size+0x170)=uint8_t(0x1c|(g.players.at(size_t(c.player)).w364&3));
        direct_case(g,c,HumanBuyerSaleAllocatorGate::zero,TransferActionStatus::insufficient_funds,
                    subsidy?"direct subsidy then signed32 funds refusal preserves pool/bids"
                           :"direct signed32 funds refusal preserves pool/bids",
                    subsidy?std::vector<uint32_t>{::bmh::layout::DeskActions_SubmitTransferBidImpl_ExecutableField,::bmh::layout::PendingOffer_ConstructionNoticeDialog_TextPointer,::bmh::layout::Advertising_FormatError_TextPointer18}
                           :std::vector<uint32_t>{::bmh::layout::DeskActions_SubmitTransferBidImpl_ExecutableField,::bmh::layout::Advertising_FormatError_TextPointer18},
                    subsidy?175000u:250000u);
    }
    for(bool reverse:{false,true}) {
        auto g=original;const auto c=reverse?make_reverse_human_control(g):make_control(g,true,-1,false,0,250000,70000,false);
        direct_case(g,c,HumanBuyerSaleAllocatorGate::zero,TransferActionStatus::accepted,
                    reverse?"Source ordinary human seller54 to buyer1 direct full composition":"Source ordinary human seller1 to buyer54 direct full composition",{::bmh::layout::DeskActions_SubmitTransferBidImpl_ExecutableField});
    }
    for(int mode:{1,2,3}) {
        auto g=original;const auto c=make_control(g,false,-1,false,0,250000,70000,false);
        g.memory.set8(::bmh::layout::GameMode,uint8_t(mode));
        direct_case(g,c,HumanBuyerSaleAllocatorGate::zero,TransferActionStatus::accepted,
                    "Source mode"+std::to_string(mode)+" all-seven-bonus direct qualification",{::bmh::layout::DeskActions_SubmitTransferBidImpl_ExecutableField});
        g=original;const auto refused=make_control(g,false,-1,false,0,250000,70000,false);
        retained_case(g,refused,mode,RetainedKind::salary_refused,
                      "Source mode"+std::to_string(mode)+" salary short circuit/persisted bid");
        if(mode!=3) {
            g=original;const auto cheap=make_control(g,false,-1,false,0,250000,70000,false);
            g.memory.set8(::bmh::layout::GameMode,uint8_t(mode));
            direct_case(g,cheap,HumanBuyerSaleAllocatorGate::zero,TransferActionStatus::accepted,
                        "Source mode"+std::to_string(mode)+" low fee replaced by200 after salary pass",{::bmh::layout::DeskActions_SubmitTransferBidImpl_ExecutableField},0,true);
        }
    }
    for(bool free:{false,true}) {
        auto g=original;const auto c=free?make_free_control(g,false):make_control(g,false,-1,false,0,250000,70000,false);
        retained_case(g,c,3,RetainedKind::fee_refused,free?"canonical free-agent format field/persisted bid":"AI seller Source name+af30c/persisted bid");
    }
    for(const auto kind:{RetainedKind::existing_bid,RetainedKind::capacity}) {
        auto g=original;const auto c=make_control(g,false,-1,false,0,250000,70000,false);
        retained_case(g,c,3,kind,kind==RetainedKind::existing_bid?"existing bidder edits without qualifier/RNG":
                      "Source capacity equality refusal before bid writes/RNG");
    }
    {
        auto g=original; const auto c=make_control(g);
        HumanBuyerSaleHooks hooks; hooks.allocator_gate=HumanBuyerSaleAllocatorGate::zero;
        auto request=c.request; request.list_address+=row_size;
        unsupported_case(g,request,hooks,"mismatched explicit coordinates");
        request=c.request; request.expected_active_manager=0;
        unsupported_case(g,request,hooks,"wrong active runtime owner");
        hooks.allocator_gate=HumanBuyerSaleAllocatorGate::unsupported;
        unsupported_case(g,c.request,hooks,"unknown allocator gate");
        hooks.allocator_gate=HumanBuyerSaleAllocatorGate::zero;
        auto loan=g;loan.memory.set32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField,uint32_t(c.player)|(uint32_t(c.seller)<<12)|(uint32_t(c.buyer)<<22));
        unsupported_case(loan,c.request,hooks,"existing return loan");
        auto missing=g; missing.squad_slots.at(find_player(missing,c.seller,c.player))=0xfff;
        unsupported_case(missing,c.request,hooks,"missing seller player/error32");
        auto small=g; small.manager_teams[1]=90; small.manager_records.at(record_size+0x290)=90;
        small.memory.set16(c.request.list_address,90);request=c.request;request.expected_buyer=90;
        unsupported_case(small,request,hooks,"16-slot human buyer");
        auto direct=g; direct.memory.set16(c.request.list_address,0xffff);
        TransferBidRequest bid;bid.manager=1;bid.list_entry=c.request.list_entry;bid.player=c.player;
        bid.source_slot=int(c.request.source_pool_slot);
        bid.fee=10000000;bid.annual_salary=1000000;bid.contract_years=2;
        const auto before=save_stream(direct);WatcomRandom w(1);MinuteReport report;size_t notices=0;
        hooks.notice=[&](const GameState&,const HumanBuyerSaleNotice&){++notices;};
        const auto result=submit_transfer_bid_source_human_buyer(direct,bid,w,hooks,report);
        check(result.status==TransferActionStatus::unsupported&&report.unsupported&&notices==0&&
              w.calls()==0&&save_stream(direct)==before,"direct default runtime context unsupported before bid/RNG/ACK");
    }
    std::puts("PASS human-buyer Source sale/loan settlement, allocator boundaries, and loan calendar expiry");
    return 0;
} catch(const std::exception& e) { std::fprintf(stderr,"human_buyer_market_tests: %s\n",e.what()); return 1; }
