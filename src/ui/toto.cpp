// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/toto.hpp"

#include <algorithm>
#include <bit>
#include <sstream>

#include "formats/vga.hpp"
#include "game/state.hpp"
#include "game/messages.hpp"
#include "ui/manager.hpp"
#include "ui/setup.hpp"

namespace bmh::ui {
namespace {
void copy(Image& out, const Image& src, int sx, int sy, int dx, int dy, int w, int h, int transparent = -1) {
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const uint8_t pixel = src.pixels.at(size_t(sy + y) * src.width + size_t(sx + x));
        if (pixel != transparent) out.at(dx + x, dy + y) = pixel;
    }
}
int source_cross_variant(int row, int column) {
    // CaseReadsGlobalPreviousTipsButCase temporarily replaces CareerSession_Save_RuntimeField and restores it before returning.
    // Reproduce its ten discarded draws and final draw without touching an
    // application generator (or the global draw hook).
    int32_t seed = row * 0x1e160 + column * 0x340f1;
    for (int i = 0; i < 11; ++i) seed = std::bit_cast<int32_t>(uint32_t(seed) * 16 + 0xa7) % 4000000;
    return seed * 3 / 4000000;
}
}

TotoState toto_from_game(const GameState& g, uint64_t revision) {
    constexpr uint32_t kCoupon = ::bmh::layout::Coupon;
    TotoState s;
    const int manager = int(g.memory.u8(::bmh::layout::ActiveManager));
    if (manager < 0 || size_t(manager) >= g.manager_teams.size()) throw FormatError("toto: invalid active manager");
    const size_t record = size_t(manager) * 0xb0c;
    if (record + 0x318 > g.manager_records.size() || !g.memory.has(kCoupon + uint32_t(manager) * 0x14 + 0x13))
        throw FormatError("toto: missing coupon or manager record");
    s.manager_slot = manager; s.runtime_club = g.manager_teams.at(size_t(manager)); s.revision = revision;
    s.balance = int32_t(uint32_t(g.manager_records.at(record + 0x314)) |
        uint32_t(g.manager_records.at(record + 0x315)) << 8 | uint32_t(g.manager_records.at(record + 0x316)) << 16 |
        uint32_t(g.manager_records.at(record + 0x317)) << 24);
    const auto end = std::find(g.manager_records.begin() + record + 0xa8,
        g.manager_records.begin() + record + 0xa8 + 26, uint8_t(0));
    s.manager_name.assign(g.manager_records.begin() + record + 0xa8, end);
    s.club_name = g.club_name(s.runtime_club); s.source = "engine_projection";
    s.gradient = !g.memory.has(::bmh::layout::TextGradientDisabled) || g.memory.u8(::bmh::layout::TextGradientDisabled) == 0;
    if (g.memory.has(::bmh::layout::CurrencyMultiplier + 3)) s.currency_multiplier = int32_t(g.memory.u32(::bmh::layout::CurrencyMultiplier));
    if (g.memory.has(::bmh::layout::CurrencyDivisor + 3)) s.currency_divisor = int32_t(g.memory.u32(::bmh::layout::CurrencyDivisor));
    if (s.currency_divisor == 0) throw FormatError("toto: invalid currency divisor");
    if (g.memory.has(::bmh::layout::Toto_FormatError_RuntimeField + 0x13)) {
        for (size_t i = 0; i < 11; ++i) s.previous_tips[i] = g.memory.u8(::bmh::layout::Toto_FormatError_RuntimeField + 8 + uint32_t(i));
    }
    const uint32_t at = kCoupon + uint32_t(manager) * 0x14;
    // CaseReadsGlobalPreviousTipsButCase case5 reads global previous tips, but case6 changes EBP
    // to this manager's coupon; cases7/8 retain it for hits and winnings.
    s.previous_winnings = g.memory.u32(at + 4);
    s.previous_correct = g.memory.u8(at + 0x13);
    s.stake = g.memory.u32(at) & 0x7fffffff;
    const uint8_t count = g.memory.u8(::bmh::layout::CareerSession_DispatchToto_RuntimeField);
    s.available = count == 11 && g.memory.has(::bmh::layout::DeskActions_SubmitPoolsCoupon_RuntimeField + 43);
    for (int i = 0; i < std::min<int>(count, 11); ++i) {
        TotoMatchState match;
        match.home = g.memory.u16(::bmh::layout::DeskActions_SubmitPoolsCoupon_RuntimeField + uint32_t(i) * 4);
        match.away = g.memory.u16(::bmh::layout::DeskActions_SubmitPoolsCoupon_RuntimeField + uint32_t(i) * 4 + 2);
        match.home_name = g.club_name(match.home); match.away_name = g.club_name(match.away);
        match.pick = g.memory.u8(at + 8 + uint32_t(i));
        // Void6(...,0xff,1), pure ratings only when the public SquadRange4
        // range actually covers a saved squad. Zero-length ranges are not a
        // rating projection, even where team_average has a non-random fallback.
        const auto has_saved_squad = [&](int team) {
            const auto range = squad_range(team);
            return range.count != 0 && range.first <= g.squad_slots.size() &&
                range.count <= g.squad_slots.size() - range.first;
        };
        if (has_saved_squad(match.home) && has_saved_squad(match.away)) {
            constexpr std::array<uint16_t, 5> flags{0xf1, 0xf2, 0xf4, 0xf8, 0xff};
            for (size_t n = 0; n < flags.size(); ++n) {
                match.home_ratings[n] = team_average(g, match.home, flags[n]).value;
                match.away_ratings[n] = team_average(g, match.away, flags[n]).value;
            }
            match.ratings_available = true;
        }
        if (match.pick > 2) throw FormatError("toto: invalid stored pick");
        s.matches.push_back(std::move(match));
    }
    return s;
}

