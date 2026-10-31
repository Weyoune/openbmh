// SPDX-License-Identifier: GPL-3.0-only
#include "ui/setup_country_symbol.hpp"
#include <algorithm>
#include <stdexcept>
namespace bmh::ui {
Image country_setup_button(const Image& prepared,size_t control,bool active) {
    if(prepared.width!=204 || prepared.height!=80 || prepared.pixels.size()!=204*80 || control>=4)
        throw std::invalid_argument("setup: expected prepared 204x80 four-country control atlas");
    Image out(51,40);
    for(int y=0;y<40;++y) {
        const auto first=prepared.pixels.begin()+size_t(y+(active?40:0))*204+control*51;
        std::copy_n(first,51,out.pixels.begin()+size_t(y)*51);
    }
    return out;
}
}
