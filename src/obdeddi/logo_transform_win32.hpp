// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <array>
#include <cstdint>
namespace bmh::obdeddi {
std::array<uint32_t,1444> transform_logo(Gdiplus::Bitmap&,int x,int y,int width,int height,float angle,bool mirror_x,bool mirror_y);
}