std::string toto_state_json(const TotoState& s) {
    std::ostringstream out;
    out << "{\"screen\":\"toto\",\"source\":" << json_quote(s.source)
        << ",\"manager_slot\":" << s.manager_slot << ",\"runtime_club\":" << s.runtime_club
        << ",\"revision\":" << s.revision << ",\"manager_name\":" << json_quote(original_to_utf8(s.manager_name))
        << ",\"club_name\":" << json_quote(original_to_utf8(s.club_name)) << ",\"balance\":" << s.balance
        << ",\"stake\":" << s.stake << ",\"available\":" << (s.available ? "true" : "false") << ",\"matches\":[";
    for (size_t i = 0; i < s.matches.size(); ++i) {
        if (i) out << ',';
        const auto& m = s.matches[i];
        out << "{\"home\":" << m.home << ",\"away\":" << m.away << ",\"home_name\":"
            << json_quote(original_to_utf8(m.home_name)) << ",\"away_name\":"
            << json_quote(original_to_utf8(m.away_name)) << ",\"pick\":" << int(m.pick) << '}';
    }
    out << "]}";
    return out.str();
}

std::string toto_request_json(const TotoRequest& r) {
    std::ostringstream out;
    out << "{\"kind\":" << json_quote(r.kind == TotoRequestKind::submit ? "submit" : "none")
        << ",\"manager_slot\":" << r.manager_slot << ",\"runtime_club\":" << r.runtime_club
        << ",\"revision\":" << r.revision << ",\"stake\":" << r.stake << ",\"tips\":[";
    for (size_t i = 0; i < r.tips.size(); ++i) { if (i) out << ','; out << int(r.tips[i]); }
    out << "]}";
    return out.str();
}

