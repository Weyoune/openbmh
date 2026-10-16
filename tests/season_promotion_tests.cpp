#include "runtime_fixture_tables.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "formats/fbm.hpp"
#include "formats/le.hpp"
#include "game/career.hpp"
#include "game/national.hpp"
#include "game/save.hpp"
#include "game/schedule.hpp"
#include "game/season.hpp"

using namespace bmh;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
GameState load(const std::filesystem::path& path, const LeImage& exe, const TextDb& texts) {
    const auto file=load_savegame(path);
    check(file.checksum_ok && file.version=="V2.24", "native fixture checksum/version");
    const DbCounts counts{exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4)};
    GameState g=GameState::from_save(SaveState(file.stream,counts),counts);
    competition_pointers(g.memory,exe);g.texts=&texts;national_coach_after_load(g);return g;
}
struct Run {
    GameState state;
    GameRandom game;
    WatcomRandom rand;
    MinuteReport report;
    SeasonTransitionOutcome outcome;
    std::vector<std::string> pauses;
#ifndef PROMOTION_BASELINE
    std::vector<SeasonPromotionEvent> events;
#endif
};
Run run(GameState initial, uint32_t seed, int32_t game_seed, bool observe, bool print) {
    Run result;result.state=std::move(initial);result.game=GameRandom(game_seed);result.rand=WatcomRandom(seed);
#ifndef PROMOTION_BASELINE
    Bytes held_state;int held_group=-1;uint64_t held_game_calls=0,held_rand_calls=0;int32_t held_game_seed=0;uint32_t held_rand_seed=0;
    auto hook=[&](const GameState& g,const SeasonPromotionEvent& event) {
        check(event.group==int(result.events.size()),"four ordered promotion groups");
        check(event.count<=event.relegated.size() && event.count<=event.promoted.size(),"Source visible prefix is available");
        const auto before=save_stream(g);const auto calls_g=result.game.calls(),calls_w=result.rand.calls();
        const auto seed_g=result.game.seed();const auto seed_w=result.rand.seed();
        result.events.push_back(event); // retain owned event past the callback
        held_group=event.group;held_state=before;held_game_calls=calls_g;held_rand_calls=calls_w;held_game_seed=seed_g;held_rand_seed=seed_w;
        if(print && std::getenv("PROMOTION_FIXTURE_OUTPUT")) {
            const auto directory=std::filesystem::path(std::getenv("PROMOTION_FIXTURE_OUTPUT"));
            std::filesystem::create_directories(directory);
            auto fixture_rand=result.rand;
            write_savegame(directory/("group"+std::to_string(event.group)+".FBM"),make_savegame(g,fixture_rand));
        }
        if(print){std::cout<<"group "<<event.group<<" count "<<event.count<<" down";for(auto t:event.relegated)std::cout<<' '<<t;
            std::cout<<" up";for(auto t:event.promoted)std::cout<<' '<<t;std::cout<<'\n';
            for(size_t i=0;i<event.count;++i) std::cout<<"  "<<g.club_name(event.relegated[i])<<" / "<<g.club_name(event.promoted[i])<<'\n';
        }
        check(save_stream(g)==before && result.game.calls()==calls_g && result.rand.calls()==calls_w &&
              result.game.seed()==seed_g && result.rand.seed()==seed_w,"observation does not mutate native state/RNG");
    };
#endif
    auto pause=[&](const char* label){result.pauses.emplace_back(label);
#ifndef PROMOTION_BASELINE
        if(observe && std::string(label).find("promotion and relegation")!=std::string::npos){
            check(held_group==int(result.events.size())-1,"typed callback precedes existing presentation pause");
            check(save_stream(result.state)==held_state && result.game.calls()==held_game_calls && result.rand.calls()==held_rand_calls &&
                  result.game.seed()==held_game_seed && result.rand.seed()==held_rand_seed,"old group state remains until presentation returns");
        }
#endif
    };
#ifdef PROMOTION_BASELINE
    (void)observe;(void)print;
    result.outcome=season_transition(result.state,result.game,result.rand,result.report,pause);
#else
    result.outcome=season_transition(result.state,result.game,result.rand,result.report,pause,{}, {},observe?SeasonPromotionHook(hook):SeasonPromotionHook{});
    if(observe)check(result.events.size()==4,"exactly four promotion observations");
#endif
    check(!result.report.unsupported,"native season transition must complete");return result;
}
}
int main(int argc,char**argv){try{
    if(argc<3 || argc>6)throw std::runtime_error("usage: season_promotion_tests <BMH data> <FBM> [Watcom seed=589388519] [Game seed=0] [output final stream]");
    if(!*argv[1]) { std::cout<<"Promotion core tests skipped: Original data not configured\n"; return 77; }
    const auto exe=bmh::test::load_executable(std::filesystem::path(argv[1])/"BMH.exe");const auto texts=load_textd(std::filesystem::path(argv[1])/"TEXTD.BLK");
    const auto initial=load(argv[2],exe,texts);const uint32_t seed=argc>3?uint32_t(std::stoul(argv[3])):589388519u;
    const int32_t game_seed=argc>4?int32_t(std::stol(argv[4])):0;
    auto plain=run(initial,seed,game_seed,false,false);
#ifndef PROMOTION_BASELINE
    auto observed=run(initial,seed,game_seed,true,true);
    // Independent display oracle: Original GameEnd post3/post4/post5 name rows and
    // final badge order, mapped through that native fixture's name offsets.
    if(initial.date==365 && initial.season==0 && initial.memory.u8(::bmh::layout::CareerDuration)==1 &&
       initial.club_name(8)=="SC PADERBORN 07" && initial.club_name(12)=="HAMBURGER SV" &&
       seed==589388519u && game_seed==665367) {
        const std::vector<std::vector<uint16_t>> down{{8,3,12},{19,18,29,34},{37,44,57,67},{81,78,80}};
        const std::vector<std::vector<uint16_t>> up{{24,27,35},{70,36,51,62},{73,87,79,77},{144,138,162}};
        for(size_t group : {size_t(0),size_t(1),size_t(3)})
            check(observed.events[group].relegated==down[group] && observed.events[group].promoted==up[group],
                  "Native GameEnd Original post3/post4/post5 participant order");
    }
    // Original before/after pair identifies clubs by persistent name offsets,
    // rather than treating shuffled runtime slots as ranks or identities.
    const auto native_after=std::filesystem::path(argv[2]).parent_path()/"SESNEW01.FBM";
    if(std::filesystem::path(argv[2]).filename()=="SESEND01.FBM" && std::filesystem::exists(native_after)) {
        const auto after=load(native_after,exe,texts);
        auto after_league=[&](uint16_t team) {
            const auto identity=initial.club_ids.at(team);
            const auto at=std::find(after.club_ids.begin(),after.club_ids.end(),identity);
            check(at!=after.club_ids.end(),"Original pair preserves club identity");
            return team_league(int(at-after.club_ids.begin()));
        };
        for(size_t group=0;group<4;++group) {
            const auto& event=observed.events[group];
            for(size_t row=0;row<event.count;++row) {
                const int down=after_league(event.relegated[row]);
                check(group==0?down==1:group==1?(down==2 || down==3):group==2?down==4:down>=5,
                      "Original SESNEW identity membership validates relegation list");
                if(group<3) {
                    const int up=after_league(event.promoted[row]);
                    check(group==0?up==0:group==1?up==1:(up==2 || up==3),
                          "Original SESNEW identity membership validates deterministic promotion list");
                }
            }
        }
        std::cout<<"Original SESEND/SESNEW stable club identities validate four down lists and deterministic up lists\n";
    }
    check(plain.outcome==observed.outcome && save_stream(plain.state)==save_stream(observed.state) &&
          plain.game.seed()==observed.game.seed() && plain.game.calls()==observed.game.calls() &&
          plain.rand.seed()==observed.rand.seed() && plain.rand.calls()==observed.rand.calls() && plain.pauses==observed.pauses,
          "typed observation preserves baseline bytes, outcome, both RNGs and all recorded pauses");
    // Source no-play-off branch lowers rendered/swapped count to2, not down-array length.
    auto no_playoff=initial;no_playoff.memory.set16(::bmh::layout::Season_Promotion_ExecutableField,0);
    const auto without=run(no_playoff,seed,game_seed,true,false);
    check(without.events[1].count==2 && without.events[1].relegated.size()>=4 && without.events[1].promoted.size()==2,
          "no-play-off Source count is independent from full relegated list");
    std::cout<<"Four groups, owned participant order, callback-before-pause, unchanged state/RNG/pauses, no-play-off prefix OK\n";
#endif
    std::cout<<"final outcome "<<int(plain.outcome)<<" date "<<plain.state.date<<" season "<<plain.state.season
             <<" G "<<plain.game.seed()<<'/'<<plain.game.calls()<<" W "<<plain.rand.seed()<<'/'<<plain.rand.calls()<<" pauses "<<plain.pauses.size()<<'\n';
    if(argc>5){const auto stream=save_stream(plain.state);std::ofstream output(argv[5],std::ios::binary);output.write(reinterpret_cast<const char*>(stream.data()),std::streamsize(stream.size()));}
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
