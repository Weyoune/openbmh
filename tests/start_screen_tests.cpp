// SPDX-License-Identifier: GPL-3.0-only
#include "app/start_screen.hpp"
#include <SDL.h>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include "testing/check.hpp"
namespace {
using bmh::test_support::check;
void frame(int width,int height,const std::filesystem::path& output) {
    const std::unique_ptr<SDL_Surface,decltype(&SDL_FreeSurface)> surface(
        SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SDL_PIXELFORMAT_RGBA32),SDL_FreeSurface);
    check(bool(surface),SDL_GetError());
    const std::unique_ptr<SDL_Renderer,decltype(&SDL_DestroyRenderer)> renderer(SDL_CreateSoftwareRenderer(surface.get()),SDL_DestroyRenderer);
    check(bool(renderer),SDL_GetError());SDL_RenderSetLogicalSize(renderer.get(),320,240);SDL_SetRenderDrawColor(renderer.get(),12,34,56,255);
    {
        bmh::app::StartScreen title(renderer.get());
        check(title.width()==1448&&title.height()==1086,"start artwork was reduced to the game canvas");title.present();
        int logical_width=0,logical_height=0;SDL_RenderGetLogicalSize(renderer.get(),&logical_width,&logical_height);
        check(logical_width==1448&&logical_height==1086,"start screen uses the low-resolution logical size");
        float sx=0,sy=0;SDL_RenderGetScale(renderer.get(),&sx,&sy);
        check(sx>0&&sy>0&&std::abs(sx-sy)<0.001f,"start artwork aspect ratio was stretched");
        std::vector<unsigned char> pixels(size_t(width)*height*4);
        check(SDL_LockSurface(surface.get())==0,SDL_GetError());
        for(int y=0;y<height;++y)std::memcpy(pixels.data()+size_t(y)*width*4,
            static_cast<const unsigned char*>(surface->pixels)+size_t(y)*surface->pitch,size_t(width)*4);
        SDL_UnlockSurface(surface.get());
        const auto center=(size_t(height/2)*width+width/2)*4;
        check(pixels[center]+pixels[center+1]+pixels[center+2]>20,"embedded start artwork was not painted");
        if(width*3>height*4){const auto side=size_t(height/2)*width*4;check(pixels[side]==0&&pixels[side+1]==0&&pixels[side+2]==0,"wide display has no clean side bars");}
        Uint8 r=0,g=0,b=0,a=0;SDL_GetRenderDrawColor(renderer.get(),&r,&g,&b,&a);
        check(r==12&&g==34&&b==56&&a==255,"start screen leaked its draw color into the game");
        if(!output.empty()) {
            std::filesystem::create_directories(output);std::ofstream file(output/(std::to_string(width)+"x"+std::to_string(height)+".ppm"),std::ios::binary);
            file<<"P6\n"<<width<<' '<<height<<"\n255\n";for(size_t i=0;i<pixels.size();i+=4)file.write(reinterpret_cast<const char*>(pixels.data()+i),3);
        }
        title.present(); // Expose/resize redraw retains the same decoded texture.
    }
    int w=0,h=0;SDL_RenderGetLogicalSize(renderer.get(),&w,&h);
    check(w==320&&h==240,"start screen did not restore the game and input coordinate system");
}
}
int main(int argc,char** argv)try {
    SDL_setenv("SDL_VIDEODRIVER","dummy",0);check(SDL_Init(SDL_INIT_VIDEO)==0,SDL_GetError());
    const std::filesystem::path output=argc>1?argv[1]:"";
    for(const auto size:std::array<std::array<int,2>,4>{{{1448,1086},{1920,1080},{3840,2160},{3440,1440}}})frame(size[0],size[1],output);
    SDL_Quit();std::cout<<"Embedded native artwork, 1080p/4K/ultrawide aspect fit, redraw and game/input restoration passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';SDL_Quit();return 1;}
