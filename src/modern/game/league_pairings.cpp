// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "modern/game/modern_state.hpp"
#include "modern/game/league_pairings.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <stdexcept>

#include "modern/game/matchday.hpp"
#include "modern/game/matchday_setup.hpp"
#include "game/match_engine.hpp"
#include "game/schedule.hpp"
#include "modern/game/source_tip.hpp"
#include "game/table.hpp"

namespace bmh::modern {
namespace {
void require(bool value,const char* reason) {
    if(!value) throw std::invalid_argument(reason);
}
void clear_computer_tip(LeaguePairingsOwner& owner) {
    owner.computer_tip_active=false;owner.computer_tip_league=owner.computer_tip_round=-1;
    owner.computer_tip_original.fill(0);owner.computer_tip_results.clear();
}
void prepare_computer_tip(ModernState& g,LeaguePairingsOwner& owner,
    const LeaguePairingsConstants& c,GameRandom& game) {
    const uint32_t preferences_at=::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField+uint32_t(owner.manager);
    require(!owner.filter_active&&!owner.postponed_active&&
        (g.memory.u8(preferences_at)&0x0f)<=3,
        "pairings: Computer Tip supports only ordinary/search modes 0..3");
    require(owner.round>=0&&size_t(owner.league)<g.league_rounds.size()&&
        int(g.league_rounds[size_t(owner.league)])<=owner.round,
        "pairings: Computer Tip requires a current/future Source round");
    require(g.memory.has(::bmh::layout::CurrentMatchday)&&g.results.size()>=kResultsSize,
        "pairings: Computer Tip Native seed/results unavailable");
    const auto page=modern::league_pairings_view_source(g,owner,c);
    const size_t expected=league_fixtures(owner.league,owner.round+1).size();
    require(page.rows.size()==expected&&page.total_rows==int(expected),
        "pairings: Computer Tip requires the complete ordinary fixture row set");
    for(const auto& row:page.rows)
        require(row.raw_result==0xcc,"pairings: Computer Tip requires every visible result to be 0xcc");
    const int32_t source_seed=std::bit_cast<int32_t>(g.memory.u32(::bmh::layout::CurrentMatchday));
    require(source_seed>=0&&source_seed<=10000,
        "pairings: Computer Tip rejects an unqualified signed Native seed");

    const size_t first=result_index(owner.league,owner.round+1,0);
    require(first+10<=g.results.size(),"pairings: Computer Tip ten-byte Source backup is unavailable");
    std::array<uint8_t,10> original{};
    if(owner.computer_tip_active) {
        require(owner.computer_tip_league==owner.league&&owner.computer_tip_round==owner.round&&
            owner.computer_tip_results.size()==expected,
            "pairings: stale Computer Tip forecast identity");
        original=owner.computer_tip_original;
        for(size_t i=0;i<10;++i)
            require(g.results[first+i]==original[i],"pairings: Native result slice changed under Computer Tip");
    } else {
        std::copy_n(g.results.begin()+static_cast<std::ptrdiff_t>(first),10,original.begin());
    }

    struct Input { const LeaguePairingsRow* row; int home_rating,away_rating,home_secondary,away_secondary; };
    std::vector<Input> inputs;inputs.reserve(expected);
    for(const auto& row:page.rows) {
        require(row.source_round==owner.round&&row.fixture_index>=0&&
            size_t(row.fixture_index)<expected&&original[size_t(row.fixture_index)]==0xcc,
            "pairings: Computer Tip fixture is outside the original 10-byte unplayed slice");
        inputs.push_back({&row,
            team_average(g,row.home.runtime_team,0x3f3).value,
            team_average(g,row.away.runtime_team,0x3f3).value,
            team_average(g,row.home.runtime_team,0x3fc).value,
            team_average(g,row.away.runtime_team,0x3fc).value});
    }
    // Work on a copy so any arithmetic/validation failure leaves the staged G
    // object untouched. set_seed preserves its cumulative Source call count.
    GameRandom staged_game=game;staged_game.set_seed(source_seed);
    std::vector<LeaguePairingsTipResult> results;results.reserve(expected);
    for(const auto& input:inputs) {
        const auto& row=*input.row;
        results.push_back({owner.league,row.source_round,row.fixture_index,
            row.home.runtime_team,row.home.club_identity,row.away.runtime_team,row.away.club_identity,
            row.raw_result,modern::source_tip_result(staged_game,c.computer_tip_gap,input.home_rating,input.away_rating,
                input.home_secondary,input.away_secondary)});
    }
    game=staged_game;owner.computer_tip_active=true;
    owner.computer_tip_league=owner.league;owner.computer_tip_round=owner.round;
    owner.computer_tip_original=original;owner.computer_tip_results=std::move(results);
    g.memory.set8(preferences_at,uint8_t(g.memory.u8(preferences_at)|0x10));
}
int filtered_fixture_count(int league,int team) {
    int count=0;
    for(int round=0;round<league_matchdays(league);++round)
        for(const auto& fixture:league_fixtures(league,round+1))
            if(fixture.home==team||fixture.away==team)++count;
    return count;
}
int postponed_fixture_count(const ModernState& g,int league) {
    int count=0;
    for(int round=0;round<league_matchdays(league);++round) {
        const auto fixtures=league_fixtures(league,round+1);
        for(size_t slot=0;slot<fixtures.size();++slot)
            if(modern::league_pairings_postponed_source(g.results.at(result_index(league,round+1,int(slot)))))++count;
    }
    require(count<=255,"pairings: Mode8 Source byte-counter overflow is outside the supported boundary");
    return count;
}
void validate(const ModernState& g,const LeaguePairingsOwner& o) {
    const bool national=o.runtime_club==no_team();
    require(o.supported&&!o.closed&&o.manager>=0&&o.manager<4&&
        size_t(o.manager)<g.manager_teams.size()&&g.memory.has(::bmh::layout::ActiveManager)&&
        g.memory.u8(::bmh::layout::ActiveManager)==o.manager&&g.manager_teams[size_t(o.manager)]==o.runtime_club&&
        (national||(o.runtime_club>=0&&o.runtime_club<league_clubs()))&&
        size_t(o.runtime_club)<g.club_ids.size()&&
        g.club_ids[size_t(o.runtime_club)]==o.club_identity,
        "pairings: stale or unsupported manager identity");
    require(o.league>=0&&o.league<9&&o.round>=0&&o.round<league_matchdays(o.league)&&
        o.league_group<=1&&size_t(o.league)<g.league_rounds.size()&&
        g.league_rounds[size_t(o.league)]<=league_matchdays(o.league),
        "pairings: unsupported league/round");
    require(o.active_group>=-1&&(!o.active_phase||o.active_group>=0),
        "pairings: invalid active presentation phase");
    require(g.memory.has(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField+uint32_t(o.manager))&&
        (((g.memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField+uint32_t(o.manager))&0x0c)==0)||
         (g.memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField+uint32_t(o.manager))&0x0f)==4||
         (g.memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField+uint32_t(o.manager))&0x0f)==8)&&
        g.memory.has(::bmh::layout::LeagueTableDisplayModes+uint32_t(o.manager)*2+1),
        "pairings: missing preferences or unsupported saved list mode");
    require(g.results.size()>=kResultsSize&&g.club_ids.size()>=size_t(league_clubs())&&g.league_rounds.size()>=9&&
        g.memory.has(::bmh::layout::Postponed+0xef)&&g.memory.has(::bmh::layout::CompetitionCalendar+365*2+1),
        "pairings: incomplete Native schedule");
    const auto selection_valid=[&](int team,int identity) {
        return team<0 ? team>=-2&&identity==-1 :
            team<league_clubs()&&team_league(team)==o.league&&g.club_ids.at(size_t(team))==identity;
    };
    require(selection_valid(o.search_first,o.search_first_identity)&&
        selection_valid(o.search_second,o.search_second_identity)&&
        (o.search_first!=-1 || (o.search_second==-1&&!o.search_second_click))&&
        (o.search_first!=-2 || (o.search_second==-2&&!o.search_second_click))&&
        (o.search_first==-1 || (g.memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField+uint32_t(o.manager))&0x0f)==2)&&
        (!o.search_second_click || (o.search_first>=0&&o.search_second>=-1)),
        "pairings: stale or invalid search selection");
    const uint8_t mode=uint8_t(g.memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField+uint32_t(o.manager))&0x0f);
    const bool mode8=mode==8;
    if(mode8) {
        require(o.postponed_active&&!o.filter_active&&o.filter_team==-1&&o.filter_identity==-1&&o.filter_offset==0&&
            o.search_first==-1&&o.search_second==-1&&o.search_first_identity==-1&&o.search_second_identity==-1&&
            !o.search_second_click&&o.postponed_offset>=0&&o.postponed_offset%10==0,
            "pairings: invalid postponed-list owner");
        const int count=postponed_fixture_count(g,o.league);
        require((count==0&&o.postponed_offset==0)||(count>0&&o.postponed_offset<count),
            "pairings: stale or unreachable postponed-list window");
    } else require(!o.postponed_active&&o.postponed_offset==0,
            "pairings: stale postponed-list owner");
    if(o.computer_tip_active) {
        const size_t expected=league_fixtures(o.league,o.round+1).size();
        require(mode<=3&&!o.filter_active&&!o.postponed_active&&
            o.computer_tip_league==o.league&&o.computer_tip_round==o.round&&
            o.round>=int(g.league_rounds.at(size_t(o.league)))&&
            o.computer_tip_results.size()==expected&&g.memory.has(::bmh::layout::CurrentMatchday),
            "pairings: invalid Computer Tip owner scope");
        const size_t first=result_index(o.league,o.round+1,0);
        require(first+10<=g.results.size(),"pairings: Computer Tip result backup is unavailable");
        for(size_t n=0;n<10;++n)
            require(g.results[first+n]==o.computer_tip_original[n],
                "pairings: Computer Tip original result slice changed");
        const auto fixtures=league_fixtures(o.league,o.round+1);
        require(fixtures.size()==expected,"pairings: Computer Tip fixture count changed");
        for(size_t n=0;n<expected;++n) {
            const auto& tip=o.computer_tip_results[n];const auto& fixture=fixtures[n];
            require(tip.league==o.league&&tip.source_round==o.round&&tip.fixture_index==int(n)&&
                tip.home_runtime_team==fixture.home&&tip.away_runtime_team==fixture.away&&
                tip.home_club_identity==g.club_ids.at(size_t(fixture.home))&&
                tip.away_club_identity==g.club_ids.at(size_t(fixture.away))&&
                tip.original_result==0xcc&&o.computer_tip_original[n]==0xcc,
                "pairings: stale Computer Tip fixture identity");
        }
    } else require(o.computer_tip_league==-1&&o.computer_tip_round==-1&&
        o.computer_tip_results.empty(),"pairings: stale inactive Computer Tip data");
    if(o.filter_active) {
        require(mode==4&&o.search_first==-1&&o.search_second==-1&&
            o.search_first_identity==-1&&o.search_second_identity==-1&&!o.search_second_click&&
            o.filter_offset>=0&&o.filter_offset%10==0&&o.filter_team>=-2,
            "pairings: invalid Source club-filter mode");
        if(o.filter_team>=0) {
            require(o.filter_team<league_clubs()&&team_league(o.filter_team)==o.league&&
                g.club_ids.at(size_t(o.filter_team))==o.filter_identity&&
                (o.filter_offset==0||o.filter_offset<filtered_fixture_count(o.league,o.filter_team)),
                "pairings: stale or out-of-range filtered club");
        } else {
            require(o.filter_team==-2&&o.filter_identity==-1&&o.filter_offset==0,
                "pairings: invalid unselected club filter");
        }
    } else {
        require(o.filter_team==-1&&o.filter_identity==-1&&o.filter_offset==0&&mode!=4&&(!mode8||o.postponed_active),
            "pairings: stale club-filter selection");
    }
}
void page_preferences(ModernState& g,const LeaguePairingsOwner& o,const LeaguePairingsConstants& c) {
    const uint32_t word=::bmh::layout::LeagueTableDisplayModes+uint32_t(o.manager)*2;
    uint16_t flags=uint16_t((g.memory.u16(word)&0xfc0f)|0x170);
    if(c.points_mode.at(size_t(o.league))) flags|=0x80;
    g.memory.set16(word,flags);
    // Move clears the temporary Computer-Tip selection after every redraw.
    const uint32_t byte=::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField+uint32_t(o.manager);
    g.memory.set8(byte,uint8_t(g.memory.u8(byte)&0xef));
}
int current_round(const ModernState& g,int league) {
    return std::min<int>(g.league_rounds.at(size_t(league)),league_matchdays(league)-1);
}
LeaguePairingsTeam team_view(const ModernState& g,int team,int played) {
    LeaguePairingsTeam out; out.runtime_team=team;out.club_identity=g.club_ids.at(size_t(team));
    if(played) {
        const int r=played-1;
        const size_t at=(size_t(team)*13+size_t(r/3))*2;
        require(at+1<g.position_history.size(),"pairings: missing Source rank history");
        const unsigned word=g.position_history[at]|unsigned(g.position_history[at+1])<<8;
        out.previous_rank=int((word>>((r%3)*5))&31)+1;
    }
    constexpr std::array<uint16_t,5> flags{0x1f1,0x1f2,0x1f4,0x1f8,0x1ff};
    for(size_t n=0;n<flags.size();++n)out.strength[n]=team_average(g,team,flags[n]).value;
    return out;
}
uint32_t round_date(const ModernState& g,int league,int round,const LeaguePairingsConstants& c) {
    require(c.domestic_league_count>0&&c.domestic_league_count<=9,
        "pairings: unsupported Source calendar constants");
    require(c.calendar_class.at(size_t(league))<32,"pairings: invalid calendar mask shift");
    const uint32_t mask=league>=c.domestic_league_count?0x10u:1u<<c.calendar_class.at(size_t(league));
    for(int day=0;day<366;++day) {
        if(modern::day_competitions(g,g.memory.u16(::bmh::layout::CompetitionCalendar+uint32_t(day)*2))&mask) {
            if(round--==0)return (g.season_start+uint32_t(day))&0xffff;
        }
    }
    // LeaguePairingsPlayedSource returns FFFF; 333f3 adds CalendarState_FormatError_RuntimeField3 and truncates to16 bits.
    return (g.season_start+0xffffu)&0xffff;
}
}

