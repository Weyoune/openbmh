// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <filesystem>
#include <span>
#include <vector>

#include "formats/vga.hpp"

namespace bmh::ui {

struct TaxAnimationCopy {
    unsigned source_x=0, source_y=0, x=0, y=0, width=0, height=0;
    bool operator==(const TaxAnimationCopy&) const = default;
};
struct TaxAnimationStep {
    std::vector<TaxAnimationCopy> copies;
    unsigned wait_ticks=0; // DecodeTaxAnimationScript2(n) passes n*20 milliseconds to the DOS delay.
    bool copy_after_present=true; // DecodeTaxAnimationScript(1), versus flip-only DecodeTaxAnimationScript(0).
    unsigned script_offset=0; // offset of the wait marker within EXE GameEndTaxAnimation_LoadGameEndTaxAnimationAssets_ExecutableField script
};
// 0 + six rectangle bytes = copy;1..100 = present(1)/wait(n);
// 101..254 = present(0)/wait(n-100);255 = end. Six bytes are never opcodes.
std::vector<TaxAnimationStep> decode_tax_animation_script(std::span<const uint8_t> bytes);

struct GameEndTaxAnimationAssets {
    Image art;        // Original KB17,248x69 in the configured installation
    Image background; // Original KB56; LoadGameEndTaxAnimationAssets pattern starts at source93/52
    std::vector<TaxAnimationStep> steps; // Original EXE GameEndTaxAnimation_LoadGameEndTaxAnimationAssets_ExecutableField
};
GameEndTaxAnimationAssets load_game_end_tax_animation_assets(const std::filesystem::path& data);

// GameEndTaxAnimationScreen calls SourceInput with variant1..4 and param4=0 (no interrupt input).
// This owned renderer has no GameState/RNG/event loop; callers present and wait.
class GameEndTaxAnimationScreen {
public:
    GameEndTaxAnimationScreen(GameEndTaxAnimationAssets assets, unsigned variant,
                             Image backdrop, bool flat_background=false);
    // Explicit two-buffer form for callers with distinct visible/drawing frames.
    GameEndTaxAnimationScreen(GameEndTaxAnimationAssets assets, unsigned variant,
                             Image visible, Image drawing, bool flat_background);
    bool advance_step(); // copies rectangles, presents by swapping, then optional full-frame copy
    bool finished() const { return steps_presented_==variant_; }
    unsigned steps_presented() const { return steps_presented_; }
    unsigned wait_milliseconds() const { return wait_milliseconds_; }
    unsigned total_milliseconds() const;
    Image render() const { return visible_; }
    // After present(0), the next Source operation draws on the older buffer.
    // Use this frame when constructing the following outcome dialog's backdrop.
    Image drawing_frame() const { return drawing_; }
private:
    GameEndTaxAnimationAssets assets_;
    unsigned variant_, steps_presented_=0, wait_milliseconds_=0;
    Image visible_, drawing_;
};

} // namespace bmh::ui
