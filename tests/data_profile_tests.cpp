// SPDX-License-Identifier: GPL-3.0-only
#include "formats/data_profile.hpp"
#include "formats/savestate.hpp"
#include "formats/executable_layout.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
int main()try {
    bmh::DataProfile classic;classic.validate();
    check(classic.pool_first()==2928 && classic.national_slots_first()==3072 && classic.total_slots()==3150,"Classic roster boundaries changed");
    auto expanded=bmh::DataProfile::international_11();expanded.validate();
    check(expanded.counts.league_teams==164 && expanded.pool_first()==3936 && expanded.total_slots()==4158,"Expanded roster boundaries incorrect");
    check(expanded.leagues[6]==20 && expanded.counts.players<4095 && expanded.counts.clubs<=512,"Expanded profile exceeds compact IDs");
    for (const auto& profile : {classic, expanded}) {
        const auto blocks = bmh::save_layout(profile.counts, true);
        const auto& end = blocks.back();
        bmh::Bytes stream(end.offset + end.size, 0);
        for (const auto& block : blocks)
            if (block.address == bmh::layout::ClubSeasonRecords && !block.heap) stream.at(block.offset) = 1;
        stream.insert(stream.end(), 8, 0); // Four empty, independent synthetic string pools.
        bmh::SaveState decoded(stream, profile.counts);
        check(decoded.block(bmh::layout::SquadSlots).size == profile.total_slots() * 2,
              "squad block ignored configured profile counts");
        check(decoded.block(bmh::layout::PlayerConditionTechniqueForm).size == profile.counts.players * 4,
              "player block ignored configured profile counts");
    }
    auto invalid=expanded;invalid.counts.players=4095;bool rejected=false;
    try{invalid.validate();}catch(const bmh::FormatError&){rejected=true;}
    check(rejected,"Player-ID sentinel collision accepted");
    const auto dir=std::filesystem::temp_directory_path()/"openbmh-profile-synthetic-test";
    std::filesystem::create_directories(dir);
    check(!bmh::load_data_profile(dir).extended(),"Original installation did not select Classic");
    {std::ofstream f(dir/"OBMHPROF.INI");f<<"[OpenBMH]\nprofile=classic-plus-1\n";}
    check(bmh::load_data_profile(dir).counts.n66==4104,"Profile marker not recognized");
    {std::ofstream f(dir/"OBMHPROF.INI");f<<"[OpenBMH]\nprofile=unknown\n";}
    rejected=false;try{bmh::load_data_profile(dir);}catch(const bmh::FormatError&){rejected=true;}
    std::filesystem::remove_all(dir);check(rejected,"Unknown profile silently became Classic");
    std::cout<<"Classic and expanded profile dimensions, discovery and limits passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