bool league_pairings_played_source(uint8_t raw) {return (raw&0xcc)!=0xcc;}
bool league_pairings_postponed_source(uint8_t raw) {return (raw&0xcf)>0xcc;}

std::optional<LeaguePairingsActivePacket> league_pairings_active_packet_source(
    const ModernState& g,const LeaguePairingsOwner& owner,const std::vector<MatchGroup>& groups,
    std::span<const uint8_t> referee_profiles) {
    validate(g,owner);
    // Mode4's armed -2 sentinel still uses the ordinary selected-round list.
    // A selected filter and Mode8 explicitly clear local74 in Move.
    if(owner.postponed_active||(owner.filter_active&&owner.filter_team>=0))return std::nullopt;
    const auto fixtures=league_fixtures(owner.league,owner.round+1);
    int found=-1;
    for(size_t n=0;n<groups.size();++n) {
        const auto& group=groups[n];
        if(group.fixtures.size()>=2&&group.fixtures[0]==fixtures.front().home&&
            group.fixtures[1]==fixtures.front().away)found=int(n);
    }
    if(found<0)return std::nullopt;
    const auto& group=groups[size_t(found)];
    require(group.fixtures.size()==fixtures.size()*2,
        "pairings: incomplete active fixture vector");
    LeaguePairingsActivePacket packet;
    packet.league=owner.league;packet.round=owner.round;packet.group_index=found;
    packet.rows.reserve(fixtures.size());
    for(size_t n=0;n<fixtures.size();++n) {
        const auto& fixture=fixtures[n];
        require(group.fixtures[n*2]==fixture.home&&group.fixtures[n*2+1]==fixture.away,
            "pairings: active fixture order does not match the selected round");
        require(group.desc.size()>0x50+n,
            "pairings: incomplete active referee/weather descriptor");
        const uint8_t referee=group.desc[0x10+n];
        require(size_t(referee)<referee_profiles.size()&&size_t(referee)*2+1<g.master_database_metadata.size(),
            "pairings: missing active referee identity/profile");
        const size_t name=size_t(g.master_database_metadata[size_t(referee)*2])|
            size_t(g.master_database_metadata[size_t(referee)*2+1])<<8;
        require(name<g.referee_names.size(),"pairings: active referee name offset is outside its Native pool");
        const auto first=g.referee_names.begin()+name;
        const auto end=std::find(first,g.referee_names.end(),uint8_t(0));
        require(end!=g.referee_names.end(),"pairings: active referee name has no terminator");
        LeaguePairingsActiveRow row;
        row.source_round=owner.round;row.fixture_index=int(n);
        row.home_runtime_team=fixture.home;row.home_club_identity=g.club_ids.at(size_t(fixture.home));
        row.away_runtime_team=fixture.away;row.away_club_identity=g.club_ids.at(size_t(fixture.away));
        row.referee_index=referee;row.referee_profile=referee_profiles[size_t(referee)];
        row.weather=group.desc[0x50+n];row.temperature=int8_t(group.desc[0x30+n]);
        row.referee_name.assign(first,end);packet.rows.push_back(std::move(row));
    }
    return packet;
}

