// SPDX-License-Identifier: GPL-3.0-only
#import <Cocoa/Cocoa.h>
#include "app/video_session.hpp"
#include <SDL_syswm.h>
#include <cstring>
#include <nfd.h>
#include <iostream>
int main() { @autoreleasepool {
    SDL_setenv("SDL_VIDEODRIVER", "cocoa", 1);
    try {
        bmh::app::VideoSession video;
        if ([NSApp activationPolicy] != NSApplicationActivationPolicyRegular) return 1;
        if (NFD_Init() != NFD_OKAY) return 2;
        NFD_Quit();
        if ([NSApp activationPolicy] != NSApplicationActivationPolicyRegular) return 3;
        SDL_Window* window=SDL_CreateWindow("Keyboard startup QA",0,0,320,240,0);
        if(!window)return 4;
        SDL_SysWMinfo info{}; SDL_VERSION(&info.version);
        if(!SDL_GetWindowWMInfo(window,&info))return 5;
        [NSApp activateIgnoringOtherApps:YES];
        [info.info.cocoa.window makeKeyAndOrderFront:nil];
        for(int i=0;i<100 && SDL_GetKeyboardFocus()!=window;++i){SDL_PumpEvents();SDL_Delay(10);}
        if(SDL_GetKeyboardFocus()!=window){std::cerr<<"Cocoa window did not acquire keyboard focus\n";return 6;}
        SDL_StartTextInput();
        SDL_Event input;while(SDL_PollEvent(&input)){}
        NSEvent* key=[NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint
            modifierFlags:0 timestamp:[[NSProcessInfo processInfo] systemUptime] windowNumber:[info.info.cocoa.window windowNumber]
            context:nil characters:@"q" charactersIgnoringModifiers:@"q" isARepeat:NO keyCode:12];
        [NSApp postEvent:key atStart:NO];
        bool key_received=false,text_received=false;
        for(int i=0;i<100 && !(key_received&&text_received);++i){
            while(SDL_PollEvent(&input)){
                key_received|=input.type==SDL_KEYDOWN;
                text_received|=input.type==SDL_TEXTINPUT && std::strcmp(input.text.text,"q")==0;
            }
            SDL_Delay(10);
        }
        SDL_DestroyWindow(window);
        if(!key_received||!text_received){std::cerr<<"Native key="<<key_received<<" text="<<text_received<<'\n';return 7;}
        std::cout<<"Native Cocoa key and text reach SDL after initial picker lifecycle\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 6;}
    return 0;
} }
