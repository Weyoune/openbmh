// SPDX-License-Identifier: GPL-3.0-only
// Declares the embedded start screen artwork (JPEG bytes and their size); the definitions
// live in a separate data translation unit. Consumed only by start_screen.cpp.
#pragma once
#include <cstddef>
namespace bmh::app {
extern const unsigned char start_screen_jpeg[];
extern const std::size_t start_screen_jpeg_size;
}