void validate_league_pairings_active_packet_source(const ModernState& g,const LeaguePairingsOwner& owner,
    const LeaguePairingsActivePacket& packet) {
    validate(g,owner);
    require(!owner.postponed_active&&!(owner.filter_active&&owner.filter_team>=0)&&
        packet.league==owner.league&&packet.round==owner.round&&packet.group_index>=0&&
        packet.group_index==owner.active_group,"pairings: stale active descriptor owner");
    const auto fixtures=league_fixtures(owner.league,owner.round+1);
    require(packet.rows.size()==fixtures.size(),"pairings: incomplete active presentation rows");
    for(size_t n=0;n<fixtures.size();++n) {
        const auto& row=packet.rows[n];const auto& fixture=fixtures[n];
        require(row.source_round==owner.round&&row.fixture_index==int(n)&&
            row.home_runtime_team==fixture.home&&row.away_runtime_team==fixture.away&&
            row.home_club_identity==g.club_ids.at(size_t(fixture.home))&&
            row.away_club_identity==g.club_ids.at(size_t(fixture.away)),
            "pairings: stale active row fixture identity");
        require(size_t(row.referee_index)*2+1<g.master_database_metadata.size(),
            "pairings: missing active referee name identity");
        const size_t at=size_t(g.master_database_metadata[size_t(row.referee_index)*2])|
            size_t(g.master_database_metadata[size_t(row.referee_index)*2+1])<<8;
        require(at<g.referee_names.size(),"pairings: stale active referee name offset");
        const auto first=g.referee_names.begin()+at;
        const auto end=std::find(first,g.referee_names.end(),uint8_t(0));
        require(end!=g.referee_names.end()&&row.referee_name==std::string(first,end),
            "pairings: stale active referee name");
    }
}

