// SPDX-License-Identifier: GPL-3.0-only
#pragma once
struct SDL_Renderer;
struct SDL_Texture;
namespace bmh::app {
// Owned, embedded artwork, rendered independently of the indexed game canvas.
// Destruction restores the caller's logical size and therefore its input mapping.
class StartScreen {
public:
    explicit StartScreen(SDL_Renderer*);
    ~StartScreen();
    StartScreen(const StartScreen&)=delete;
    StartScreen& operator=(const StartScreen&)=delete;
    void present();
    int width()const{return width_;}
    int height()const{return height_;}
private:
    SDL_Renderer* renderer_;
    SDL_Texture* texture_=nullptr;
    int width_=0,height_=0,previous_width_=0,previous_height_=0;
};
}
