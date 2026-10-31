// SPDX-License-Identifier: GPL-3.0-only
#include "ui/setup_country_symbol.hpp"
#include <algorithm>
#include <stdexcept>
namespace bmh::ui {
Image dual_country_button(const Image& prepared,bool active) {
    if(prepared.width!=51 || prepared.height!=80 || prepared.pixels.size()!=51*80)
        throw std::invalid_argument("setup: expected prepared 51x80 Original country control");
    Image out(51,40);
    const auto first=prepared.pixels.begin()+(active?51*40:0);
    std::copy_n(first,51*40,out.pixels.begin());return out;
}
}
