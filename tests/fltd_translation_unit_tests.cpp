// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "launch/fltd.hpp"
#include "game/environment.hpp"
#include "formats/league_visibility.hpp"

#include <chrono>
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

int main() {
    using namespace bmh;
    std::filesystem::path root;
    try {
        root = std::filesystem::temp_directory_path() / ("openbmh-fltd-map-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(root);
        // Authored X-32-shaped data: only tested source slots have nonzero bytes.
        Bytes source(507925);
        source[0] = 'M'; source[1] = 'Z';
        const auto put = [&](uint32_t address, uint8_t value) { source.at(address + 0x496d) = value; };
        const auto check = [](bool value, const char* message) { if (!value) throw std::runtime_error(message); };
        const auto put_pointer = [&](uint32_t address, uint32_t value) {
            for (uint32_t i = 0; i < 4; ++i) put(address + i, uint8_t(value >> (i * 8)));
        };
        constexpr uint32_t english_overwrite_pointer = 0x6c43c;
        constexpr uint32_t authored_question_address = layout::EnDataStart + 0x100;
        const std::string authored_question = "Authored overwrite question?";
        put_pointer(english_overwrite_pointer, authored_question_address);
        for (uint32_t i = 0; i <= authored_question.size(); ++i)
            put(authored_question_address + i, i < authored_question.size() ? uint8_t(authored_question[i]) : 0);
        // Adjacent English pointers are intentionally distinct from the retained
        // empty German slots. An over-wide copy must not import either one.
        put_pointer(english_overwrite_pointer - 4, authored_question_address + 1);
        put_pointer(english_overwrite_pointer + 4, authored_question_address + 2);
        // The analytics label mapping intentionally replaces the final 72 bytes
        // of the old empty-pointer range. Give that independent source its own data.
        Bytes analytics_labels(72);
        for (uint32_t i = 0; i < analytics_labels.size(); ++i) {
            analytics_labels[i] = uint8_t(0x31 + i * 3);
            put(layout::EnglishLayoutTranslation50 + i, analytics_labels[i]);
        }
        put(layout::EnglishLayoutTranslation259,5);put(layout::EnglishLayoutTranslation259+1,4);put(layout::EnglishLayoutTranslation259+2,9);
        const uint8_t toggle_rows[] = {3, 7, 11, 13, 17, 19};
        put(layout::EnglishLayoutTranslation16 - 1, 99); // alignment padding is not a toggle row
        for (uint32_t i = 0; i < 6; ++i) put(layout::EnglishLayoutTranslation16 + i, toggle_rows[i]);
        put(layout::EnglishLayoutTranslation73 - 1, 0xfb); // negative colour precedes positive colour
        put(layout::EnglishLayoutTranslation73, 0x47);
        for (uint32_t i = 0; i < 2; ++i) {
            put(layout::EnglishDesktopBinSourceX + i, uint8_t(10 + i));
            put(layout::EnglishDesktopBinSourceY + i, uint8_t(20 + i));
            put(layout::EnglishDesktopSocketSourceX + i, uint8_t(30 + i));
        }
        const std::array<uint8_t,6> environment_x{20,80,140,200,250,20},environment_y{40,40,40,80,80,140},environment_steps{2,4,5,5,5,5};
        for(uint32_t i=0;i<6;++i){put(0x6d230+i,environment_x[i]);put(0x6d238+i,environment_y[i]);put(0x6d244+i,environment_steps[i]);}
        const std::array<uint8_t,3> youth_colors{0x41,0x42,0x43};for(uint32_t i=0;i<3;++i)put(0x6d240+i,youth_colors[i]);
        const std::string position_suffix=". / ",cost_suffix=".";
        for(uint32_t i=0;i<=position_suffix.size();++i)put(0x6d24a+i,i<position_suffix.size()?uint8_t(position_suffix[i]):0);
        for(uint32_t i=0;i<=cost_suffix.size();++i)put(0x6d24f+i,i<cost_suffix.size()?uint8_t(cost_suffix[i]):0);
        const auto path = root / "FLTD.EXE";
        { std::ofstream out(path, std::ios::binary); out.write(reinterpret_cast<const char*>(source.data()), std::streamsize(source.size())); }
        const auto image = launch::translate_fltd(path);
        check(image.u32(layout::SaveLoad_OverwriteQuestionPointer) == authored_question_address,
              "overwrite question pointer must select the original English source slot");
        check(image.string_at_pointer(layout::SaveLoad_OverwriteQuestionPointer) == authored_question,
              "overwrite question must retain its complete source text");
        for (uint32_t address = layout::Bank_OriginalToUtf8_RuntimeField;
             address < layout::Analytics_FormatError_ExecutableField; address += 4) {
            if (address == layout::SaveLoad_OverwriteQuestionPointer) continue;
            check(image.u32(address) == layout::EmptyString,
                  "mapping the overwrite question must preserve all seventeen other empty pointers before analytics");
        }
        for (uint32_t i = 0; i < analytics_labels.size(); ++i)
            check(image.u8(layout::Analytics_FormatError_ExecutableField + i) == analytics_labels[i],
                  "mapping the overwrite question must preserve the complete independent analytics mapping");
        for (uint32_t i = 0; i < 6; ++i)
            check(image.u8(layout::Setup_FormatError_RuntimeField8 + i) == toggle_rows[i], "toggle mapping includes alignment padding");
        check(image.u8(layout::Desktop_Copy_TextPointer) == 0xfb, "negative balance colour");
        check(image.u8(layout::Desktop_Copy_TextPointer + 1) == 0x47, "positive balance must use the next source byte");
        for (uint32_t i = 0; i < 2; ++i) {
            check(image.u8(layout::Desktop_Render_RuntimeField + i) == 10 + i, "bin source x");
            check(image.u8(layout::Desktop_Render_RuntimeField2 + i) == 20 + i, "bin source y");
            check(image.u8(layout::Desktop_Render_RuntimeField3 + i) == 30 + i, "socket source x");
        }
        for(uint32_t i=0;i<6;++i)check(image.u8(layout::Environment_FormatError_RuntimeField+i)==environment_x[i]&&image.u8(layout::Environment_FormatError_RuntimeField2+i)==environment_y[i]&&image.u8(layout::EnvironmentBudgetSteps+i)==environment_steps[i],"English environment geometry/steps lost source table alignment");
        for(uint32_t i=0;i<3;++i)check(image.u8(layout::EnvironmentYouthColors+i)==youth_colors[i],"English youth colours lost source alignment");
        check(image.cstring(layout::EnvironmentYouthPositionSuffix)==position_suffix&&image.cstring(layout::EnvironmentCostSuffix)==cost_suffix,"English environment suffix mapping is incomplete");
        install_environment_rules(image);check(environment_rules_supported(),"authored English numeric profile depends on source instructions");
        check(player_league_count(image)==4&&image.u8(layout::LeaguePairings_LeaguePairingsConstants_RuntimeField)==5,"English visible league count overwrote internal leagues");
        std::filesystem::remove_all(root);
        std::cout << "FLTD setup, colour, desktop-control and overwrite-question mappings passed\n";
        return 0;
    } catch (const std::exception& error) {
        if (!root.empty()) { std::error_code ec; std::filesystem::remove_all(root, ec); }
        std::cerr << error.what() << '\n'; return 1;
    }
}
