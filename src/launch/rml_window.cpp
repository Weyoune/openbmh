// SPDX-License-Identifier: GPL-3.0-only
#include "launch/rml_window.hpp"
#include "launch/archive.hpp"
#include "launch/process.hpp"
#include <cmath>
namespace bmh::rml {
using launch::path_to_utf8;
std::string escape(std::string_view value) {
    std::string result;
    for (char c : value)
        switch (c) {
        case '&':
            result += "&amp;";
            break;
        case '<':
            result += "&lt;";
            break;
        case '>':
            result += "&gt;";
            break;
        case '"':
            result += "&quot;";
            break;
        case '\n':
            result += "<br/>";
            break;
        default:
            result += c;
        }
    return result;
}
[[noreturn]] void sdl_error(const char *operation) {
    throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}
int modifiers(Uint16 state) {
    int result = 0;
    if (state & KMOD_CTRL)
        result |= Rml::Input::KM_CTRL;
    if (state & KMOD_SHIFT)
        result |= Rml::Input::KM_SHIFT;
    if (state & KMOD_ALT)
        result |= Rml::Input::KM_ALT;
    if (state & KMOD_GUI)
        result |= Rml::Input::KM_META;
#ifdef __APPLE__
    // RmlUI text widgets use the Ctrl flag for editing shortcuts.
    if (state & KMOD_GUI)
        result |= Rml::Input::KM_CTRL;
#endif
    if (state & KMOD_CAPS)
        result |= Rml::Input::KM_CAPSLOCK;
    if (state & KMOD_NUM)
        result |= Rml::Input::KM_NUMLOCK;
    return result;
}
struct Video {
    SDL_Window *window = nullptr;
    SDL_GLContext gl = nullptr;
    bool loader = false;
    Video(const char *title, int width, int height, int min_width, int min_height) {
        try {
            if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0)
                sdl_error("Launcher video initialization");
            SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
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
            window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
                                      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
                                          SDL_WINDOW_HIDDEN);
            if (!window)
                sdl_error("Launcher window");
            SDL_SetWindowMinimumSize(window, min_width, min_height);
            gl = SDL_GL_CreateContext(window);
            if (!gl)
                sdl_error("Launcher requires OpenGL 3.3 (macOS: 4.1)");
            SDL_GL_MakeCurrent(window, gl);
            SDL_GL_SetSwapInterval(1);
            std::string error;
            if (!RmlGL3::Initialize(&error))
                throw std::runtime_error("Launcher graphics initialization: " + error);
            loader = true;
            SDL_StopTextInput();
        } catch (...) {
            close();
            throw;
        }
    }
    void close() {
        if (loader)
            RmlGL3::Shutdown();
        if (gl)
            SDL_GL_DeleteContext(gl);
        if (window)
            SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER);
    }
    ~Video() { close(); }
};

struct Window::Platform {
    Video video;
    SystemInterface_SDL system;
    TextInputMethodEditor_SDL ime;
    Platform(const char *title, int w, int h, int mw, int mh)
        : video(title, w, h, mw, mh), system(video.window) {}
};
class ResourceRenderer : public RenderInterface_GL3 {
    ImageProvider images_;

  public:
    explicit ResourceRenderer(ImageProvider images) : images_(std::move(images)) {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i &dimensions, const Rml::String &source) override {
        if (source.starts_with("asset:")) {
            if (!images_)
                return 0;
            auto image = images_(source);
            if (!image || image->width <= 0 || image->height <= 0 ||
                image->rgba.size() != size_t(image->width) * size_t(image->height) * 4)
                return 0;
            // Providers return straight RGBA; the upstream renderer uses premultiplied alpha.
            for (size_t pixel = 0; pixel < image->rgba.size(); pixel += 4)
                for (size_t channel = 0; channel < 3; ++channel)
                    image->rgba[pixel + channel] =
                        Rml::byte(unsigned(image->rgba[pixel + channel]) * image->rgba[pixel + 3] / 255);
            dimensions = {image->width, image->height};
            return GenerateTexture({image->rgba.data(), image->rgba.size()}, dimensions);
        }
        return RenderInterface_GL3::LoadTexture(dimensions, source);
    }
};
Window::Window(const std::string &document, const char *title, int width, int height, int mw, int mh,
               bool fit, ImageProvider images)
    : platform_(std::make_unique<Platform>(title, width, height, mw, mh)),
      renderer_owner_(std::make_unique<ResourceRenderer>(std::move(images))), reference_width_(width),
      reference_height_(height), fit_(fit), window_(platform_->video.window), renderer_(*renderer_owner_) {
    if (!renderer_)
        throw std::runtime_error("RmlUI could not create the OpenGL renderer");
    try {
        Rml::SetSystemInterface(&platform_->system);
        Rml::SetRenderInterface(&renderer_);
        Rml::SetTextInputHandler(&platform_->ime);
        initialized_ = Rml::Initialise();
        if (!initialized_)
            throw std::runtime_error("RmlUI initialization failed");
        const auto assets = launch::executable_path({}).parent_path() / "assets";
        for (const char *weight : {"Regular", "Bold"})
            if (!Rml::LoadFontFace(
                    path_to_utf8(assets / "launcher" / (std::string("LatoLatin-") + weight + ".ttf"))))
                throw std::runtime_error("RmlUI font missing; extract the complete OpenBMH package");
        context_ = Rml::CreateContext("OpenBMH", {width, height});
        if (!context_)
            throw std::runtime_error("RmlUI context failed");
        document_ = context_->LoadDocument(path_to_utf8(assets / document));
        if (!document_)
            throw std::runtime_error("RmlUI layout missing; extract the complete OpenBMH package");
        resize();
        document_->Show();
        SDL_ShowWindow(window_);
    } catch (...) {
        if (initialized_)
            Rml::Shutdown();
        initialized_ = false;
        throw;
    }
}
void Window::shutdown() {
    if (initialized_)
        Rml::Shutdown();
    initialized_ = false;
    context_ = nullptr;
    document_ = nullptr;
}
Window::~Window() { shutdown(); }
Rml::Element *Window::element(const char *id) const {
    auto *value = document_->GetElementById(id);
    if (!value)
        throw std::runtime_error(std::string("Launcher element missing: ") + id);
    return value;
}
void Window::text(const char *id, const std::string &value) {
    element(id)->SetInnerRML(escape(value));
    dirty_ = true;
}
void Window::display(const char *id, bool shown, const char *style) {
    element(id)->SetProperty("display", shown ? style : "none");
    dirty_ = true;
}
void Window::enabled(const char *id, bool active) {
    auto *button = element(id);
    if (active)
        button->RemoveAttribute("disabled");
    else
        button->SetAttribute("disabled", "disabled");
    button->SetPseudoClass("disabled", !active);
    button->SetProperty("focus", active ? "auto" : "none");
    dirty_ = true;
}
std::string Window::value(const char *id) const {
    auto *input = dynamic_cast<Rml::ElementFormControl *>(element(id));
    if (!input)
        throw std::runtime_error("Launcher input has the wrong type");
    return input->GetValue();
}
void Window::resize() {
    int w = 0, h = 0, pixels_w = 0, pixels_h = 0;
    SDL_GetWindowSize(window_, &w, &h);
    SDL_GL_GetDrawableSize(window_, &pixels_w, &pixels_h);
    if (w <= 0 || h <= 0 || pixels_w <= 0 || pixels_h <= 0)
        return;
    renderer_.SetViewport(pixels_w, pixels_h);
    context_->SetDimensions({pixels_w, pixels_h});
    const float scale =
        std::clamp(std::min(w / float(reference_width_), h / float(reference_height_)), .72f, 2.5f);
    context_->SetDensityIndependentPixelRatio(float(pixels_w) / w * (fit_ ? scale : 1.f));
    dirty_ = true;
}

