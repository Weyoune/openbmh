// SPDX-License-Identifier: GPL-3.0-only
// Synthetic document test; no original content or window server required.
#include "modern/ui/ticket_popover.hpp"
#include <RmlUi/Core.h>
#include <filesystem>
#include <stdexcept>
#define CHECK(value)                                                           \
  do {                                                                         \
    if (!(value))                                                              \
      throw std::runtime_error("Check failed: " #value);                       \
  } while (false)
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
using namespace bmh::modern_ui;
struct NullRenderer final : Rml::RenderInterface {
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
struct Fake final : DataSource {
  Snapshot s{1, {}, {{"ticket", 13}}};
  bool fail = false;
  int applies = 0;
  Snapshot snapshot() const override { return s; }
  ActionResult apply(const Action &a) override {
    ++applies;
    CHECK(a.kind == "ticket" && a.revision == s.revision);
    if (fail)
      return {false, s.revision, "Speicher voll. Bitte erneut versuchen."};
    s.state["ticket"] = a.value;
    return {true, ++s.revision, {}};
  }
};
struct Spy final : Rml::EventListener {
  int count = 0;
  void ProcessEvent(Rml::Event &e) override {
    ++count;
    std::cerr << "LEAK " << e.GetType() << " " << e.GetTargetElement()->GetId()
              << "\n";
  }
};
int main(int argc, char **argv) try {
  CHECK(argc >= 2);
  const auto assets = std::filesystem::path(argv[1]);
  const std::string theme_name = argc > 2 ? argv[2] : "midnight";
  NullRenderer renderer;
  Rml::SystemInterface system;
  Rml::SetRenderInterface(&renderer);
  Rml::SetSystemInterface(&system);
  CHECK(Rml::Initialise());
  CHECK(Rml::LoadFontFace((assets / "LatoLatin-Regular.ttf").string(), "Modern",
                          Rml::Style::FontStyle::Normal,
                          Rml::Style::FontWeight::Normal));
  CHECK(Rml::LoadFontFace((assets / "LatoLatin-Bold.ttf").string(), "Modern",
                          Rml::Style::FontStyle::Normal,
                          Rml::Style::FontWeight::Bold));
  auto *context = Rml::CreateContext("ticket-fixture", {1280, 720});
  CHECK(context);
  context->SetDensityIndependentPixelRatio(1.f / 3);
  std::ifstream theme(assets / "theme.rcss"), ticket(assets / "ticket.rcss");
  std::ostringstream css;
  css << theme.rdbuf() << ticket.rdbuf();
  auto *doc = context->LoadDocumentFromMemory(
      "<rml><head><style>" + css.str() + "</style></head><body class=\"" +
      theme_name +
      "\"><button id=\"desktop-ticket\" "
      "style=\"position:absolute;left:400px;top:200px;width:100px;height:40px;"
      "padding:0px;border-width:0px;\">13 €</button></body></rml>");
  CHECK(doc);
  doc->Show();
  context->Update();
  Fake source;
  int commits = 0;
  Spy spy;
  doc->AddEventListener("click", &spy);
  doc->AddEventListener("keydown", &spy);
  {
    TicketPopover popup(*context, *doc, source, [&] { ++commits; });
    auto *anchor = doc->GetElementById("desktop-ticket");
    auto node = [&](const char *id) {
      auto *n = doc->GetElementById(id);
      CHECK(n);
      return n;
    };
    auto key = [&](Rml::Input::KeyIdentifier k) {
      Rml::Dictionary p;
      p["key_identifier"] = int(k);
      node("native-ticket-range")->DispatchEvent("keydown", p);
      context->Update();
      popup.update();
    };
    auto click = [&](const char *action) {
      Rml::ElementList all;
      node("native-ticket-layer")
          ->QuerySelectorAll(all, std::string("[data-ticket-action=\"") +
                                      action + "\"]");
      CHECK(all.size() == 1);
      all[0]->DispatchEvent("click", {});
      context->Update();
      popup.update();
    };
    auto geometry = [&](float ratio) {
      context->Update();
      popup.update();
      context->Update();
      auto *p = node("native-ticket-panel");
      const auto b = p->GetBox().GetSize(Rml::BoxArea::Border);
      const auto at = p->GetAbsoluteOffset(Rml::BoxArea::Border);
      std::cerr << "geometry " << b.x << " " << b.y << " at " << at.x << " "
                << at.y << " font " << p->GetComputedValues().font_size()
                << "\n";
      CHECK(std::abs(b.x - 330 * ratio) < 1);
      CHECK(std::abs(b.y - 196 * ratio) < 1);
      CHECK(std::abs(
                node("native-ticket-range")->GetComputedValues().font_size() -
                14 * ratio) < .1);
      CHECK(at.x >= 8 * ratio - 1 && at.y >= 8 * ratio - 1);
    };
    CHECK(popup.open(anchor, 1));
    geometry(1);
    CHECK(source.applies == 0);
    key(Rml::Input::KI_END);
    CHECK(node("native-ticket-range")->GetAttribute<int>("value", -1) == 25);
    CHECK(source.s.state["ticket"] == 13);
    CHECK(spy.count == 0);
    click("review");
    CHECK(source.applies == 0);
    CHECK(node("native-ticket-panel")->GetInnerRML().find("13 €") !=
          std::string::npos);
    click("back");
    key(Rml::Input::KI_HOME);
    key(Rml::Input::KI_RIGHT);
    CHECK(node("native-ticket-range")->GetAttribute<int>("value", -1) == 1);
    click("review");
    source.fail = true;
    click("confirm");
    CHECK(popup.is_open() && source.s.state["ticket"] == 13 && commits == 0);
    CHECK(node("native-ticket-error")->GetInnerRML().find("Speicher") !=
          std::string::npos);
    source.fail = false;
    click("confirm");
    CHECK(!popup.is_open() && source.s.state["ticket"] == 1 && commits == 1);
    CHECK(context->GetFocusElement() == anchor);
    CHECK(spy.count == 0);
    CHECK(popup.open(anchor, 1));
    key(Rml::Input::KI_END);
    key(Rml::Input::KI_ESCAPE);
    CHECK(!popup.is_open() && source.s.state["ticket"] == 1);
    CHECK(popup.open(anchor, 1));
    node("native-ticket-layer")->DispatchEvent("click", {});
    CHECK(!popup.is_open());
    CHECK(popup.open(anchor, 1));
    source.s.revision++;
    popup.update();
    CHECK(!popup.is_open());
    context->SetDimensions({2560, 1440});
    context->SetDensityIndependentPixelRatio(2.f / 3);
    CHECK(popup.open(anchor, 2));
    geometry(2);
    popup.close();
    context->SetDimensions({1280, 720});
    context->SetDensityIndependentPixelRatio(1.f / 3);
    anchor->SetProperty("left", "1190px");
    anchor->SetProperty("top", "650px");
    context->Update();
    CHECK(popup.open(anchor, 1));
    geometry(1);
    CHECK(node("native-ticket-panel")
              ->GetAttribute<Rml::String>("data-side", "") == "above");
    auto at =
        node("native-ticket-panel")->GetAbsoluteOffset(Rml::BoxArea::Border);
    CHECK(at.x + 330 <= 1273);
    popup.close();
    CHECK(!popup.open(anchor, 1, 2, 25, 2));
    CHECK(!popup.is_open());
  }
  doc->Close();
  Rml::RemoveContext("ticket-fixture");
  Rml::Shutdown();
  std::cout << "PASS native ticket fixture\n";
} catch (const std::exception &e) {
  std::cerr << e.what() << "\n";
  return 1;
}
