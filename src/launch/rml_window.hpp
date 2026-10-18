// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi_Platform_SDL.h>
#include <RmlUi_Renderer_GL3.h>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
namespace bmh::rml {
struct Bitmap {
    int width = 0, height = 0;
    std::vector<Rml::byte> rgba;
};
using ImageProvider = std::function<std::optional<Bitmap>(const std::string &)>;
class Window {
    struct Platform;
    std::unique_ptr<Platform> platform_;
    std::unique_ptr<RenderInterface_GL3> renderer_owner_;
    int reference_width_, reference_height_;
    bool fit_;

  protected:
    SDL_Window *window_ = nullptr;
    RenderInterface_GL3 &renderer_;
    Rml::Context *context_ = nullptr;
    Rml::ElementDocument *document_ = nullptr;
    bool initialized_ = false, closed_ = false, dirty_ = true;
    virtual bool on_key(const SDL_Event &) { return false; }
    virtual void on_close() { closed_ = true; }
    Rml::Element *element(const char *) const;
    void text(const char *, const std::string &);
    void display(const char *, bool, const char *style = "block");
    void enabled(const char *, bool);
    std::string value(const char *) const;
    void resize();
    void step(bool wait = true);
    void shutdown();
    void trap_tab(const SDL_Event &, const char *root);

  public:
    Window(const std::string &document, const char *title, int width, int height, int min_width,
           int min_height, bool fit = false, ImageProvider images = {});
    virtual ~Window();
};
std::string escape(std::string_view);
} // namespace bmh::rml
