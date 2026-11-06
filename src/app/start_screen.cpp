// SPDX-License-Identifier: GPL-3.0-only
// Implementation of StartScreen (start_screen.hpp): decodes the embedded JPEG
// (start_screen_data.hpp) with stb_image into an RGB24 texture, switches the renderer's
// logical size to the image size while presenting and restores the previous logical size in
// the destructor. Errors are reported as std::runtime_error; the stb_image implementation
// is compiled into this file only (JPEG decoding, no stdio).
#include "app/start_screen.hpp"
#include "app/start_screen_data.hpp"
#include <SDL.h>
#include <memory>
#include <stdexcept>
#include <string>
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STB_IMAGE_IMPLEMENTATION
#include "app/third_party/stb_image.h"
namespace bmh::app {
StartScreen::StartScreen(SDL_Renderer* renderer):renderer_(renderer) {
    if(!renderer_)throw std::runtime_error("Start screen: missing renderer");
    SDL_RenderGetLogicalSize(renderer_,&previous_width_,&previous_height_);
    int channels=0;
    const std::unique_ptr<unsigned char,decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(start_screen_jpeg,int(start_screen_jpeg_size),&width_,&height_,&channels,3),stbi_image_free);
    if(!pixels)throw std::runtime_error("Start screen: embedded artwork could not be decoded");
    texture_=SDL_CreateTexture(renderer_,SDL_PIXELFORMAT_RGB24,SDL_TEXTUREACCESS_STATIC,width_,height_);
    if(!texture_)throw std::runtime_error(SDL_GetError());
    if(SDL_UpdateTexture(texture_,nullptr,pixels.get(),width_*3)!=0||SDL_SetTextureScaleMode(texture_,SDL_ScaleModeLinear)!=0) {
        const std::string error=SDL_GetError();SDL_DestroyTexture(texture_);texture_=nullptr;throw std::runtime_error(error);
    }
}
StartScreen::~StartScreen() {
    SDL_DestroyTexture(texture_);
    SDL_RenderSetLogicalSize(renderer_,previous_width_,previous_height_);
}
void StartScreen::present() {
    if(SDL_RenderSetLogicalSize(renderer_,width_,height_)!=0)throw std::runtime_error(SDL_GetError());
    Uint8 r=0,g=0,b=0,a=0;SDL_GetRenderDrawColor(renderer_,&r,&g,&b,&a);
    SDL_SetRenderDrawColor(renderer_,0,0,0,255);
    const int cleared=SDL_RenderClear(renderer_);SDL_SetRenderDrawColor(renderer_,r,g,b,a);
    if(cleared!=0||SDL_RenderCopy(renderer_,texture_,nullptr,nullptr)!=0)throw std::runtime_error(SDL_GetError());
    SDL_RenderPresent(renderer_);
}
}
