// SPDX-License-Identifier: GPL-3.0-only
#include "ui/world12_other_games.hpp"
#include "ui/manager.hpp"
#include <algorithm>
#include <stdexcept>

namespace bmh::ui {
namespace {
using namespace bmh::world12;
constexpr const char* labels[]={"DFB-POKAL","UEFA-CUP","CHAMP. LEAGUE","POKALSIEGER","WM/EM-QUALI.","SUPER-CUP","FA CUP","KOKA KOLA CUP"};
bool enabled(const World& world,int page) {
    switch(World12OtherPage(page)) {
    case World12OtherPage::german_cup:return world.catalogue.scope.germany;
    case World12OtherPage::fa_cup:case World12OtherPage::league_cup:return world.catalogue.scope.england;
    case World12OtherPage::national_qualifiers:return world.catalogue.scope.international;
    default:return page>=0&&page<8&&world.catalogue.scope.club_play();
    }
}
void validate_owner(const World& world,const World12OtherGamesIdentity& owner) {
    if(!owner.scope || owner.manager_slot>3 || !owner.club.valid())throw FormatError("other games: invalid typed owner");
    const auto manager=std::find_if(world.managers.begin(),world.managers.end(),[&](const auto& m){return m.seat==owner.manager_slot;});
    if(manager==world.managers.end() || manager->club!=owner.club)throw FormatError("other games: stale typed manager owner");
}
std::vector<const KnockoutStage*> stages(const CareerSnapshot& snapshot,DomesticCup cup) {
    std::vector<const KnockoutStage*> out;
    for(const auto& stage:snapshot.cups)if(stage.cup==cup)out.push_back(&stage);
    return out;
}
void add_fixture(OtherGamesState& state,const World& world,const Fixture& fixture,
    std::optional<uint16_t> score,std::optional<uint16_t> other,int source_match,int baseline=-1) {
    OtherGamesRow row;row.kind=OtherGamesRowKind::match;
    const int token=int(state.projection.rows.size())*2;
    row.home_team=token;row.away_team=token+1;row.home_name=world.clubs.at(fixture.home).name;row.away_name=world.clubs.at(fixture.away).name;
    state.world12_teams[token]=fixture.home;state.world12_teams[token+1]=fixture.away;
    row.source_match=source_match;row.played=score.has_value();row.source_result=score.value_or(0xcccc);
    row.home_goals=score?int(*score>>5&31):0;row.away_goals=score?int(*score&31):0;
    row.source_second_leg=other.value_or(0xcccc);row.second_leg_visible=other.has_value();row.second_leg_array_present=other.has_value();
    const auto metadata=[&](ClubId id){
        const auto& club=world.clubs.at(id);OtherGamesClubMetadata result;
        if(!club.league.valid()){result.foreign_strength=club.rating;return result;}
        GameState view;view.squad_slots.assign(24,0xfff);view.team_morale={club.morale};
        for(const auto& slot:club.roster)if(!slot.empty()){
            const auto index=view.players.size();view.players.push_back(world.players.at(slot.player).attributes);view.squad_slots[index]=uint16_t(index);
        }
        result.has_squad_values=true;result.averages=other_games_club_averages(view,0);return result;
    };
    row.home_metadata=metadata(fixture.home);row.away_metadata=metadata(fixture.away);
    OtherGamesDisplayRow display;display.name_colors={0x5f,0x5f};display.countries={0xff,0xff};
    const auto result=[](uint16_t value){return std::to_string(value>>5&31)+":"+std::to_string(value&31);};
    display.primary_result=score?result(*score):"-";if(other)display.second_leg_result=result(*other);
    for(size_t side=0;side<2;++side) {
        const auto club_id=side?fixture.away:fixture.home;const auto& club=world.clubs.at(club_id);
        display.league_tiers[side]=club.league.valid()?world.catalogue.league(club.league).local_index:255;
        for(const auto& manager:world.managers)if(manager.club==club_id)display.name_colors[side]=manager.seat==state.world12_identity->manager_slot?0x7a:0x75;
    }
    state.projection.rows.push_back(std::move(row));state.rows.push_back(std::move(display));
    if(baseline>=0)state.fixture_baselines.push_back(baseline);
}
}
World12OtherSelection default_world12_other_selection(const bmh::world12::World& world) {
    if(world.catalogue.scope.germany)return {};
    World12OtherSelection selection;selection.page=world.catalogue.scope.england?World12OtherPage::fa_cup:World12OtherPage::national_qualifiers;return selection;
}
OtherGamesState world12_other_games_state(const bmh::world12::CareerSnapshot& snapshot,
    const World12OtherGamesIdentity& owner,World12OtherSelection selection) {
    const auto& world=snapshot.world;world.validate();validate_cup_programmes(world,snapshot.cups);validate_owner(world,owner);
    const int page=int(selection.page);
    if(page<0||page>=8||!enabled(world,page))throw FormatError("other games: disabled typed competition page");
    OtherGamesState state;state.world12_identity=owner;auto& p=state.projection;
    p.competition=page;p.descriptor_competition=page;p.entry_index=0;p.round=0;p.rounds=1;p.knockout=true;
    p.display_mode=selection.mode;p.title=labels[page];p.round_heading="1. RUNDE";
    for(int slot=0;slot<8;++slot) {
        const bool active=enabled(world,slot);
        p.selectors.push_back({slot,active?slot:-1,active?labels[slot]:"",active});
        state.selector_labels[size_t(slot)]=active?labels[slot]:"";
    }
    if(selection.page==World12OtherPage::supercups) {
        p.display_mode=OtherGamesDisplayMode::detail;p.title="SUPER-CUP";p.round_heading="FINALSPIELE";
        int block=0;const int blocks=int(world.catalogue.scope.germany)+int(world.catalogue.scope.england);
        if(blocks==1){p.round_heading="FINALE";if(world.catalogue.scope.england)p.title="CHARITY SHIELD";}
        for(const auto cup:{DomesticCup::german_supercup,DomesticCup::english_supercup}) {
            if(!world.catalogue.scope.clubs_enabled(cup_country(cup)))continue;
            const int baseline=blocks==1?104:70+60*block++;
            OtherGamesState::Section section;
            section.title=cup==DomesticCup::german_supercup?"Deutschland - Super-Cup":"England - Charity Shield";
            section.first_row=p.rows.size();section.heading_baseline=baseline-16;section.fixture_baseline=baseline;
            const auto available=stages(snapshot,cup);
            if(available.empty())section.message="Noch keine Paarung";
            else {
                const auto& round=available.back()->rounds.front();
                if(round.fixtures.size()!=1)throw FormatError("other games: a supercup must contain one independent final");
                add_fixture(state,world,round.fixtures[0],round.first_leg.empty()?std::nullopt:std::optional(round.first_leg[0]),{},0,baseline);section.row_count=1;
            }
            state.sections.push_back(std::move(section));
        }
        p.source_participant_count=int(p.rows.size())*2;return state;
    }
    std::optional<DomesticCup> cup;
    if(selection.page==World12OtherPage::german_cup)cup=DomesticCup::german_cup;
    if(selection.page==World12OtherPage::fa_cup)cup=DomesticCup::fa_cup;
    if(selection.page==World12OtherPage::league_cup)cup=DomesticCup::league_cup;
    const auto available=cup?stages(snapshot,*cup):std::vector<const KnockoutStage*>{};
    if(available.empty()) {p.fixtures_available=false;state.unavailable_message="Spielplan noch nicht initialisiert";return state;}
    const int stage_index=selection.stage<0?int(available.size())-1:selection.stage;
    if(stage_index<0||stage_index>=int(available.size()))throw FormatError("other games: unknown typed cup stage");
    const auto& stage=*available[size_t(stage_index)];
    p.entry_index=stage.source_entry;p.linked_page=stage_index;p.rounds=stage.planned_rounds;p.round=selection.round<0?int(stage.rounds.size())-1:selection.round;
    if(p.round<0||p.round>=p.rounds)throw FormatError("other games: invalid typed round");
    p.flags=stage.flags;p.current_round=int(stage.rounds.size())-1;p.round_arrows_visible=p.rounds>1;p.two_legs=stage.two_legs();
    p.round_heading=std::to_string(p.round+1)+". RUNDE";
    if(available.size()>1)for(size_t i=0;i<available.size();++i)p.linked_pages.push_back({int(i),available[i]->source_entry,page,0,1});
    p.source_participant_count=int(stage.rounds.front().fixtures.size()*2)>>p.round;
    p.display_mode_toggle_available=p.source_participant_count>16;
    if(selection.participant_offset<0||selection.participant_offset%16 || selection.participant_offset>=std::max(1,p.source_participant_count))throw FormatError("other games: invalid typed participant window");
    state.source_participant_offset=selection.participant_offset;p.participant_offset=selection.mode==OtherGamesDisplayMode::detail?selection.participant_offset:0;
    if(p.source_participant_count>16)state.selector_labels[size_t(page)]+=" "+std::to_string(selection.participant_offset/16+1);
    if(p.round>=int(stage.rounds.size())) {p.fixtures_available=false;state.unavailable_message="Auslosung noch nicht erfolgt";return state;}
    const auto& round=stage.rounds[size_t(p.round)];
    const size_t first=selection.mode==OtherGamesDisplayMode::detail?size_t(selection.participant_offset/2):0;
    const size_t last=std::min(round.fixtures.size(),selection.mode==OtherGamesDisplayMode::detail?first+8:round.fixtures.size());
    for(size_t m=first;m<last;++m)add_fixture(state,world,round.fixtures[m],round.first_leg.empty()?std::nullopt:std::optional(round.first_leg[m]),
        round.second_leg.empty()?std::nullopt:std::optional(round.second_leg[m]),int(m));
    return state;
}
World12OtherSelection navigate_world12_other_games(const bmh::world12::CareerSnapshot& snapshot,const World12OtherGamesIdentity& owner,
    World12OtherSelection selection,const OtherGamesRequest& request) {
    if(!request.world12_identity || *request.world12_identity!=owner || request.identity!=OtherGamesIdentity{} || request.world12_team.valid())
        throw FormatError("other games: stale/mixed typed navigation request");
    const auto current=world12_other_games_state(snapshot,owner,selection);
    if(request.action!=OtherGamesAction::select_competition&&request.action!=OtherGamesAction::select_linked_page&&request.value!=-1)
        throw FormatError("other games: unexpected typed navigation value");
    switch(request.action) {
    case OtherGamesAction::select_competition:
        if(request.value<0||request.value>=8||!enabled(snapshot.world,request.value))throw FormatError("other games: disabled selector");
        selection={};selection.page=World12OtherPage(request.value);break;
    case OtherGamesAction::previous_round:case OtherGamesAction::next_round:
        if(!current.projection.round_arrows_visible)throw FormatError("other games: round arrows are disabled");
        selection.round=current.projection.round;
        if(selection.mode==OtherGamesDisplayMode::detail&&request.action==OtherGamesAction::next_round&&selection.participant_offset+16<current.projection.source_participant_count)selection.participant_offset+=16;
        else if(selection.mode==OtherGamesDisplayMode::detail&&request.action==OtherGamesAction::previous_round&&selection.participant_offset>=16)selection.participant_offset-=16;
        else {selection.round=std::clamp(selection.round+(request.action==OtherGamesAction::next_round?1:-1),0,current.projection.rounds-1);selection.participant_offset=0;}
        break;
    case OtherGamesAction::select_linked_page:selection.stage=request.value;selection.round=-1;selection.participant_offset=0;break;
    case OtherGamesAction::toggle_display_mode:
        if(!current.projection.display_mode_toggle_available)throw FormatError("other games: display mode control is disabled");
        selection.mode=selection.mode==OtherGamesDisplayMode::detail?OtherGamesDisplayMode::compact:OtherGamesDisplayMode::detail;break;
    default:throw FormatError("other games: action belongs to the typed window owner");
    }
    world12_other_games_state(snapshot,owner,selection);return selection;
}
}