void Window::trap_tab(const SDL_Event &event, const char *root) {
    Rml::ElementList controls;
    element(root)->QuerySelectorAll(controls, "input, button, select");
    std::erase_if(controls, [](auto *c) { return !c->IsVisible() || c->HasAttribute("disabled"); });
    if (controls.empty())
        return;
    const auto at = std::find(controls.begin(), controls.end(), context_->GetFocusElement());
    int index = at == controls.end() ? -1 : int(at - controls.begin());
    const int direction = event.key.keysym.mod & KMOD_SHIFT ? -1 : 1;
    controls[size_t((index + direction + int(controls.size())) % int(controls.size()))]->Focus(true);
}
void Window::step(bool wait) {
    if (dirty_)
        context_->Update();
    SDL_Event event;
    int timeout = dirty_ ? 0 : int(std::clamp(context_->GetNextUpdateDelay(), 0.0, .2) * 1000);
    bool got = wait ? SDL_WaitEventTimeout(&event, timeout) != 0 : SDL_PollEvent(&event) != 0;
    bool redraw = dirty_ || got || context_->GetNextUpdateDelay() <= 0;
    while (got) {
        if (event.type == SDL_QUIT ||
            (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE))
            on_close();
        else if ((event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) && on_key(event)) {
        } else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
            auto *focused = context_->GetFocusElement();
            if (event.type == SDL_KEYDOWN && !event.key.repeat && focused &&
                focused->HasAttribute("data-action") && !focused->HasAttribute("disabled") &&
                (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_SPACE))
                focused->DispatchEvent("click", {});
            else if (event.type == SDL_KEYDOWN)
                context_->ProcessKeyDown(RmlSDL::ConvertKey(event.key.keysym.sym),
                                         modifiers(event.key.keysym.mod));
            else
                context_->ProcessKeyUp(RmlSDL::ConvertKey(event.key.keysym.sym),
                                       modifiers(event.key.keysym.mod));
        } else {
            if (event.type == SDL_TEXTEDITING)
                platform_->ime.HandleEdit(event.edit);
            int w = 0, h = 0, pw = 0, ph = 0;
            SDL_GetWindowSize(window_, &w, &h);
            SDL_GL_GetDrawableSize(window_, &pw, &ph);
            if (w > 0 && h > 0) {
                if (event.type == SDL_MOUSEMOTION) {
                    event.motion.x = int(double(event.motion.x) * pw / w);
                    event.motion.y = int(double(event.motion.y) * ph / h);
                }
                if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
                    event.button.x = int(double(event.button.x) * pw / w);
                    event.button.y = int(double(event.button.y) * ph / h);
                    context_->ProcessMouseMove(event.button.x, event.button.y, RmlSDL::GetKeyModifierState());
                }
            }
            RmlSDL::InputEventHandler(context_, window_, event);
        }
        if (event.type == SDL_WINDOWEVENT)
            resize();
        got = SDL_PollEvent(&event) != 0;
    }
    if (redraw && !closed_) {
        context_->Update();
        renderer_.BeginFrame();
        renderer_.Clear();
        context_->Render();
        renderer_.EndFrame();
        SDL_GL_SwapWindow(window_);
        dirty_ = false;
    }
}
} // namespace bmh::rml