LeaguePairingsOwner open_league_pairings_source(ModernState& g,int manager,const LeaguePairingsConstants& c) {
    require(manager>=0&&manager<4&&size_t(manager)<g.manager_teams.size(),"pairings: unsupported manager");
    LeaguePairingsOwner out;out.manager=manager;out.runtime_club=g.manager_teams[size_t(manager)];
    const bool national=out.runtime_club==no_team();
    require((national||(out.runtime_club>=0&&out.runtime_club<league_clubs()))&&
        size_t(out.runtime_club)<g.club_ids.size(),"pairings: unsupported or unowned manager");
    out.club_identity=g.club_ids[size_t(out.runtime_club)];
    // TeamLeague2 maps the Original national runtime team 0x165 to league 0.
    out.league=national?0:team_league(out.runtime_club);
    require(size_t(out.league)<g.league_rounds.size(),"pairings: missing Native round counter");
    out.round=current_round(g,out.league);out.title_round=g.league_rounds.at(size_t(out.league));out.supported=true;
    if((g.memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField+uint32_t(manager))&0x0f)==8)out.postponed_active=true;
    validate(g,out);page_preferences(g,out,c);
    return out;
}

LeaguePairingsOwner edit_league_pairings_source(ModernState& g,const LeaguePairingsOwner& before,
    const LeaguePairingsEvent& e,const LeaguePairingsConstants& c,GameRandom& game,WatcomRandom& rand) {
    validate(g,before);auto out=before;
    if(e.kind!=LeaguePairingsAction::computer_tip)clear_computer_tip(out);
    const auto reset=[&] {const uint32_t at=::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField+uint32_t(out.manager);
        g.memory.set8(at,uint8_t((g.memory.u8(at)&0xf0)|1));
        out.search_first=out.search_second=out.search_first_identity=out.search_second_identity=-1;
        out.search_second_click=false;
        out.filter_team=out.filter_identity=-1;out.filter_offset=0;out.filter_active=false;
        out.postponed_active=false;out.postponed_offset=0;};
    switch(e.kind) {
    case LeaguePairingsAction::presentation_redraw:
        require(before.active_group>=0&&(e.value==0||e.value==1)&&bool(e.value)!=before.active_phase,
            "pairings: invalid active presentation redraw");
        out.active_phase=bool(e.value);break;
    case LeaguePairingsAction::reset: out.round=current_round(g,out.league);out.title_round=g.league_rounds.at(size_t(out.league));reset();break;
    case LeaguePairingsAction::previous:
        if(out.postponed_active) {if(out.postponed_offset>=10)out.postponed_offset-=10;break;}
        if((g.memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField+uint32_t(out.manager))&3)==0) {
            if(out.filter_active&&out.filter_offset>=10)out.filter_offset-=10;
            break;
        }
        out.round=(out.round+league_matchdays(out.league)-1)%league_matchdays(out.league);out.title_round=out.round;break;
    case LeaguePairingsAction::next:
        if(out.postponed_active) {
            const auto page=modern::league_pairings_view_source(g,before,c);
            if(page.rows.size()==10&&out.postponed_offset+10<page.total_rows)out.postponed_offset+=10;
            else if(page.rows.size()==10&&out.postponed_offset==0&&page.total_rows==10)
                throw std::invalid_argument("pairings: bounded Mode8 exact-ten non-progress rejected");
            break;
        }
        if((g.memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField+uint32_t(out.manager))&3)==0) {
            if(out.filter_active&&out.filter_team>=0&&
                out.filter_offset+10<filtered_fixture_count(out.league,out.filter_team))out.filter_offset+=10;
            break;
        }
        out.round=(out.round+1)%league_matchdays(out.league);out.title_round=out.round;break;
    case LeaguePairingsAction::toggle_group:
        require(g.memory.has(::bmh::layout::CalendarState_FormatError_RuntimeField2)&&g.memory.u8(::bmh::layout::CalendarState_FormatError_RuntimeField2)==0,
            "pairings: secondary leagues disabled");
        out.league_group^=1;
        require(size_t(out.manager)*0xb0c+0x210<g.manager_records.size(),
            "pairings: missing manager league preference");
        out.league=out.league_group?5:g.manager_records.at(size_t(out.manager)*0xb0c+0x210);
        require(out.league>=0&&out.league<9,"pairings: invalid manager league preference");
        [[fallthrough]];
    case LeaguePairingsAction::select_league:
        if(e.kind==LeaguePairingsAction::select_league) {
            require(e.value>=0&&e.value<9,"pairings: invalid league selection");out.league=e.value;
        }
        if(out.league!=before.league) {
            // LeagueMatchdays: retained round must be in the league and at/before
            // its next round. Directparam0 does not apply the automatic reset.
            if(out.round>=league_matchdays(out.league)||out.round>g.league_rounds.at(size_t(out.league)))
                out.round=current_round(g,out.league);
            out.title_round=out.round;
            reset();
        }
        break;
    case LeaguePairingsAction::team_info:
    case LeaguePairingsAction::search_team: {
        require(!out.filter_active&&!(e.kind==LeaguePairingsAction::search_team&&out.postponed_active),
            "pairings: filtered rows cannot open ordered search");
        require((e.kind==LeaguePairingsAction::search_team)==(out.search_first!=-1),
            "pairings: row action does not match Source search ownership");
        const auto view=modern::league_pairings_view_source(g,before,c);bool found=false;
        for(const auto& row:view.rows)for(const auto* team:{&row.home,&row.away})
            found|=team->runtime_team==e.value&&team->club_identity==e.expected_club_identity;
        require(found,"pairings: stale fixture team identity");
        if(e.kind==LeaguePairingsAction::team_info)break;
        if(!out.search_second_click) {
            out.search_first=e.value;out.search_first_identity=e.expected_club_identity;
            out.search_second=-1;out.search_second_identity=-1;out.search_second_click=true;
        } else {
            out.search_second=e.value;out.search_second_identity=e.expected_club_identity;
            // LeagueMatchdays2 scans directed home/away fixtures in ascending round
            // then row order. A missing self-pair keeps round and local30=1.
            for(int round=0;round<league_matchdays(out.league);++round) {
                const auto fixtures=league_fixtures(out.league,round+1);
                if(std::any_of(fixtures.begin(),fixtures.end(),[&](const auto& fixture) {
                    return fixture.home==out.search_first&&fixture.away==out.search_second;
                })) {
                    out.round=out.title_round=round;out.search_second_click=false;break;
                }
            }
        }
        break;
    }
    case LeaguePairingsAction::search:
        out.filter_team=out.filter_identity=-1;out.filter_offset=0;out.filter_active=false;
        out.postponed_active=false;out.postponed_offset=0;
        out.title_round=out.round; // local34 was clamped during the prior draw.
        g.memory.set8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField+uint32_t(out.manager),
            uint8_t((g.memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField+uint32_t(out.manager))&0xf0)|2));
        out.search_first=out.search_second=-2;
        out.search_first_identity=out.search_second_identity=-1;out.search_second_click=false;
        break;
    case LeaguePairingsAction::filter_by_club:
        out.postponed_active=false;out.postponed_offset=0;out.title_round=out.round;
        g.memory.set8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField+uint32_t(out.manager),
            uint8_t((g.memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField+uint32_t(out.manager))&0xf0)|4));
        out.search_first=out.search_second=out.search_first_identity=out.search_second_identity=-1;
        out.search_second_click=false;
        out.filter_team=-2;out.filter_identity=-1;out.filter_offset=0;out.filter_active=true;
        break;
    case LeaguePairingsAction::postponed_list:
        g.memory.set8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField+uint32_t(out.manager),
            uint8_t((g.memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField+uint32_t(out.manager))&0xf0)|8));
        out.search_first=out.search_second=out.search_first_identity=out.search_second_identity=-1;
        out.search_second_click=false;out.filter_team=out.filter_identity=-1;out.filter_offset=0;
        out.filter_active=false;out.postponed_active=true;out.postponed_offset=0;
        break;
    case LeaguePairingsAction::computer_tip:
        prepare_computer_tip(g,out,c,game);break;
    case LeaguePairingsAction::filter_club: {
        require(out.filter_active,"pairings: club selection outside Source filter mode");
        const auto visible=modern::league_pairings_view_source(g,before,c);bool found=false;
        for(const auto& row:visible.rows)for(const auto* team:{&row.home,&row.away})
            found|=team->runtime_team==e.value&&team->club_identity==e.expected_club_identity;
        require(found,"pairings: stale filtered-row team identity");
        out.filter_team=e.value;out.filter_identity=e.expected_club_identity;
        while(out.filter_offset>=filtered_fixture_count(out.league,out.filter_team)&&out.filter_offset>0)
            out.filter_offset-=10;
        break;
    }
    case LeaguePairingsAction::close:
        reset();g.memory.set8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField+uint32_t(out.manager),
            uint8_t(g.memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField+uint32_t(out.manager))&0xef));
        out.active_phase=false;out.active_group=-1;
        out.closed=true;modern::stir_random(game,rand);return out;
    case LeaguePairingsAction::unsupported:
        throw std::invalid_argument("pairings: Original Zusatzaktion ist noch nicht verfügbar");
    }
    validate(g,out);
    const uint32_t at=::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField+uint32_t(out.manager);
    if(e.kind!=LeaguePairingsAction::computer_tip)
        g.memory.set8(at,uint8_t(g.memory.u8(at)&0xef));
    return out;
}

