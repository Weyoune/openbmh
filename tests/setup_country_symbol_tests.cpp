// SPDX-License-Identifier: GPL-3.0-only
#include "ui/setup_country_symbol.hpp"
#include <iostream>
#include <set>
#include <stdexcept>

int main()try {
    bmh::Palette palette;
    for(uint32_t i=0;i<256;++i)palette.argb[i]=0xff000000u|i<<16|i<<8|i;
    bmh::Image source(51,40,7);
    for(int y=0;y<40;++y)for(int x=0;x<51;++x)
        if(x<1 || x>=50 || y<1 || y>=39)source.at(x,y)=uint8_t((x+y)%200+30);
    const auto enabled=bmh::ui::dual_country_button(source,palette,true);
    const auto disabled=bmh::ui::dual_country_button(source,palette,false);
    int changed=0;std::set<uint8_t> colors;
    for(int y=0;y<40;++y)for(int x=0;x<51;++x) {
        const auto index=size_t(y)*51+x;
        if(x<1 || x>=50 || y<1 || y>=39) {
            if(enabled.pixels[index]!=source.pixels[index] || disabled.pixels[index]!=source.pixels[index])
                throw std::runtime_error("country glyph overwrote the existing button frame");
        }
        changed+=enabled.pixels[index]!=source.pixels[index];
        if(x>=2 && x<49 && y>=2 && y<38)colors.insert(enabled.pixels[index]);
    }
    if(changed<200 || colors.size()<4 || enabled.pixels==disabled.pixels)
        throw std::runtime_error("country symbols or their disabled state are missing");
    bool rejected=false;
    try{bmh::ui::dual_country_button(bmh::Image(50,40),palette,true);}catch(const std::invalid_argument&){rejected=true;}
    if(!rejected)throw std::runtime_error("unsupported button dimensions accepted");
    std::cout<<"Dual-country glyph, frame preservation and active/disabled states passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
