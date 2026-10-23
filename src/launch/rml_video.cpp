// SPDX-License-Identifier: GPL-3.0-only
#include "launch/rml_video.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string_view>
#ifdef _WIN32
#include <SDL_syswm.h>
#endif

namespace bmh::rml {
namespace {
[[noreturn]] void sdl_error(const char *operation) {
    throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}
#ifdef _WIN32
void check(HRESULT result, const char *operation) {
    if (FAILED(result)) {
        char code[24];
        std::snprintf(code, sizeof(code), "0x%08lX", static_cast<unsigned long>(result));
        throw std::runtime_error(std::string(operation) + ": " + code);
    }
}
std::wstring wide(const std::string &text) {
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring result(size_t(length), L' ');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), result.data(), length);
    return result;
}
#endif
} // namespace

Video::Video(const char *title, int width, int height, int minimum_width, int minimum_height) {
    try {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0)
            sdl_error("Video initialization");
        SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
        Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN;
#ifndef _WIN32
        flags |= SDL_WINDOW_OPENGL;
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#ifdef __APPLE__
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#else
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
#endif
        window =
            SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, flags);
        if (!window)
            sdl_error("Window creation");
        SDL_SetWindowMinimumSize(window, minimum_width, minimum_height);
#ifdef _WIN32
        SDL_SysWMinfo info{};
        SDL_VERSION(&info.version);
        if (!SDL_GetWindowWMInfo(window, &info))
            sdl_error("Native window handle");
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferCount = 2;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.OutputWindow = info.info.win.window;
        desc.SampleDesc.Count = 1;
        desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
        auto create = [&](D3D_DRIVER_TYPE driver) {
            return D3D11CreateDeviceAndSwapChain(nullptr, driver, nullptr, 0, levels, 1, D3D11_SDK_VERSION,
                                                 &desc, swapchain_.ReleaseAndGetAddressOf(),
                                                 device_.ReleaseAndGetAddressOf(), nullptr,
                                                 context_.ReleaseAndGetAddressOf());
        };
        const auto mode = std::getenv("OPENBMH_RML_DRIVER");
        const bool force_warp = mode && std::string_view(mode) == "warp";
        HRESULT result = force_warp ? E_FAIL : create(D3D_DRIVER_TYPE_HARDWARE);
        bool software = false;
        if (FAILED(result)) {
            result = create(D3D_DRIVER_TYPE_WARP);
            software = true;
        }
        check(result, "Direct3D 11 initialization (hardware and WARP)");
        // Upstream's full-featured renderer requires the 11.1 runtime interfaces.
        Microsoft::WRL::ComPtr<ID3D11Device1> device1;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext1> context1;
        check(device_.As(&device1), "Direct3D 11.1 device");
        check(context_.As(&context1), "Direct3D 11.1 context");
        std::fprintf(stderr, "RmlUI renderer: Direct3D 11 (%s)\n", software ? "WARP software" : "hardware");
        int pw, ph;
        drawable_size(pw, ph);
        resize(pw, ph);
#else
        gl_ = SDL_GL_CreateContext(window);
        if (!gl_)
            sdl_error("OpenGL 3.3 initialization (macOS: 4.1)");
        if (SDL_GL_MakeCurrent(window, gl_) != 0)
            sdl_error("OpenGL context activation");
        SDL_GL_SetSwapInterval(1);
        std::string error;
        if (!RmlGL3::Initialize(&error))
            throw std::runtime_error("OpenGL renderer initialization: " + error);
        loader_ = true;
