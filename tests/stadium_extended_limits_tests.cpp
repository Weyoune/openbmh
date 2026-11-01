// SPDX-License-Identifier: GPL-3.0-only
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
    auto image=LeImage::from_bytes(layout::GermanDataSegmentBegin,Bytes(0x3000));
    const uint8_t original_max[]={8,18,8,14,8,18,8,14},original_weeks[]={30,37,30,25,30,37,30,25};
    for(uint32_t i=0;i<8;++i){image.patch(layout::CareerSession_Invalid_RuntimeField+i,original_max+i,1);image.patch(layout::CareerSession_DispatchStadium_RuntimeField2+(i+22)*5,original_weeks+i,1);const uint8_t price[]={0xe0,0x93,0x04,0};image.patch(layout::CareerSession_DispatchStadium_RuntimeField+(i+8)*4,price,4);}
    const auto constants=stadium_block_expansion_constants(image);
    check(constants.max_places==std::array<uint32_t,8>{14000,24000,14000,20000,14000,24000,14000,20000},"expanded block limits differ");
    check(std::accumulate(constants.max_places.begin(),constants.max_places.end(),0u)==144000,"full stadium does not reach144k");
    check(constants.weeks==std::array<uint8_t,8>{30,48,30,40,30,48,30,40},"larger block duration is not scaled with size");
    check(constants.price_per_1000[0]==300000&&constants.price_per_1000[7]==300000,"edition unit prices were replaced");
    const uint8_t custom=20;image.patch(layout::CareerSession_Invalid_RuntimeField,&custom,1);check(stadium_block_expansion_constants(image).max_places[0]==20000,"larger supplied custom stand was reduced");
    const uint8_t unsupported=255;image.patch(layout::CareerSession_Invalid_RuntimeField,&unsupported,1);bool rejected=false;try{stadium_block_expansion_constants(image);}catch(const FormatError&){rejected=true;}check(rejected,"unrepresentable capacity/duration accepted");
    std::cout<<"144k stadium policy, edition prices, capacity-based weeks, custom floors and saved byte limits passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
