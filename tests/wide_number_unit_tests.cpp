// SPDX-License-Identifier: GPL-3.0-only
#include "game/messages.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace bmh;
namespace { void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);} }
int main()try {
    GameState state;state.currency_suffix=" EU";
    state.memory.add_block(layout::CurrencyMultiplier,8);
    state.memory.set32(layout::CurrencyMultiplier,1);state.memory.set32(layout::CurrencyDivisor,1);
    for(int32_t amount:{-1000000,-1,0,1,1234567})
        for(int flags:{0,1,8,0x44,0x82,0x101,0x108,0x188})
            check(format_number_wide(state,amount,flags)==format_number(state,amount,flags),
                  "wide display changed ordinary grouping, padding or currency");
    check(format_number_wide(state,6300000000LL,0x101)=="6.300.000.000 EU","large positive aggregate wrapped");
    check(format_number_wide(state,-6300000000LL,0x101)=="-6.300.000.000 EU","large negative aggregate lost its sign");
    state.memory.set32(layout::CurrencyMultiplier,2);state.memory.set32(layout::CurrencyDivisor,3);
    check(format_number_wide(state,6300000000LL,0x101)=="4.200.000.000 EU","wide totals changed currency units");
    check(format_number_wide(state,-5,0x101)=="-3 EU","currency division stopped truncating toward zero");
    state.memory.set32(layout::CurrencyMultiplier,2);state.memory.set32(layout::CurrencyDivisor,2);
    check(format_number_wide(state,std::numeric_limits<int64_t>::max(),0x101)=="9.223.372.036.854.775.807 EU",
          "representable currency conversion overflowed its intermediate product");
    check(format_number_wide(state,std::numeric_limits<int64_t>::min(),0x101)=="-9.223.372.036.854.775.808 EU",
          "minimum signed display amount overflowed while taking its magnitude");
    state.currency_suffix=" &";
    check(format_number_wide(state,3000000000LL,0x101)=="& 3.000.000.000","English currency prefix changed");
    state.memory.set32(layout::CurrencyDivisor,1);
    bool rejected=false;
    try{(void)format_number_wide(state,std::numeric_limits<int64_t>::max(),0x101);}catch(const FormatError&){rejected=true;}
    check(rejected,"unrepresentable currency output was silently clamped or wrapped");
    std::cout<<"Wide aggregate formatting, native parity, currency scaling and signed boundaries passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
