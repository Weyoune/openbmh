// SPDX-License-Identifier: GPL-3.0-only
#include <algorithm>
#include <filesystem>
#include <cstdio>
#include <stdexcept>
#include "app/controller.hpp"
#include "game/save.hpp"

using namespace bmh;
using namespace bmh::ui;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Harness {
    std::filesystem::path data, saves;
    StartupFlow flow;
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision=1;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio;
    Harness(const std::filesystem::path& d,const std::filesystem::path& input)
        :data(d),saves(input.parent_path()),flow(d,saves),session(std::make_unique<app::CareerSession>(d,2454678578u,input)) {
        session->game().set_seed(410903);
        flow.click(214,215);
        check(flow.send_key("ENTER").request==FlowRequest::file_action,"load dialog binds native fixture");
        flow.attach_loaded_game(session->desktop_projection(revision),session->preparation_projection(revision));
    }
    std::string serve(const FlowResult& r) {
        return app::serve_flow_request(flow,r,session,data,saves,1,revision,playback,audio,false);
    }
    void open() {
        const auto& items=flow.desktop()->state().items;
        const auto it=std::find_if(items.begin(),items.end(),[](const auto& i){return i.kind==29&&!i.disabled;});
        check(it!=items.end(),"native desk has new-manager menu");
        check(serve(flow.click(it->x+it->width/2,it->y+it->height/2)).empty(),"open production join controller");
        check(flow.screen_name()=="joining_options","Original options modal opens above existing desk");
    }
    FlowResult options(int additions) {
        for(int i=0;i<additions;++i)flow.click(260,135);
        auto request=flow.click(120,100,true);
        check(request.request==FlowRequest::join_options,"RMT emits pending join options");
        return request;
    }
    FlowResult choose_hsv() {
        check(flow.screen_name()=="manager_selection","Original manager dialog");
        flow.click(205,95);
        check(flow.screen_name()=="club_picker","Original club picker");
        flow.click(145,25); // Native BEFORENP HSV identity is selection index 2.
        flow.click(5,5,true);
        flow.click(5,5,true);
        check(flow.screen_name()=="manager_confirm","Original JA/NEIN confirmation");
        auto request=flow.click(90,170);
        check(request.request==FlowRequest::join_manager && request.join_manager.selection_club==2,
              "HSV choice uses current engine club order");
        return request;
    }
};
void test_cancel_and_replay(const std::filesystem::path& data,const std::filesystem::path& input) {
    Harness h(data,input);
    const auto original=save_stream(h.session->state());
    const auto rand=h.session->rand(); const auto game=h.session->game();
    const auto inert=[&] {
        check(save_stream(h.session->state())==original&&h.revision==1&&
              h.session->rand().seed()==rand.seed()&&h.session->rand().calls()==rand.calls()&&
              h.session->game().seed()==game.seed()&&h.session->game().calls()==game.calls(),
              "draft/cancel/forged input preserves state and both RNGs");
    };
    h.open(); check(h.serve(h.flow.send_key("ESC")).empty(),"cancel options"); inert();
    check(!h.flow.desktop()->pending(),"cancel clears local pending menu");
    h.open(); auto options=h.options(2);
    auto forged=options; ++forged.join_options.difficulty;
    check(h.serve(forged).empty(),"forged options inert"); inert();
    check(h.serve(options).empty(),"begin two-manager draft"); inert();
    auto choice=h.choose_hsv(); auto altered=choice; ++altered.join_manager.portrait;
    check(h.serve(altered).empty(),"forged choice inert"); inert();
    check(h.serve(choice).empty(),"first provisional manager accepted"); inert();
    check(h.flow.screen_name()=="manager_selection","next choice receives fresh order");
    check(h.serve(choice).empty(),"consumed provisional choice cannot replay"); inert();
    check(h.serve(h.flow.send_key("ESC")).empty(),"cancel after provisional Core init"); inert();
    check(h.flow.screen_name()=="desktop"&&!h.flow.desktop()->pending(),"cancel returns intact desk");
}
void test_options_and_multiple(const std::filesystem::path& data,const std::filesystem::path& input) {
    Harness noadd(data,input);
    const auto w=noadd.session->rand();const auto g=noadd.session->game();
    const auto mail=noadd.session->state().mailbox;
    noadd.open();noadd.flow.click(260,85);auto option=noadd.options(0);
    check(noadd.serve(option).empty()&&noadd.flow.screen_name()=="desktop"&&noadd.revision==2&&
          noadd.session->state().manager_teams.size()==1&&noadd.session->state().level==5,
          "Source permits difficulty-only acceptance without adding a manager");
    check(noadd.session->rand().seed()==w.seed()&&noadd.session->rand().calls()==w.calls()&&
          noadd.session->game().seed()==g.seed()&&noadd.session->game().calls()==g.calls()&&
          noadd.session->state().mailbox==mail,"same-count options has no draws or startup welcomes");
    Harness multi(data,input);
    multi.open();check(multi.serve(multi.options(2)).empty(),"begin two additions");
    check(multi.serve(multi.choose_hsv()).empty(),"first addition stays provisional");
    multi.flow.click(5,5,true);
    auto second=multi.flow.click(90,170);
    check(second.request==FlowRequest::join_manager&&second.join_manager.slot==2&&
          second.join_manager.roster_revision==2,"second addition uses refreshed source club order");
    check(multi.serve(second).empty()&&multi.flow.screen_name()=="desktop"&&multi.revision==2&&
          multi.session->state().manager_teams.size()==3,"multiple additions commit atomically once");
    const auto stream=save_stream(multi.session->state());
    check(multi.serve(second).empty()&&stream==save_stream(multi.session->state()),"multiple confirmation replay inert");
}
void test_accept_reload(const std::filesystem::path& data,const std::filesystem::path& input) {
    Harness h(data,input);
    const auto before=h.flow.desktop()->state();
    check(before.next_competition.has_value()&&before.next_opponent.has_value(),
          "loaded current event supplies Source desk defaults before manager join");
    h.open();auto options=h.options(1);check(h.serve(options).empty(),"accept join options");
    auto choice=h.choose_hsv();check(h.serve(choice).empty(),"commit original joining flow");
    check(h.flow.screen_name()=="desktop"&&h.revision==2,"join commits once and returns desk");
    auto joined=save_stream(h.session->state());
    const auto& desk=h.flow.desktop()->state();
    check(desk.options.managers==2&&desk.manager_slot==0&&desk.day_counter==before.day_counter,
          "join retains current active desk and calendar");
    check(desk.next_competition.has_value()&&desk.next_opponent.has_value(),
          "join preserves the existing manager's event-local Source callargs after club-slot reorder");
    check(desk.next_competition==before.next_competition&&desk.next_opponent==before.next_opponent&&
          desk.next_match_away==before.next_match_away,
          "join does not recompute existing manager callargs from the changed roster");
    check(h.session->state().manager_teams==std::vector<int>({15,10})&&
          h.session->state().club_ids.at(10)==193,"HSV identity moves from runtime club2 to10");
    check(desk.managers[0].club_identity_key==before.managers[0].club_identity_key&&
          desk.managers[0].name==before.managers[0].name&&desk.managers[0].portrait==before.managers[0].portrait,
          "previous manager identity survives");
    check(h.serve(choice).empty()&&save_stream(h.session->state())==joined&&h.revision==2,
          "committed confirmation replay is inert");
    auto copied=h.session->rand();
    const auto file=std::filesystem::temp_directory_path()/"openbmh-manager-join-app.FBM";
    write_savegame(file,make_savegame(h.session->state(),copied));
    app::CareerSession reload(data,123,file);
    check(reload.state().manager_teams==h.session->state().manager_teams&&reload.state().date==h.session->state().date,
          "native FBM reload preserves added managers/calendar");
    uint64_t loaded_revision=1;
    auto loaded_desk=reload.desktop_projection(loaded_revision);
    DeskRequest loaded_advance; loaded_advance.kind=DeskRequestKind::advance_turn;
    loaded_advance.manager_slot=loaded_desk.manager_slot;loaded_advance.revision=loaded_revision;
    std::string loaded_error;
    auto loaded_result=reload.dispatch_kickoff(loaded_advance,loaded_revision,loaded_error);
    check(loaded_error.empty()&&reload.desktop_projection(loaded_revision).manager_slot==1,
          "loaded joining save continues through actual manager handoff");
    std::filesystem::remove(file);
    const auto& items=h.flow.desktop()->state().items;
    const auto advance=std::find_if(items.begin(),items.end(),[](const auto& i){return !i.disabled&&(i.kind==12||(i.kind==58&&i.open));});
    check(advance!=items.end(),"Original desk advance control exists");
    auto next=h.flow.click(advance->x+advance->width/2,advance->y+advance->height/2);
    check(next.request==FlowRequest::kickoff,"Weiter creates actual advance request");
    check(h.serve(next).empty(),"Weiter production controller handoff");
    check(h.flow.desktop()->state().manager_slot==1&&h.flow.desktop()->state().runtime_club==10&&
          h.flow.desktop()->state().day_counter==before.day_counter,"Weiter hands same event to HSV manager");
    check(h.flow.desktop()->state().next_competition==34&&
          h.flow.desktop()->state().next_opponent==-1&&
          !h.flow.desktop()->state().next_match_away,
          "newly joined manager keeps legacy routine's event-local default callargs until the next event");
}
}
int main(int argc,char**argv) {
    try { if(argc<3)throw std::runtime_error("usage: data INPUT.FBM");
        test_cancel_and_replay(argv[1],argv[2]);test_options_and_multiple(argv[1],argv[2]);test_accept_reload(argv[1],argv[2]);
        std::puts("manager join production controller: OK");return 0;
    } catch(const std::exception&e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
