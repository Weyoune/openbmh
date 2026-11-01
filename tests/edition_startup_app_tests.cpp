// SPDX-License-Identifier: GPL-3.0-only
#include "app/controller.hpp"
#include "formats/executable_layout.hpp"
#include "game/save.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace bmh;
using namespace bmh::ui;
namespace { void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);} }
int main(int argc,char** argv)try {
    if(argc<2||!*argv[1])return 77;
    const std::filesystem::path data=argv[1];
    StartupFlow flow(data);
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision=0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio;
    const auto serve=[&](const FlowResult& result) {
        check(app::serve_flow_request(flow,result,session,data,data,0x12345678,
            revision,playback,audio,false).empty(),"production startup/navigation failed");
    };
    flow.click(150,215);flow.click(5,225,true);flow.click(5,225,true);
    serve(flow.click(90,169));
    check(session&&flow.screen_name()=="desktop","edition did not reach an actual career desk");
    check(session->state().memory.has(layout::CompactDialogFrame)&&
          session->state().memory.u8(layout::CompactDialogFrame)==0,
          "setup retained the boot-only black dialog mode");
    const auto native=save_stream(session->state());
    const auto game=session->game();const auto random=session->rand();
    const auto before_revision=revision;
    for(int kind:{8,7}) {
        const auto& items=flow.desktop()->state().items;
        const auto it=std::find_if(items.begin(),items.end(),[&](const auto& item){return item.kind==kind&&!item.disabled;});
        check(it!=items.end(),"edition lacks the enabled original calendar menu");
        serve(flow.click(it->x+it->width/2,it->y+it->height/2));
        check(flow.calendar()&&flow.screen_name()==(kind==8?"calendar":"reschedule_calendar"),
              "original calendar menu did not open");
        check(flow.render().pixels.size()==320*240,"calendar did not render a complete original frame");
        serve(flow.send_key("ESC"));
        check(flow.screen_name()=="desktop"&&!flow.desktop()->pending(),"calendar did not return its desk owner");
    }
    check(save_stream(session->state())==native&&revision==before_revision&&
          session->game().seed()==game.seed()&&session->game().calls()==game.calls()&&
          session->rand().seed()==random.seed()&&session->rand().calls()==random.calls(),
          "opening calendars changed the career or random streams");
    std::cout<<"Edition career startup, original calendar/rescheduling navigation and dialog mode passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
