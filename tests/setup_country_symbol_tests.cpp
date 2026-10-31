// SPDX-License-Identifier: GPL-3.0-only
#include "ui/setup_country_symbol.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
int main()try {
    bmh::Image prepared(204,80,7);
    for(int y=0;y<40;++y)for(int x=0;x<204;++x){
        prepared.at(x,y)=uint8_t((x+y)%200);
        prepared.at(x,y+40)=uint8_t((x+y+17)%200);
    }
    const auto disabled=bmh::ui::country_setup_button(prepared,0,false);
    const auto enabled=bmh::ui::country_setup_button(prepared,0,true);
    for(int y=0;y<40;++y)for(int x=0;x<51;++x)
        if(disabled.pixels[size_t(y)*51+x]!=prepared.pixels[size_t(y)*204+x] ||
           enabled.pixels[size_t(y)*51+x]!=prepared.pixels[size_t(y+40)*204+x])
            throw std::runtime_error("prepared Original control frames changed during rendering");
    const auto load=bmh::ui::country_setup_button(prepared,3,true);
    if(load.pixels[0]!=prepared.pixels[40*204+3*51])throw std::runtime_error("load control selected from another cell");
    for(auto invalid:{bmh::Image(51,40),bmh::Image(50,80),bmh::Image(204,81)}){
        bool rejected=false;
        try{bmh::ui::country_setup_button(invalid,0,true);}catch(const std::invalid_argument&){rejected=true;}
        if(!rejected)throw std::runtime_error("unsupported control dimensions accepted");
    }
    std::cout<<"Prepared Original control states and strict dimensions passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
