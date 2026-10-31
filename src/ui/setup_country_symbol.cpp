// SPDX-License-Identifier: GPL-3.0-only
#include "ui/setup_country_symbol.hpp"
#include "ui/country_glyphs.hpp"
#include <stdexcept>

namespace bmh::ui {
Image dual_country_button(const Image& original,const Palette& palette,bool active) {
    if(original.width!=51 || original.height!=40)throw std::invalid_argument("setup: expected native 51x40 league control");
    Image out=original;
    const auto background=original.pixels[2*51+2];
    for(int y=1;y<39;++y)for(int x=1;x<50;++x)out.at(x,y)=background;
    const auto outline=palette.nearest(5,39,39),shadow=palette.nearest(0,24,24);
    const auto white=active?palette.nearest(225,224,211):palette.nearest(90,106,103);
    const auto red=active?palette.nearest(190,33,36):palette.nearest(81,65,65);
    const auto gold=active?palette.nearest(228,184,24):palette.nearest(94,96,72);
    const auto black=active?palette.nearest(22,27,28):palette.nearest(43,57,55);
    const auto draw=[&](const auto& mask,int offset,bool england) {
        const auto pixel=[&](int x,int y,uint8_t color) {
            if(x>=2 && x<49 && y>=2 && y<38)out.at(x,y)=color;
        };
        for(int y=0;y<country_glyphs::height;++y)for(int x=0;x<country_glyphs::width;++x)
            if(mask[size_t(y)]&(1u<<x))pixel(offset+x+1,y+4,shadow);
        for(int y=0;y<country_glyphs::height;++y)for(int x=0;x<country_glyphs::width;++x)
            if(mask[size_t(y)]&(1u<<x))for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
                pixel(offset+x+dx,y+3+dy,outline);
        for(int y=0;y<country_glyphs::height;++y)for(int x=0;x<country_glyphs::width;++x)
            if(mask[size_t(y)]&(1u<<x)) {
                const auto color=england?((x>=10 && x<=12)||(y>=16 && y<=18)?red:white):
                    y<12?black:y<23?red:gold;
                pixel(offset+x,y+3,color);
            }
    };
    draw(country_glyphs::england,3,true);draw(country_glyphs::germany,26,false);
    return out;
}
}
