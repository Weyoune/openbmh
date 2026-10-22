// SPDX-License-Identifier: GPL-3.0-only
#include <RmlUi/Core.h>
#include "launch/rml_path.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
struct System : Rml::SystemInterface {
    std::string errors;
    bool LogMessage(Rml::Log::Type type, const Rml::String &text) override {
        if (type <= Rml::Log::LT_WARNING)
            errors += text + "\n";
        return true;
    }
};
struct Renderer : Rml::RenderInterface {
    size_t rendered_vertices = 0;
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int>) override {
        return Rml::CompiledGeometryHandle(vertices.size());
    }
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f, Rml::TextureHandle) override {
        rendered_vertices += size_t(geometry);
    }
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i &dimensions, const Rml::String &name) override {
        auto *files = Rml::GetFileInterface();
        const auto input = files->Open(name);
        if (!input)
            throw std::runtime_error("Stadium asset unavailable: " + name);
        unsigned char header[18]{};
        const auto read = files->Read(header, sizeof(header), input);
        files->Close(input);
        if (read != sizeof(header) || header[2] != 2 || header[16] != 24)
            throw std::runtime_error("Invalid stadium asset: " + name);
        dimensions = {header[12] + 256 * header[13], header[14] + 256 * header[15]};
        return 1;
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return 2; }
    void ReleaseTexture(Rml::TextureHandle) override {}
};
void require(bool value, const std::string &error) {
    if (!value)
        throw std::runtime_error(error);
}
} // namespace
int main(int argc, char **argv) try {
    require(argc == 2 || (argc == 3 && std::string(argv[2]) == "unicode"), "launcher asset directory required");
    std::filesystem::path assets(argv[1]);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            if (!path.empty()) {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        }
    } cleanup;
    if (argc == 3) {
        cleanup.path = std::filesystem::temp_directory_path() /
            ("openbmh-rml-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(cleanup.path);
        const auto unicode = cleanup.path / std::filesystem::path(u8"Fußball 日本語") / "launcher";
        std::filesystem::create_directories(unicode);
        std::filesystem::copy(assets, unicode, std::filesystem::copy_options::recursive);
        assets = unicode;
    }
    System system;
    Renderer renderer;
    bmh::rml::AssetFiles files;
    Rml::SetSystemInterface(&system);
    Rml::SetRenderInterface(&renderer);
    Rml::SetFileInterface(&files);
    require(Rml::Initialise(), "RmlUI initialization");
    struct Shutdown {
        ~Shutdown() { Rml::Shutdown(); }
    } shutdown;
    for (const auto *weight : {"Regular", "Bold"})
        require(Rml::LoadFontFace(bmh::rml::asset_path(assets / (std::string("LatoLatin-") + weight + ".ttf"))),
                "font load");
    auto *context = Rml::CreateContext("test", {1100, 688});
    auto *document = context->LoadDocument(bmh::rml::asset_path(assets / "launcher.rml"));
    require(document != nullptr, "launcher document load");
    document->Show();
    for (const auto size : {Rml::Vector2i(1100, 688), Rml::Vector2i(800, 540), Rml::Vector2i(2200, 1376)}) {
        context->SetDimensions(size);
        context->SetDensityIndependentPixelRatio(float(size.x) / 1100);
        context->Update();
        context->Render();
        for (const auto *id : {"start", "add", "delete", "rename", "end"}) {
            auto *button = document->GetElementById(id);
            require(button != nullptr, "missing launcher action");
            const auto origin = button->GetAbsoluteOffset(Rml::BoxArea::Border);
            const auto dimensions = button->GetBox().GetSize(Rml::BoxArea::Border);
            require(origin.x >= 0 && origin.y >= 0 && origin.x + dimensions.x <= size.x + 1 &&
                        origin.y + dimensions.y <= size.y + 1 && dimensions.y >= 30,
                    "action outside viewport or too small");
            require(button->GetNumChildren() >= 2, "action lost its visible text label");
        }
        require(!document->GetElementById("overlay")->IsVisible(), "modal unexpectedly visible");
    }
    require(renderer.rendered_vertices > 100, "launcher did not render");
    require(system.errors.empty(), system.errors);
    std::cout << "Launcher RML/RCSS, fonts, background, visible labels and responsive layout passed\n";
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
