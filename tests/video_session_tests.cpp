// SPDX-License-Identifier: GPL-3.0-only
#include "app/video_session.hpp"
#include <iostream>
int main()try {
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);
    { bmh::app::VideoSession video;
      if (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO)) return 1;
      // The initial chooser executes inside this scope, not before it.
    }
    if (SDL_WasInit(SDL_INIT_VIDEO)) return 1;
    SDL_setenv("SDL_VIDEODRIVER","unavailable-openbmh-test-driver",1);
    bool rejected=false;
    try { bmh::app::VideoSession failed; } catch(const std::exception&) { rejected=true; }
    if(!rejected || SDL_WasInit(SDL_INIT_VIDEO))return 1;
    std::cout<<"Video lifetime initializes before picker and releases on early return\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
