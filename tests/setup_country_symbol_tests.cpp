// SPDX-License-Identifier: GPL-3.0-only
#include "ui/setup_country_symbol.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
int main()try {
    bmh::Image prepared(51,80,7);
    for(int y=0;y<40;++y)for(int x=0;x<51;++x){
        prepared.at(x,y)=uint8_t((x+y)%200);
        prepared.at(x,y+40)=uint8_t((x+y+17)%200);
    }
    const auto disabled=bmh::ui::dual_country_button(prepared,false);
    const auto enabled=bmh::ui::dual_country_button(prepared,true);
    if(!std::equal(disabled.pixels.begin(),disabled.pixels.end(),prepared.pixels.begin()) ||
       !std::equal(enabled.pixels.begin(),enabled.pixels.end(),prepared.pixels.begin()+51*40))
        throw std::runtime_error("prepared Original control frames changed during rendering");
    for(auto invalid:{bmh::Image(51,40),bmh::Image(50,80),bmh::Image(51,81)}){
        bool rejected=false;
        try{bmh::ui::dual_country_button(invalid,true);}catch(const std::invalid_argument&){rejected=true;}
        if(!rejected)throw std::runtime_error("unsupported control dimensions accepted");
    }
    std::cout<<"Prepared Original control states and strict dimensions passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
