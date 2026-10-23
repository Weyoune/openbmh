// SPDX-License-Identifier: GPL-3.0-only
// Exercise the production control builder and stylesheet with synthetic labels.
#include "modern/ui/controls.hpp"
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace bmh::modern_ui;
namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
struct Renderer final : Rml::RenderInterface {
  Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>,
                                              Rml::Span<const int>) override {
    return 1;
  }
  void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f,
                      Rml::TextureHandle) override {}
  void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
  Rml::TextureHandle LoadTexture(Rml::Vector2i &,
                                 const Rml::String &) override {
    return 0;
  }
  Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>,
                                     Rml::Vector2i) override {
    return 1;
  }
  void ReleaseTexture(Rml::TextureHandle) override {}
  void EnableScissorRegion(bool) override {}
  void SetScissorRegion(Rml::Rectanglei) override {}
};
struct System final : Rml::SystemInterface {
  size_t stylesheet_errors = 0;
  bool LogMessage(Rml::Log::Type type, const Rml::String &message) override {
    if ((type == Rml::Log::LT_WARNING || type == Rml::Log::LT_ERROR) &&
        (message.find("property") != message.npos ||
         message.find("style") != message.npos ||
         message.find("Syntax error") != message.npos)) {
      ++stylesheet_errors;
      std::cerr << message << '\n';
    }
    return true;
  }
};
void text_centers(Rml::Element &node, float cx, float cy, bool horizontal,
                  size_t &lines) {
  if (auto *text = dynamic_cast<Rml::ElementText *>(&node)) {
    const auto origin = text->GetAbsoluteOffset();
    const auto metrics = Rml::GetFontEngineInterface()->GetFontMetrics(
        text->GetFontFaceHandle());
    for (const auto &line : text->GetLines()) {
      if (line.text.empty())
        continue;
      ++lines;
      const float x = origin.x + line.position.x + line.width / 2.f;
      const float y = origin.y + line.position.y - metrics.ascent +
                      (metrics.ascent + metrics.descent) / 2.f;
      require(std::abs(y - cy) <= .8f,
              "Control text is not vertically centered");
      if (horizontal)
        require(std::abs(x - cx) <= .8f,
                "Control text is not horizontally centered");
    }
  }
  for (int i = 0; i < node.GetNumChildren(); ++i)
    text_centers(*node.GetChild(i), cx, cy, horizontal, lines);
}
void centered_text(Rml::Element &text_root, Rml::Element &control,
                   bool horizontal = true) {
  const auto p = control.GetAbsoluteOffset(Rml::BoxArea::Border),
             s = control.GetBox().GetSize(Rml::BoxArea::Border);
  size_t lines = 0;
  text_centers(text_root, p.x + s.x / 2, p.y + s.y / 2, horizontal, lines);
  require(lines == 1, "A control label disappeared or wrapped");
}
} // namespace
int main(int argc, char **argv) try {
  require(argc == 2, "Pass the Modern asset directory");
  const auto root = std::filesystem::path(argv[1]);
  Renderer renderer;
  System system;
  Rml::SetRenderInterface(&renderer);
  Rml::SetSystemInterface(&system);
  require(Rml::Initialise(), "Rml initialization");
  require(Rml::LoadFontFace((root / "LatoLatin-Regular.ttf").string(), "Modern",
                            Rml::Style::FontStyle::Normal,
                            Rml::Style::FontWeight::Normal),
          "Font missing");
  require(Rml::LoadFontFace((root / "LatoLatin-Bold.ttf").string(), "Modern",
                            Rml::Style::FontStyle::Normal,
                            Rml::Style::FontWeight::Bold),
          "Bold font missing");
  std::ostringstream css;
  for (const auto *name : {"theme.rcss", "ticket.rcss", "sports.rcss",
                            "calendar.rcss", "reports.rcss", "report_details.rcss",
                            "report_pickers.rcss"}) {
    std::ifstream input(root / name);
    require(bool(input), "Production stylesheet missing");
    css << input.rdbuf() << '\n';
  }
  for (const auto *theme : {"midnight", "silver", "matchday"})
    for (float density : {1.f / 3, .5f, 1.f, 2.f}) {
      auto *context = Rml::CreateContext(
          "controls", {int(3840 * density), int(2160 * density)});
      require(context, "Context missing");
      context->SetDensityIndependentPixelRatio(density);
      const auto select = [](const std::string &id) {
        return "<select id='" + id +
               "'><option value='a' selected>Option A</option><option "
               "value='b'>Option B</option><option value='c'>Option "
               "C</option></select>";
      };
      std::string content =
          button_markup("decrease", "−", {},
                        "id='step' "
                        "style='position:absolute;left:40dp;top:40dp;width:"
                        "94dp;height:124dp;font-size:72dp;padding:0;' ");
      content += button_markup(
          "region", "Region", {},
          "id='region' "
          "style='position:absolute;left:200dp;top:40dp;min-height:86dp;' ");
      content += button_markup("list", "Sample slot", "choice",
                               "id='list' "
                               "style='position:absolute;left:40dp;top:220dp;"
                               "width:600dp;height:120dp;text-align:left;' ");
      content += button_markup(
          "rich",
          "<div class='picture' style='height:100dp;'>Image</div><p "
          "class='caption'>Portrait</p>",
          "choice",
          "id='rich' "
          "style='position:absolute;left:700dp;top:40dp;width:250dp;height:"
          "260dp;' ");
      content +=
          "<div "
          "style='position:absolute;left:40dp;top:400dp;width:2608dp;height:"
          "1000dp;'>" +
          select("natural") +
          "<div class='toolbar' "
          "style='position:absolute;top:140dp;width:2500dp;'>" +
          select("toolbar") +
          "</div><div style='position:absolute;top:280dp;width:1200dp;'>" +
          select("manager-strength") + "</div><div class='bottom-controls'>" +
          button_markup("continue", "Continue", {}, "id='footer'") +
          select("tall") + "</div></div>";
      auto *doc = context->LoadDocumentFromMemory(
          "<rml><head><style>" + css.str() + "</style></head><body class='" +
          theme + "'>" + content + "</body></rml>");
      require(doc, "Document missing");
      require(system.stylesheet_errors == 0,
              "Rml rejected a production stylesheet declaration");
      doc->Show();
      context->Update();
      context->Render();
      for (int i = 0; i < 3; ++i) {
        if (center_selects(*doc))
          context->Update();
        context->Render();
      }
      require(!center_selects(*doc), "Control centering does not settle");
      for (const auto *id : {"step", "region", "footer"}) {
        auto *button = doc->GetElementById(id);
        require(button, "Button missing");
        centered_text(*button, *button);
      }
      auto *list = doc->GetElementById("list");
      centered_text(*list, *list, false);
      auto *rich = doc->GetElementById("rich");
      require(!rich->IsClassSet("control-button"),
              "Rich card became a horizontal text control");
      require(rich->QuerySelector(".caption")->GetAbsoluteOffset().y >
                  rich->QuerySelector(".picture")->GetAbsoluteOffset().y,
              "Rich card stacking changed");
      for (const auto *id :
           {"natural", "toolbar", "tall", "manager-strength"}) {
        auto *control = doc->GetElementById(id);
        auto *caption = select_part(*control, "selectvalue"),
             *arrow = select_part(*control, "selectarrow");
        require(caption && arrow, "Select parts missing");
        centered_text(*caption, *control, false);
        const auto y = control->GetAbsoluteOffset(Rml::BoxArea::Border).y +
                       control->GetBox().GetSize(Rml::BoxArea::Border).y / 2;
        require(std::abs(arrow->GetAbsoluteOffset(Rml::BoxArea::Border).y +
                         arrow->GetBox().GetSize(Rml::BoxArea::Border).y / 2 -
                         y) < .8f,
                "Dropdown arrow is off center");
        auto *select_control =
            dynamic_cast<Rml::ElementFormControlSelect *>(control);
        select_control->ShowSelectBox();
        context->Update();
        context->Render();
        auto *option = select_control->GetOption(0);
        require(option->GetComputedValues().font_size() ==
                    control->GetComputedValues().font_size(),
                "Dropdown popup font differs from its caption");
        require(option->GetComputedValues().line_height().value <
                        control->GetBox().GetSize(Rml::BoxArea::Content).y ||
                    std::string(id) == "natural",
                "Caption centering leaked into popup rows");
        select_control->CancelSelectBox();
        context->Update();
      }
      doc->Close();
      Rml::RemoveContext("controls");
    }
  Rml::Shutdown();
  std::cout << "Control text, dropdown arrows, popup fonts and rich cards "
               "passed in 3 themes at 4 densities\n";
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
