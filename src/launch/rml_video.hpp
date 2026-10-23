// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <SDL.h>
#include <cstdint>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <RmlUi_Renderer_DX11.h>
#include <wrl/client.h>
#else
#include <RmlUi_Renderer_GL3.h>
#endif

namespace bmh::rml {
// The native Windows renderer needs no OpenGL ICD in a Remote Desktop session.
// The device falls back to Microsoft's built-in WARP when hardware is absent.
class Video {
  public:
    SDL_Window *window = nullptr;
    Video(const char *title, int width, int height, int minimum_width, int minimum_height);
    ~Video();
    Video(const Video &) = delete;
    Video &operator=(const Video &) = delete;
    void drawable_size(int &width, int &height) const;
    void present();
    // RGBA rows in top-to-bottom order, independent of the graphics API.
    std::vector<uint8_t> capture(int &width, int &height);
#ifdef _WIN32
    ID3D11Device *device() const { return device_.Get(); }
    ID3D11RenderTargetView *target() const { return target_.Get(); }
    void resize(int width, int height);
#endif
  private:
    void close() noexcept;
#ifdef _WIN32
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swapchain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target_;
    int width_ = 0, height_ = 0;
#else
    SDL_GLContext gl_ = nullptr;
    bool loader_ = false;
#endif
};

#ifdef _WIN32
class Renderer : public RenderInterface_DX11 {
    Video &video_;

  public:
    explicit Renderer(Video &video) : RenderInterface_DX11(video.device()), video_(video) {}
    void SetViewport(int width, int height) {
        video_.resize(width, height);
        RenderInterface_DX11::SetViewport(width, height);
    }
    void Clear() { RenderInterface_DX11::Clear(video_.target()); }
    void EndFrame() { RenderInterface_DX11::EndFrame(video_.target()); }
    explicit operator bool() const { return video_.device() != nullptr; }
};
#else
class Renderer : public RenderInterface_GL3 {
  public:
    explicit Renderer(Video &) {}
};
#endif
// Also used before SDL initialization, so an unavailable graphics driver cannot
// prevent reporting the actual startup error. No preferences or tokens are logged.
void report_start_error(const char *product, const std::string &message, bool dialog = true) noexcept;
void attach_parent_console() noexcept;
} // namespace bmh::rml
