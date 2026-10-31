// SPDX-License-Identifier: GPL-3.0-only
#include <SDL.h>
#include "ui/world12_other_games.hpp"
#include "ui/other_games.hpp"
#include "app/world12_cups.hpp"
#include "game/world12/codec.hpp"
#include "launch/fltd.hpp"
#include "formats/palette.hpp"
#include <fstream>
#include <iostream>

namespace {
using namespace bmh;using namespace bmh::world12;using namespace bmh::ui;
void write_image(const Image& image,const Palette& palette,const std::filesystem::path& file) {
    SDL_Surface* surface=SDL_CreateRGBSurfaceWithFormat(0,image.width,image.height,32,SDL_PIXELFORMAT_ARGB8888);
    if(!surface)throw std::runtime_error(SDL_GetError());
    for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x)
        static_cast<uint32_t*>(surface->pixels)[y*(surface->pitch/4)+x]=palette.argb[image.pixels[size_t(y)*image.width+x]];
    const int result=SDL_SaveBMP(surface,file.string().c_str());SDL_FreeSurface(surface);
    if(result)throw std::runtime_error(SDL_GetError());
}
CareerSnapshot demo(const World& master,GameScope scope,const LeImage& german,const LeImage& english) {
    CareerSnapshot state;state.world=select_world_scope(master,scope);state.results=SeasonResults::empty(state.world.catalogue);
    const auto country=scope.germany?Country::germany:Country::england;const auto league=state.world.catalogue.starting_league(country,0);
    state.world.managers={{0,"UI Demo",state.world.membership[league.value].clubs[0],league,0}};
    CountryCupRanking de,en;
    for(uint8_t l=0;l<5;++l){if(scope.germany)de[l]=state.world.membership[l].clubs;if(scope.england)en[l]=state.world.membership[5+l].clubs;}
    bmh::app::world12::install_domestic_cup_fields(state,de,en,{german,english});
    for(auto cup:{DomesticCup::german_supercup,DomesticCup::english_supercup}) {
        if(!scope.domestic_enabled(cup_country(cup)))continue;
        const auto league=state.world.catalogue.starting_league(cup_country(cup),0);const auto& clubs=state.world.membership[league.value].clubs;
        auto stage=KnockoutStage::from_source(cup,cup_country(cup)==Country::germany?german:english,23,{{clubs[0],clubs[1]}},state.world);
        commit_cup_leg(stage,{cup,23,0,{uint16_t(cup==DomesticCup::german_supercup?65:32)},{},{}},state.world);state.cups.push_back(stage);
    }
    state.validate();return state;
}
}
int main(int argc,char** argv)try {
    if(argc!=5)throw std::runtime_error("usage: world12_other_games_demo <WORLD12.JSON> <reference assets directory> <supplied FLTD.EXE> <output-directory>");
    const auto master=decode_world(read_file(argv[1]));const std::filesystem::path data=argv[2],out=argv[4];std::filesystem::create_directories(out);
    const auto german=load_le(data/"BMH.exe"),english=bmh::launch::translate_fltd(argv[3]);const auto palette=load_palette(data/"M3PC.COL");
    const auto render=[&](const CareerSnapshot& state,World12OtherPage page,const std::string& name) {
        World12OtherSelection selection;selection.page=page;
        const World12OtherGamesIdentity owner{0,state.world.managers[0].club,42,71,1};
        OtherGamesScreen screen(data,world12_other_games_state(state,owner,selection));write_image(screen.render(),palette,out/(name+".bmp"));
        std::ofstream(out/(name+".json"))<<screen.state_json()<<'\n';
    };
    const auto both=demo(master,{},german,english);
    {
        World12OtherSelection selection;selection.page=World12OtherPage::supercups;
        const World12OtherGamesIdentity owner{0,both.world.managers[0].club,42,71,1};
        auto legacy=world12_other_games_state(both,owner,selection);
        legacy.world12_identity.reset();legacy.world12_teams.clear();legacy.sections.clear();legacy.fixture_baselines.clear();
        legacy.identity={0,0,0,0,42,71,1,0x80};legacy.projection.competition=8;legacy.projection.descriptor_competition=8;
        legacy.projection.source_participant_count=2;legacy.projection.rows.resize(1);legacy.rows.resize(1);legacy.projection.round_heading="FINALE";
        const auto texts=load_textd(data/"TEXTD.BLK");
        legacy.rows[0].details[0]=other_games_club_detail_text(legacy.projection.rows[0].home_metadata,texts,german);
        legacy.rows[0].details[1]=other_games_club_detail_text(legacy.projection.rows[0].away_metadata,texts,german);
        for(int slot=0;slot<8;++slot){const bool available=slot<6;legacy.projection.selectors[size_t(slot)].competition=available?(slot==5?8:slot):-1;if(!available)legacy.selector_labels[size_t(slot)].clear();}
        OtherGamesScreen screen(data,std::move(legacy));write_image(screen.render(),palette,out/"legacy-single-final-reference.bmp");
        std::ofstream(out/"legacy-single-final-reference.json")<<screen.state_json()<<'\n';
    }
    render(both,World12OtherPage::german_cup,"dfb-mixed-selectors");render(both,World12OtherPage::supercups,"supercups-both");
    render(both,World12OtherPage::fa_cup,"fa-cup");render(both,World12OtherPage::league_cup,"league-cup");
    auto pending=both;pending.cups.pop_back();render(pending,World12OtherPage::supercups,"supercups-pending-english");
    render(demo(master,{true,false,true},german,english),World12OtherPage::supercups,"supercups-germany-only");
    render(demo(master,{false,true,true},german,english),World12OtherPage::supercups,"supercups-england-only");
    std::cout<<"Rendered seven typed demo views and one reconstructed legacy-layout reference. Supercup scores are explicit synthetic UI examples, not played career results.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
