// SPDX-License-Identifier: GPL-3.0-only
#include "formats/native_save_version.hpp"
#include "formats/executable_layout.hpp"
#include "formats/savestate.hpp"
#include "game/save.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace bmh;
namespace {
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F function,const char* message){bool rejected=false;try{function();}catch(const FormatError&){rejected=true;}check(rejected,message);}
}
int main()try {
    GameState state;state.manager_records.resize(5*0xb0c);state.manager_teams={0};state.club_ids={0};state.club_names={'Q',0};
    state.memory.add_block(layout::ManagerCount,1);state.memory.set8(layout::ManagerCount,1);
    WatcomRandom random(713);const auto original=make_savegame(state,random);
    for(const char* version:{"V2.24","V3.7"}) {
        auto saved=original;saved.version=version;require_native_savegame(saved);
        const SaveState parsed(saved.stream);check(!parsed.blocks().empty(),"recognized synthetic native layout does not parse");
        auto truncated=saved;truncated.stream.pop_back();
        rejects([&]{require_native_savegame(truncated);(void)SaveState(truncated.stream);},"recognized version bypassed strict state/trailer validation");
        auto counts=DbCounts{};++counts.players;
        rejects([&]{require_native_savegame(saved);(void)SaveState(saved.stream,counts);},"recognized version bypassed installed database dimensions");
    }
    for(const char* version:{"V2.20","V3.70","v3.7","V9.99","O1.1","O1.2",""}) {
        auto saved=original;saved.version=version;
        rejects([&]{require_native_savegame(saved);},"unknown/different-layout native version was accepted");
    }
    check(supported_native_save_version("O1.1",true)&&!supported_native_save_version("V2.24",true)&&
          !supported_native_save_version("V3.7",true)&&!supported_native_save_version("V2.20",true),
          "expanded layout accepted a classic/English marker or rejected its own version");
    auto damaged=original;damaged.checksum_ok=false;
    try{require_native_savegame(damaged);throw std::runtime_error("checksum failure was accepted");}
    catch(const FormatError& error){check(std::string(error.what()).find("checksum mismatch")!=std::string::npos,"checksum failure was confused with version support");}
    auto unsupported=original;unsupported.version="V2.20";
    try{require_native_savegame(unsupported);throw std::runtime_error("different-layout version was accepted");}
    catch(const FormatError& error){check(std::string(error.what()).find("unsupported FBM version V2.20")!=std::string::npos,"version rejection lost its actual marker");}
    const auto path=std::filesystem::temp_directory_path()/("openbmh-version-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".FBM");
    auto version37=original;version37.version="V3.7";write_savegame(path,version37);const auto loaded=load_savegame(path);
    require_native_savegame(loaded);check(loaded.version=="V3.7"&&loaded.stream==original.stream,"synthetic V3.7 container roundtrip changed its state");
    auto bytes=read_file(path);bytes.back()^=1;{std::ofstream file(path,std::ios::binary|std::ios::trunc);file.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));}
    const auto corrupt=load_savegame(path);std::filesystem::remove(path);
    rejects([&]{require_native_savegame(corrupt);},"actual corrupt container checksum was accepted");
    std::cout<<"Known native save versions, separate expanded layout, strict dimensions/trailers and checksum/version diagnostics passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
