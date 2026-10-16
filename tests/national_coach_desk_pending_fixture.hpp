// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#pragma once

#include <algorithm>
#include <stdexcept>
#include "game/national.hpp"
#include "game/pending_offer.hpp"

// Explicit in-memory controls, never presented as untouched Original saves.
// The base is a loaded national desk. Only its order prefix, a selected national
// row (owner0 control), and one fully declared format field-shaped offer change.
namespace national_pending_control {
constexpr uint32_t offers=::bmh::layout::OfferSlots, list=::bmh::layout::EditorList+40*0x58;
constexpr size_t pool=0xb70+40, record_size=0xb0c, board_size=0x840, row_size=0x58;
inline int record_team(const bmh::GameState& g,int owner) {
    const size_t at=size_t(owner)*record_size+0x290;
    return int(g.manager_records.at(at)) | int(g.manager_records.at(at+1))<<8;
}
inline uint32_t read(const std::vector<uint8_t>& v,size_t at) {
    uint32_t n=0;for(int i=0;i<4;++i)n|=uint32_t(v.at(at+size_t(i)))<<(8*i);return n;
}
inline void write(std::vector<uint8_t>& v,size_t at,uint32_t n) {
    for(int i=0;i<4;++i)v.at(at+size_t(i))=uint8_t(n>>(8*i));
}
inline bool contains(const bmh::GameState& g,int team,int player) {
    const auto r=bmh::squad_range(team);
    for(size_t i=r.first;i<r.first+r.count;++i)if((g.squad_slots.at(i)&0xfff)==player)return true;
    return false;
}
struct Offer {
    int owner=-1, seller=-1, buyer=-1, player=-1;
    int anchor=-1, row=-1, seller_row=-1;
    uint32_t salary=0, fee=2470000;
};
inline Offer prepare(bmh::GameState& g,int owner,bool salary=true) {
    if(g.manager_teams!=std::vector<int>{bmh::kNoTeam} || !g.memory.u8(::bmh::layout::CoachActive) ||
        owner<0 || owner>=g.memory.u8(::bmh::layout::OriginalManagerCount))
        throw std::runtime_error("control requires a genuine national takeover and retained owner");
    Offer o;o.owner=owner;o.seller=record_team(g,owner==0?4:owner);
    const auto seller=bmh::squad_range(o.seller);
    for(size_t row=0;row<seller.count;++row) {
        const int player=g.squad_slots.at(seller.first+row)&0xfff;
        if(player==0xfff || size_t(player)>=g.players.size())continue;
        o.player=player;o.seller_row=int(row);break;
    }
    if(o.player<0)throw std::runtime_error("control seller has no actual player");
    const int parked=record_team(g,4);
    for(int team=0;team<90;++team) {
        if(team==o.seller || team==parked || g.manager_of(team)>=0 || contains(g,team,o.player))continue;
        const auto r=bmh::squad_range(team);
        for(size_t at=r.first;at<r.first+r.count;++at)
            if(g.squad_slots.at(at)==0xfff){o.buyer=team;break;}
        if(o.buyer>=0)break;
    }
    if(o.buyer<0)throw std::runtime_error("control has no genuine AI buyer with room");
    const auto runtime=bmh::squad_range(owner==0?bmh::kNoTeam:o.seller);
    o.row=owner==0?2:o.seller_row;o.anchor=int(runtime.first)+o.row;
    if(owner==0)g.squad_slots.at(size_t(o.anchor))=uint16_t(o.player);
    const size_t board=size_t(owner)*board_size+size_t(o.row)*row_size;
    g.tactics.at(board+0x0c)=24;
    o.salary=std::max(100000u,read(g.tactics,board+0x50)+1000u);
    // NO also reads the seller's club row against param8's board, rather than
    // the national anchor row. Set both explicit controls above the old salary.
    o.salary=std::max(o.salary,read(g.tactics,size_t(owner)*board_size+size_t(o.seller_row)*row_size+0x50)+1000u);
    for(int raw=0;raw<4;++raw){g.memory.set32(offers+uint32_t(raw)*4,0xffffffffu);g.squad_slots.at(pool+size_t(raw))=0xfff;}
    for(uint32_t byte=0;byte<0x58;++byte)g.memory.set8(list+byte,0);
    for(uint32_t bid=0;bid<4;++bid)g.memory.set16(list+bid*2,0xffff);
    g.memory.set16(list,uint16_t(o.buyer));g.memory.set16(list+8,uint16_t(o.seller)|0x8000u);
    g.memory.set32(list+0x0c,o.salary);g.memory.set32(list+0x1c,o.fee);g.memory.set8(list+0x34,24);
    g.memory.set32(offers,uint32_t(o.player)*1024u+uint32_t(owner)+(salary?0x80000000u:0));
    g.squad_slots.at(pool)=uint16_t(o.player);
    int position=-1;
    for(int i=0;i<g.memory.u8(::bmh::layout::OriginalManagerCount);++i)if(g.memory.u16(::bmh::layout::NextDeskManager+uint32_t(i)*2)==owner)position=i;
    if(position<0)throw std::runtime_error("control owner missing from genuine permutation");
    const auto old=g.memory.u16(::bmh::layout::NextDeskManager);
    g.memory.set16(::bmh::layout::NextDeskManager,uint16_t(owner));g.memory.set16(::bmh::layout::NextDeskManager+uint32_t(position)*2,old);
    g.memory.set8(::bmh::layout::DeskPosition,0);g.memory.set8(::bmh::layout::ActiveManager,0);
    return o;
}
// Further declared controls: point the real retained owner's entry at the
// actual parked club, give it a known budget/no subsidy and a free final row.
inline Offer prepare_parked_buyer(bmh::GameState& g,bool loan=false,bool salary=true) {
    Offer o=prepare(g,1,salary);o.buyer=record_team(g,4);
    const auto buyer=bmh::squad_range(o.buyer);
    g.squad_slots.at(buyer.first+buyer.count-1)=0xfff;
    g.memory.set16(list,uint16_t(o.buyer));g.memory.set8(list+0x34,loan?0x8c:24);
    g.memory.set32(list+0x2c,1000000);g.memory.set32(list+0x30,10000);
    g.memory.set8(list+0x54,100);g.memory.set8(list+0x55,0);
    for(int k=0;k<7;++k)g.memory.set8(list+0x38+uint32_t(k),uint8_t(k+2));
    write(g.manager_records,4*record_size+0x314,100000000);
    write(g.manager_records,4*record_size+0x31c,0);
    g.manager_records.at(4*record_size+0x170)=0;
    g.level=6;
    return o;
}
inline void fill_with_free_agents(bmh::GameState& g,int team) {
    const auto range=bmh::squad_range(team);
    size_t agent=2972;
    for(size_t at=range.first;at<range.first+range.count;++at) {
        if(g.squad_slots.at(at)!=0xfff)continue;
        while(agent<3072 && ((g.squad_slots.at(agent)&0xfff)==0xfff ||
            contains(g,team,g.squad_slots.at(agent)&0xfff)))++agent;
        if(agent==3072)throw std::runtime_error("full buyer control exhausted genuine free agents");
        g.squad_slots.at(at)=g.squad_slots.at(agent)&0xfff;g.squad_slots.at(agent++)=0xfff;
    }
}
inline auto request(){return bmh::PendingOfferDeskRequest{bmh::PendingOfferDeskRuntimeMode::normal_interactive,1,0};}
inline void cleanup(bmh::GameState& g,int owner) {
    g.memory.set8(::bmh::layout::ActiveManager,uint8_t(owner));g.memory.set8(::bmh::layout::DeskPosition,1);
    for(int raw=0;raw<4;++raw){g.memory.set32(offers+uint32_t(raw)*4,0xffffffffu);g.squad_slots.at(pool+size_t(raw))=0xfff;}
}
} // namespace national_pending_control
