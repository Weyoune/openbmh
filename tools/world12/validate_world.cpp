// SPDX-License-Identifier: GPL-3.0-only
#include "game/world12/codec.hpp"
#include <iostream>
#include "formats/vga.hpp"
int main(int argc,char**argv)try{if(argc!=2&&argc!=3)throw std::runtime_error("usage: world12_validate <WORLD12.JSON> [resource-directory]");auto raw=bmh::read_file(argv[1]);auto world=bmh::world12::decode_world(raw);auto encoded=bmh::world12::encode_world(world);if(bmh::world12::decode_world(encoded)!=world)throw std::runtime_error("world roundtrip mismatch");if(argc==3)for(const auto&[id,crest]:world.crests){auto image=bmh::load_vga(std::filesystem::path(argv[2])/crest.path);if(image.width!=304||image.height!=38||crest.cell>=8)throw std::runtime_error("invalid crest atlas");}
std::cout<<"Validated world: "<<world.catalogue.league_clubs()<<" league clubs, "<<world.clubs.size()<<" total club/country records, "<<world.players.size()<<" players, "<<world.crests.size()<<" crest identities\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
