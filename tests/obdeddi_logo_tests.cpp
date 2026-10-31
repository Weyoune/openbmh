// SPDX-License-Identifier: GPL-3.0-only
#define NOMINMAX
#include "obdeddi/logo_transform_win32.hpp"
#include <iostream>
#include <stdexcept>
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main()try{Gdiplus::GdiplusStartupInput input;ULONG_PTR token=0;check(Gdiplus::GdiplusStartup(&token,&input,nullptr)==Gdiplus::Ok,"GDI startup");
 {Gdiplus::Bitmap source(16,8,PixelFormat32bppARGB);for(int y=0;y<8;++y)for(int x=0;x<16;++x)source.SetPixel(x,y,x<8?Gdiplus::Color(255,255,0,0):Gdiplus::Color(255,0,0,255));
 auto normal=bmh::obdeddi::transform_logo(source,0,0,16,8,0,false,false);auto mirror=bmh::obdeddi::transform_logo(source,0,0,16,8,0,true,false);auto rotate=bmh::obdeddi::transform_logo(source,0,0,16,8,180,false,false);auto crop=bmh::obdeddi::transform_logo(source,0,0,8,8,0,false,false);
 check((normal[19*38+8]&0xffffff)==0xff0000,"normal left color");check((mirror[19*38+8]&0xffffff)==0xff,"horizontal mirror");check((rotate[19*38+8]&0xffffff)==0xff,"rotation");check((crop[19*38+30]&0xffffff)==0xff0000,"crop respects source rectangle");check((normal[0]>>24)==0,"transparent fit border");
 bool rejected=false;try{bmh::obdeddi::transform_logo(source,-1,0,16,8,0,false,false);}catch(...){rejected=true;}check(rejected,"invalid crop accepted");}
 Gdiplus::GdiplusShutdown(token);std::cout<<"Native logo crop, rotation, mirroring and alpha passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
