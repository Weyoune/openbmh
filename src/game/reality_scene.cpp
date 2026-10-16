// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/reality_scene.hpp"

#include <algorithm>
#include <cstdlib>
#include "game/messages.hpp"

namespace bmh {
namespace {
bool goal_mouth(const RealityScene& scene,const LeImage& exe) {
    const auto [x,y]=scene.setup.grid.ball();
    return x==exe.u8(::bmh::layout::RealityScene_GoalMouth_RuntimeFieldUnsigned+uint32_t(scene.setup.attacking_side))&&y>=11&&y<=13;
}
void finish(RealityScene& scene,RealitySceneStop stop) {
    scene.finished=true;
    auto& out=scene.result;out.stop=stop;out.side=scene.setup.attacking_side;
    out.return_flags=scene.return_flags;
    if(stop!=RealitySceneStop::possession_ended&&!out.scene_status&&out.shooter_row!=0xfff)
        out.return_flags|=1;
    // RealityReporterPanel converts only a same-side assist; missing/cross-side stays fff.
    if(out.shooter_row==0xfff||out.assistant_row==0xfff||out.shooter_row/11!=out.assistant_row/11)
        out.assistant=out.assistant_row=0xfff;
    else {
        out.assistant=scene.setup.players.at(size_t(out.assistant_row)).player;
        out.assistant_row%=11;
    }
    if(out.shooter_row!=0xfff) {
        out.shooter=scene.setup.players.at(size_t(out.shooter_row)).player;
        out.shooter_row%=11;
    }
}
void prepare_report(RealityScene& scene,const RealityBallDecision& decision,const RealityBallOutcome& outcome) {
    auto& frame=scene.reporter_frame;
    frame.action=uint8_t(outcome.action);frame.opponent=decision.opponent_at_actor;
    frame.airborne=scene.ball_history.airborne;frame.scene_status=uint8_t(outcome.scene_status);
    frame.distance_before=uint8_t(decision.goal_distance);
    frame.distance_after=uint8_t(42-std::abs(decision.side*42-scene.setup.grid.ball().first));
    frame.player_side=uint8_t(decision.side);frame.lost_duel=outcome.lost_duel;
    frame.received=outcome.received;frame.inaccurate=decision.inaccurate;
    frame.blocked_attempt=decision.blocked_attempt;frame.intercepted=decision.intercepted;
    frame.offside=decision.receiver_offside;frame.leading_run=decision.leading_run;
    frame.receiver=decision.receiver;frame.receiver_player=decision.receiver_player;frame.line_selected=decision.line_selected;
    // Verified 63e96: MOVZX EBX,byte(side), preserved by45bb5 and64d4d.
    // Thus the normal ApplyRealityBall -> InitializeRealityReporter call enters with BH=0.
    frame.incoming_bh=0;
}
}

void initialize_reality_scene(GameState& state,const std::array<int,2>& teams,int side,
    const RealityFormations& positions,const LeImage& exe,const RealitySceneContext& context,
    WatcomRandom& rand,GameRandom& game,RealityScene& scene) {
    for(auto actor:context.retained_cue_actors)
        if(actor>=22)throw FormatError("Reality scene: retained Source cue actor outside figures");
    auto retained_setup=std::move(scene.setup);
    scene={};scene.setup=std::move(retained_setup);scene.context=context;
    initialize_reality(state,teams,side,positions,exe,rand,game,scene.setup);
    scene.setup.motion.numbers=context.numbers;scene.setup.motion.ball_airborne=false;
    scene.reporter=initialize_reality_reporter(exe);
    scene.reporter_frame.referee=context.referee_index;scene.reporter_frame.card_kind=context.retained_card_kind;
    scene.result.side=side;scene.result.deflected=context.retained_deflection;
    scene.result.distance=context.retained_distance;
    if(!scene.setup.ready){scene.result.scene_status=3;finish(scene,RealitySceneStop::unavailable);}
}

void exit_reality_scene_after_animation(RealityScene& scene) {
    if (scene.finished || !scene.rounds) throw FormatError("Reality scene: exit outside post-animation boundary");
    finish(scene,RealitySceneStop::user_exit);
}

RealitySceneTick advance_reality_scene(GameState& state,RealityScene& scene,const LeImage& exe,
    WatcomRandom& rand,GameRandom& game,const RealityForcedEvent& forced_event,bool user_exit) {
    if(scene.finished||!scene.setup.ready)throw FormatError("Reality scene: round requested after scene end");
    RealitySceneTick tick;tick.round=scene.rounds;tick.side=scene.setup.attacking_side;
    const auto done=[&](){tick.result=scene.result;tick.finished=scene.finished;return tick;};
    if(goal_mouth(scene,exe)){finish(scene,RealitySceneStop::goal_mouth);return done();}
    if(scene.result.restart){finish(scene,RealitySceneStop::boundary);return done();}
    const int previous_side=scene.setup.attacking_side;
    const auto [bx,by]=scene.setup.grid.ball();
    for(int side=0;side<2;++side)
        if(scene.setup.grid.occupant(bx,by,side)&&!scene.setup.grid.occupant(bx,by,1-side))
            scene.setup.attacking_side=side;
    const int side=scene.setup.attacking_side;tick.side=side;
    if(scene.stagnation>3||previous_side!=side) {
        if(scene.stagnation<4&&rand.range(0,1)) {
            scene.return_flags^=0x80;
            const int shift=int8_t(exe.u8(::bmh::layout::RealityScene_AdvanceRealityScene_RuntimeFieldUnsigned+uint32_t(side)));
            for(auto& player:scene.setup.players) {
                player.min_x=int16_t(player.min_x+shift);player.max_x=int16_t(player.max_x+shift);
            }
            scene.shot=false;scene.result.shot_attempted=false;
        } else {
            RealityReporterResult ended;
            ended.text={message_text(state,::bmh::layout::RealityScene_AdvanceRealityScene_TextPointer)};tick.reports.push_back(std::move(ended));
            finish(scene,RealitySceneStop::possession_ended);return done();
        }
    }
    scene.reporter_frame.action=3;scene.reporter_frame.player_side=uint8_t(side);
    uint8_t blocked=0;
    int actor=-1;
    if(!scene.first_round) {
        // Source scans coordinate arrays0..21, not occupancy lookup/figure order.
        for(int id=side*11;id<side*11+11;++id)
            if(scene.setup.grid.position(id)==scene.setup.grid.ball()){actor=id;break;}
        if(actor>=0) {
            tick.ball_actor=actor;
            const auto start=scene.setup.grid.position(actor);scene.setup.grid.clear(start.first,start.second,side);
            const auto decision=decide_reality_ball(state,scene.setup,actor,scene.ball_history,scene.context.rating,rand);
            std::vector<RealityFoulCue> cues;RealityBallOutcome outcome;auto effective=decision;
            if(decision.boundary==RealityBallBoundary::dribble_or_duel) {
                const auto dribble=continue_reality_dribble(state,scene.setup,exe,decision,scene.context.referee_profile,
                    scene.context.muted,rand,forced_event,scene.context.sound);
                blocked=decision.opponent_at_actor;cues=dribble.cues;effective=dribble.ball;
                if(dribble.card_kind)scene.context.retained_card_kind=scene.reporter_frame.card_kind=*dribble.card_kind;
                outcome=apply_reality_dribble(state,scene.setup,dribble,scene.ball_history,scene.context.rating,
                    scene.context.referee_profile,rand,game);
            } else outcome=apply_reality_ball(state,scene.setup,decision,scene.ball_history,scene.context.rating,
                scene.context.referee_profile,rand,game);
            scene.result.scene_status=uint8_t(outcome.scene_status);scene.result.restart=uint8_t(outcome.restart);
            scene.result.shooter_row=outcome.shooter_row;scene.result.assistant_row=outcome.assistant_row;
            scene.result.distance=decision.goal_distance;scene.result.deflected=decision.blocked_attempt;
            scene.result.shot_attempted=scene.result.shot_attempted||outcome.shot_ready;scene.shot=outcome.shot_ready;scene.event_stop=outcome.event_stops_scene;
            prepare_report(scene,effective,outcome);
            if(outcome.needs_reporter) {
                if(cues.empty())tick.reports.push_back(report_reality(state,scene.setup,exe,scene.reporter_frame,
                    scene.reporter,actor,side,0,rand));
                else {
                    if(cues.size()>scene.context.retained_cue_actors.size())throw FormatError("Reality scene: too many Source foul cues");
                    for(size_t i=0;i<cues.size();++i) {
                        if(cues[i].actor>=0)scene.context.retained_cue_actors[i]=uint8_t(cues[i].actor);
                        tick.reports.push_back(report_reality(state,scene.setup,exe,scene.reporter_frame,scene.reporter,
                            scene.context.retained_cue_actors[i],side,uint8_t(cues[i].code),rand));
                    }
                }
            }
            scene.stagnation=0;
        }
    }
    if(actor<0)++scene.stagnation;
    if(!scene.result.scene_status&&!goal_mouth(scene,exe)) {
        auto context=reality_move_context(scene.setup,exe);
        context.defensive_push=scene.defensive_push;context.blocked_figure=blocked;context.keeper_player=scene.result.keeper;
        bool first_defender=true;
        for(int id=side?21:0;id!=(side?-1:22);id+=side?-1:1) {
            if(!scene.setup.motion.figures[size_t(id)].active)continue;
            const int figure_side=id/11;
            if(figure_side==side) {
                if(scene.first_round||id==actor)continue;
                const auto start=scene.setup.grid.position(id);scene.setup.grid.clear(start.first,start.second,figure_side);
                const auto movement=reality_move_attacker(state,scene.setup,exe,id,context,rand,game);
                tick.moved_figures.push_back(id);
                if(movement.reporter_code)tick.reports.push_back(report_reality(state,scene.setup,exe,scene.reporter_frame,
                    scene.reporter,id,figure_side,uint8_t(movement.reporter_code),rand));
            } else {
                if(first_defender){reality_defensive_push(scene.setup,context,rand);first_defender=false;}
                const auto start=scene.setup.grid.position(id);scene.setup.grid.clear(start.first,start.second,figure_side);
                reality_move_defender(state,scene.setup,exe,id,context,rand,game);tick.moved_figures.push_back(id);
            }
        }
        scene.defensive_push=context.defensive_push;scene.result.keeper=context.keeper_player;
    }
    scene.first_round=false;++scene.rounds;
    if(!scene.event_stop) {
        if (scene.return_flags < 2 && scene.context.sound) scene.context.sound(16); // RealityFieldScreen.
        tick.motion=reality_motion(scene.setup.motion,exe);
        const auto ball_phase=scene.setup.motion.figures[23].phase;
        scene.setup.motion.figures[23]=scene.setup.motion.figures[22];scene.setup.motion.figures[23].phase=ball_phase;
        for(size_t i=0;i<24;++i) {
            auto& figure=scene.setup.motion.figures[i];figure.phase=tick.motion->phases_after[i];
            if(i<23){figure.old_x=figure.next_x;figure.old_y=figure.next_y;}
        }
    }
    if(scene.event_stop)finish(scene,RealitySceneStop::forced_event);
    else if(user_exit)finish(scene,RealitySceneStop::user_exit);
    else if(scene.shot)finish(scene,RealitySceneStop::shot);
    else if(scene.result.scene_status||scene.result.restart)finish(scene,RealitySceneStop::boundary);
    return done();
}
} // namespace bmh
