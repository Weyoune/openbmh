// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <string>
#include <vector>
#include "formats/font.hpp"
#include "formats/le.hpp"

namespace bmh::ui {
struct RealityReporterLayoutState {
    int next_baseline = 46; // bdba2; origin46, x66, width189, flags6 in RealityReporterPanel
    std::string pending;   // bdc3e
    std::vector<int> word_ends; // bdba8; excludes last boundary during justification
};
struct RealityReporterLine {
    int baseline = 0;
    std::string text;
    bool justified = false, hyphenated = false, scrolled = false;
};
// RealityReporterPanel2/RealityReporterPanel3 and Source syllable helpers696e3..InsertsRealSpacesAtRecordedWordEnds. Receives already
// selected Original-byte text, draws into a caller-owned 320x240 surface.
// There is no template evaluation, GameState access, timer or RNG here.
class RealityReporterPanel {
public:
    explicit RealityReporterPanel(const std::filesystem::path& data);
    std::vector<RealityReporterLine> append(Image&,RealityReporterLayoutState&,
        const std::vector<std::string>& text,bool gradient = true) const;
private:
    Font font_;
    LeImage exe_;
    std::string vowels_;
    std::vector<std::string> clusters_;
    bool split_allowed(std::string_view,size_t,size_t) const;
    int hyphenate(std::string&,int pending_width) const;
    void draw(Image&,std::string_view,int baseline,bool gradient) const;
    void next_line(Image&,RealityReporterLayoutState&,RealityReporterLine&) const;
};
} // namespace bmh::ui