LeaguePairingsView league_pairings_view_source(const ModernState& g,const LeaguePairingsOwner& o,
    const LeaguePairingsConstants& c) {
    validate(g,o);LeaguePairingsView out;out.owner=o;out.played_rounds=g.league_rounds.at(size_t(o.league));
    out.total_rounds=league_matchdays(o.league);out.preferences=g.memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField+uint32_t(o.manager));
    const bool postponed=o.postponed_active;
    if(!postponed)out.match_date=round_date(g,o.league,o.round,c);
    auto append_row=[&](int round,size_t n,const Fixture& fixture) {
        LeaguePairingsRow row;
        row.source_round=round;row.fixture_index=int(n);
        row.home=team_view(g,fixture.home,out.played_rounds);
        row.away=team_view(g,fixture.away,out.played_rounds);
        row.raw_result=g.results.at(result_index(o.league,round+1,int(n)));
        if(o.computer_tip_active) {
            const auto match=std::find_if(o.computer_tip_results.begin(),o.computer_tip_results.end(),
                [&](const auto& tip) {return tip.league==o.league&&tip.source_round==round&&
                    tip.fixture_index==int(n)&&tip.home_runtime_team==row.home.runtime_team&&
                    tip.home_club_identity==row.home.club_identity&&
                    tip.away_runtime_team==row.away.runtime_team&&
                    tip.away_club_identity==row.away.club_identity;});
            if(match!=o.computer_tip_results.end()) {
                require(row.raw_result==match->original_result,
                    "pairings: Native result changed under Computer Tip forecast");
                row.computer_tip_result=match->predicted_result;
            }
        }
        if(postponed) {
            for(uint32_t k=0;k<40;++k) {
                const uint32_t at=::bmh::layout::Postponed+k*6;const uint16_t day=g.memory.u16(at);
                if(day&&g.memory.u8(at+3)==o.league&&g.memory.u8(at+2)==round&&g.memory.u8(at+4)==n) {
                    row.postponed_queue_slot=uint8_t(k);row.postponed_day=day;
                    row.postponed_date=g.season_start+day;break;
                }
            }
        } else if(!modern::league_pairings_played_source(row.raw_result)&&row.raw_result!=0xcc) {
            for(uint32_t k=0;k<40;++k) {
                const uint32_t at=::bmh::layout::Postponed+k*6;const uint16_t day=g.memory.u16(at);
                if(day&&g.memory.u8(at+3)==o.league&&g.memory.u8(at+2)==round&&g.memory.u8(at+4)==n) {
                    row.postponed_date=g.season_start+day;break;
                }
            }
        }
        if(!o.active_phase&&!postponed&&!(o.filter_active&&o.filter_team>=0)&&round>=out.total_rounds/2) {
            const uint8_t raw=g.results.at(result_index(o.league,round+1-out.total_rounds/2,int(n)));
            if(modern::league_pairings_played_source(raw))row.first_leg_result=raw;
        }
        if(o.filter_active&&o.filter_team>=0&&row.raw_result==0xcc)
            row.scheduled_date=round_date(g,o.league,round,c);
        out.rows.push_back(std::move(row));
    };
    if(postponed) {
        out.page_offset=o.postponed_offset;
        for(int round=0;round<out.total_rounds;++round) {
            const auto fixtures=league_fixtures(o.league,round+1);
            for(size_t n=0;n<fixtures.size();++n) {
                const auto raw=g.results.at(result_index(o.league,round+1,int(n)));
                if(!modern::league_pairings_postponed_source(raw))continue;
                if(out.total_rows>=o.postponed_offset&&out.rows.size()<10)append_row(round,n,fixtures[n]);
                ++out.total_rows;
            }
        }
    } else if(o.filter_active&&o.filter_team>=0) {
        out.page_offset=o.filter_offset;
        for(int round=0;round<out.total_rounds;++round) {
            const auto fixtures=league_fixtures(o.league,round+1);
            for(size_t n=0;n<fixtures.size();++n) {
                if(fixtures[n].home!=o.filter_team&&fixtures[n].away!=o.filter_team)continue;
                if(out.total_rows>=o.filter_offset&&out.rows.size()<10)append_row(round,n,fixtures[n]);
                ++out.total_rows;
            }
        }
    } else {
        const auto fixtures=league_fixtures(o.league,o.round+1);
        out.total_rows=int(fixtures.size());
        for(size_t n=0;n<fixtures.size();++n)append_row(o.round,n,fixtures[n]);
    }
    return out;
}
} // namespace bmh::modern
