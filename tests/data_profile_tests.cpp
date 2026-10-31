// SPDX-License-Identifier: GPL-3.0-only
#include "formats/data_profile.hpp"
#include "formats/savestate.hpp"
#include "formats/le.hpp"
#include "formats/badge_policy.hpp"
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
        check(decoded.block(bmh::layout::ManagerMatchHistory).size == profile.counts.n42 * 50,
              "opponent history truncated in extended saves");
        check(decoded.block(bmh::layout::ClubKitSelectors).size == profile.counts.clubs &&
              decoded.block(bmh::layout::ClubShortNameOffsets).size == profile.counts.n42,
              "extended visual tables truncated");
        check(decoded.block(bmh::layout::DeskRecords_Teams_RuntimeField).size == profile.counts.league_teams * 2,
              "last international club has no fair-play counter");
        bmh::StaticMemory memory(decoded);
        const auto last = bmh::layout::DeskRecords_Teams_RuntimeField + (profile.counts.league_teams - 1) * 2;
        memory.set16(last, 0x0102);
        check(memory.u16(last) == 0x0102, "last club card counters are not addressable");
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
    bmh::Bytes image_bytes(0x20000, 0);
    const auto at = [&](uint32_t address) -> uint8_t& { return image_bytes.at(address - bmh::layout::GermanDataSegmentBegin); };
    for (int league = 0; league < 9; ++league) at(bmh::layout::LeagueSizes + uint32_t(league)) = classic.leagues[size_t(league)];
    for (int day = 24; day < 24 + 34 * 8; day += 8) at(bmh::layout::CompetitionCalendar + uint32_t(day * 2)) = 1;
    bmh::save_image(bmh::LeImage::from_bytes(bmh::layout::GermanDataSegmentBegin, image_bytes), dir / "BMH.exe");
    const auto before = bmh::read_file(dir / "BMH.exe");
    const auto image = bmh::load_le(dir / "BMH.exe");
    check(image.expanded_profile() && image.u16(bmh::layout::NoTeamSquadStart) == 194,
          "national name base confused with absolute national squad slot");
    check(image.u32(bmh::layout::ProfileNationalSlotsFirst) == 4080 && image.u32(bmh::layout::ProfilePoolFirst) == 3936,
          "verified scalar overlay disagrees with runtime profile");
    check(image.u8(bmh::layout::LeagueSizes + 6) == 20 && image.u16(bmh::layout::TeamsWithSquads) == 164,
          "UI executable view has stale league/roster dimensions");
    int domestic_days=0,foreign_days=0;
    for(int day=0;day<365;++day) {
        const auto flags=image.u16(bmh::layout::CompetitionCalendar+uint32_t(day*2));
        domestic_days += (flags & 1) != 0;
        foreign_days += (flags & 16) != 0;
    }
    check(domestic_days==34 && foreign_days==38,"extended calendar changed domestic dates or omitted English fixtures");
    check(!bmh::original_badge_fallback(300, &image),"foreign cup clubs still use the computer badge");
    check(bmh::read_file(dir / "BMH.exe") == before,"loading modified the supplied executable");
    {std::ofstream f(dir/"OBMHPROF.INI");f<<"[OpenBMH]\nprofile=unknown\n";}
    rejected=false;try{bmh::load_data_profile(dir);}catch(const bmh::FormatError&){rejected=true;}
    std::filesystem::remove_all(dir);check(rejected,"Unknown profile silently became Classic");
    std::cout<<"Classic and expanded profile dimensions, discovery and limits passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
