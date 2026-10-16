// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stdexcept>
#include "formats/vga.hpp"
namespace bmh::ui::test {
inline void expect_notice_symbols(const Image& image,const Image& atlas) {
    for(int y=0;y<22;++y)for(int x=0;x<22;++x)
        for(int left:{99,192})
            if(image.pixels.at(size_t(90+y)*image.width+left+x)!=atlas.pixels.at(size_t(44+y)*atlas.width+55+x))
                throw std::runtime_error("notice must use Original red exclamation art, not green question art");
}
}
