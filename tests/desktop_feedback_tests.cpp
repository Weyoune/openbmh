// SPDX-License-Identifier: GPL-3.0-only
#include "ui/desktop.hpp"
#include "formats/palette.hpp"
#include "formats/le.hpp"
#include "formats/fbm.hpp"
#include "formats/savestate.hpp"
#include "formats/executable_layout.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace bmh;
using namespace bmh::ui;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void capture(const std::filesystem::path& dir,const char* name,const Image& image,const Palette& palette){
    if(dir.empty())return;
    std::filesystem::create_directories(dir);
    std::ofstream out(dir/name,std::ios::binary);out<<"P6\n"<<image.width<<' '<<image.height<<"\n255\n";
    for(auto pixel:image.pixels){const auto c=palette.argb[pixel];for(int shift:{16,8,0})out.put(char(c>>shift));}
}
int main(int argc,char**argv)try{
    if(argc<2||!*argv[1])return 77;
    const std::filesystem::path data=argv[1],output=argc>2?argv[2]:"";
    const auto palette=load_palette(data/"M3PC.COL");
    if (argc > 3) {
        const auto file = load_savegame(argv[3]);
        check(file.checksum_ok,"reference save checksum invalid");
        const auto exe = load_le(data/"BMH.exe");
        const SaveState original(file.stream);
        const auto option_offset = original.block(layout::ManagerAutomation).offset;
        for (int selected=0; selected<=4; ++selected) {
            auto stream = file.stream;
            stream.at(option_offset)=uint8_t(selected);
            const SaveState saved(stream);
            auto projected = desk_from_save(saved,exe,1);
            check(projected.automation_formation.has_value()==(selected!=0),"formation visibility did not follow saved automation option");
            if(selected)for(size_t i=0;i<22;++i)
                check(projected.automation_formation->at(i)==saved.data(layout::Formations)[size_t(selected-1)*22+i],
                      "miniature did not use the selected saved formation");
            projected.items={{1,20,20,173,40,64,0,0,true,false}};
            const auto rendered = DesktopScreen(data,projected).render();
            const std::string name="saved-formation-"+std::to_string(selected)+".ppm";
            capture(output,name.c_str(),rendered,palette);
        }
    }
    DeskState state;state.options.managers=1;state.options.duration=5;state.options.mode=3;state.options.difficulty=2;
    state.options.league=true;state.options.world=true;
    state.manager_slot=0;state.runtime_club=0;state.manager_teams={0};state.manager_name="QA";state.club_name="QA";
    DeskManagerIdentity manager;manager.runtime_club=0;manager.club_identity_key=0;manager.name="QA";manager.league=0;manager.portrait=0;manager.badge=0;
    state.managers={manager};state.league=0;state.portrait=0;state.badge=0;
    state.items={{1,20,20,173,40,64,0,0,true,false}};
    state.automation[0]=1;state.automation_formation.emplace();state.automation_formation->fill(0);
    (*state.automation_formation)[1]=18;(*state.automation_formation)[12]=3;
    const auto first=DesktopScreen(data,state).render();
    check(first.pixels[size_t(32)*320+49]==0x27,"formation player not rendered at the original miniature coordinate");
    state.automation[0]=2;(*state.automation_formation)[1]=6;(*state.automation_formation)[12]=2;
    const auto second=DesktopScreen(data,state).render();
    check(first.pixels!=second.pixels,"changing selected formation left the monitor unchanged");
    capture(output,"formation-1.ppm",first,palette);capture(output,"formation-2.ppm",second,palette);
    state.items={{1,20,20,32,25,0,0,0,false,false}};state.drawer_handles=true;
    DesktopScreen drag(data,state);const auto normal=drag.render();
    const auto idle=drag.state_json();
    drag.pointer_down(25,25,true);
    check(drag.state_json()!=idle,"drag press did not invalidate the rendered desktop");
    drag.pointer_move(65,65);
    const auto moving=drag.render();
    check(moving.pixels[size_t(60)*320+60]==0x27 && moving.pixels[size_t(84)*320+91]==0x27,"moving icon lacks inclusive yellow outline");
    const auto request=drag.pointer_up(65,65,true);
    check(request.kind==DeskRequestKind::change_layout,"drag did not retain its layout transaction");
    check(drag.render().pixels==normal.pixels,"yellow preview survived release without acknowledgement");
    capture(output,"drag-normal.ppm",normal,palette);capture(output,"drag-active.ppm",moving,palette);
    std::cout<<"Automation formation changes and transient yellow drag border passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