TotoScreen::TotoScreen(const std::filesystem::path& data, TotoState state)
    : state_(std::move(state)), data_(data),
      background_(load_vga(asset_path(data / "KB", "49.VGA"))),
      widgets_(load_vga(asset_path(data / "KB", "0.VGA"))),
      exe_(load_le(asset_path(data, "BMH.exe"))), texts_(load_textd(asset_path(data, "TEXTD.BLK"))),
      font_(load_font(asset_path(data / "F", "BMH3.FNT"))), sprite_font_(data), stake_(state_.stake) {
    for (size_t i = 0; i < state_.matches.size() && i < tips_.size(); ++i) tips_[i] = state_.matches[i].pick;
    if (background_.width != 320 || background_.height != 240 || state_.manager_slot < 0 ||
        (state_.available && state_.matches.size() != 11))
        throw FormatError("toto: unsupported background or state");
}
void TotoScreen::text_at(Image& out, int x, int baseline, std::string_view s, uint8_t color, bool shadow) const {
    const int y = baseline - font_.baseline, width = font_.text_width(s);
    if (x < 0 || y < 0 || y + font_.height > out.height || x + width + (shadow ? 2 : 0) > out.width) return;
    if (shadow) font_.draw(out, x + 2, y + 2, s, 0x40);
    if (!state_.gradient || exe_.u8(::bmh::layout::TextGradientMode) != 2) { font_.draw(out, x, y, s, color); return; }
    int ramp = 0;
    while (ramp < 32 && exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2 + uint32_t(ramp)) != color) ++ramp;
    if (ramp == 32) { font_.draw(out, x, y, s, color); return; }
    Image mask(std::max(1, width), font_.height);
    font_.draw(mask, 0, 0, s, 1);
    for (int yy = 0; yy < font_.height; ++yy) for (int xx = 0; xx < width; ++xx)
        if (mask.at(xx, yy)) out.at(x + xx, y + yy) = exe_.u8(::bmh::layout::Advertising_Text_RuntimeField + uint32_t(ramp * 6 + std::min(yy, 5)));
}
Image TotoScreen::render() const {
    if (calculator_) return calculator_->render();
    Image out = background_;
    const auto title = original_to_utf8(texts_[text_id(::bmh::layout::Toto_Render_TextPointer2)]);
    sprite_font_.draw(out, SpriteFont::centered_title_x(sprite_font_.text_width(title)), 13, title, 5);
    sprite_font_.draw(out, 5, 235, original_to_utf8(state_.manager_name), 5);
    if (!state_.available) {
        const auto& message = texts_[text_id(::bmh::layout::Toto_Render_TextPointer3)];
        text_at(out, (319 - font_.text_width(message)) / 2, 100, message, 0x6a, true);
        return out;
    }
    const auto detail = [&](const std::array<int, 5>& ratings) {
        std::string value = exe_.cstring(::bmh::layout::CupDraw_FormatError_ExecutableField2);
        for (size_t n = 0; n < 5; ++n) {
            value += format_number(GameState{}, ratings[n], 2);
            value += n < 3 ? texts_[text_id(::bmh::layout::CupDraw_FormatError_TextPointer5 + uint32_t(n) * 4)] : exe_.cstring(n == 3 ? ::bmh::layout::CupDraw_FormatError_TextPointer2 : ::bmh::layout::CupDraw_FormatError_TextPointer3);
        }
        return value;
    };
    constexpr std::array<uint8_t, 3> source_picks{1, 0, 2};
    for (int i = 0; i < 11; ++i) {
        const auto& match = state_.matches.at(size_t(i));
        const int baseline = 25 + i * 15;
        text_at(out, 4, baseline, format_number(GameState{}, i + 1, 2), 0x27);
        text_at(out, 109, baseline, exe_.cstring(::bmh::layout::Toto_Render_ExecutableField), 0x27);
        text_at(out, 17, baseline, match.home_name, 0x27);
        text_at(out, 125, baseline, match.away_name, 0x27);
        if (match.ratings_available) {
            text_at(out, 17, baseline + 6, detail(match.home_ratings), 0x5f);
            text_at(out, 125, baseline + 6, detail(match.away_ratings), 0x5f);
        }
        for (int column = 0; column < 3; ++column)
            if (tips_[size_t(i)] == source_picks[size_t(column)])
                copy(out, widgets_, 48, source_cross_variant(i, column) * 12,
                     260 + column * 15, 20 + i * 15, 16, 12, 0);
    }
    GameState currency;
    currency.memory.add_block(::bmh::layout::CurrencyMultiplier, 8);
    currency.memory.set32(::bmh::layout::CurrencyMultiplier, uint32_t(state_.currency_multiplier));
    currency.memory.set32(::bmh::layout::CurrencyDivisor, uint32_t(state_.currency_divisor));
    std::string previous, current;
    for (size_t i = 0; i < tips_.size(); ++i) {
        previous += char('0' + state_.previous_tips[i]); current += char('0' + tips_[i]);
        if (i + 1 < tips_.size()) { previous += exe_.cstring(::bmh::layout::Toto_Render_ExecutableField2); current += exe_.cstring(::bmh::layout::Toto_Render_ExecutableField2); }
    }
    // LeagueTableRangeDialog/CaseReadsGlobalPreviousTipsButCase: fixed nine rows and EXE visibility flags, not a
    // fabricated balance/submission toolbar. Prizes scale with the local stake.
    for (int i = 0; i < 9; ++i) {
        const int baseline = 195 + (i < 5 ? i : i - 5) * 7;
        const int left = i < 5 ? 12 : 165, value_x = i < 5 ? 104 : 216;
        text_at(out, left, baseline, texts_[text_id(::bmh::layout::Toto_Render_TextPointer + uint32_t(i) * 4)], 0x6a, true);
        if (i != 1) text_at(out, value_x - 5, baseline, exe_.cstring(::bmh::layout::Bank_OriginalToUtf8_ExecutableField), 0x6a, true);
        if (!exe_.u8(::bmh::layout::Toto_Render_RuntimeField2 + uint32_t(i))) continue;
        std::string value;
        if (i < 5) value = format_number(currency, int32_t(uint32_t(exe_.u8(::bmh::layout::Toto_Render_RuntimeField + uint32_t(i))) * stake_), 0x108);
        else if (i == 5) value = previous;
        else if (i == 6) value = current;
        else if (i == 7) value = format_number(currency, state_.previous_correct, 1);
        else value = format_number(currency, int32_t(state_.previous_winnings), 0x101);
        text_at(out, value_x, baseline, value, 0x6a, true);
    }
    return out;
}
void TotoScreen::open_calculator() {
    calculator_.emplace(data_, render(), TotoStakeDialogProfile{
        state_.gradient ? TableRangeTextMode::source_gradient : TableRangeTextMode::flat,
        calculator_x_, calculator_y_});
    calculator_clock_valid_ = false; calculator_clock_reset_ = true;
}
TotoRequest TotoScreen::submit() {
    if (pending_ || !state_.available) return {};
    TotoRequest r{TotoRequestKind::submit, state_.manager_slot, state_.runtime_club, state_.revision, stake_, tips_};
    pending_ = r; feedback_.clear(); return r;
}
TotoRequest TotoScreen::click(int x, int y, bool right) {
    if (pending_) return {};
    if (right && calculator_right_release_pending_) {
        calculator_right_release_pending_ = false;
        if (calculator_) calculator_->pointer_up(x, y, true);
        return {};
    }
    if (calculator_) { calculator_->pointer_up(x, y, right); return {}; }
    if (right) {
        if (!state_.available || (stake_ == state_.stake && std::equal(tips_.begin(), tips_.end(),
            state_.matches.begin(), [](uint8_t tip, const TotoMatchState& match) { return tip == match.pick; }))) {
            close_requested_ = true; return {};
        }
        close_after_ack_ = true;
        return submit();
    }
    if (!state_.available) { close_requested_ = true; return {}; }
    if (y > 186) { open_calculator(); return {}; }
    if (y > 21 && y < 182) {
        int pick = -1;
        if (x > 258 && x < 273) pick = 1;
        if (x > 273 && x < 288) pick = 0;
        if (x > 288 && x < 303) pick = 2;
        const int row = (y - 21) / 15;
        if (pick >= 0 && row < 11) {
            tips_[size_t(row)] = uint8_t(pick);
            if (stake_ == 0) stake_ = 10000;
        }
    }
    return {};
}
TotoRequest TotoScreen::key(std::string_view key) {
    if (pending_) return {};
    if (calculator_) {
        calculator_->key(key); calculator_clock_reset_ = true; return {};
    }
    if (key == "ESC") { close_requested_ = true; return {}; }
    if (!state_.available) return {};
    if (key == "E" || key == "e") { open_calculator(); return {}; }
    if (key == "ENTER") return submit();
    return {};
}
bool TotoScreen::text(std::string_view value) {
    if (!calculator_ || pending_ || !calculator_->source_input_ready()) return false;
    const bool changed = calculator_->text(value);
    if (changed) calculator_clock_reset_ = true;
    return changed;
}
bool TotoScreen::move(int x, int y) { return calculator_ && calculator_->move(x, y); }
bool TotoScreen::pointer_down(int x, int y, bool right) {
    if (!calculator_ || pending_) return false;
    const bool changed = right ? calculator_->cancel_from_source_event() : calculator_->pointer_down(x, y);
    if (right && changed) calculator_right_release_pending_ = true;
    if (changed) calculator_clock_reset_ = true;
    return changed;
}
bool TotoScreen::pointer_up(int x, int y, bool right) { return calculator_ && calculator_->pointer_up(x, y, right); }
bool TotoScreen::process_timer_expired() {
    // BalanceFromGame raw event8 bypasses N's 50-unit wait, but still restores
    // and presents the coupon before returning its cancelled value. It
    // creates no physical right-button release owner.
    if (!calculator_ || pending_ || !calculator_->cancel_from_source_event()) return false;
    calculator_clock_reset_ = true;
    return true;
}
bool TotoScreen::after_present(uint64_t units) {
    if (!calculator_) return false;
    if (!calculator_clock_valid_ || calculator_clock_reset_) {
        calculator_clock_ = units; calculator_clock_valid_ = true; calculator_clock_reset_ = false;
    }
    const bool exit_before = calculator_->waiting_for_exit_present();
    const auto outcome = calculator_->after_present();
    if (!outcome) return !exit_before && calculator_->waiting_for_exit_present();
    calculator_x_ = outcome->x; calculator_y_ = outcome->y;
    // LeagueTableRangeDialog maps the numeric dialog's cancelled return to stake zero.
    // This edits the coupon only; settlement and payment remain at matchday.
    stake_ = outcome->result == TableRangeDialogResult::accept ? outcome->value : 0;
    calculator_.reset(); calculator_clock_valid_ = false;
    return true;
}
bool TotoScreen::tick_source_units(uint64_t units) {
    if (!calculator_ || !calculator_clock_valid_ || calculator_clock_reset_) return false;
    const auto elapsed = units >= calculator_clock_ ? units - calculator_clock_ : 0;
    calculator_clock_ = units;
    return calculator_->tick_source_units(uint32_t(std::min<uint64_t>(elapsed, UINT32_MAX)));
}
void TotoScreen::acknowledge(TotoState updated) {
    if (!pending_ || updated.revision <= state_.revision || updated.manager_slot != state_.manager_slot ||
        updated.runtime_club != state_.runtime_club || updated.matches.size() != 11)
        throw FormatError("toto: expected newer authoritative coupon for the same manager");
    state_ = std::move(updated); stake_ = state_.stake;
    for (size_t i = 0; i < tips_.size(); ++i) tips_[i] = state_.matches[i].pick;
    pending_.reset(); feedback_ = "TIPP ABGEGEBEN";
    if (close_after_ack_) close_requested_ = true;
}
void TotoScreen::reject_action(std::string reason) {
    if (!pending_ || reason.empty()) throw FormatError("toto: rejection requires a pending request");
    pending_.reset(); close_after_ack_ = false; feedback_ = std::move(reason);
}
std::string TotoScreen::state_json() const {
    std::string out = toto_state_json(state_); out.pop_back();
    out += ",\"local_stake\":" + std::to_string(stake_) + ",\"local_tips\":[";
    for (size_t i = 0; i < tips_.size(); ++i) { if (i) out += ','; out += std::to_string(tips_[i]); }
    out += "],\"pending_action\":" + (pending_ ? toto_request_json(*pending_) : "null") +
        ",\"stake_dialog\":" + (calculator_ ? "true" : "false") + ",\"amount_input\":" +
        json_quote(calculator_ ? std::to_string(calculator_->value()) : "") + ",\"feedback\":" +
        (feedback_.empty() ? "null" : json_quote(feedback_)) + '}';
    return out;
}

}  // namespace bmh::ui