#endif
    } catch (...) {
        close();
        throw;
    }
}
Video::~Video() { close(); }
void Video::close() noexcept {
#ifdef _WIN32
    if (context_)
        context_->ClearState();
    target_.Reset();
    swapchain_.Reset();
    context_.Reset();
    device_.Reset();
#else
    if (loader_)
        RmlGL3::Shutdown();
    if (gl_)
        SDL_GL_DeleteContext(gl_);
#endif
    if (window)
        SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER);
}
void Video::drawable_size(int &width, int &height) const {
#ifdef _WIN32
    SDL_GetWindowSizeInPixels(window, &width, &height);
#else
    SDL_GL_GetDrawableSize(window, &width, &height);
#endif
}
#ifdef _WIN32
void Video::resize(int width, int height) {
    if (width < 1 || height < 1 || (width == width_ && height == height_ && target_))
        return;
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    target_.Reset();
    check(swapchain_->ResizeBuffers(0, UINT(width), UINT(height), DXGI_FORMAT_UNKNOWN, 0),
          "Swapchain resize");
    Microsoft::WRL::ComPtr<ID3D11Texture2D> buffer;
    check(swapchain_->GetBuffer(0, IID_PPV_ARGS(&buffer)), "Swapchain buffer");
    check(device_->CreateRenderTargetView(buffer.Get(), nullptr, &target_), "Render target");
    width_ = width;
    height_ = height;
}
#endif
void Video::present() {
#ifdef _WIN32
    check(swapchain_->Present(1, 0), "Direct3D presentation");
#else
    SDL_GL_SwapWindow(window);
#endif
}
std::vector<uint8_t> Video::capture(int &width, int &height) {
    drawable_size(width, height);
    std::vector<uint8_t> pixels(size_t(width) * size_t(height) * 4);
#ifdef _WIN32
    Microsoft::WRL::ComPtr<ID3D11Texture2D> buffer, staging;
    check(swapchain_->GetBuffer(0, IID_PPV_ARGS(&buffer)), "Screenshot buffer");
    D3D11_TEXTURE2D_DESC desc{};
    buffer->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    check(device_->CreateTexture2D(&desc, nullptr, &staging), "Screenshot staging texture");
    context_->CopyResource(staging.Get(), buffer.Get());
    D3D11_MAPPED_SUBRESOURCE data{};
    check(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &data), "Screenshot readback");
    for (int row = 0; row < height; ++row)
        std::memcpy(pixels.data() + size_t(row) * width * 4,
                    static_cast<const uint8_t *>(data.pData) + size_t(row) * data.RowPitch,
                    size_t(width) * 4);
    context_->Unmap(staging.Get(), 0);
#else
    using ReadPixels = void (*)(int, int, int, int, unsigned, unsigned, void *);
    auto read = reinterpret_cast<ReadPixels>(SDL_GL_GetProcAddress("glReadPixels"));
    if (!read)
        sdl_error("Screenshot readback");
    read(0, 0, width, height, 0x1908, 0x1401, pixels.data());
    for (int row = 0; row < height / 2; ++row)
        for (int col = 0; col < width * 4; ++col)
            std::swap(pixels[size_t(row) * width * 4 + col],
                      pixels[size_t(height - row - 1) * width * 4 + col]);
#endif
    return pixels;
}
void report_start_error(const char *product, const std::string &message, bool dialog) noexcept {
    std::fprintf(stderr, "%s: %s\n", product, message.c_str());
#ifdef _WIN32
    try {
        wchar_t directory[32768]{};
        const auto length = GetEnvironmentVariableW(L"LOCALAPPDATA", directory, 32768);
        auto root = (length > 0 && length < 32768 ? std::filesystem::path(directory)
                                                  : std::filesystem::temp_directory_path()) /
                    L"OpenBMH";
        std::filesystem::create_directories(root);
        std::ofstream log(root / (std::string(product) + "-startup.log"), std::ios::app);
        log << message << '\n';
        if (dialog && !std::getenv("OPENBMH_NO_ERROR_DIALOG"))
            MessageBoxW(nullptr, wide(message).c_str(), wide(product).c_str(), MB_OK | MB_ICONERROR);
    } catch (...) {
        if (dialog && !std::getenv("OPENBMH_NO_ERROR_DIALOG"))
            MessageBoxW(nullptr, L"The application could not start.", L"OpenBMH", MB_OK | MB_ICONERROR);
    }
#else
    if (dialog && !std::getenv("OPENBMH_NO_ERROR_DIALOG"))
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, product, message.c_str(), nullptr);
#endif
}
void attach_parent_console() noexcept {
#ifdef _WIN32
    const bool out_free = _fileno(stdout) < 0, err_free = _fileno(stderr) < 0;
    if ((out_free || err_free) && AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE *stream = nullptr;
        if (out_free)
            freopen_s(&stream, "CONOUT$", "w", stdout);
        if (err_free)
            freopen_s(&stream, "CONOUT$", "w", stderr);
    }
#endif
}
} // namespace bmh::rml
