// SPDX-License-Identifier: GPL-3.0-only
#include "game/world12/cups.hpp"
#include "game/world12/codec.hpp"
#include "game/cup.hpp"
#include "launch/fltd.hpp"
#include "formats/le.hpp"
#include <iostream>

int main(int argc,char** argv)try {
    using namespace bmh::world12;
    if(argc!=4)throw std::runtime_error("usage: world12_cups_validate <German rules directory> <supplied FLTD.EXE> <WORLD12.JSON>");
    const auto world=decode_world(bmh::read_file(argv[3]));
    const auto german=bmh::load_le(std::filesystem::path(argv[1])/"BMH.exe");
    const auto english=bmh::launch::translate_fltd(argv[2]);
    int verified=0,unsupported=0;
    for(const auto country:{Country::germany,Country::england}) {
        const auto& image=country==Country::germany?german:english;
        std::vector<ClubId> available;
        for(const auto& [id,club]:world.clubs)
            if(club.country==country && club.kind==ClubKind::club && club.league.valid())available.push_back(id);
        for(uint8_t entry=0;entry<bmh::kCupEntries;++entry) {
            const auto at=bmh::cup_entry_address(entry);
            const auto source_id=image.u16(at+0x18);
            const auto flags=image.u8(at+0x14),teams=image.u8(at+0x1a);
            if(!(flags&4) || teams==0 || (source_id!=0 && source_id!=8 && !(country==Country::england && source_id==11)))continue;
            const auto cup=source_id==11?DomesticCup::league_cup:source_id==8?
                (country==Country::germany?DomesticCup::german_supercup:DomesticCup::english_supercup):
                (country==Country::germany?DomesticCup::german_cup:DomesticCup::fa_cup);
            const auto seed_count=image.u8(at+0x20);
            std::cout<<(country==Country::germany?"DE":"EN")<<" stage "<<int(entry)<<": teams="<<int(teams)<<", rounds="<<int(image.u8(at+0x1b))<<", seeded="<<int(seed_count)<<", next="<<int(image.u8(at+0x16))<<", flags="<<int(flags)<<" ";
            try {
                if(teams%2 || size_t(teams)+seed_count>available.size())throw std::runtime_error("not enough league clubs for metadata probe");
                std::vector<Fixture> draw;
                for(size_t i=0;i<teams;i+=2)draw.push_back({available[i],available[i+1]});
                const std::vector<ClubId> seeds(available.begin()+teams,available.begin()+teams+seed_count);
                const auto stage=KnockoutStage::from_source(cup,image,entry,draw,world,seeds);
                stage.validate(world);++verified;std::cout<<"metadata verified\n";
            }catch(const std::exception& error){++unsupported;std::cout<<"unsupported: "<<error.what()<<'\n';}
        }
    }
    std::cout<<verified<<" source stages verified; "<<unsupported<<" require additional stage adapters.\n"
             <<"Metadata probes use synthetic club pairings, not the original entry draws or simulated matches.\n";
    return unsupported?2:0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
