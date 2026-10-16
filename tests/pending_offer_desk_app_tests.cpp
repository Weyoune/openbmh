// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "app/controller.hpp"
#include "game/save.hpp"

using namespace bmh;
namespace fs=std::filesystem;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct TemporarySaves {
    fs::path path;
    TemporarySaves(const fs::path& input){
        static unsigned serial=0;
        path=fs::temp_directory_path()/("openbmh-pending-desk-app-"+
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++serial));
        require(fs::create_directory(path),"isolated test save directory created");
        fs::copy_file(input,path/"AUTOSAVE.FBM");
    }
    ~TemporarySaves(){std::error_code ignored;fs::remove_all(path,ignored);}
};
struct Context {
    fs::path data;
    TemporarySaves saves;
    ui::StartupFlow flow;
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision=0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio;
    Context(const fs::path& d,const fs::path& input):data(d),saves(input),flow(d,saves.path){
        require(load_savegame(input).checksum_ok,"genuine native fixture checksum");
        playback.set_muted(true);
        flow.click(214,215);
        auto load=flow.send_key("ENTER");
        require(load.request==ui::FlowRequest::file_action&&load.file_action.action==ui::SaveLoadAction::load,
                "actual production load request selects the isolated native fixture");
        require(serve(load).empty()&&session&&revision==1&&flow.screen_name()=="desktop",
                "actual controller loads genuine Native QA_INPUT at revision1");
        require(session->state().memory.u32(::bmh::layout::OfferSlots)==0x80003800u&&
                session->state().manager_teams.size()==1&&session->state().manager_teams[0]==33,
                "actual loaded desk contains original N.Brown salary demand");
    }
    std::string serve(const ui::FlowResult& request,PendingOfferHooks hooks={},
        std::function<void(int,const std::vector<MatchGroup>&,const GameState&)> live={}){
        return app::serve_flow_request(flow,request,session,data,saves.path,1,revision,playback,audio,false,
                                      std::move(live),{},{},{},{},{},{},{},std::move(hooks),
                                      app::PendingOfferEntryPolicy::normal_interactive);
    }
    ui::FlowResult advance(){
        require(flow.desktop()!=nullptr&&!flow.desktop()->pending(),"desktop is available for a fresh actual request");
        const auto& items=flow.desktop()->state().items;
        const auto item=std::find_if(items.begin(),items.end(),[](const auto& i){return !i.disabled&&(i.kind==12||(i.kind==58&&i.open));});
        require(item!=items.end(),"actual native advance item is enabled");
        auto request=flow.click(item->x+item->width/2,item->y+item->height/2);
        require(request.request==ui::FlowRequest::kickoff&&flow.desktop()->pending()&&request.desk_action.revision==revision,
                "actual flow owns same-revision advance request");return request;
    }
};
struct Snapshot {Bytes native;WatcomRandom w;GameRandom g;};
Snapshot snapshot(const GameState& state,const WatcomRandom& w,const GameRandom& g){return {save_stream(state),w,g};}
Snapshot snapshot(app::CareerSession& s){return snapshot(s.state(),s.rand(),s.game());}
void same(const Snapshot& a,const Snapshot& b){
    require(a.native==b.native&&a.w.seed()==b.w.seed()&&a.w.calls()==b.w.calls()&&
            a.g.seed()==b.g.seed()&&a.g.calls()==b.g.calls(),"full native state and W/G match the owned snapshot");
}
bool same_group(const MatchGroup& a,const MatchGroup& b){
    return a.desc==b.desc&&a.fixtures==b.fixtures&&a.results==b.results&&a.cup_results==b.cup_results&&
        a.cup_other_leg==b.cup_other_leg&&a.cup_result_address[0]==b.cup_result_address[0]&&
        a.cup_result_address[1]==b.cup_result_address[1]&&a.phase==b.phase&&a.records==b.records&&
        a.end_minute==b.end_minute&&a.spectators==b.spectators&&a.log==b.log&&a.ended==b.ended&&
        a.kickoff_void_mask==b.kickoff_void_mask;
}
bool same_groups(const std::vector<MatchGroup>& a,const std::vector<MatchGroup>& b){
    return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),same_group);
}
void same_driver(const TurnDriver& a,const TurnDriver& b){
    require(a.stage==b.stage&&a.manager_position==b.manager_position&&a.revision==b.revision&&
            a.loaded_matchday_resume==b.loaded_matchday_resume&&
            a.suppress_remaining_pre_match_offers==b.suppress_remaining_pre_match_offers&&
            a.calendar_entry==b.calendar_entry&&a.session_id==b.session_id&&a.next_event_id==b.next_event_id&&
            a.referee_source==b.referee_source&&a.referees==b.referees&&same_groups(a.groups,b.groups)&&
            a.last_results.valid==b.last_results.valid&&a.last_results.date==b.last_results.date&&
            a.last_results.calendar_entry==b.last_results.calendar_entry&&a.last_results.matchday==b.last_results.matchday&&
            same_groups(a.last_results.groups,b.last_results.groups),"waiting retains complete unclaimed driver cursor and event data");
}
struct AnswerSentinel {
    const void* identity;
    PendingOfferAnswer operator()(const GameState&,const PendingOfferQuestion&)const{return PendingOfferAnswer::unsupported;}
};
struct ChoiceSentinel {
    const void* identity;
    PendingOfferTransferChoice operator()(const GameState&,const TransferOfferListView&)const{return {};}
};
struct NoticeSentinel {
    const void* identity;
    void operator()(const GameState&,const PendingOfferNotice&)const{}
};
void install_sentinels(Context& c,const void* id){
    PendingOfferHooks h;h.answer=AnswerSentinel{id};h.select_transfer=ChoiceSentinel{id};h.notice=NoticeSentinel{id};
    c.session->set_pending_offer_hooks(std::move(h));
}
void restored(const Context& c,const void* id){
    const auto& h=c.session->pending_offer_hooks();
    const auto* a=h.answer.target<AnswerSentinel>();const auto* s=h.select_transfer.target<ChoiceSentinel>();
    const auto* n=h.notice.target<NoticeSentinel>();
    require(a&&s&&n&&a->identity==id&&s->identity==id&&n->identity==id,
            "controller restores all three preexisting pending presenters after borrowed scope");
    require(c.session->pending_offer_entry_policy()==app::PendingOfferEntryPolicy::legacy_unpresented,
            "controller restores previous explicit legacy entry policy after normal-mode borrowed scope");
}
void waiting(Context& c,const Snapshot& before,const TurnDriver& driver){
    same(snapshot(*c.session),before);same_driver(c.session->manager_join_checkpoint().driver,driver);
    require(c.revision==1&&c.flow.desktop()->state().revision==1&&!c.flow.desktop()->pending()&&
            c.flow.screen_name()=="desktop"&&!c.session->completed_matchday_results()&&c.playback.queued_bytes()==0,
            "waiting rejects UI request at unchanged revision, without audio/result handoff");
}
struct Oracle {
    Snapshot pending,first_minute,final;
    std::vector<Bytes> question_states,chooser_states,notice_states;
    std::vector<uint32_t> notice_addresses;
    TurnAdvanceResult result;
    bool minute_seen=false;
};
Oracle oracle(Context& c,const ui::FlowResult& request,bool fee){
    auto checkpoint=c.session->manager_join_checkpoint();Oracle out;
    PendingOfferHooks h;
    h.answer=[&](const GameState& state,const PendingOfferQuestion&){out.question_states.push_back(save_stream(state));return PendingOfferAnswer::yes;};
    h.select_transfer=[&](const GameState& state,const TransferOfferListView&){out.chooser_states.push_back(save_stream(state));return PendingOfferTransferChoice{PendingOfferTransferChoiceKind::bid,0};};
    h.notice=[&](const GameState& state,const PendingOfferNotice& n){out.notice_states.push_back(save_stream(state));out.notice_addresses.push_back(n.text_address);};
    MinuteReport report;
    const auto processed=process_natural_pending_offers_after_desk(checkpoint.state,
        {PendingOfferDeskRuntimeMode::normal_interactive,1,checkpoint.driver.manager_position},
        checkpoint.game,checkpoint.rand,h,report);
    require(processed.completed&&!report.unsupported&&processed.processed==1,
            "direct actual Core oracle completes genuine native post-desk batch");
    require(checkpoint.state.memory.u8(::bmh::layout::DeskPosition)==1&&checkpoint.state.date==c.session->state().date,
            "Core oracle completes manager cursor before match/date advancement");
    out.pending=snapshot(checkpoint.state,checkpoint.rand,checkpoint.game);
    require(fee?out.chooser_states.size()==1&&out.notice_addresses==std::vector<uint32_t>{::bmh::layout::PendingOffer_FormatError_ExecutableField2}:
                out.question_states.size()==1&&processed.salary_accepted==1,
            "oracle takes intended salary or actual rawbid0 settlement branch");
    checkpoint.driver.revision=c.revision;
    checkpoint.driver.live_minute=[&](int,MatchEngine&,std::vector<MatchGroup>&,GameState& state){
        if(!out.minute_seen){out.first_minute=snapshot(state,checkpoint.rand,checkpoint.game);out.minute_seen=true;}
    };
    MinuteReport turn_report;
    out.result=advance_to_next_desk(checkpoint.driver,checkpoint.state,
        {request.desk_action.manager_slot,request.desk_action.revision},checkpoint.game,checkpoint.rand,turn_report);
    require(!turn_report.unsupported&&out.result.kind!=TurnDeskKind::Failed&&out.minute_seen,
            "actual bound Native event oracle reaches next desk and has a real first minute");
    out.final=snapshot(checkpoint.state,checkpoint.rand,checkpoint.game);return out;
}
void guards(Context& c,const ui::FlowResult& request,const Snapshot& before){
    same(snapshot(*c.session),before);
    require(c.revision==1&&c.flow.desktop()->state().revision==1&&
            c.session->pending_offer_entry_policy()==app::PendingOfferEntryPolicy::normal_interactive,
            "pending callback borrows explicit normal policy without intermediate revision publication");
    std::string error;
    require(c.session->dispatch_kickoff(request.desk_action,c.revision,error).kind==TurnDeskKind::Failed&&
            error=="pending_offer_decision_active","nested kickoff is blocked inside pending presenter");
    require(!c.session->dispatch_club_player_pending_offers({33,14,5,5,-1},1,{},c.revision,error),
            "nested manual batch is blocked inside natural pending presenter");
    const auto forbidden=c.saves.path/"MUST-NOT-WRITE.FBM";
    require(!c.session->save_game(forbidden,0,false,error)&&error=="pending_offer_decision_active"&&
            !fs::exists(forbidden),"partial pending transaction cannot write a save");
    same(snapshot(*c.session),before);
}
void successful_retry(Context& c,bool fee){
    auto request=c.advance();auto before=snapshot(*c.session);const auto before_date=c.session->state().date;
    const auto expected=oracle(c,request,fee);
    size_t questions=0,choosers=0,notices=0;bool minute_seen=false;
    PendingOfferHooks h;
    h.answer=[&](const GameState& staged,const PendingOfferQuestion& q){
        require(!fee&&questions<expected.question_states.size()&&save_stream(staged)==expected.question_states[questions++]&&
                q.source_flags==1&&q.clicked_source_slot==797&&q.amount==52300,
                "actual controller salary presenter borrows exact pre-driver Core state");
        guards(c,request,before);return PendingOfferAnswer::yes;
    };
    h.select_transfer=[&](const GameState& staged,const TransferOfferListView& v){
        require(fee&&choosers<expected.chooser_states.size()&&save_stream(staged)==expected.chooser_states[choosers++]&&
                v.valid&&v.list_entry==40,"actual chooser borrows exact Core special-entry quote state");
        guards(c,request,before);return PendingOfferTransferChoice{PendingOfferTransferChoiceKind::bid,0};
    };
    h.notice=[&](const GameState& staged,const PendingOfferNotice& n){
        require(fee&&notices<expected.notice_states.size()&&save_stream(staged)==expected.notice_states[notices]&&
                n.text_address==expected.notice_addresses[notices],"actual notice borrows settled staged accounting before driver");
        ++notices;guards(c,request,before);
    };
    auto live=[&](int,const std::vector<MatchGroup>&,const GameState& state){
        if(minute_seen)return;minute_seen=true;
        require(&state==&c.session->state()&&c.revision==1&&state.date==before_date&&
                state.memory.u8(::bmh::layout::DeskPosition)==1,"real live-minute callback sees committed manager phase without intermediate publication");
        same(snapshot(state,c.session->rand(),c.session->game()),expected.first_minute);
    };
    require(c.serve(request,std::move(h),live).empty()&&minute_seen&&c.revision==2&&
            c.flow.desktop()->state().revision==2&&!c.flow.desktop()->pending(),
            "fresh same-revision retry completes through actual controller with one outer revision publication");
    require(fee?choosers==1&&notices==1&&questions==0:questions==1&&choosers==0&&notices==0,
            "borrowed pending presenters run exactly on the intended Core branch");
    same(snapshot(*c.session),expected.final);
    const auto settled=snapshot(*c.session);
    c.serve(request,{});same(snapshot(*c.session),settled);
    require(c.revision==2,"consumed kickoff replay cannot repeat decision or simulation");
}
void missing_then_retry(const fs::path& data,const fs::path& input){
    Context c(data,input);int marker=0;install_sentinels(c,&marker);
    const auto before=snapshot(*c.session);const auto driver=c.session->manager_join_checkpoint().driver;
    auto request=c.advance();require(c.serve(request).empty(),"absent borrowed presenter returns a controlled waiting rejection");
    waiting(c,before,driver);restored(c,&marker);
    // Same Source request is now retryable; a fresh UI request must be accepted.
    successful_retry(c,false);restored(c,&marker);
}
void quoted_throw_then_retry(const fs::path& data,const fs::path& input){
    Context c(data,input);int marker=0;install_sentinels(c,&marker);
    // Controlled scalar transform of the genuine offered record, retaining rawbid0.
    c.session->state().memory.set32(::bmh::layout::OfferSlots,c.session->state().memory.u32(::bmh::layout::OfferSlots)&0x7fffffffu);
    const auto before=snapshot(*c.session);const auto driver=c.session->manager_join_checkpoint().driver;
    auto request=c.advance();int calls=0;PendingOfferHooks h;
    h.select_transfer=[&](const GameState&,const TransferOfferListView& v)->PendingOfferTransferChoice{
        require(v.valid&&v.list_entry==40&&v.quoted_fee>0,"actual Core quote precedes throwing chooser");
        ++calls;guards(c,request,before);throw std::runtime_error("");
    };
    require(c.serve(request,std::move(h)).empty()&&calls==1,"empty-message presenter throw yields typed waiting with safe nonempty UI reason");
    waiting(c,before,driver);restored(c,&marker);
    successful_retry(c,true);restored(c,&marker);
}
void partial_sale_rollback(const fs::path& data,const fs::path& input){
    Context c(data,input);int marker=0;install_sentinels(c,&marker);
    auto& state=c.session->state();const int second=state.squad_slots.at(798)&0xfff;
    require(second!=0xfff&&second!=14,"genuine native next squad row supplies second offered player");
    const uint32_t from=::bmh::layout::EditorList+40*0x58,to=from+0x58;
    for(uint32_t i=0;i<0x58;++i)state.memory.set8(to+i,state.memory.u8(from+i));
    state.memory.set32(::bmh::layout::Weekly_PreMatchOffer_RuntimeField,0x80000000u|uint32_t(second)*1024);state.squad_slots.at(0xb70+41)=uint16_t(second);
    const auto before=snapshot(*c.session);const auto driver=c.session->manager_join_checkpoint().driver;
    auto request=c.advance();int calls=0;PendingOfferHooks h;
    h.answer=[&](const GameState& staged,const PendingOfferQuestion& q){
        ++calls;guards(c,request,before);
        if(q.raw_offer_slot==0)return PendingOfferAnswer::no;
        require(q.raw_offer_slot==1&&staged.memory.u32(::bmh::layout::OfferSlots)==0xffffffffu&&save_stream(staged)!=before.native,
                "first native forced sale really settled before unavailable second answer");
        return PendingOfferAnswer::unsupported;
    };
    require(c.serve(request,std::move(h)).empty()&&calls==2,"incomplete later answer rejects whole previously settled batch");
    waiting(c,before,driver);restored(c,&marker);
    // A direct typed call with the restored unsupported sentinel proves RAII guards
    // reset and the same native driver remains retryable, without a default NO.
    auto retry=c.advance();std::string error;
    c.session->set_pending_offer_entry_policy(app::PendingOfferEntryPolicy::normal_interactive);
    auto result=c.session->dispatch_kickoff(retry.desk_action,c.revision,error);
    require(result.kind==TurnDeskKind::WaitingAtDesk&&!error.empty(),"unclaimed driver accepts retry and explicitly waits for a real answer");
    same(snapshot(*c.session),before);same_driver(c.session->manager_join_checkpoint().driver,driver);
}
}
int main(int argc,char** argv){try{
    require(argc==3,"usage: pending_offer_desk_app_tests ORIGINAL_DATA QA_INPUT.FBM");
    if(!*argv[1]){std::puts("pending offer desk app tests skipped: Original data not configured");return 77;}
    missing_then_retry(argv[1],argv[2]);quoted_throw_then_retry(argv[1],argv[2]);partial_sale_rollback(argv[1],argv[2]);
    std::puts("pending offer desk app tests: real Native controller load, waiting rollback/retry, staged oracle and hook restoration passed");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"pending_offer_desk_app_tests: %s\n",e.what());return 1;}}
