// SPDX-License-Identifier: GPL-3.0-only
// OpenBMH - first milestone: asset viewer.
// Shows the original .VGA screens with the original palette and font.
//   openbmh --data <path to BMH installation>
//   Left/Right: previous/next image, Esc: quit
//   openbmh --data <path> --dump <out.bmp> [--image N]   render one screen to a file, no window
#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "formats/font.hpp"
#include "formats/m3d.hpp"
#include "formats/palette.hpp"
#include "formats/vga.hpp"

namespace fs = std::filesystem;
using namespace bmh;

constexpr int SCREEN_W = 320, SCREEN_H = 240, SCALE = 3;

static std::vector<fs::path> find_images(const fs::path& data) {
    std::vector<std::pair<int, fs::path>> found;
    for (const auto& e : fs::directory_iterator(data / "KB")) {
        std::string stem = e.path().stem().string(), ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::toupper);
        if (ext == ".VGA" && !stem.empty() && stem.find_first_not_of("0123456789") == std::string::npos)
            found.emplace_back(std::stoi(stem), e.path());
    }
    std::sort(found.begin(), found.end());
    std::vector<fs::path> out;
    for (auto& f : found) out.push_back(f.second);
    return out;
}

static Image render_screen(const fs::path& image, const Font& font, const Palette& pal, const std::string& club) {
    Image screen(SCREEN_W, SCREEN_H, 0);
    Image img = load_vga(image);
    screen.blit(img, (SCREEN_W - img.width) / 2, (SCREEN_H - img.height) / 2);
    std::string caption =
        image.filename().string() + "  " + std::to_string(img.width) + "x" + std::to_string(img.height) + "  " + club;
    font.draw(screen, 3, 2, caption, pal.nearest(0, 0, 0));  // shadow
    font.draw(screen, 2, 1, caption, pal.nearest(255, 255, 255));
    return screen;
}

int main(int argc, char** argv) {
    fs::path data = ".", dump;
    size_t current = 0;
    for (int i = 1; i + 1 < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--data") data = argv[i + 1];
        if (arg == "--dump") dump = argv[i + 1];
        if (arg == "--image") current = std::stoul(argv[i + 1]);
    }

    try {
        Palette pal = load_palette(data / "M3PC.COL");
        Font font = load_font(data / "F" / "BMH2.FNT");
        Database db = load_database(data / "M3D.MAN");
        std::vector<fs::path> images = find_images(data);
        if (images.empty()) throw FormatError("no KB\\*.VGA images found in " + data.string());
        current %= images.size();
        auto club = [&](size_t i) { return db.club_names[i % db.club_names.size()]; };

        if (!dump.empty()) {
            Image screen = render_screen(images[current], font, pal, club(current));
            SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, SCREEN_W, SCREEN_H, 32, SDL_PIXELFORMAT_ARGB8888);
            auto* px = static_cast<uint32_t*>(s->pixels);
            for (int y = 0; y < SCREEN_H; y++)
                for (int x = 0; x < SCREEN_W; x++) px[y * (s->pitch / 4) + x] = pal.argb[screen.at(x, y)];
            int rc = SDL_SaveBMP(s, dump.string().c_str());
            SDL_FreeSurface(s);
            if (rc != 0) throw FormatError(std::string("cannot write ") + dump.string() + ": " + SDL_GetError());
            return 0;
        }

        SDL_Init(SDL_INIT_VIDEO);
        SDL_Window* win = SDL_CreateWindow("OpenBMH", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_W * SCALE,
                                           SCREEN_H * SCALE, SDL_WINDOW_RESIZABLE);
        SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
        SDL_RenderSetLogicalSize(ren, SCREEN_W, SCREEN_H);
        SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);

        bool dirty = true, running = true;
        std::vector<uint32_t> argb(SCREEN_W * SCREEN_H);
        while (running) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT) running = false;
                if (ev.type == SDL_KEYDOWN) {
                    switch (ev.key.keysym.sym) {
                        case SDLK_ESCAPE: running = false; break;
                        case SDLK_RIGHT: current = (current + 1) % images.size(); dirty = true; break;
                        case SDLK_LEFT: current = (current + images.size() - 1) % images.size(); dirty = true; break;
                    }
                }
            }
            if (dirty) {
                Image screen = render_screen(images[current], font, pal, club(current));
                for (size_t i = 0; i < argb.size(); i++) argb[i] = pal.argb[screen.pixels[i]];
                SDL_UpdateTexture(tex, nullptr, argb.data(), SCREEN_W * 4);
                SDL_SetWindowTitle(win, ("OpenBMH - " + images[current].filename().string()).c_str());
                dirty = false;
            }
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex, nullptr, nullptr);
            SDL_RenderPresent(ren);
        }
        SDL_DestroyTexture(tex);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 0;
}
