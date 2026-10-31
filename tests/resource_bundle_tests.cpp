// SPDX-License-Identifier: GPL-3.0-only
#include "formats/resource_bundle.hpp"
#include "formats/le.hpp"
#include "formats/data_profile.hpp"
#include "formats/executable_layout.hpp"
#include "game/calendar.hpp"
#include "miniz.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace bmh;
void check(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
void bundle(const std::filesystem::path& path,const std::vector<std::pair<std::string,Bytes>>& files) {
    mz_zip_archive zip{};
    check(mz_zip_writer_init_heap(&zip,0,0)!=0,"synthetic ZIP initialization");
    for(const auto& [name,bytes]:files)check(mz_zip_writer_add_mem(&zip,name.c_str(),bytes.data(),bytes.size(),MZ_BEST_COMPRESSION)!=0,"synthetic ZIP entry");
    void* data=nullptr;size_t size=0;
    check(mz_zip_writer_finalize_heap_archive(&zip,&data,&size)!=0,"synthetic ZIP finalization");
    std::ofstream out(path,std::ios::binary);out.write(static_cast<const char*>(data),std::streamsize(size));
    mz_free(data);mz_zip_writer_end(&zip);check(bool(out),"synthetic ZIP write");
}
Bytes bytes(std::string text){return Bytes(text.begin(),text.end());}
int main()try {
    const auto root=std::filesystem::temp_directory_path()/("openbmh-bundle-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    Bytes data(0x20000,0);
    const auto put=[&](uint32_t address,uint8_t value){data.at(address-layout::GermanDataSegmentBegin)=value;};
    for(int league=0;league<9;++league)put(layout::LeagueSizes+uint32_t(league),league<5?18:12);
    for(int day=24;day<24+34*8;day+=8)put(layout::CompetitionCalendar+uint32_t(day*2),1);
    RuntimePolicy policy{2030,2,90,1,{{layout::FreeKickTakerLabel,"Test label"}}};
    save_runtime_image(LeImage::from_bytes(layout::GermanDataSegmentBegin,data),policy,root/"source.dat");
    const auto runtime=read_file(root/"source.dat");
    bundle(root/kCommunityBundle,{{"RUNTIME.DAT",runtime},{"OBMHPROF.INI",bytes("[OpenBMH]\nprofile=classic-plus-1\n")},{"KB/20.VGA",bytes("synthetic picture")}});
    std::filesystem::remove(root/"source.dat");
    check(!std::filesystem::exists(root/"BMH.exe"),"synthetic bundle accidentally requires an executable");
    check(resource_exists(root/"kb"/"20.vga") && read_file(root/"kb"/"20.vga")==bytes("synthetic picture"),"case-insensitive resource read");
    check(load_data_profile(root).extended(),"bundled profile metadata not found");
    const auto image=load_le(root/"BMH.exe");
    check(image.expanded_profile() && image.runtime_policy(),"data-only image did not bind the extended profile");
    check(!image.contains(layout::CalendarDateSetup) && !image.contains(layout::ForeignBadgeGateInstruction),"machine-code segment retained");
    check(calendar_epoch_from_executable(image).year==2030,"calendar still depends on original instructions");
    check(image.cstring(layout::FreeKickTakerLabel)=="Test label","resolved auxiliary label lost");
    check(image.u16(layout::TeamsWithSquads)==164,"standalone runtime roster dimensions differ");
    // Loose user-created files remain writable and are not replaced by bundle data.
    {std::ofstream out(root/"user.FBM");out<<"user save";}
    check(read_file(root/"user.FBM")==bytes("user save"),"bundle swallowed a loose user save");
    for(int kind=0;kind<2;++kind) {
        const auto bad=root/("bad-"+std::to_string(kind));std::filesystem::create_directory(bad);
        bundle(bad/kCommunityBundle,kind==0?std::vector<std::pair<std::string,Bytes>>{{"../escape",{1}}}:
            std::vector<std::pair<std::string,Bytes>>{{"DUP",{1}},{"dup",{2}}});
        bool rejected=false;try{resource_exists(bad/"anything");}catch(const FormatError&){rejected=true;}
        check(rejected,"unsafe or duplicate resource accepted");
    }
    auto broken=runtime;broken.pop_back();
    {std::ofstream out(root/"broken.dat",std::ios::binary);out.write(reinterpret_cast<const char*>(broken.data()),std::streamsize(broken.size()));}
    bool rejected=false;try{load_le(root/"broken.dat");}catch(const FormatError&){rejected=true;}
    check(rejected,"truncated runtime labels accepted");
    std::filesystem::remove_all(root);
    std::cout<<"Standalone data policies, bundled resources, paths and loose saves passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
