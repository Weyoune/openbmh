// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "formats/vga.hpp"
#include "formats/textd.hpp"
#include "formats/font.hpp"
#include "formats/le.hpp"
#include "game/highscore.hpp"

namespace bmh::ui {

// EXE-backed Source word/hyphen wrapping, exposed for deterministic presenter
// checks and reuse by text-only UI code.
std::vector<std::string> wrap_source_text(const std::filesystem::path& data,
                                         std::string_view text, int max_width);
// RealityReporterPanel2/RealityReporterPanel3: full overflow rows may use flag2 (blocksatz); the final
// flush is never justified. Text and returned rows use Original bytes.
std::vector<std::string> wrap_source_text(const Font&, const LeImage&,
                                         std::string_view text, int max_width,
                                         bool justify = false);

struct GameEndRenderOptions {
    bool plain_background = false;        // Original PlainBackground, not its inverse
    bool gradient_text = true;        // TextGradientDisabled==0 && BMH.exe[TextGradientMode]==2
    bool preserve_restored_backdrop = false; // Nested construction notices retain their owning screen.
};

// Rendering-only projection. Names retain the Source ISO646-DE bytes; values and order have
// already been resolved by the caller. No career state is mutated here.
class GameEndHighscoreScreen {
public:
    GameEndHighscoreScreen(const std::filesystem::path& data, HighscoreProjection projection,
                           GameEndRenderOptions options = {});
    Image render() const;
private:
    HighscoreProjection projection_;
    GameEndRenderOptions options_;
    Image background_, logo_;
    TextDb texts_;
    struct FontData;
    std::shared_ptr<FontData> font_;
};

// Source's two end-of-career messages; drawing only, with acknowledgement owned
// by the application loop (the Original then waits and exits the process).
class GameEndFinalScreen {
public:
    explicit GameEndFinalScreen(const std::filesystem::path& data, GameEndRenderOptions options = {});
    Image render() const;
private:
    Image background_;
    GameEndRenderOptions options_;
    TextDb texts_;
    struct FontData;
    std::shared_ptr<FontData> font_;
};

enum class TaxDialogKind { acknowledgement, question };
// PresentLoanReturn: Original full-screen program-error diagnostic, no manager identity.
class ProgramErrorScreen {
public:
    ProgramErrorScreen(const std::filesystem::path& data, uint16_t code,
                       GameEndRenderOptions options = {});
    Image render() const;
private:
    uint16_t code_;
    GameEndRenderOptions options_;
    TextDb texts_;
    struct FontData;
    std::shared_ptr<FontData> font_;
};
struct TaxDialogProjection {
    TaxDialogKind kind = TaxDialogKind::acknowledgement;
    bool compact_dialog_frame = false; // PresentManagerDialog selects its frame shadow path from this flag
    uint8_t source_manager_flags = 0; // PresentManagerDialog param1; 0x40 timed, 0x80 interactive ACK
    uint16_t source_flags = 0; // PresentManagerDialog param3; selects labels in the loaded TEXTD pointer table
    bool source_system_identity = false; // PresentManagerDialog low-six-bit identity 4 (SystemManagerNameText, portrait 14 twice)
    std::string manager_name;
    std::string body; // exact, already-composed Source ISO646-DE text
    int portrait = 0;
    bool badge_fallback = false; // Source runtime club gate, distinct from sprite identity
    int badge_sprite = 0; // Source selector game.club_heap_78[team*2] & 0x1ff
};

class GameEndTaxDialog {
public:
    enum class Button { none, acknowledge, yes, no };
    GameEndTaxDialog(const std::filesystem::path& data, TaxDialogProjection projection,
                     GameEndRenderOptions options = {}, std::optional<Image> restored_backdrop = {});
    Image backdrop() const;
    Image render(std::optional<Button> pressed = {}) const;
    // The hit test reports only which Original button was clicked. It does not
    // acknowledge or decide the tax event; the caller owns that transaction.
    Button hit_test(int x, int y) const;
    bool source_timed_notice() const {
        return projection_.kind == TaxDialogKind::acknowledgement &&
               (projection_.source_manager_flags & 0xc0) == 0x40;
    }
private:
    TaxDialogProjection projection_;
    GameEndRenderOptions options_;
    std::optional<Image> restored_backdrop_;
    Image background_, widgets_, faces_, badge_;
    TextDb texts_;
    struct FontData;
    std::shared_ptr<FontData> font_;
};

class SpriteFont;
// Borrow the caller's already loaded Original assets. All manager notices,
// including the existing season-end dialog, share PresentManagerDialog's drawing path.
// SourceBadgeFallback classifies the raw club token before reading its badge selector.
bool source_badge_fallback(int club_token);
struct SourceDialogAssets {
    const Image& widgets;
    const Image& faces;
    const Image* badge;
    const Font& small;
    const Font& heading;
    const LeImage& exe;
    const SpriteFont& sprite;
    const TextDb& texts;
};
void draw_source_dialog(Image&, const SourceDialogAssets&, const TaxDialogProjection&,
                        bool gradient,
                        std::optional<GameEndTaxDialog::Button> pressed = {},
                        bool preserve_backdrop = false);

} // namespace bmh::ui
