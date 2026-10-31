// SPDX-License-Identifier: GPL-3.0-only
#include "formats/data_profile.hpp"
#include "formats/savestate.hpp"
#include "game/profile_runtime.hpp"
#include "game/state.hpp"
#include "game/weekly.hpp"
#include "game/schedule.hpp"
#include "formats/executable_layout.hpp"
#include <filesystem>
#include <algorithm>
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
    bmh::install_runtime_profile(expanded);
    check(bmh::squad_range(163).first==3912 && bmh::squad_range(163).count==24,
          "last international club does not have a full-size squad");
    check(bmh::squad_range(bmh::kNoTeam).first==4080 && bmh::kNoTeam==383,
          "national squad overlaps the expanded clubs/pool");
    check(bmh::slot_team(3935)==163 && bmh::slot_team(3936)==-1,
          "absolute slot ownership was truncated or misclassified");
    uint8_t sizes[9];std::copy(expanded.leagues.begin(),expanded.leagues.end(),sizes);
    bmh::set_league_sizes(sizes);
    check(bmh::league_table_teams(6)==20 && bmh::league_table_teams(5)==18,
          "England incorrectly shares the smaller foreign fixture table");
    bmh::install_runtime_profile(classic);std::copy(classic.leagues.begin(),classic.leagues.end(),sizes);bmh::set_league_sizes(sizes);
    check(bmh::runtime_dimensions.record_teams==168,"Classic cup-only rating boundary changed");
    check(bmh::squad_range(90).first==2160 && bmh::squad_range(90).count==16 && bmh::kNoTeam==357,
          "switching back did not restore original squad conventions");
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
