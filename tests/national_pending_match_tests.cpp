// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include "app/career_session.hpp"
#include "game/save.hpp"
#include "national_coach_desk_pending_fixture.hpp"
using namespace bmh;
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
auto request(const GameState& g){return PendingOfferDeskRequest{
    PendingOfferDeskRuntimeMode::normal_interactive,int(g.manager_teams.size()),int(g.memory.u8(::bmh::layout::DeskPosition))};}
TurnAdvanceResult advance(app::CareerSession& s,uint64_t& revision){
    std::string error;
    return s.dispatch_kickoff({ui::DeskRequestKind::advance_turn,int(s.state().memory.u8(::bmh::layout::ActiveManager)),
        -1,-1,-1,revision},revision,error);
}
void test(const std::filesystem::path& data,const std::filesystem::path& input){
    app::CareerSession session(data,8128,input);
    // Explicit test decision policy for the June->July contract boundary reached
    // after this match. Production still owns the actual contract screen.
    session.set_contract_expiry_presenter([](GameState&,WatcomRandom&,int,int){return 0;});
    session.set_pending_offer_entry_policy(app::PendingOfferEntryPolicy::normal_interactive);
    const auto checkpoint=session.manager_join_checkpoint();const auto& original=checkpoint.state;
    check(original.date==1453 && original.manager_teams==std::vector<int>{kNoTeam} &&
        original.memory.u8(::bmh::layout::CoachActive)==1,"normal fixture must be the genuine Germany-Italy takeover");
    // The Original searches these club players in the national squad and
    // displays error58. OpenBMH deliberately keeps its final cleanup/state,
    // without requiring the user to acknowledge this known context mismatch.
    const auto range=squad_range(kNoTeam);
    int owned=0;
    for(int raw=0;raw<4;++raw){const auto word=original.memory.u32(::bmh::layout::OfferSlots+raw*4);
        if(word==0xffffffffu)continue;
        check((word&31)==0,"fixture offer belongs to inherited manager0");++owned;
        for(size_t i=range.first;i<range.first+range.count;++i)
            check((original.squad_slots.at(i)&0xfff)!=((word>>10)&0xfff),"fixture offer must be absent from national squad");
    }
    check(owned==3,"fixture has three actual club offers, not an empty batch");
    auto actual=original,oracle=original;auto game=checkpoint.game;auto random=checkpoint.rand;
    int acknowledgements=0;PendingOfferHooks hooks;
    hooks.program_error=[&](const GameState&,uint16_t){++acknowledgements;};
    hooks.answer=[](const auto&,const auto&)->PendingOfferAnswer{throw std::runtime_error("club salary must not resolve in spare record4");};
    MinuteReport report;const auto result=process_natural_pending_offers_after_desk(actual,request(actual),game,random,hooks,report);
    check(result.completed && !report.unsupported && result.processed==0 && acknowledgements==0,
        "verified club offers in national context clean up without error58");
    for(int raw=0;raw<4;++raw){oracle.memory.set32(::bmh::layout::OfferSlots+raw*4,0xffffffffu);oracle.squad_slots.at(0xb70+40+raw)=0xfff;}
    oracle.memory.set8(::bmh::layout::DeskPosition,1);
    check(save_stream(actual)==save_stream(oracle) && game.seed()==checkpoint.game.seed() &&
        game.calls()==checkpoint.game.calls() && random.seed()==checkpoint.rand.seed() &&
        random.calls()==checkpoint.rand.calls(),"Source oracle matches entire Native and both RNGs");
    // Evidence guards: no blanket suppression for damaged pool/owner/club data.
    for(int variant=0;variant<4;++variant){
        auto damaged=original;
        if(variant==0)damaged.squad_slots.at(0xb70+40)=0xfff;
        if(variant==1)damaged.memory.set8(::bmh::layout::OriginalManagerCount,0);
        if(variant==2){damaged.manager_records.at(4*0xb0c+0x290)=0x65;damaged.manager_records.at(4*0xb0c+0x291)=1;}
        if(variant==3){const auto club=squad_range(29);for(size_t i=club.first;i<club.first+club.count;++i)
            if((damaged.squad_slots[i]&0xfff)==2539)damaged.squad_slots[i]=0xfff;}
        const auto native=save_stream(damaged);auto g=checkpoint.game;auto w=checkpoint.rand;MinuteReport rejected;
        const auto r=process_natural_pending_offers_after_desk(damaged,request(damaged),g,w,{},rejected);
        check(!r.completed && rejected.unsupported && save_stream(damaged)==native &&
            g.seed()==checkpoint.game.seed() && g.calls()==checkpoint.game.calls() &&
            w.seed()==checkpoint.rand.seed() && w.calls()==checkpoint.rand.calls(),
            "unverified club-context offer retains diagnostics with no state/RNG effects");
    }
    // Unknown/mismatched offers still require a diagnostic, with App rollback.
    // They must not be swallowed by the narrowly qualified club-context case.
    auto invalid=checkpoint;
    invalid.state.memory.set32(::bmh::layout::OfferSlots,0x80000000u|(4094u<<10));
    session.restore_manager_join_checkpoint(invalid);
    const auto invalid_native=save_stream(session.state());uint64_t revision=1;
    auto blocked=advance(session,revision);
    check(blocked.kind==TurnDeskKind::WaitingAtDesk && revision==1 &&
        save_stream(session.state())==invalid_native,"unknown offer without presenter remains retryable and mutation-free");
    session.set_pending_offer_hooks(hooks);
    auto throwing=hooks;throwing.program_error=[](const auto&,uint16_t){throw std::runtime_error("ACK interrupted");};
    session.set_pending_offer_hooks(throwing);blocked=advance(session,revision);
    check(blocked.kind==TurnDeskKind::WaitingAtDesk && revision==1 &&
        save_stream(session.state())==invalid_native,"interrupted real diagnostic preserves the entire staged state");
    // The same real loaded match can have an old-club player present in the
    // national squad. Explicit control, no changes to the original Native input.
    for(int failure=0;failure<3;++failure){
        auto present=checkpoint;
        const auto offered=national_pending_control::prepare(present.state,0);
        session.restore_manager_join_checkpoint(present);
        const auto native=save_stream(session.state());const auto g=session.game();const auto w=session.rand();
        const auto cursor=session.manager_join_checkpoint().driver;uint64_t failed_revision=1;
        PendingOfferHooks unavailable;
        if(failure!=0)unavailable.answer=[&](const auto&,const auto& q){
            check(q.clicked_source_slot==offered.anchor && q.manager==0,"match offer preserves actual national anchor");
            if(failure==2)throw std::runtime_error("controlled presenter interruption");
            return PendingOfferAnswer::unsupported;
        };
        session.set_pending_offer_hooks(unavailable);const auto failed=advance(session,failed_revision);
        const auto after=session.manager_join_checkpoint().driver;
        check(failed.kind==TurnDeskKind::WaitingAtDesk && failed_revision==1 && save_stream(session.state())==native &&
            session.game().seed()==g.seed() && session.game().calls()==g.calls() &&
            session.rand().seed()==w.seed() && session.rand().calls()==w.calls() &&
            after.stage==cursor.stage && after.manager_position==cursor.manager_position && after.revision==cursor.revision &&
            after.next_event_id==cursor.next_event_id && after.loaded_matchday_resume==cursor.loaded_matchday_resume,
            "present national offer failure rolls back complete Native, G/W, revision and driver cursor");
    }
    session.restore_manager_join_checkpoint(checkpoint);
    session.set_pending_offer_hooks(hooks);acknowledgements=0;
    const auto next=advance(session,revision);
    std::cout<<"kickoff kind="<<int(next.kind)<<" date="<<session.state().date<<" ACKs="<<acknowledgements<<" reason="<<next.reason<<'\n';
    check((next.kind==TurnDeskKind::NextDesk || next.kind==TurnDeskKind::SeasonEndDesk) && session.state().date>1453 && acknowledgements==0,
        "normal interactive loaded match advances with no spurious error ACKs");
    const auto file=std::filesystem::temp_directory_path()/
        ("openbmh-national-pending-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".FBM");
    std::string error;check(session.save_game(file,0,false,error),error.c_str());
    app::CareerSession fresh(data,8128,file);
    check(load_savegame(file).checksum_ok && save_stream(fresh.state())==save_stream(session.state()),
        "normal manual post-match save survives a fresh load with identical Native state");
    std::filesystem::remove(file);
}
void retained_owner_match(const std::filesystem::path& data,const std::filesystem::path& input){
    app::CareerSession session(data,8128,input);
    session.set_contract_expiry_presenter([](GameState&,WatcomRandom&,int,int){return 0;});
    session.set_pending_offer_entry_policy(app::PendingOfferEntryPolicy::normal_interactive);
    PendingOfferHooks hooks;
    hooks.answer=[](const auto&,const auto&){return PendingOfferAnswer::no;};
    hooks.select_transfer=[](const auto&,const auto&){return PendingOfferTransferChoice{
        PendingOfferTransferChoiceKind::decline,-1};};
    session.set_pending_offer_hooks(hooks);
    const auto checkpoint=session.manager_join_checkpoint();const auto& g=checkpoint.state;
    check(g.date==28 && g.manager_teams==std::vector<int>{kNoTeam} &&
        g.memory.u8(::bmh::layout::CoachActive)==1 && g.memory.u8(::bmh::layout::OriginalManagerCount)==2 &&
        g.memory.u8(::bmh::layout::ActiveManager)==0 && g.memory.u16(::bmh::layout::NextDeskManager)==1 &&
        !checkpoint.driver.groups.empty() && !checkpoint.driver.national_nomination_desk,
        "GUI checkpoint is an actual national match with retained owner1, not a nomination-only desk");
    for(int variant=0;variant<2;++variant){
        auto invalid=checkpoint;
        if(variant==0)invalid.state.memory.set16(::bmh::layout::NextDeskManager,2); // Outside the retained two-manager roster.
        session.restore_manager_join_checkpoint(invalid);
        const auto before=save_stream(session.state());const auto game=session.game();const auto random=session.rand();
        uint64_t revision=1;std::string error;
        const auto rejected=session.dispatch_kickoff({ui::DeskRequestKind::advance_turn,
            variant==1?1:0,-1,12,-1,revision},revision,error);
        check((rejected.kind==TurnDeskKind::Failed || rejected.kind==TurnDeskKind::WaitingAtDesk) &&
            revision==1 && save_stream(session.state())==before &&
            session.game().seed()==game.seed() && session.game().calls()==game.calls() &&
            session.rand().seed()==random.seed() && session.rand().calls()==random.calls(),
            "invalid retained owner or runtime request remains rejected without Native/G/W/revision effects");
    }
    session.restore_manager_join_checkpoint(checkpoint);uint64_t revision=1;
    const auto next=advance(session,revision);
    check(next.kind==TurnDeskKind::NextDesk && session.state().date>g.date,
        "retained owner1 no longer rejects a valid national kickoff after pending cleanup");
    const auto completed=session.manager_join_checkpoint().driver;
    check(completed.last_results.valid && completed.last_results.date==g.date &&
        !completed.last_results.groups.empty(),
        "actual national match is simulated and recorded, never skipped as a nomination day");
    const auto file=std::filesystem::temp_directory_path()/
        ("openbmh-national-retained-owner-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".FBM");
    std::string error;check(session.save_game(file,session.state().memory.u8(::bmh::layout::ActiveManager),false,error),error.c_str());
    app::CareerSession fresh(data,8128,file);
    check(load_savegame(file).checksum_ok && save_stream(fresh.state())==save_stream(session.state()),
        "post-match ordinary save reloads the complete Native state");
    std::filesystem::remove(file);
}
}
int main(int argc,char**argv){try{if(argc!=4)return 2;if(!*argv[1])return 0;test(argv[1],argv[2]);
    retained_owner_match(argv[1],argv[3]);
    std::cout<<"National club-context cleanup: no spurious ACKs; real diagnostic rollback, kickoff and normal save/reload passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
