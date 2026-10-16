// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include "app/career_session.hpp"
#include "game/month.hpp"
#include "game/save.hpp"
#include "game/turn_driver.hpp"
using namespace bmh;
namespace {
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
void install_contract_decisions(app::CareerSession& s){
    s.set_contract_expiry_presenter([](GameState& g,WatcomRandom& w,int m,int row){
        const auto demands=contract_demands(g,m,row,false,w);
        int term=-1;
        for(int i:{1,0,2,3})if(demands[size_t(i)]>0&&demands[size_t(i)]<=200000){term=i;break;}
        return term>=0&&contract_offer(g,m,row,term,demands[size_t(term)],demands[size_t(term)],false,true,w,nullptr)?1:0;
    });
}
TurnAdvanceResult advance(app::CareerSession& s,uint64_t& revision){
    std::string error;
    auto result=s.dispatch_kickoff({ui::DeskRequestKind::advance_turn,int(s.state().memory.u8(::bmh::layout::ActiveManager)),-1,-1,-1,revision},revision,error);
    check(result.kind!=TurnDeskKind::Failed&&result.kind!=TurnDeskKind::WaitingAtDesk,(error+" "+result.reason).c_str());
    return result;
}
void natural_reload(const std::filesystem::path& data,const std::filesystem::path& input){
    app::CareerSession session(data,1,input);install_contract_decisions(session);
    uint64_t revision=1;TurnAdvanceResult end;
    for(int n=0;n<20;++n){end=advance(session,revision);if(end.kind==TurnDeskKind::SeasonEndDesk)break;}
    check(end.kind==TurnDeskKind::SeasonEndDesk,"natural simulation did not reach season end");
    const auto& g=session.state();
    check(g.date==1461&&g.season==3&&g.memory.u8(::bmh::layout::CareerSession_RuntimeError_RuntimeField4)==1,"wrong natural closing date/season");
    check(g.memory.u8(::bmh::layout::DeskPosition)==0&&g.memory.u8(::bmh::layout::ActiveManager)==0,"natural closing desktop retains completed manager cursor");
    check(session.desktop_projection(revision).next_competition==32,"closing desktop lost Source Saisonende callarg");
    const auto file=std::filesystem::temp_directory_path()/"openbmh-season-end-desk-test.FBM";
    std::string error;check(session.save_game(file,0,true,error),error.c_str());
    check(load_savegame(file).checksum_ok,"normal closing save checksum");
    app::CareerSession reloaded(data,1,file);install_contract_decisions(reloaded);
    check(save_stream(reloaded.state())==save_stream(g),"normal closing save changed game state on fresh load");
    uint64_t fresh_revision=1;
    auto next=advance(reloaded,fresh_revision);
    check(next.kind==TurnDeskKind::NextDesk&&reloaded.state().season==4&&reloaded.state().season_day()==0,
          "fresh closing save cannot advance to next-season opening");
    std::filesystem::remove(file);
}
void multiplayer_loop(const std::filesystem::path& data){
    app::CareerSession session(data,8128);ui::SetupOptions options;options.managers=2;options.mode=1;
    std::string error;
    for(int m=0;m<2;++m){ui::ManagerChoice c;c.slot=m;c.master_club=m;c.name=m?"Beta":"Alpha";c.strength=2;
        check(session.confirm_manager(options,c,error),error.c_str());}
    ui::CareerInitializationResult init;check(session.finish(init,error),error.c_str());
    // Controlled Source annual context on a genuine initialized two-manager roster.
    auto g=session.state();g.date=365;g.memory.set8(::bmh::layout::CareerSession_RuntimeError_RuntimeField4,1);g.memory.set8(::bmh::layout::DeskPosition,0);g.memory.set8(::bmh::layout::ActiveManager,1);
    g.memory.set16(::bmh::layout::NextDeskManager,1);g.memory.set16(::bmh::layout::FixtureSecondDeskCursor,0);g.memory.set8(::bmh::layout::Desk_EnterDesk_RuntimeField,0);
    g.manager_records.at(0x176)=1;
    TurnDriver driver;driver.stage=TurnDriver::Stage::SeasonEndDesk;driver.manager_position=0;driver.session_id=1;driver.revision=1;
    int transitions=0;driver.season_pause=[&](const char*,const GameState&){++transitions;};
    GameRandom game(7);WatcomRandom random(9);MinuteReport report;
    const auto first=advance_to_next_desk(driver,g,{1,1},game,random,report);
    check(!report.unsupported&&first.kind==TurnDeskKind::SeasonEndDesk&&first.manager_slot==0,
          "annual transition ran before the remaining manager's closing desktop");
    check(g.date==365&&g.season==0&&transitions==0&&g.memory.u8(::bmh::layout::DeskPosition)==1&&g.manager_records.at(0x176)==0,
          "remaining closing manager lost date, ownership or sponsor settlement");
    auto copy=random;auto normal=make_savegame(g,copy);auto restored=GameState::from_save(SaveState(normal.stream));
    check(restored.memory.u8(::bmh::layout::DeskPosition)==1&&restored.memory.u8(::bmh::layout::ActiveManager)==0,
          "remaining closing manager is not persisted in the native save");
    const auto file=std::filesystem::temp_directory_path()/"openbmh-multiplayer-closing-test.FBM";
    write_savegame(file,normal);
    app::CareerSession fresh(data,1,file);
    check(fresh.state().memory.u8(::bmh::layout::DeskPosition)==1&&fresh.state().memory.u8(::bmh::layout::ActiveManager)==0&&
          fresh.desktop_projection(1).next_competition==32,
          "fresh multiplayer closing save loses remaining manager or annual context");
    std::filesystem::remove(file);
    const auto stable=save_stream(g);const auto calls=random.calls();
    const auto stale=advance_to_next_desk(driver,g,{1,1},game,random,report);
    check(stale.kind==TurnDeskKind::Failed&&save_stream(g)==stable&&random.calls()==calls&&transitions==0,
          "stale closing request repeated sponsor settlement or began the annual transition");
}
}
int main(int argc,char**argv){try{if(argc!=3)return 2;if(!*argv[1]){std::cout<<"season end desktop: skipped (Original data directory not configured)\n";return 77;}natural_reload(argv[1],argv[2]);multiplayer_loop(argv[1]);
    std::cout<<"Natural season end -> manual FBM -> fresh load -> next season; remaining manager and stale-request checks passed\n";
    return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
