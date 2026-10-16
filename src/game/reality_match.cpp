// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/reality_match.hpp"

#include <algorithm>
#include "game/lineup.hpp"

namespace bmh {
namespace {
void validate_owner(const MatchEngine& engine,const RealityMatchScene& owner) {
    if(owner.state_owner!=&engine.state||owner.rand_owner!=&engine.rand||owner.game_owner!=&engine.game||
       owner.date!=engine.state.date||owner.group>=engine.groups.size()||owner.match<0||
       owner.group_count!=engine.groups.size()||owner.fixture_owner!=&engine.groups[owner.group])
        throw FormatError("Reality match: stale engine/fixture owner");
    const auto& group=engine.groups[owner.group];
    if(owner.match>=group.matches()||group.fixtures.at(size_t(owner.match)*2)!=owner.teams[0]||
       group.fixtures.at(size_t(owner.match)*2+1)!=owner.teams[1])
        throw FormatError("Reality match: fixture changed during scene");
}
}
void initialize_reality_match_scene(MatchEngine& engine,size_t group,int match,int side,int minute,
    const LeImage& exe,RealitySceneContext context,RealityMatchScene& owner) {
    if(group>=engine.groups.size()||match<0||match>=engine.groups[group].matches()||side<0||side>1||minute<1)
        throw FormatError("Reality match: invalid Source fixture/minute");
    if(owner.state_owner&&(!owner.scene.finished||!owner.settled))
        throw FormatError("Reality match: previous scene/change has not completed");
    const auto& fixture=engine.groups[group];
    context.referee_index=fixture.desc.at(0x10+size_t(match));
    context.referee_profile=engine.referees.at(context.referee_index);
    context.numbers=engine.button(::bmh::layout::RealityMatch_FormatError_ExecutableField)==0;
    const std::array<int,2> teams{fixture.fixtures[size_t(match)*2],fixture.fixtures[size_t(match)*2+1]};
    owner.pending.reset();owner.settled=owner.settling=false;
    owner.group=group;owner.group_count=engine.groups.size();owner.fixture_owner=&fixture;owner.match=match;owner.minute=minute;owner.teams=teams;
    owner.date=engine.state.date;owner.state_owner=&engine.state;
    owner.rand_owner=&engine.rand;owner.game_owner=&engine.game;
    initialize_reality_scene(engine.state,teams,side,reality_formations(engine.state,teams),exe,
        context,engine.rand,engine.game,owner.scene);
}
RealitySceneTick advance_reality_match_scene(MatchEngine& engine,RealityMatchScene& owner,const LeImage& exe,
    MinuteReport& report,bool user_exit) {
    validate_owner(engine,owner);
    if(owner.scene.finished||owner.settled||owner.settling||report.unsupported)
        throw FormatError("Reality match: round outside active Source scene");
    // RealityReporterPanel resets bdb14 before each DecideRealityBall, not after scene completion.
    owner.pending.reset();
    const auto forced=[&](int slot,int side) {
        auto& group=engine.groups[owner.group];
        const int player=engine.state.squad_slots.at(size_t(slot))&0xfff;
        const auto event=engine.forced_event(group,owner.match,side,owner.minute,slot,report);
        if(report.unsupported)throw FormatError(report.reason);
        if(event.change) {
            const int kind=(event.code&0x7f)==4?2:(event.code&0x7f)==1?0:1;
            const int team=owner.teams.at(size_t(side)),other=owner.teams.at(size_t(1-side));
            owner.pending=RealityDeferredRequest{*event.change,
                LiveMatchEvent{owner.group,owner.match,side,team,other,player,kind,owner.minute,
                    event.change->human?event.change->mode:engine.state.manager_of(team)},slot};
        }
        return event.code;
    };
    return advance_reality_scene(engine.state,owner.scene,exe,engine.rand,engine.game,forced,user_exit);
}
bool settle_reality_match_scene(MatchEngine& engine,RealityMatchScene& owner,MinuteReport& report) {
    validate_owner(engine,owner);
    if(!owner.scene.finished||owner.settling)
        throw FormatError("Reality match: change before scene end or reentrant completion");
    if(owner.settled||report.unsupported)return false;
    if(!owner.pending){owner.settled=true;return false;}
    const auto request=*owner.pending;const auto& event=request.event;const auto& change=request.source;
    if(event.group_index!=owner.group||event.match_index!=owner.match||event.minute!=owner.minute||
       event.side<0||event.side>1||event.team!=owner.teams[size_t(event.side)]||
       event.opponent!=owner.teams[size_t(1-event.side)])
        throw FormatError("Reality match: deferred request belongs to another fixture");
    const auto range=squad_range(event.team);
    if(request.offender_slot<int(range.first)||request.offender_slot>=int(range.first+range.count)||
       (engine.state.squad_slots.at(size_t(request.offender_slot))&0xfff)!=event.player)
        throw FormatError("Reality match: deferred offender slot changed");
    if(change.human) {
        if(change.team!=event.opponent||change.slot!=0xff||change.mode!=event.manager_slot||
           change.mode<0||engine.state.manager_of(event.team)!=change.mode||
           (event.kind!=1&&event.kind!=2))
            throw FormatError("Reality match: invalid Source human request");
        if(!engine.live_event&&!engine.leave_screens) {
            report.fail("deferred Reality substitution screen of a human manager (PitchScreen)");return false;
        }
    } else if(change.team!=event.team||change.slot!=request.offender_slot||
              change.mode!=engine.state.players.at(size_t(event.player)).position()+20) {
        throw FormatError("Reality match: invalid Source automatic request");
    }
    owner.settling=true;
    try {
        if(change.human) {
            if(engine.state.memory.has(::bmh::layout::ActiveManager))engine.state.memory.set8(::bmh::layout::ActiveManager,uint8_t(change.mode));
            if(engine.live_event)engine.live_event(event);
        } else {
            // Exact Injure(team,absoluteSlot,position+20). This tail does not
            // call BuildSideRecords3 again: the actual card routine already did so.
            (void)substitute(engine.state,change.team,change.slot,change.mode,engine.rand);
        }
        validate_owner(engine,owner); // callbacks retain stable fixture identity
    } catch(...) {owner.settling=false;throw;}
    owner.settling=false;owner.pending.reset();owner.settled=true;return true;
}
} // namespace bmh
