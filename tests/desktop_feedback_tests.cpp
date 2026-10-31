// SPDX-License-Identifier: GPL-3.0-only
#include "ui/desktop.hpp"
#include "formats/palette.hpp"
#include "formats/le.hpp"
#include "formats/fbm.hpp"
#include "formats/savestate.hpp"
#include "formats/executable_layout.hpp"
#include <filesystem>
#include <algorithm>
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
    state.drawer_handles=true;
    state.items={{2,20,20,32,25,0,0,0,false,false},{1,20,20,173,40,64,0,0,true,false}};
    state.layout_records[0][5]=2;
    state.layout_records[2][5]=1; state.layout_records[2][6]=0;
    state.layout_records[1][5]=0; state.layout_records[1][6]=2;
    state.layout_records[1][12]=64; state.layout_records[1][13]=0x80;
    DesktopScreen widget_drag(data,state);
    widget_drag.pointer_down(21,21,true);
    widget_drag.pointer_move(61,61);
    const auto widget_move=widget_drag.pointer_up(61,61,true);
    check(widget_move.kind==DeskRequestKind::change_layout && widget_move.layout_operation==DeskLayoutOperation::move_item,
          "right-button widget movement was replaced by closing");
    DesktopScreen close(data,state);
    capture(output,"close-before.ppm",close.render(),palette);
    const auto removal=close.pointer_down(21,21,false);
    check(removal.kind==DeskRequestKind::change_layout &&
          removal.layout_operation==DeskLayoutOperation::toggle_quickbar_drawer &&
          removal.layout_target_item==1,"left corner did not request widget removal");
    check(removal.layout_after[2*16+5]==0 && removal.layout_after[16+5]==0xff &&
          removal.layout_after[16+6]==0xff,"corner removal corrupted the remaining layout chain");
    check(close.state_json().find("\"dragging_item\":null")!=std::string::npos,
          "close corner incorrectly entered drag mode");
    check(!close.pointer_move(100,100),"close press turned into movement");
    auto removed=state; removed.revision=state.revision+1; removed.items.erase(removed.items.begin()+1);
    for(size_t i=0;i<54;++i)std::copy_n(removal.layout_after.begin()+i*16,16,removed.layout_records[i].begin());
    close.acknowledge(removed);
    check(close.pointer_up(21,21,false).kind==DeskRequestKind::none,
          "close release activated the icon underneath the removed widget");
    capture(output,"close-after.ppm",close.render(),palette);
    check(close.pointer_up(21,21,false).kind==DeskRequestKind::open_menu,
          "close release barrier consumed a subsequent independent click");
    state.layout_records={}; state.layout_records[0][5]=1;
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
