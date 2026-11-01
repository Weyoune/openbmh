// SPDX-License-Identifier: GPL-3.0-only
#include "app/controller.hpp"
#include "formats/executable_layout.hpp"
#include "game/save.hpp"
#include "game/messages.hpp"
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
    auto finances=session->state();
    const int manager=int(finances.memory.u8(layout::ActiveManager));
    const size_t record=size_t(manager)*0xb0c;
    const auto record_money=[&](size_t offset,int32_t value) {
        for(int byte=0;byte<4;++byte)finances.manager_records.at(record+offset+byte)=uint8_t(uint32_t(value)>>(byte*8));
    };
    record_money(0x314,1500000000);record_money(0x318,1500000000);
    for(uint32_t i=0;i<6;++i) {
        finances.memory.set32(layout::FundBlock+i*0x1c,1000000);
        finances.memory.set16(layout::Advertising_OriginalShareAdjustment_RuntimeField+uint32_t(manager)*2+i*0x1c,i<2?1500:0);
        finances.memory.set32(layout::Bank_OriginalToUtf8_RuntimeField5+uint32_t(manager)*4+i*0x1c,0);
    }
    for(size_t i=0;i<12;++i) {
        const size_t at=i*0x5c;
        const uint16_t price=i==0?60000:1,held=i==0?50000:0;
        finances.stocks.at(at+0x4e)=uint8_t(price);finances.stocks.at(at+0x4f)=uint8_t(price>>8);
        finances.stocks.at(at+0x50+size_t(manager)*2)=uint8_t(held);
        finances.stocks.at(at+0x51+size_t(manager)*2)=uint8_t(held>>8);
        for(size_t byte=0;byte<4;++byte)finances.stocks.at(at+4+size_t(manager)*4+byte)=0;
    }
    for(uint32_t i=0;i<5;++i) {
        const uint32_t at=layout::Analytics_SourceAssetMaskE_RuntimeField+uint32_t(manager)*0x78+i*0x18;
        finances.memory.set32(at,i<2?1500000000:0);finances.memory.set32(at+0x10,0);
    }
    finances.memory.set32(layout::CurrencyMultiplier,1);finances.memory.set32(layout::CurrencyDivisor,1);
    const auto finance_native=save_stream(finances);
    const auto exe=load_le(asset_path(data,"BMH.exe"));
    const auto bank=bank_from_game(finances,exe,revision);
    check(bank.finance_values[2]==3000000000LL&&bank.finance_values[3]==3000000000LL&&
          bank.finance_values[4]==3000000000LL&&bank.finance_values[5]==6000000000LL&&
          bank.finance_gains[0]==3000000000LL&&bank.finance_gains[1]==3000000000LL,
          "bank display totals wrapped although individual saved amounts fit their native words");
    check(bank.finance_value_text[5].find("6.000.000.000")!=std::string::npos,
          "bank net worth text truncated its wide projection");
    check(save_stream(finances)==finance_native,"read-only bank totals changed the save representation");
    finances.memory.set32(layout::CurrencyMultiplier,2);finances.memory.set32(layout::CurrencyDivisor,3);
    check(bank_from_game(finances,exe,revision).finance_value_text[5].find("4.000.000.000")!=std::string::npos,
          "bank aggregate display uses the wrong native currency units");
    std::cout<<"Edition career startup, original calendar/rescheduling navigation, dialog mode and wide bank display passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
