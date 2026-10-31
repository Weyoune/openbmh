// SPDX-License-Identifier: GPL-3.0-only
#define NOMINMAX
#include "obdeddi/logo_transform_win32.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <memory>
namespace bmh::obdeddi {
std::array<uint32_t,1444> transform_logo(Gdiplus::Bitmap& source,int x,int y,int width,int height,float angle,bool mirror_x,bool mirror_y){
 if(x<0||y<0||width<=0||height<=0||int64_t(x)+width>source.GetWidth()||int64_t(y)+height>source.GetHeight()||!std::isfinite(angle)||std::abs(angle)>360)throw std::runtime_error("invalid logo transform");
 std::unique_ptr<Gdiplus::Bitmap> cropped(source.Clone(Gdiplus::Rect(x,y,width,height),PixelFormat32bppARGB));if(!cropped||cropped->GetLastStatus()!=Gdiplus::Ok)throw std::runtime_error("logo crop failed");
 Gdiplus::ImageAttributes edges;edges.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
 Gdiplus::Bitmap target(38,38,PixelFormat32bppARGB);
 {Gdiplus::Graphics g(&target);g.Clear(Gdiplus::Color(0,0,0,0));g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);float radians=angle*3.14159265358979323846f/180,w=float(width),h=float(height);float extent=std::max(std::abs(std::cos(radians))*w+std::abs(std::sin(radians))*h,std::abs(std::sin(radians))*w+std::abs(std::cos(radians))*h);float scale=36/extent;g.TranslateTransform(19,19);g.RotateTransform(angle);g.ScaleTransform(mirror_x?-1.f:1.f,mirror_y?-1.f:1.f);if(g.DrawImage(cropped.get(),Gdiplus::RectF(-w*scale/2,-h*scale/2,w*scale,h*scale),0.f,0.f,w,h,Gdiplus::UnitPixel,&edges)!=Gdiplus::Ok)throw std::runtime_error("logo render failed");g.Flush(Gdiplus::FlushIntentionSync);}
 std::array<uint32_t,1444> rgba{};for(int py=0;py<38;++py)for(int px=0;px<38;++px){Gdiplus::Color color;if(target.GetPixel(px,py,&color)!=Gdiplus::Ok)throw std::runtime_error("logo pixel read failed");rgba[size_t(py*38+px)]=color.GetValue();}return rgba;
}
}
