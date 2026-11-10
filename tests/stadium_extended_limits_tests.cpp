// SPDX-License-Identifier: GPL-3.0-only
// The stadium block limits come from the edition unchanged (Diamond 3.7: 96,000 places in total).
// The file keeps its historical name: until 1.0.12 it tested the withdrawn 144,000 place extension.
#include "game/stadium_blocks.hpp"
#include "formats/le.hpp"
#include <iostream>
#include <numeric>
#include <stdexcept>
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}
int main()try{
    using namespace bmh;
    auto image=LeImage::from_bytes(layout::GermanDataSegmentBegin,Bytes(layout::CareerSession_Invalid_RuntimeField + 8 - layout::GermanDataSegmentBegin));
    const uint8_t original_max[]={8,18,8,14,8,18,8,14},original_weeks[]={30,37,30,25,30,37,30,25};
    for(uint32_t i=0;i<8;++i){image.patch(layout::CareerSession_Invalid_RuntimeField+i,original_max+i,1);image.patch(layout::CareerSession_DispatchStadium_RuntimeField2+(i+22)*5,original_weeks+i,1);const uint8_t price[]={0xe0,0x93,0x04,0};image.patch(layout::CareerSession_DispatchStadium_RuntimeField+(i+8)*4,price,4);}
    const auto constants=stadium_block_expansion_constants(image);
    check(constants.max_places==std::array<uint32_t,8>{8000,18000,8000,14000,8000,18000,8000,14000},"block limits are not the edition's");
    check(std::accumulate(constants.max_places.begin(),constants.max_places.end(),0u)==96000,"a full Diamond 3.7 stadium does not have 96,000 places");
    check(constants.weeks==std::array<uint8_t,8>{30,37,30,25,30,37,30,25},"construction weeks are not the edition's");
    check(constants.price_per_1000[0]==300000&&constants.price_per_1000[7]==300000,"edition unit prices were replaced");
    // an edition (or edited data) with other limits is taken as supplied
    const uint8_t custom=20;image.patch(layout::CareerSession_Invalid_RuntimeField,&custom,1);check(stadium_block_expansion_constants(image).max_places[0]==20000,"a supplied custom limit was changed");
    const uint8_t unsupported=255;image.patch(layout::CareerSession_Invalid_RuntimeField,&unsupported,1);bool rejected=false;try{stadium_block_expansion_constants(image);}catch(const FormatError&){rejected=true;}check(rejected,"limits beyond the saved capacity byte accepted");
    std::cout<<"96,000 place stadium of the edition, edition prices and weeks, custom limits and saved byte limit passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
