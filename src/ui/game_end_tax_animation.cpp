// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/game_end_tax_animation.hpp"

#include <utility>

#include "formats/le.hpp"

namespace bmh::ui {
std::filesystem::path asset_path(const std::filesystem::path&, const std::string&);
namespace {
void valid_image(const Image& image, int width, int height, const char* message) {
    if (image.width<width || image.height<height || image.width<0 || image.height<0 ||
        image.pixels.size()!=size_t(image.width)*size_t(image.height)) throw FormatError(message);
}
void copy(Image& target, const Image& source, const TaxAnimationCopy& rectangle) {
    for (unsigned y=0; y<rectangle.height; ++y)
        for (unsigned x=0; x<rectangle.width; ++x)
            target.at(int(rectangle.x+x),int(rectangle.y+y))=
                source.pixels[size_t(rectangle.source_y+y)*source.width+rectangle.source_x+x];
}
void frame(Image& target, const Image& background, bool flat) {
    // LoadGameEndTaxAnimationAssets(NULL,93,71,225,168): interior131x96; no shadow or fullscreen clear.
    if (flat)
        for (int y=71; y<=168; ++y) for (int x=93; x<=225; ++x) target.at(x,y)=0x42;
    else copy(target,background,{93,52,94,72,131,96});
    // PaintsFourBorderEdges ordered inclusive lines: left/top0x46, then bottom/right0x40.
    for (int y=71; y<=168; ++y) target.at(93,y)=0x46;
    for (int x=93; x<=225; ++x) target.at(x,71)=0x46;
    for (int x=93; x<=225; ++x) target.at(x,168)=0x40;
    for (int y=71; y<=168; ++y) target.at(225,y)=0x40;
}
}

std::vector<TaxAnimationStep> decode_tax_animation_script(std::span<const uint8_t> bytes) {
    std::vector<TaxAnimationStep> steps;
    std::vector<TaxAnimationCopy> copies;
    size_t offset=0;
    while (offset<bytes.size()) {
        const unsigned token=bytes[offset];
        if (token==255) {
            if (!copies.empty()) throw FormatError("tax animation has unpresented trailing rectangles");
            return steps;
        }
        if (token==0) {
            if (bytes.size()-offset<7) throw FormatError("truncated tax animation rectangle");
            copies.push_back({bytes[offset+1],bytes[offset+2],bytes[offset+3],bytes[offset+4],
                              bytes[offset+5],bytes[offset+6]});
            offset+=7;
        } else {
            steps.push_back({std::move(copies),token<=100?token:token-100,token<=100,unsigned(offset)});
            copies.clear();++offset;
        }
    }
    throw FormatError("tax animation script has no terminator");
}

GameEndTaxAnimationAssets load_game_end_tax_animation_assets(const std::filesystem::path& data) {
    const auto exe=load_le(asset_path(data,"BMH.exe"));
    std::vector<uint8_t> script;
    // Read complete commands so zero/255 inside rectangles cannot end the script.
    uint32_t address=::bmh::layout::GameEndTaxAnimation_LoadGameEndTaxAnimationAssets_ExecutableField;
    while (script.size()<4096) {
        if (!exe.contains(address)) throw FormatError("truncated EXE tax animation script");
        const auto token=exe.u8(address);
        const unsigned length=token==0?7:1;
        if (!exe.contains(address+length-1)) throw FormatError("truncated EXE tax animation rectangle");
        for (unsigned i=0; i<length; ++i) script.push_back(exe.u8(address+i));
        address+=length;
        if (token==255)
            return {load_vga(asset_path(data/"KB","17.VGA")),load_vga(asset_path(data/"KB","56.VGA")),
                    decode_tax_animation_script(script)};
    }
    throw FormatError("unterminated EXE tax animation script");
}

GameEndTaxAnimationScreen::GameEndTaxAnimationScreen(GameEndTaxAnimationAssets assets,
        unsigned variant, Image backdrop, bool flat_background)
    : GameEndTaxAnimationScreen(std::move(assets),variant,backdrop,backdrop,flat_background) {}

GameEndTaxAnimationScreen::GameEndTaxAnimationScreen(GameEndTaxAnimationAssets assets,
        unsigned variant, Image visible, Image drawing, bool flat_background)
    : assets_(std::move(assets)),variant_(variant),visible_(std::move(visible)),drawing_(std::move(drawing)) {
    if (variant<1 || variant>4 || variant>assets_.steps.size()) throw FormatError("invalid tax animation variant");
    valid_image(visible_,320,240,"tax animation requires a visible320x240 frame");
    valid_image(drawing_,320,240,"tax animation requires a drawing320x240 frame");
    if (visible_.width!=320 || visible_.height!=240 || drawing_.width!=320 || drawing_.height!=240)
        throw FormatError("tax animation requires exact320x240 frames");
    valid_image(assets_.art,1,1,"tax animation requires Original KB17");
    if (!flat_background) valid_image(assets_.background,224,148,"tax animation requires Original KB56");
    for (const auto& step:assets_.steps) {
        if (step.wait_ticks<1 || step.wait_ticks>154) throw FormatError("invalid tax animation wait marker");
        for (const auto& r:step.copies)
            if (r.width==0 || r.height==0 || r.x>320 || r.width>320-r.x || r.y>240 || r.height>240-r.y ||
                r.source_x>unsigned(assets_.art.width) || r.width>unsigned(assets_.art.width)-r.source_x ||
                r.source_y>unsigned(assets_.art.height) || r.height>unsigned(assets_.art.height)-r.source_y)
                throw FormatError("invalid tax animation copy rectangle");
    }
    frame(drawing_,assets_.background,flat_background);
}
bool GameEndTaxAnimationScreen::advance_step() {
    if (finished()) return false;
    const auto& step=assets_.steps[steps_presented_];
    for (const auto& rectangle:step.copies) copy(drawing_,assets_.art,rectangle);
    std::swap(visible_,drawing_); // DecodeTaxAnimationScript always swaps these two buffers.
    if (step.copy_after_present) drawing_=visible_; // TotalMilliseconds full image copy, only param1!=0.
    wait_milliseconds_=step.wait_ticks*20;
    ++steps_presented_;
    return true;
}
unsigned GameEndTaxAnimationScreen::total_milliseconds() const {
    unsigned total=0;
    for (unsigned i=0; i<variant_; ++i) total+=assets_.steps[i].wait_ticks*20;
    return total;
}

} // namespace bmh::ui
