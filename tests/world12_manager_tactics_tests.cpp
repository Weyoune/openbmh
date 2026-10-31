// SPDX-License-Identifier: GPL-3.0-only
#include "world12_fixture.hpp"
#include "app/world12_manager.hpp"
#include "game/lineup.hpp"
#include "game/world12/codec.hpp"
#include "game/world12/save.hpp"
#include "cli/json.hpp"
#include "formats/le.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
using namespace bmh::world12;namespace app=bmh::app::world12;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Function> void rejects(Function function,const char* message){bool rejected=false;try{function();}catch(const std::exception&){rejected=true;}check(rejected,message);}
ManagerTactics tactics() {
    ManagerTactics result;result.automatic_weights=0xc9;result.automatic_formation=3;
    const uint8_t depth[]={0,5,5,5,5,10,10,10,10,17,17};
    const uint8_t lane[]={4,1,3,5,7,1,3,5,7,3,5};
    for(size_t r=0;r<24;++r){for(size_t a=0;a<4;++a)result.rows[r].orders[a]=uint8_t(2+(r+a)%39);if(r<11){result.rows[r].depth=depth[r];result.rows[r].lane=lane[r];}}
    result.rows[23].orders={255,0,49,128};result.rows[23].depth=255;result.rows[23].lane=254;
    return result;
}
World world(bool english=false) {
    auto w=bmh::test::world12::complete_world();const auto club=w.membership[english?5:0].clubs[0];
    auto& roster=w.clubs.at(club).roster;
    for(uint32_t r=11;r<20;++r){PlayerRecord p;p.id={900000+r};p.attributes={"Reserve",50u|(60u<<7)|(65u<<14),0,1};w.players[p.id]=p;roster[r]={p.id,uint16_t(r<17?2:0)};}
    w.managers={{3,"Human",club,{english?5u:0u},3,ManagerFinance{5000,1000,0,0,0},tactics()}};w.validate();return w;
}
bmh::GameState native(const World& w,TacticalPolicy policy={}) {
    const auto& t=*w.managers[0].tactics;const auto& club=w.clubs.at(w.managers[0].club);
    constexpr size_t manager=3,team=72;
    bmh::GameState g;g.manager_teams={0xfff,0xfff,0xfff,int(team)};g.club_ids.resize(445);g.team_flags.resize(445);g.club_heap_78.resize(445*2);
    g.manager_records.assign(4*0xb0c,0x5a);g.manager_options.assign(20,0x5a);g.tactics.assign(4*0x840,0xa5);g.formations.resize(4*0x160);
    g.manager_records[manager*0xb0c+0x290]=uint8_t(team);g.manager_records[manager*0xb0c+0x291]=0;
    g.manager_records[manager*0xb0c+0x288]=t.automatic_weights;g.manager_options[manager*5]=t.automatic_formation;
    g.memory.add_block(bmh::layout::ActiveManager,1);g.memory.set8(bmh::layout::ActiveManager,3);
    g.memory.add_block(bmh::layout::GameMode,1);g.memory.set8(bmh::layout::GameMode,uint8_t(policy.mode));
    g.indoor=policy.indoor;g.lineups_frozen=policy.frozen;g.level=policy.difficulty;g.projected_foreign_rule=policy.foreign_rule_enabled;
    g.projected_match_clubs.resize(445);g.projected_match_clubs[team]={0,int(club.country),3};g.squad_slots.assign(bmh::runtime_dimensions.total_slots,0xfff);
    g.players.resize(24);g.player_cards.resize(24);g.player_bonus.resize(24);
    const auto range=bmh::squad_range(int(team));check(range.count==24,"native human squad must have 24 rows");
    for(size_t r=0;r<24;++r) {
        const auto at=manager*0x840+r*0x58;const auto& row=t.rows[r];
        std::copy(row.orders.begin(),row.orders.end(),g.tactics.begin()+at+2);g.tactics[at+0x3f]=row.partner;g.tactics[at+0x41]=row.depth;g.tactics[at+0x42]=row.lane;
        const auto& slot=club.roster[r];if(slot.empty())continue;
        const auto local=23-r;g.players[local]=w.players.at(slot.player).attributes;g.player_cards[local]=w.players.at(slot.player).cards;g.player_bonus[local]=w.players.at(slot.player).bonus;
        g.squad_slots[range.first+r]=uint16_t(local|((slot.flags&1)?0x8000:0)|((slot.flags&2)?0x4000:0)|((slot.flags&4)?0x2000:0)|((slot.flags&8)?0x1000:0));
    }
    return g;
}
bool direct(bmh::GameState& g,const TacticalAction& action) {
    const auto range=bmh::squad_range(72);const auto slot=action.row>=0&&action.row<24?g.squad_slots[range.first+size_t(action.row)]:uint16_t(0xfff);
    switch(action.kind){
    case TacticalActionKind::order:return bmh::set_player_order_source(g,3,72,action.row,slot&0xfff,action.aspect,action.value);
    case TacticalActionKind::move:return bmh::move_pitch_player_source(g,3,72,action.row,slot&0xfff,slot,action.old_depth,action.old_lane,action.depth,action.lane);
    case TacticalActionKind::weights:return bmh::set_pitch_automatic_weight_source(g,3,72,action.aspect,action.old_weights,action.new_weights);
    }
    return false;
}
void compare(const World& w,const TacticalAction& action,TacticalPolicy policy={},bool check_allowed=true) {
    auto reference=native(w,policy);const auto before=reference;const bool allowed=direct(reference,action);
    const auto actual=calculate_manager_tactical_action(w,3,action,policy);
    if(check_allowed)check(actual.allowed==allowed,"typed tactical acceptance differs from direct Original helper");
    check(actual.tactics==manager_tactics_from_legacy(reference,3),"typed tactical fields differ from direct Original helper");
    check(actual.changed==(actual.tactics!=*w.managers[0].tactics),"wrong tactical changed result");
    // Narrow helpers must leave unrelated bytes, other managers and rosters intact.
    check(reference.squad_slots==before.squad_slots&&reference.players.size()==before.players.size()&&reference.player_cards==before.player_cards,
        "tactical helper changed squad/player mapping");
    for(size_t m=0;m<3;++m)check(std::equal(before.tactics.begin()+m*0x840,before.tactics.begin()+(m+1)*0x840,reference.tactics.begin()+m*0x840),"another manager board changed");
}
void core_cases() {
    size_t cases=0;
    for(bool english:{false,true}) {
        auto w=world(english);const auto& roster=w.clubs.at(w.managers[0].club).roster;
        for(int row:{0,5,10,11,19})for(int aspect=0;aspect<4;++aspect)for(int value:{-100,0,2,17,40,200}) {
            TacticalAction a;a.row=row;a.player=roster[size_t(row)].player;a.aspect=aspect;a.value=value;a.expected_order=w.managers[0].tactics->rows[size_t(row)].orders[size_t(aspect)];compare(w,a);++cases;
        }
        for(bool indoor:{false,true})for(const auto target:{std::pair{5,3},std::pair{5,2},std::pair{7,4},std::pair{-1,4},std::pair{20,4},std::pair{5,1}}) {
            TacticalAction a;a.kind=TacticalActionKind::move;a.row=1;a.player=roster[1].player;a.old_depth=5;a.old_lane=1;a.depth=target.first;a.lane=target.second;
            compare(w,a,{3,indoor,false});++cases;
        }
        for(int mode:{1,2,3})for(int aspect=0;aspect<3;++aspect)for(unsigned field=0;field<4;++field) {
            auto weighted=w;weighted.managers[0].tactics->automatic_weights=uint8_t((0xc9u&~(3u<<unsigned(aspect*2)))|(field<<unsigned(aspect*2)));
            TacticalAction a;a.kind=TacticalActionKind::weights;a.aspect=aspect;a.old_weights=weighted.managers[0].tactics->automatic_weights;
            const unsigned shift=unsigned(aspect*2);a.new_weights=uint8_t((a.old_weights&~(3u<<shift))|((((a.old_weights>>shift)+1)&3u)<<shift));
            compare(weighted,a,{mode,false,false});++cases;
        }
        auto linked=w;linked.managers[0].tactics->rows[1].partner=2;linked.managers[0].tactics->rows[2].partner=1;
        TacticalAction clear;clear.kind=TacticalActionKind::move;clear.row=1;clear.player=roster[1].player;clear.old_depth=5;clear.old_lane=1;clear.depth=7;clear.lane=4;
        compare(linked,clear,{3,true,false});++cases;
        const auto cleared=calculate_manager_tactical_action(linked,3,clear,{3,true,false});
        check(cleared.allowed&&cleared.tactics.rows[1].partner==255&&cleared.tactics.rows[2].partner==255,"old indoor partnership survived placement");
        TacticalAction injured;injured.kind=TacticalActionKind::move;injured.row=1;injured.player=roster[1].player;injured.old_depth=5;injured.old_lane=1;injured.depth=7;injured.lane=4;
        w.players.at(injured.player).attributes.w364|=1u<<16;compare(w,injured);++cases;
    }
    std::cout<<cases<<" German/English order, outdoor/indoor placement, injury and weight cases match direct Original helpers\n";
}
void ownership_codec() {
    auto w=world();const auto initial=w;const auto club=w.managers[0].club;app::ManagerActionOwner owner{71,8};
    app::ManagerActionIdentity identity{3,club,71,8};TacticalAction action;action.row=0;action.player=w.clubs.at(club).roster[0].player;action.aspect=0;
    action.expected_order=w.managers[0].tactics->rows[0].orders[0];action.value=40;
    const auto result=app::apply_manager_tactical_action(w,owner,identity,action,{});
    check(result.allowed&&result.changed&&owner.revision==9&&w.managers[0].tactics->rows[0].orders[0]==40,"tactical change/revision not published");
    const auto committed=w;rejects([&]{app::apply_manager_tactical_action(w,owner,identity,action,{});},"tactical replay accepted");
    check(w==committed&&owner.revision==9,"replay changed tactics");
    identity.revision=9;action.expected_order=40;action.value=200;
    const auto nochange=app::apply_manager_tactical_action(w,owner,identity,action,{});check(nochange.allowed&&!nochange.changed&&owner.revision==9,"clamped no-op advanced revision");
    auto stale=action;stale.player={4000000};check(!app::apply_manager_tactical_action(w,owner,identity,stale,{}).allowed,"stale global player accepted");
    stale=action;stale.expected_order=2;check(!app::apply_manager_tactical_action(w,owner,identity,stale,{}).allowed,"stale expected instruction accepted");
    auto foreign=identity;foreign.club=w.membership[5].clubs[0];rejects([&]{app::apply_manager_tactical_action(w,owner,foreign,action,{});},"wrong manager club accepted");
    foreign=identity;++foreign.scope;rejects([&]{app::apply_manager_tactical_action(w,owner,foreign,action,{});},"old session scope accepted");
    action.value=3;const auto frozen=app::apply_manager_tactical_action(w,owner,identity,action,{3,false,true});check(!frozen.allowed&&w==committed&&owner.revision==9,"frozen tactics changed");
    check(decode_world(encode_world(w))==w,"tactical world roundtrip lost state");CareerSnapshot save;save.world=w;save.results=SeasonResults::empty(w.catalogue);
    check(read_save(make_save(save))==save,"tactical snapshot roundtrip lost state");
    auto raw=encode_world(w);auto json=bmh::cli::parse_json(std::string(raw.begin(),raw.end()));json.object()->at("version")=5;
    for(auto& m:std::get<bmh::cli::Json::Array>(json.object()->at("managers").value))m.object()->erase("tactics");const auto legacy=bmh::cli::dump_json(json);
    const auto old=decode_world(bmh::Bytes(legacy.begin(),legacy.end()));check(!old.managers[0].tactics&&old.managers[0].finance==w.managers[0].finance,"old tactical state guessed or finances lost");
    rejects([&]{calculate_manager_tactical_action(old,3,action,{});},"missing tactics silently initialized");
    auto invalid=initial;invalid.managers[0].tactics->rows[0].partner=24;rejects([&]{encode_world(invalid);},"invalid partner persisted");
    invalid=initial;invalid.managers[0].tactics->rows[0].depth=20;rejects([&]{encode_world(invalid);},"out-of-field starting position persisted");
}
void swap_cases() {
    size_t cases=0;
    for(bool english:{false,true})for(bool indoor:{false,true})for(bool foreign:{false,true})
    for(auto pair:{std::pair{0,1},std::pair{1,11},std::pair{11,19},std::pair{0,0},std::pair{1,23}}) {
        auto w=world(english);const auto club=w.managers[0].club;
        for(auto& [id,p]:w.players)p.cards=0xa5e0;
        auto& roster=w.clubs.at(club).roster;roster[1].flags|=8;roster[11].flags|=4;
        TacticalPolicy policy{3,indoor,false,foreign};auto reference=native(w,policy);const auto original=reference;
        const auto range=bmh::squad_range(72);const auto a=range.first+size_t(pair.first),b=range.first+size_t(pair.second);
        const bool allowed=bmh::swap_pitch_roster_rows_source(reference,3,72,pair.first,pair.second,reference.squad_slots[a],reference.squad_slots[b],foreign);
        auto expected=original;
        const bool expected_allowed=pair.first!=pair.second && pair.second!=23 && (!foreign||((pair.first<11)==(pair.second<11)));
        check(allowed==expected_allowed,"Source roster exchange accepted wrong row/foreign policy");
        if(allowed) {
            expected.squad_slots[a]=uint16_t((original.squad_slots[b]&0x1fff)|(original.squad_slots[a]&0xc000));
            expected.squad_slots[b]=uint16_t((original.squad_slots[a]&0x1fff)|(original.squad_slots[b]&0xc000));
            const size_t board=3*0x840;const auto first=board+size_t(pair.first)*0x58,second=board+size_t(pair.second)*0x58;
            std::swap_ranges(expected.tactics.begin()+first,expected.tactics.begin()+first+0x58,expected.tactics.begin()+second);
            for(auto at:{first,second}){expected.tactics[at+0x41]=original.tactics[at+0x41];expected.tactics[at+0x42]=original.tactics[at+0x42];}
            for(size_t row=0;row<range.count;++row){const auto raw=expected.squad_slots[range.first+row];if((raw&0xc000)&&(raw&0xfff)!=0xfff)expected.player_cards[raw&0xfff]=uint16_t((expected.player_cards[raw&0xfff]&0xffe0)|row);}
            expected.manager_options[15]=0;
        }
        check(reference.squad_slots==expected.squad_slots&&reference.tactics==expected.tactics&&reference.player_cards==expected.player_cards&&reference.manager_options==expected.manager_options,
            "shared kernel changed original role/auxiliary bits, full board entries or card-row mapping");
        TacticalAction action;action.kind=TacticalActionKind::swap;action.row=pair.first;action.second_row=pair.second;
        action.player=roster[size_t(pair.first)].player;action.first_slot=roster[size_t(pair.first)];action.second_slot=roster[size_t(pair.second)];
        const auto before=w;app::ManagerActionOwner owner{71,8};const app::ManagerActionIdentity identity{3,club,71,8};
        const auto result=app::apply_manager_tactical_action(w,owner,identity,action,policy);
        check(result.allowed==allowed&&result.changed==allowed&&owner.revision==uint64_t(allowed?9:8),"typed swap acceptance/publication differs from Core");
        check(w.managers[0].tactics==manager_tactics_from_legacy(reference,3),"typed swap lost instructions, partners, positions or manual selector");
        for(size_t row=0;row<24;++row) {
            const auto raw=reference.squad_slots[range.first+row];const auto& actual=w.clubs.at(club).roster[row];
            if((raw&0xfff)==0xfff){check(actual.empty(),"swap filled an empty row");continue;}
            const auto original_row=size_t(23-(raw&0xfff));const auto global=before.clubs.at(club).roster[original_row].player;
            const auto flags=uint16_t(((raw&0x8000)?1:0)|((raw&0x4000)?2:0)|((raw&0x2000)?4:0)|((raw&0x1000)?8:0));
            check(actual.player==global&&actual.flags==flags&&w.players.at(global).cards==reference.player_cards[raw&0xfff],"global roster/cards mapping differs from native row swap");
        }
        check(w.managers[0].finance==before.managers[0].finance,"swap changed manager finances");
        check(decode_world(encode_world(w))==w,"swapped lineup failed world persistence");
        CareerSnapshot snap;snap.world=w;snap.results=SeasonResults::empty(w.catalogue);check(read_save(make_save(snap))==snap,"swapped lineup lost cards/positions on reload");
        if(allowed){const auto after=w;rejects([&]{app::apply_manager_tactical_action(w,owner,identity,action,policy);},"swap replay accepted");check(w==after,"swap replay changed state");}
        ++cases;
    }
    auto w=world();const auto club=w.managers[0].club;const auto& roster=w.clubs.at(club).roster;
    TacticalAction action;action.kind=TacticalActionKind::swap;action.row=1;action.second_row=11;action.player=roster[1].player;action.first_slot=roster[1];action.second_slot=roster[11];
    auto stale=action;++stale.second_slot.flags;check(!calculate_manager_tactical_action(w,3,stale,{}).allowed,"stale bench flags accepted");
    w.players.at(action.player).attributes.w364|=1u<<16;check(!calculate_manager_tactical_action(w,3,action,{}).allowed,"injured swap bypassed confirmation owner");
    std::cout<<cases<<" typed/native lineup swaps, independent byte-level kernel expectations, stale rows and snapshot/replay cases passed\n";
}
void complete_tactical_cases() {
    size_t cases=0;
    for(bool english:{false,true})for(bool indoor:{false,true})for(int difficulty:{0,6,9}) {
        auto w=world(english);const auto club=w.managers[0].club;
        for(size_t row=0;row<20;++row)w.players.at(w.clubs.at(club).roster[row].player).attributes.w364=uint32_t(row==0||row==11?0:row<5?1:row<9?2:3);
        TacticalPolicy policy{3,indoor,false,true,difficulty};auto source=native(w,policy);
        std::vector<PlayerId> mapping(24);
        for(size_t row=0;row<20;++row)mapping[23-row]=w.clubs.at(club).roster[row].player;
        const auto external=w.clubs.at(w.membership[10].clubs[0]).roster[0].player;
        mapping.push_back(external);source.players.push_back(w.players.at(external).attributes);source.player_cards.push_back(w.players.at(external).cards);source.player_bonus.push_back(w.players.at(external).bonus);
        for(size_t row=0;row<24;++row)for(size_t off:{0x44u,0x46u}){source.tactics[3*0x840+row*0x58+off]=0xff;source.tactics[3*0x840+row*0x58+off+1]=0x0f;}
        source.tactics[3*0x840+0x44]=24;source.tactics[3*0x840+0x45]=0;
        const uint8_t depth[]={0,5,5,5,5,10,10,10,10,17,17},lane[]={4,1,3,5,7,1,3,5,7,3,5};
        for(size_t manager=0;manager<4;++manager)for(size_t mode=0;mode<2;++mode)for(size_t f=0;f<8;++f)for(size_t row=0;row<11;++row) {
            const auto at=manager*0x160+mode*0xb0+f*22+row;source.formations[at]=depth[row];source.formations[at+11]=lane[row];
        }
        source.manager_records[3*0xb0c+0x218]=source.manager_records[3*0xb0c+0x219]=15;
        w.managers[0].tactics=manager_full_tactics_from_legacy(source,3,mapping);w.validate();
        check(w.managers[0].tactics->backing->marks[0]==external,"full board extraction truncated external global player reference");
        check(decode_world(encode_world(w))==w,"full tactical payload/reference/formation roundtrip failed");
        for(int action_kind=0;action_kind<4;++action_kind)for(int formation:{0,3,4,7}) {
            if(action_kind==3&&formation!=0)continue;
            auto reference=source;TacticalAction action;action.old_weights=w.managers[0].tactics->automatic_weights;action.formation=formation;
            bool allowed=false;
            if(action_kind==0){action.kind=TacticalActionKind::apply_formation;allowed=bmh::apply_pitch_formation_source(reference,3,72,formation,action.old_weights);}
            else if(action_kind==1){action.kind=TacticalActionKind::save_formation;
                for(size_t row=0;row<11;++row){action.formation_positions[row]=w.managers[0].tactics->rows[row].depth;action.formation_positions[11+row]=w.managers[0].tactics->rows[row].lane;}
                allowed=bmh::save_pitch_formation_source(reference,3,72,formation,action.old_weights,action.formation_positions);}
            else if(action_kind==2){action.kind=TacticalActionKind::prepare;bmh::auto_lineup(reference,72,0xe4);bmh::prepare_lineup_entry(reference,72);allowed=true;}
            else {action.kind=TacticalActionKind::swap;action.row=0;action.second_row=2;const auto& roster=w.clubs.at(club).roster;
                action.player=roster[0].player;action.first_slot=roster[0];action.second_slot=roster[2];const auto start=bmh::squad_range(72).first;
                allowed=bmh::swap_pitch_roster_rows_source(reference,3,72,0,2,reference.squad_slots[start],reference.squad_slots[start+2],true);}
            const auto before=w;auto actual=w;app::ManagerActionOwner owner{71,8};
            const auto outcome=app::apply_manager_tactical_action(actual,owner,{3,club,71,8},action,policy);
            check(outcome.allowed==allowed,"full formation action acceptance differs from direct Original helper");
            const auto expected=manager_full_tactics_from_legacy(reference,3,mapping);
            check(actual.managers[0].tactics==expected,"full tactical record/formation changes differ after local remapping");
            const auto range=bmh::squad_range(72);
            for(size_t row=0;row<24;++row){const auto raw=reference.squad_slots[range.first+row];const auto& got=actual.clubs.at(club).roster[row];
                if((raw&0xfff)==0xfff){check(got.empty(),"formation filled empty row");continue;}
                const auto id=mapping.at(raw&0xfff);check(got.player==id&&actual.players.at(id).cards==reference.player_cards[raw&0xfff],"formation global lineup/cards changed identity");}
            check(actual.managers[0].finance==before.managers[0].finance,"formation changed financial state");
            CareerSnapshot snap;snap.world=actual;snap.results=SeasonResults::empty(actual.catalogue);check(read_save(make_save(snap))==snap,"full formation snapshot lost state");
            ++cases;
        }
        auto invalid=w;invalid.managers[0].tactics->backing->scalar_rows[0][0x44]=1;rejects([&]{invalid.validate();},"packed player reference smuggled into numeric payload");
        invalid=w;invalid.managers[0].tactics->backing->marks[0]={4000000};rejects([&]{invalid.validate();},"dangling full tactical reference accepted");
    }
    auto missing=world();TacticalAction action;action.kind=TacticalActionKind::apply_formation;action.formation=0;
    rejects([&]{calculate_manager_tactical_action(missing,3,action,{});},"incomplete tactical record guessed a formation");
    std::cout<<cases<<" complete-board formation/save/entry cases, cross-club wide references and numeric-payload guards passed\n";
}
void original_save(const std::filesystem::path& data,const std::filesystem::path& save) {
    const auto exe=bmh::load_le(data/"BMH.exe");const bmh::DbCounts counts{exe.u32(bmh::layout::CareerSession_DataDirectory_RuntimeField),exe.u32(bmh::layout::CareerSession_DataDirectory_RuntimeField3),exe.u32(bmh::layout::CareerSession_DataDirectory_RuntimeField2),exe.u32(bmh::layout::CareerSession_DataDirectory_RuntimeField4)};
    const auto file=bmh::load_savegame(save);check(file.checksum_ok&&file.version=="V2.24","invalid reference save");
    const auto source=bmh::GameState::from_save(bmh::SaveState(file.stream,counts),counts);const auto extracted=manager_tactics_from_legacy(source,0);
    auto w=world();w.managers[0].tactics=extracted;w.validate();check(decode_world(encode_world(w))==w,"extracted Original tactical scalar fields lost on roundtrip");
    std::vector<PlayerId> mapping;for(const auto& [id,p]:w.players)mapping.push_back(id);
    check(mapping.size()>=source.players.size(),"synthetic global mapping too small for Original reference");mapping.resize(source.players.size());
    w.managers[0].tactics=manager_full_tactics_from_legacy(source,0,mapping);w.validate();
    check(decode_world(encode_world(w))==w,"full Original tactical data/reference extraction failed persistence");
    w.managers[0].tactics=extracted;
    const auto before=source;
    for(int aspect=0;aspect<4;++aspect)for(int value:{-100,17,200}) {
        auto expected=source;const int team=expected.manager_teams[0];const auto range=bmh::squad_range(team);const auto player=expected.squad_slots[range.first]&0xfff;
        const bool allowed=bmh::set_player_order_source(expected,0,team,0,player,aspect,value);
        TacticalAction action;action.row=0;action.player=w.clubs.at(w.managers[0].club).roster[0].player;action.aspect=aspect;action.value=value;action.expected_order=extracted.rows[0].orders[size_t(aspect)];
        const auto actual=calculate_manager_tactical_action(w,3,action,{});
        check(actual.allowed==allowed&&actual.tactics==manager_tactics_from_legacy(expected,0),"original-save order clamp/extraction differs");
    }
    std::cout<<"Original-save scalar/full-board extraction and 12 direct order cases passed; no career conversion or reference writes\n";
}
}
int main(int argc,char** argv)try {
    core_cases();ownership_codec();swap_cases();complete_tactical_cases();if(argc==3)original_save(argv[1],argv[2]);else if(argc!=1)throw std::runtime_error("usage: world12_manager_tactics_tests [reference-directory original.FBM]");
    std::cout<<"Tactical persistence, ownership, wide player IDs, no-op/frozen handling and snapshot compatibility passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
