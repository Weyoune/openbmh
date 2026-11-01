// SPDX-License-Identifier: GPL-3.0-only
#include "app/controller.hpp"
#include "formats/native_save_version.hpp"
#include "game/save.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace bmh;using namespace bmh::ui;
namespace {void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}}
int main(int argc,char** argv)try {
    if(argc<3||!*argv[1]||!*argv[2])return 77;
    const std::filesystem::path data=argv[1],input=argv[2];const auto unchanged=read_file(input);
    const auto container=load_savegame(input);require_native_savegame(container);
    app::CareerSession imported(data,713,input);const auto desk=imported.desktop_projection(1);
    check(desk.manager_slot>=0&&!desk.manager_name.empty(),"unchanged external save did not produce an actual career desk");
    const auto exe=load_le(asset_path(data,"BMH.exe"));
    check(load_reference_desk(input,exe).manager_slot==desk.manager_slot,"read-only desktop importer rejects an accepted version");
    check(load_preparation_reference(input,exe).desktop.manager_slot==desk.manager_slot,"preparation importer rejects an accepted version");
    const auto directory=std::filesystem::temp_directory_path()/("openbmh-native-import-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    try {
        std::filesystem::copy_file(input,directory/"IMPORT.FBM");
        SaveLoadScreen browser(data,directory,SaveLoadMode::load,desk.manager_slot,1,Image(320,240));
        check(browser.info_press(80,45)&&browser.info_open(),"load-list information rejects the accepted original save");
        StartupFlow flow(data,directory);std::unique_ptr<app::CareerSession> session;uint64_t revision=0;
        openbmh::audio::Playback playback;std::optional<uint64_t> audio;
        flow.click(214,215);
        check(flow.screen_name()=="save_load","setup did not open the actual game browser");
        const auto request=flow.send_key("ENTER");check(request.request==FlowRequest::file_action,"browser did not emit the original save load request");
        const auto result=app::serve_flow_request(flow,request,session,data,directory,713,revision,playback,audio,false);
        check(result.empty()&&session&&flow.screen_name()=="desktop","shared production controller rejects the original version");
        const auto output=directory/"ROUNDTRIP.FBM";std::string error;
        check(imported.save_game(output,desk.manager_slot,false,error),"regular session save rejected the imported original career");
        const auto saved=load_savegame(output);require_native_savegame(saved);
        app::CareerSession reloaded(data,911,output);
        check(reloaded.desktop_projection(1).manager_slot==desk.manager_slot&&
              save_stream(reloaded.state())==save_stream(imported.state()),
              "ordinary save/reload changed the imported career's native state");
        check(read_file(input)==unchanged,"original input save was changed");
        std::filesystem::remove_all(directory);
    }catch(...){std::filesystem::remove_all(directory);throw;}
    std::cout<<"Unchanged external "<<container.version<<" imported through CareerSession, desktop/preparation and load-list/controller; ordinary session save/reload passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
