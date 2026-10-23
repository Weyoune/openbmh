// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/world_layout.hpp"
#include "ui/contracts.hpp"
#include "ui/game_end.hpp"

#include <algorithm>
#include <limits>
#include <sstream>

#include "game/state.hpp"
#include "ui/manager.hpp"

namespace bmh::ui {
namespace {
constexpr size_t kManagerRecord = 0xb0c;
constexpr size_t kBoard = 0x840;
constexpr size_t kBoardSlot = 0x58;

void copy_region(Image& target, const Image& source, int sx, int sy, int x, int y, int width, int height, bool transparent = false) {
    if (sx < 0 || sy < 0 || x < 0 || y < 0 || width < 0 || height < 0 ||
        sx + width > source.width || sy + height > source.height || x + width > target.width || y + height > target.height)
        throw FormatError("contracts: invalid original widget bounds");
    for (int yy = 0; yy < height; ++yy) for (int xx = 0; xx < width; ++xx) {
        const uint8_t value = source.pixels[size_t(sy + yy) * size_t(source.width) + size_t(sx + xx)];
        if (!transparent || value != 0) target.at(x + xx, y + yy) = value;
    }
}

void fill(Image& target, int x, int y, int width, int height, uint8_t color);

void pattern(Image& out, const LeImage& exe, int x, int y, int width, int height) {
    for (int yy = 0; yy < height; ++yy)
        for (int xx = 0; xx < width; ++xx)
            out.at(x + xx, y + yy) = exe.u8(::bmh::layout::Advertising_PatternRect_RuntimeField + size_t(yy % 16) * 16 + size_t(xx % 16));
}

void frame(Image& out, const LeImage& exe, const Image& widgets, int x, int y, int width, int height, bool shadow = true) {
    if (shadow) { fill(out, x + 2, y + height, width, 2, 0x40); fill(out, x + width, y + 2, 2, height, 0x40); }
    pattern(out, exe, x + 4, y + 4, width - 7, height - 7);
    copy_region(out, widgets, 0, 0, x, y, 4, 4);
    copy_region(out, widgets, 4, 0, x + width - 4, y, 4, 4);
    copy_region(out, widgets, 0, 4, x, y + height - 4, 4, 4);
    copy_region(out, widgets, 4, 4, x + width - 4, y + height - 4, 4, 4);
    for (int at = 4; at < width - 4; at += 74) {
        const int n = std::min(74, width - 4 - at);
        copy_region(out, widgets, 0, 104, x + at, y, n, 4);
        copy_region(out, widgets, 0, 108, x + at, y + height - 4, n, 4);
    }
    for (int at = 4; at < height - 4; at += 96) {
        const int n = std::min(96, height - 4 - at);
        copy_region(out, widgets, 0, 8, x, y + at, 4, n);
        copy_region(out, widgets, 4, 8, x + width - 4, y + at, 4, n);
    }
}

void fill(Image& target, int x, int y, int width, int height, uint8_t color) {
    if (x < 0 || y < 0 || width < 0 || height < 0 || x + width > target.width || y + height > target.height)
        throw FormatError("contracts: invalid fill bounds");
    for (int row = 0; row < height; ++row)
        std::fill_n(target.pixels.begin() + size_t(y + row) * size_t(target.width) + size_t(x), size_t(width), color);
}

// FormatGroupedDigits: '%' is the original bitmap font's blank padding glyph.
std::string original_number(uint32_t value, int width, bool grouped = false) {
    std::string digits = std::to_string(value);
    const int actual = int(digits.size());
    const int padding = std::max(0, width - actual);
    digits.insert(0, size_t(padding), '%');
    if (grouped) {
        for (int from = actual - 3; from > 0; from -= 3)
            digits.insert(size_t(padding + from), ".");
        const int wanted = width > 3 ? (width - 4) / 3 + 1 : 0;
        const int used = actual > 3 ? (actual - 1) / 3 : 0;
        if (wanted > used) digits.insert(0, size_t(wanted - used), ' ');
    }
    return digits;
}
std::string money(uint32_t value, int width = 5) {
    return original_number(value, width, true) + " EU";
}
std::string plain_number(uint32_t value) {
    return original_number(value, 0, true);
}
}

ContractsState contracts_from_game(const GameState& game, int manager_slot, uint64_t revision) {
    if (manager_slot < 0 || manager_slot >= 4 || size_t(manager_slot) >= game.manager_teams.size() ||
        size_t(manager_slot) * kManagerRecord + kManagerRecord > game.manager_records.size())
        throw FormatError("contracts: invalid manager slot");
    const int team = game.manager_teams[size_t(manager_slot)];
    if (team == no_team()) throw FormatError("contracts: Vereinsverträge sind für Nationalteams nicht verfügbar");
    const SquadRange range = squad_range(team);
    const size_t board = size_t(manager_slot) * kBoard;
    if (range.count > 24 || range.first + range.count > game.squad_slots.size() || board + range.count * kBoardSlot > game.tactics.size())
        throw FormatError("contracts: invalid squad bounds");
    ContractsState state;
    state.manager_slot = manager_slot;
    state.runtime_club = team;
    state.revision = revision;
    state.mode_three = game.memory.u8(::bmh::layout::GameMode) == 3;
    state.gradient_text = game.memory.u8(::bmh::layout::TextGradientDisabled) == 0;
    state.decorated_background = game.memory.u8(::bmh::layout::PlainBackground) == 0;
    state.currency_numerator = game.memory.has(::bmh::layout::CurrencyMultiplier) ? game.memory.u32(::bmh::layout::CurrencyMultiplier) : 1;
    state.currency_denominator = game.memory.has(::bmh::layout::CurrencyDivisor) ? game.memory.u32(::bmh::layout::CurrencyDivisor) : 1;
    if (!state.currency_denominator) throw FormatError("contracts: invalid currency divisor");
    state.club_name = original_to_utf8(game.club_name(team));
    const size_t manager = size_t(manager_slot) * kManagerRecord;
    if (manager + 0x23a > game.manager_records.size()) throw FormatError("contracts: manager identity is unavailable");
    const auto name_begin = game.manager_records.begin() + std::ptrdiff_t(manager + 0xa8);
    const auto name_end = std::find(name_begin, name_begin + 26, uint8_t(0));
    state.manager_name = original_to_utf8(std::string_view(
        reinterpret_cast<const char*>(&*name_begin), size_t(name_end - name_begin)));
    state.manager_portrait = game.manager_records[manager + 0x221];
    if (state.manager_portrait < 0 || state.manager_portrait > 11) throw FormatError("contracts: invalid manager portrait");
    if (size_t(team) * 2 + 2 > game.club_heap_78.size()) throw FormatError("contracts: club badge identity is unavailable");
    state.badge_sprite = (int(game.club_heap_78[size_t(team) * 2]) |
                          int(game.club_heap_78[size_t(team) * 2 + 1]) << 8) & 0x1ff;
    for (size_t row = 0; row < range.count; ++row) {
        const int player = game.squad_slots.at(range.first + row) & 0x0fff;
        if (player == 0x0fff) continue;
        if (player < 0 || size_t(player) >= game.players.size())
            throw FormatError("contracts: invalid player in squad");
        const size_t at = board + row * kBoardSlot;
        const auto& source = game.players[size_t(player)];
        ContractPlayer view;
        view.row = int(row);
        view.player_id = player;
        view.name = original_to_utf8(game.player_name(player));
        view.age = source.age();
        view.position = source.position();
        view.remaining_months = game.tactics.at(at + 0x0c);
        view.current_monthly_salary = uint32_t(game.tactics.at(at + 0x50)) |
                              uint32_t(game.tactics.at(at + 0x51)) << 8 |
                              uint32_t(game.tactics.at(at + 0x52)) << 16 |
                              uint32_t(game.tactics.at(at + 0x53)) << 24;
        view.insurance = game.tactics.at(at + 0x43);
        view.insurance_available = view.insurance == 0 && (game.tactics.at(at + 0x0b) & 0x60) == 0 &&
                                   (source.w35c >> 27) == 0;
        view.offer_blocked = (game.tactics.at(at + 0x4f) & 0x80) != 0;
        view.insurance_fee = uint16_t(game.tactics.at(at + 0x4e)) |
                             uint16_t(game.tactics.at(at + 0x4f) & 0x7f) << 8;
        for (size_t premium = 0; premium < view.team_premiums.size(); ++premium)
            view.team_premiums[premium] = game.tactics.at(at + 0x12 + premium);
        view.attributes = {uint8_t(source.condition()), uint8_t(source.technique()), uint8_t(source.form()), uint8_t(source.strength())};
        view.board_flags = game.tactics.at(at + 0xb);
        view.source.row = int(row); view.source.player_id = player;
        view.source.slot = game.squad_slots.at(range.first + row);
        view.source.name = game.player_name(player); view.source.attributes = source;
        view.source.cards = game.player_cards.at(size_t(player)); view.source.bonus = game.player_bonus.at(size_t(player));
        std::copy_n(game.tactics.begin() + std::ptrdiff_t(at), 0x58, view.source.board.begin());
        state.players.push_back(std::move(view));
    }
    if (!state.players.empty()) {
        state.team_premiums = state.players.front().team_premiums;
        for (const auto& player : state.players)
            for (size_t i = 0; i < state.team_premiums.size(); ++i)
                state.team_premiums[i] = std::min(state.team_premiums[i], player.team_premiums[i]);
    }
    return state;
}

std::string contract_request_json(const ContractRequest& r) {
    const char* kind = "demands";
    switch (r.kind) {
    case ContractActionKind::request_demands: kind = "demands"; break;
    case ContractActionKind::submit_offer: kind = "offer"; break;
    case ContractActionKind::submit_bonuses: kind = "bonuses"; break;
    case ContractActionKind::insure_player: kind = "insurance"; break;
    }
    std::ostringstream out;
    out << "{\"kind\":\"" << kind
        << "\",\"request_id\":" << r.request_id << ",\"revision\":" << r.revision
        << ",\"manager_slot\":" << r.manager_slot << ",\"runtime_club\":" << r.runtime_club
        << ",\"row\":" << r.row << ",\"player_id\":" << r.player_id
        << ",\"quote_id\":" << r.quote_id << ",\"term_column\":" << unsigned(r.term_column)
        << ",\"demand\":" << r.demand << ",\"monthly_salary\":" << r.monthly_salary
        << ",\"insurance_months\":" << unsigned(r.insurance_months) << ",\"proposed_bonuses\":[";
    for (size_t i = 0; i < r.proposed_bonuses.size(); ++i) { if (i) out << ','; out << unsigned(r.proposed_bonuses[i]); }
    out << "]}";
    return out.str();
}

std::string contracts_state_json(const ContractsState& s) {
    std::ostringstream out;
    out << "{\"manager_slot\":" << s.manager_slot << ",\"runtime_club\":" << s.runtime_club
        << ",\"revision\":" << s.revision << ",\"club_name\":" << json_quote(s.club_name)
        << ",\"manager_name\":" << json_quote(s.manager_name) << ",\"manager_portrait\":" << s.manager_portrait
        << ",\"badge_sprite\":" << s.badge_sprite
        << ",\"selected_row\":" << s.selected_row << ",\"quote_id\":" << s.quote_id
        << ",\"quote_row\":" << s.quote_row << ",\"players\":[";
    for (size_t i = 0; i < s.players.size(); ++i) {
        const auto& p = s.players[i];
        if (i) out << ',';
        out << "{\"row\":" << p.row << ",\"player_id\":" << p.player_id
            << ",\"name\":" << json_quote(p.name) << ",\"age\":" << p.age
            << ",\"position\":" << p.position << ",\"remaining_months\":" << unsigned(p.remaining_months)
            << ",\"current_monthly_salary\":" << p.current_monthly_salary << '}';
    }
    out << "],\"demands\":";
    if (s.demands) {
        out << '[';
        for (size_t i = 0; i < s.demands->size(); ++i) { if (i) out << ','; out << (*s.demands)[i]; }
        out << ']';
    } else out << "null";
    out << ",\"feedback\":" << json_quote(s.feedback) << '}';
    return out.str();
}

ContractsScreen::ContractsScreen(const std::filesystem::path& data, ContractsState state)
    : state_(std::move(state)), exe_(load_le(asset_path(data, "BMH.exe"))),
      font_(load_font(asset_path(data / "F", "BMH3.FNT"))),
      large_font_(load_font(asset_path(data / "F", "BMH2.FNT"))), sprite_font_(data),
      texts_(load_textd(asset_path(data, "TEXTD.BLK"))),
      atlas_(load_vga(asset_path(data / "KB", "2.VGA"))),
      flags_(load_vga(asset_path(data / "KB", "24.VGA"))),
      background_(load_vga(asset_path(data / "KB", "129.VGA"))),
      widgets_(load_vga(asset_path(data / "KB", "0.VGA"))),
      calculator_(load_vga(asset_path(data / "KB", "6.VGA"))),
      faces_(load_vga(asset_path(data / "KB", "26.VGA"))) {
    if (atlas_.width == 314) { Image padded(320, atlas_.height, 0); padded.blit(atlas_,0,0); atlas_ = std::move(padded); }
    if (state_.manager_slot < 0 || state_.manager_slot > 3 || state_.runtime_club < 0 ||
        state_.players.size() > 24 || background_.width != 320 || background_.height != 240)
        throw FormatError("contracts: invalid state");
    if (state_.manager_portrait < 0 || state_.manager_portrait > 11 || state_.badge_sprite < 0 || state_.badge_sprite > 0x1ff)
        throw FormatError("contracts: invalid manager identity");
    if (source_badge_fallback(state_.runtime_club)) badge_atlas_ = widgets_;
    else badge_atlas_ = load_vga(asset_path(data / "W", std::to_string(state_.badge_sprite >> 3) + ".VGA"));
}

void ContractsScreen::text(Image& out, int x, int baseline, std::string_view value, uint8_t color, int right) const {
    std::string bytes = utf8_to_original(value);
    const int limit = right < 0 ? out.width : std::min(right, out.width);
    if (font_.text_width(bytes) > limit - x) return;
    const int top = baseline - font_.baseline;
    if (x < 0 || top < 0 || top + font_.height > out.height) return;
    const int width = std::max(1, font_.text_width(bytes));
    Image mask(width, font_.height, 0);
    font_.draw(mask, 0, 0, bytes, 1);
    int gradient = 0;
    while (gradient < 32 && exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2 + gradient) != color) ++gradient;
    for (int yy = 0; yy < font_.height; ++yy)
        for (int xx = 0; xx < width; ++xx)
            if (mask.at(xx, yy)) out.at(x + xx, top + yy) = state_.gradient_text && exe_.u8(::bmh::layout::TextGradientMode) == 2 && gradient < 32 ? exe_.u8(::bmh::layout::Advertising_Text_RuntimeField + gradient * 6 + std::min(yy, 5)) : color;
}

void ContractsScreen::emit(ContractRequest request) {
    if (pending_) return;
    request.request_id = state_.request_id_base + next_request_id_++;
    request.revision = state_.revision;
    request.manager_slot = state_.manager_slot;
    request.runtime_club = state_.runtime_club;
    pending_ = request;
}

void ContractsScreen::open_calculator(int target, Page return_page, uint32_t initial) {
    calculator_backdrop_ = render();
    calculator_bonus_index_ = target;
    calculator_return_ = return_page;
    calculator_initial_ = initial;
    salary_input_ = initial;
    if (target >= 0) {
        calculator_max_ = target < 3 ? 20u : 1000u;
        const uint32_t first = target < 3 ? 1u : uint32_t(50 - 10 * (target - 3));
        const uint32_t step = first;
        for (size_t i = 0; i < 6; ++i) calculator_presets_[i] = first + uint32_t(i) * step;
    } else if (target == -2) {
        calculator_max_ = 60;
        for (size_t i = 0; i < 6; ++i) calculator_presets_[i] = 12u + uint32_t(i) * 6u;
    } else {
        calculator_max_ = 200000;initial=std::min(initial,calculator_max_);
        uint32_t factor=1000, step=0;
        while(!step && factor) {step=factor*((initial/factor)/10);if(!step)factor/=10;}
        if(!step)step=1;
        initial=std::min(initial,calculator_max_);
        const uint32_t rounded = 1000u * (initial / 1000u);
        const uint32_t first = rounded >= 3u * step ? rounded - 3u * step : 0u;
        for (size_t i = 0; i < 6; ++i) calculator_presets_[i] = first + uint32_t(i) * step;
    }
    salary_input_=std::min(initial,calculator_max_);calculator_initial_=salary_input_;
    for(auto& preset:calculator_presets_)if(preset>calculator_max_)preset=UINT32_MAX;
    calculator_presets_[6] = 0;
    calculator_presets_[7] = calculator_max_;
    page_ = Page::calculator;
    editing_salary_ = true;
}

bool ContractsScreen::text(std::string_view value) {
    if (pending_ || !editing_salary_) return false;
    bool changed = false;
    for (char c : value) {
        if (c == '\b' || c == 127) { salary_input_ /= 10; changed = true; }
        else if (c >= '0' && c <= '9') {
            const uint32_t digit = uint32_t(c - '0');
            if (salary_input_ <= (calculator_max_ - digit) / 10) {
                salary_input_ = salary_input_ * 10 + digit;
                changed = true;
            }
        }
    }
    return changed;
}

// LineupScreen, contract mode: the same roster as lineup, including its
// original fixed-width number strings, jersey marks, and bitmap icons.
Image ContractsScreen::render_roster() const {
    Image out = state_.decorated_background ? background_ : Image(320, 240);
    if (!state_.decorated_background) {
        fill(out, 0, 4, 320, 13, 0x44); fill(out, 1, 5, 318, 11, 0x39);
        fill(out, 0, 228, 320, 10, 0x44); fill(out, 1, 229, 318, 8, 0x39);
    }
    auto original_text = [&](int x, int y, std::string_view raw, uint8_t color, int right = -1) {
        const int width = font_.text_width(raw);
        if (right >= 0) x += (right - x) / 2 - width / 2;
        text(out, x, y, original_to_utf8(raw), color, -1);
    };
    const auto fixed = [](uint32_t value, int width, bool grouped = false) { return original_number(value, width, grouped); };
    const auto title = original_to_utf8(texts_[(::bmh::layout::Contracts_RenderRoster_TextPointer2 - ::bmh::layout::TextPointerTable) / 4]);
    sprite_font_.draw(out, SpriteFont::centered_title_x(sprite_font_.text_width(title)), 13, title, 5);
    sprite_font_.draw(out, 5, 235, state_.club_name, 5);
    const auto header = tab_ == 0 ? texts_[(::bmh::layout::ClubInfo_Render_TextPointer3 - ::bmh::layout::TextPointerTable) / 4] : tab_ == 1 ? texts_[(::bmh::layout::ClubInfo_Render_TextPointer4 - ::bmh::layout::TextPointerTable) / 4] : exe_.cstring(exe_.u32(::bmh::layout::Contracts_RenderRoster_TextPointer));
    original_text(0, 26, header, 0x51);
    int baseline = 36;
    uint16_t preceding = 0;
    for (size_t visual = 0; visual < state_.players.size(); ++visual) {
        const auto& p = state_.players[visual]; const auto& s = p.source; const auto& a = s.attributes;
        if (visual && (s.slot & 0xc000) != (preceding & 0xc000)) baseline += 2;
        preceding = s.slot;
        if (s.slot & 0xc000) {
            const int jersey = (s.cards & 31) + 1;
            const uint8_t color = s.slot & 0x1000 ? 0xf3 : s.slot & 0x8000 ? 0x6c : 0x68;
            original_text(4, baseline, std::string(1, char('0' + jersey % 10)), color);
            if (jersey >= 10) original_text(0, baseline, exe_.cstring(::bmh::layout::ClubInfo_Render_TextPointer), color);
        }
        const uint8_t name_color = p.row == state_.selected_row ? 0x27 : p.row == hovered_row_ ? 0x3e :
            tab_ == 2 && (p.offer_blocked || (p.board_flags & 0x20)) ? 0xf7 : visual % 2 ? 0x67 : 0x6b;
        original_text(13, baseline, s.name, name_color);
        if (tab_ != 1) {
            original_text(66, baseline, texts_[(::bmh::layout::ClubInfo_Render_TextPointer2 - ::bmh::layout::TextPointerTable) / 4 + a.position()], 0x6c, 82);
            for (int field = 0; field < 2; ++field) {
                int value = (a.w35c >> (field == 0 ? 24 : 21)) & 7; if (value > 4) --value;
                copy_region(out, atlas_, 208 + value * 16, field == 0 ? 17 : 22,
                            field == 0 ? 88 : 103, baseline - 5, 16, 5, true);
            }
            std::string values = fixed(a.condition(), 2) + exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField) + fixed(a.technique(), 2) +
                exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField) + fixed(a.form(), 2) + exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField2) + fixed(a.strength(), 2) + exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField3);
            if (tab_ == 2 || tab_ == 3) {
                values += fixed(a.age(), 2) + texts_[(::bmh::layout::Contracts_RenderRoster_TextPointer3 - ::bmh::layout::TextPointerTable) / 4];
                const uint32_t numerator = state_.currency_numerator, denominator = state_.currency_denominator;
                const auto currency = [&](uint32_t value, int width) { return fixed(uint32_t(value * numerator) / denominator, width, true) + exe_.cstring(::bmh::layout::CurrencySuffixText); };
                values += currency(p.current_monthly_salary, 6) + exe_.cstring(::bmh::layout::Contracts_RenderRoster_ExecutableField2);
                values += p.remaining_months == 255 ? exe_.cstring(::bmh::layout::Contracts_RenderRoster_ExecutableField3) : fixed(p.remaining_months, 2) + exe_.cstring(::bmh::layout::Contracts_RenderRoster_ExecutableField4);
                values += p.insurance == 0 ? texts_[(::bmh::layout::Contracts_RenderRoster_TextPointer4 - ::bmh::layout::TextPointerTable) / 4] : currency(p.insurance_fee, 5) +
                    exe_.cstring(::bmh::layout::Contracts_RenderRoster_ExecutableField2) + fixed(p.insurance & 0x3f, 2) + exe_.cstring(::bmh::layout::Contracts_RenderRoster_ExecutableField);
                original_text(117, baseline, values, 0x6c);
            } else {
                original_text(117, baseline, values + exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField4), 0x6c);
                copy_region(out, atlas_, 304, int(s.board[0xe] >> 6) * 3, 179, baseline - 4, 16, 3, true);
                const int injury = (a.w364 >> 16) & 31, marker = a.w35c >> 27;
                if (!marker && !injury) {
                    const int status = s.slot & 0x8000 ? 0 : s.slot & 0x4000 ? 1 : 2;
                    copy_region(out, atlas_, 272, status * 5, 191, baseline - 5, 32, 5, true);
                } else {
                    const int status = !marker ? 5 : marker == 31 ? 8 : !injury ? 4 : 3;
                    copy_region(out, atlas_, 197, (status - 3) * 6 + 1, 196, baseline - 5, 10, 5);
                }
                const int nation = a.nationality() + ((s.bonus & 0x80) >> 2);
                copy_region(out, flags_, nation % 12 * 9, nation / 12 * 6, 217, baseline - 6, 9, 6);
                original_text(232, baseline, fixed(s.board[0xf], 2), s.board[0xf] < 61 ? 0x66 : s.board[0xf] < 71 ? 0x27 : 0xfb);
                original_text(247, baseline, fixed(a.w364 >> 26, 2) + exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField4) + fixed((a.w364 >> 2) & 63, 2) +
                    exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField4) + fixed((a.w368 >> 18) & 63, 2) + exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField4) +
                    fixed((a.w368 >> 14) & 15, 2) + exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField4) + fixed((a.w368 >> 24) & 7, 2) + exe_.cstring(::bmh::layout::ClubInfo_Raw_ExecutableField), 0x6c);
            }
        } else {
            original_text(65, baseline, fixed(a.age(), 2) + texts_[(::bmh::layout::ClubInfo_Render_TextPointer5 - ::bmh::layout::TextPointerTable) / 4], 0x6c);
            for (int skill = 0; skill < 7; ++skill) copy_region(out, atlas_, 256 + a.skill(Player::Skill(skill)) * 16, 32, 87 + skill * 10, baseline - 5, 16, 5, true);
            auto word = [&](int at) { return uint16_t(s.board[at] | uint16_t(s.board[at + 1]) << 8); };
            original_text(165, baseline, fixed(s.bonus & 127, 3) + exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField5) + fixed(s.board[7], 3) + exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField5) +
                fixed(s.board[9], 3) + exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField5) + fixed(word(0x48), 3) + exe_.cstring(::bmh::layout::ClubInfo_Raw_ExecutableField2) +
                fixed(word(0x4a), 3) + exe_.cstring(::bmh::layout::ClubInfo_Raw_ExecutableField2) + fixed(word(0x4c), 3) + exe_.cstring(::bmh::layout::ClubInfo_Raw_ExecutableField), 0x6c);
        }
        baseline += 7;
    }
    return out;
}

Image ContractsScreen::render() const {
    Image out(320, 240);
    if (page_ == Page::result && result_backdrop_) out = *result_backdrop_;
    else if (page_ == Page::calculator && calculator_backdrop_) out = *calculator_backdrop_;
    else out = render_roster();
    if (page_ == Page::result) {
        // The original manager response is drawn over the still-open contract
        // panel; the saved pixels below it are the exact pre-ACK screen.
    }
    const auto selected = std::find_if(state_.players.begin(), state_.players.end(),
        [&](const auto& p) { return p.row == state_.selected_row; });
    static constexpr std::array<std::string_view, 4> tabs{"DATEN-A", "DATEN-B", "GEHALT", "TEAM-PRÄMIEN"};
    for (int i = 0; i < (state_.mode_three?4:3); ++i) {
        const int x = i * 60 + 2, w = i == 3 ? 71 : 51;
        if(i!=tab_) {fill(out,x+2,225,w,2,0x40);fill(out,x+w,217,2,10,0x40);}
        else {const uint8_t color=out.at(x,225);fill(out,x,215,w+1,2,color);fill(out,x,215,2,11,color);}
        const int source_y = i == tab_ ? 44 : 74;
        const int dest_y = 215 + (i == tab_ ? 2 : 0);
        const int dest_x = x + (i == tab_ ? 2 : 0);
        copy_region(out, widgets_, 24, source_y, dest_x, dest_y, 31, 10);
        int tab_at = 31, remain = w - 31;
        while (remain > 20) { const int n = std::min(8, remain - 20); copy_region(out, widgets_, 24, source_y + 10, dest_x + tab_at, dest_y, n, 10); tab_at += n; remain -= n; }
        if (remain > 0) copy_region(out, widgets_, 45 - remain, source_y + 20, dest_x + tab_at, dest_y, remain, 10);
        sprite_font_.draw(out, dest_x + w/2 - sprite_font_.text_width(tabs[size_t(i)])/2, dest_y+7, tabs[size_t(i)], exe_.u8(::bmh::layout::Advertising_Button_RuntimeField + (i==tab_)));
    }

    auto panel = [&](int x, int y, int w, int h, bool shadow = true) {
        frame(out, exe_, widgets_, x, y, w, h, shadow);
    };
    auto button = [&](int x, int y, int w, std::string_view label) {
        fill(out, x + 2, y + 10, w, 2, 0x40); fill(out, x + w, y + 2, 2, 10, 0x40);
        copy_region(out, widgets_, 24, 74, x, y, 31, 10);
        int at = 31, remaining = w - 31;
        while (remaining > 20) { const int n = std::min(8, remaining - 20); copy_region(out, widgets_, 24, 84, x + at, y, n, 10); at += n; remaining -= n; }
        if (remaining > 0) copy_region(out, widgets_, 45 - remaining, 94, x + at, y, remaining, 10);
        const int font = exe_.u8(::bmh::layout::Advertising_Button_RuntimeField);
        sprite_font_.draw(out, x + (w - sprite_font_.text_width(label)) / 2, y + 7, label, font);
    };
    auto db = [&](uint32_t address) { return original_to_utf8(texts_[(address-::bmh::layout::TextPointerTable)/4]); };
    auto centered = [&](int left,int right,int y,std::string_view value,uint8_t color) {
        const int x=left+(right-left)/2-font_.text_width(utf8_to_original(value))/2;
        text(out,x,y,value,color,-1);
    };
    const bool negotiating = page_ == Page::player_offer || page_ == Page::team_premiums;
    if (negotiating) {
        const bool team = page_ == Page::team_premiums && team_premium_mode_;
        const int top = team || selected == state_.players.end() || selected->row >=13 ?22:130;
        panel(0,top,319,90,false);
        const auto heading = db(::bmh::layout::Contracts_Out_ExecutableField3 + (page_==Page::team_premiums?4:0)) + db(::bmh::layout::Contracts_Out_ExecutableField5) + (team?db(::bmh::layout::Contracts_Out_ExecutableField6):selected->name);
        large_font_.draw(out,5,top+11-large_font_.baseline,utf8_to_original(heading),108);
        fill(out,5,top+12,309,1,108);
        if(!team && selected!=state_.players.end()) {
            std::string current=db(::bmh::layout::Contracts_Out_ExecutableField7)+selected->name;
            if(!selected->remaining_months)current+=db(::bmh::layout::Contracts_Out_ExecutableField8);
            else current+=db(::bmh::layout::Contracts_Out_ExecutableField9)+std::to_string(selected->remaining_months)+db(::bmh::layout::Contracts_Out_ExecutableField10)+money(selected->current_monthly_salary)+db(::bmh::layout::ClubPlayerDetail_Copy_ExecutableField4);
            text(out,5,top+19,current,108,-1);
            std::string insurance=db(::bmh::layout::Contracts_Out_TextPointer4);
            if(selected->insurance) insurance=db(::bmh::layout::Contracts_Out_TextPointer3)+std::to_string(selected->insurance&63)+db(::bmh::layout::Contracts_Out_ExecutableField11)+
                db(::bmh::layout::ClubPlayerDetail_Copy_TextPointer6+((selected->insurance>>6)&3)*4)+db(::bmh::layout::Contracts_Out_ExecutableField12)+money(selected->insurance_fee)+db(::bmh::layout::Contracts_Out_ExecutableField13);
            text(out,5,top+25,insurance,108,-1);
        }
        if(page_==Page::player_offer && selected!=state_.players.end()) {
            text(out,5,top+46,db(::bmh::layout::Contracts_Out_ExecutableField14),106,-1);text(out,5,top+54,db(::bmh::layout::Contracts_Out_TextPointer5),106,-1);
            if(state_.mode_three)text(out,5,top+72,db(::bmh::layout::Contracts_Out_TextPointer6),106,-1);
            for(int term=0;term<4;++term) {
                const int left=70+60*term;
                centered(left,left+60,top+37,std::to_string(term+1)+db(::bmh::layout::Contracts_Out_ExecutableField15)+(term?exe_.cstring(::bmh::layout::ClubPlayerDetail_RenderCard_TextPointer):""),107);
                if(salary_offer_set_ && term==offer_term_) centered(left,left+60,top+46,money(offer_salary_),108);
                const uint32_t demand=state_.demands?(*state_.demands)[size_t(term)]:UINT32_MAX;
                centered(left,left+60,top+54,demand==UINT32_MAX?db(::bmh::layout::Contracts_Out_ExecutableField16):money(demand),
                    demand==UINT32_MAX || selected->offer_blocked || (selected->board_flags&0x60)?251:95);
                if(state_.mode_three) {
                    centered(left,left+55,top+64,db(::bmh::layout::ClubPlayerDetail_Copy_TextPointer6+term*4),107);
                    const uint32_t fee=(((uint32_t(term*10+10)*selected->current_monthly_salary)/1000)*10);
                    centered(left,left+60,top+72,original_number(fee,5,true)+db(::bmh::layout::ClubPlayerDetail_Copy_ExecutableField4),selected->insurance && ((selected->insurance>>6)&3)==term?39:106);
                }
            }
        } else {
            uint32_t base=0; int occupied=0;
            if(team)for(const auto& p:state_.players){base+=p.current_monthly_salary;++occupied;}
            else if(selected!=state_.players.end())base=selected->current_monthly_salary;
            for(int i=0;i<7;++i) {
                text(out,25,top+32+i*7,db(::bmh::layout::Contracts_Out_ExecutableField4+i*4),exe_.u8(::bmh::layout::Contracts_Out_TextPointer2+i),-1);
                const unsigned rate=proposed_bonuses_[size_t(i)];
                if(!rate) text(out,132,top+32+i*7,db(::bmh::layout::Contracts_Out_TextPointer8),108,-1);
                else {
                    const unsigned percent=rate*(i<3?1:5);
                    uint32_t salary=base;
                    if(team&&i<3&&occupied){salary/=occupied;if(i==2)salary*=11;if(i==1)salary*=occupied;}
                    text(out,97,top+32+i*7,original_number(percent,4,true)+db(::bmh::layout::Contracts_Out_TextPointer7)+money((uint32_t(percent)*salary)/100,7)+exe_.cstring(::bmh::layout::Contracts_Out_TextPointer),108,-1);
                }
            }
        }
        button(5,top+76,51,db(::bmh::layout::Contracts_Out_ExecutableField17));button(90,top+76,80,db(::bmh::layout::Contracts_Out_ExecutableField18));
        if(!team&&state_.mode_three)button(261,top+76,51,db(page_==Page::player_offer?::bmh::layout::Contracts_Out_ExecutableField2: ::bmh::layout::Contracts_Out_ExecutableField));
    } else if (page_ == Page::calculator) {
        copy_region(out, calculator_, 0, 0, 10, 10, 113, 136);
        fill(out,123,12,2,136,0x40);fill(out,12,146,113,2,0x40);
        const auto label=calculator_bonus_index_==-2?db(::bmh::layout::Contracts_Out_ExecutableField19):calculator_bonus_index_<0?db(::bmh::layout::Contracts_Out_ExecutableField14):db(::bmh::layout::Contracts_Out_ExecutableField4+calculator_bonus_index_*4);
        centered(18,115,24,label,98);
        for(int i=0;i<15;++i) {
            const int x=10+exe_.u16(::bmh::layout::Advertising_Click_RuntimeField+i*2), y=10+exe_.u8(::bmh::layout::Advertising_Click_RuntimeField2+i);
            copy_region(out,calculator_,0,136,x,y,15,14);
            text(out,x+5,y+9,std::string(1,"0123456789DRCJN"[i]),98,-1);
        }
        for(int i=0;i<8;++i) {
            const int x=10+exe_.u16(::bmh::layout::Advertising_Click_RuntimeField+(i+15)*2), y=10+exe_.u8(::bmh::layout::Advertising_Click_RuntimeField2+i+15);
            if(calculator_presets_[size_t(i)]==UINT32_MAX)continue;
            const auto value=plain_number(calculator_presets_[size_t(i)]);
            text(out,x+47-font_.text_width(value),y+12,value,98,-1);
        }
        const auto value=plain_number(salary_input_);
        text(out,116-font_.text_width(value),120,value,0,-1);
    } else if (page_ == Page::result) {
        TaxDialogProjection notice;
        notice.manager_name=utf8_to_original(state_.manager_name);
        notice.portrait=state_.manager_portrait;
        notice.badge_sprite=state_.badge_sprite;
        notice.badge_fallback=source_badge_fallback(state_.runtime_club);
        notice.body=utf8_to_original(state_.feedback);
        notice.source_manager_flags=0x80;
        draw_source_dialog(out,{widgets_,faces_,&badge_atlas_,font_,large_font_,
            exe_,sprite_font_,texts_},notice,state_.gradient_text);
    }
    return out;
}

bool ContractsScreen::click(int x, int y, bool right) {
    if (page_ == Page::result) {
        if (x >= 95 && x <= 220 && y >= 166 && y <= 176) { page_ = result_return_; result_backdrop_.reset(); state_.feedback.clear(); return true; }
        return false;
    }
    if (right && page_ == Page::roster) {
        if (x >= 61 || y < 29) { close_requested_ = true; return true; }
        const ContractPlayer* p = nullptr; int baseline = 36;
        for (size_t i=0;i<state_.players.size();++i) {
            const auto& candidate=state_.players[i];
            if (i && (candidate.source.slot&0xc000)!=(state_.players[i-1].source.slot&0xc000)) baseline+=2;
            if(y>=baseline-7 && y<baseline+2) {p=&candidate;break;} baseline+=7;
        }
        if (!p) {close_requested_=true;return true;}
        state_.selected_row = p->row;
        ContractRequest request; request.kind = ContractActionKind::request_demands; request.row = p->row; request.player_id = p->player_id;
        emit(request); page_ = Page::player_offer; return true;
    }
    if (page_ == Page::roster && x < 18 && y < 18) { close_requested_ = true; pending_.reset(); return true; }
    if (pending_) return false;
    if(right && page_==Page::calculator){page_=calculator_return_;calculator_backdrop_.reset();editing_salary_=false;return true;}
    if(right && page_!=Page::roster){page_=Page::roster;return true;}
    if (page_ == Page::calculator) {
        auto accept_calculator = [&]() {
            if (calculator_bonus_index_ >= 0) {
                const auto selected_player = std::find_if(state_.players.begin(), state_.players.end(), [&](const auto& p) { return p.row == state_.selected_row; });
                const uint32_t maximum = calculator_bonus_index_ < 3 ? 20u : 1000u;
                if ((!team_premium_mode_ && selected_player == state_.players.end()) || salary_input_ > maximum) return false;
                proposed_bonuses_[size_t(calculator_bonus_index_)] = uint8_t(calculator_bonus_index_ < 3 ? salary_input_ : salary_input_ / 5);
                page_ = Page::team_premiums; editing_salary_ = false; calculator_bonus_index_ = -1; return true;
            }
            if (calculator_bonus_index_ == -2) {
                const auto selected_player = std::find_if(state_.players.begin(), state_.players.end(), [&](const auto& p) { return p.row == state_.selected_row; });
                if (selected_player == state_.players.end() || salary_input_ < 1 || salary_input_ > 60) return false;
                ContractRequest req; req.kind = ContractActionKind::insure_player; req.row = selected_player->row;
                req.player_id = selected_player->player_id; req.quote_id = state_.quote_id; req.term_column = insurance_term_;
                req.insurance_months = uint8_t(salary_input_); emit(req); calculator_bonus_index_ = -1; return true;
            }
            if (calculator_return_ == Page::player_offer && calculator_bonus_index_ == -1) {
                offer_salary_ = salary_input_; offer_term_ = selected_term_; salary_offer_set_ = true;
            }
            page_ = calculator_return_; calculator_backdrop_.reset(); editing_salary_ = false; return true;
        };
        if (x >= 69 && x < 85 && y >= 125 && y < 139) {
            salary_input_ = 0;
            return true;
        }
        // Key positions are read from the original calculator hitbox table.
        for (int i = 0; i < 15; ++i) {
            const int key_x = 10 + exe_.u16(::bmh::layout::Advertising_Click_RuntimeField + size_t(i) * 2);
            const int key_y = 10 + exe_.u8(::bmh::layout::Advertising_Click_RuntimeField2 + size_t(i));
            if (x < key_x || x >= key_x + 16 || y < key_y || y >= key_y + 14) continue;
            if (i <= 9) return text(std::to_string(i));
            if (i == 10) { salary_input_ /= 10; return true; }
            if (i == 11) { salary_input_ = calculator_initial_; return true; }
            if (i == 12) { salary_input_ = 0; return true; }
            if (i == 13) return accept_calculator();
            if (i == 14) { page_ = calculator_return_; calculator_backdrop_.reset(); editing_salary_ = false; calculator_bonus_index_ = -1; return true; }
        }
        for (int i = 0; i < 8; ++i) {
            const int preset_x = 10 + exe_.u16(::bmh::layout::Advertising_Click_RuntimeField + size_t(i + 15) * 2);
            const int preset_y = 10 + exe_.u8(::bmh::layout::Advertising_Click_RuntimeField2 + size_t(i + 15));
            if (x < preset_x || x >= preset_x + 53 || y < preset_y || y >= preset_y + 14) continue;
            if(calculator_presets_[size_t(i)]==UINT32_MAX)return false;
            salary_input_ = calculator_presets_[size_t(i)];
            return true;
        }
        return false;
    }
    if (page_ == Page::player_offer) {
        const auto p = std::find_if(state_.players.begin(), state_.players.end(), [&](const auto& v) { return v.row == state_.selected_row; });
        if (p == state_.players.end()) return false;
        const int top = p->row < 13 ? 130 : 22;
        if (y >= top + 76 && y <= top + 85 && x < 60) { page_ = Page::roster; return true; }
        if (y >= top + 76 && y <= top + 85 && x >= 261 && x <= 312) {
            team_premium_mode_ = false; page_ = Page::team_premiums; return true;
        }
        if (y >= top + 76 && y <= top + 85 && x >= 90 && x <= 170 && state_.demands) {
            if (!salary_offer_set_ && proposed_bonuses_ == p->team_premiums) { page_ = Page::roster; return true; }
            ContractRequest request; request.kind = salary_offer_set_ ? ContractActionKind::submit_offer : ContractActionKind::submit_bonuses;
            request.row = state_.quote_row; request.quote_id = state_.quote_id; request.player_id = p->player_id;
            if (salary_offer_set_) { request.term_column = offer_term_; request.demand = (*state_.demands)[offer_term_]; request.monthly_salary = offer_salary_; }
            request.proposed_bonuses = proposed_bonuses_;
            if (request.demand == std::numeric_limits<uint32_t>::max()) return false;
            emit(request); return true;
        }
        if (y >= top + 40 && y < top + 58 && x >= 70 && x < 310 && !p->offer_blocked && !(p->board_flags & 0x60)) {
            selected_term_ = uint8_t(std::clamp((x - 70) / 60, 0, 3));
            const uint32_t initial = state_.demands && state_.quote_row == state_.selected_row ? (*state_.demands)[selected_term_] : 0;
            open_calculator(-1, page_, initial); return true;
        }
        if (y >= top + 58 && y < top + 74 && x >= 70 && x < 310 && state_.mode_three) {
            if(!p->insurance_available) {
                const uint32_t address=p->board_flags&0x20?::bmh::layout::CareerSession_Reject_TextPointer:p->board_flags&0x40?::bmh::layout::CareerSession_Reject_TextPointer2:p->insurance?::bmh::layout::CareerSession_Reject_TextPointer3: ::bmh::layout::CareerSession_Reject_TextPointer4;
                result_backdrop_=render();state_.feedback=original_to_utf8(texts_[(address-::bmh::layout::TextPointerTable)/4]);result_return_=page_;page_=Page::result;return true;
            }
            insurance_term_ = uint8_t(std::clamp((x - 70) / 60, 0, 3));
            open_calculator(-2, page_, 12); return true;
        }
        return false;
    }
    if (page_ == Page::team_premiums) {
        const auto selected_player = std::find_if(state_.players.begin(), state_.players.end(), [&](const auto& player) { return player.row == state_.selected_row; });
        const int top = team_premium_mode_ || selected_player == state_.players.end() || selected_player->row >= 13 ? 22 : 130;
        if (y >= top + 76 && y <= top + 85 && x < 60) { page_ = Page::roster; return true; }
        if (y >= top + 76 && y <= top + 85 && x >= 261 && x <= 312 && !team_premium_mode_) { page_ = Page::player_offer; return true; }
        if (y >= top + 76 && y <= top + 85 && x >= 90 && x <= 170) {
            ContractRequest req;
            req.proposed_bonuses = proposed_bonuses_;
            if (team_premium_mode_) { req.kind = ContractActionKind::submit_bonuses; req.row = -1; req.player_id = -1; }
            else if (salary_offer_set_ && state_.demands && selected_player != state_.players.end()) {
                req.kind = ContractActionKind::submit_offer; req.row = selected_player->row; req.player_id = selected_player->player_id;
                req.quote_id = state_.quote_id; req.term_column = offer_term_; req.demand = (*state_.demands)[offer_term_]; req.monthly_salary = offer_salary_;
            } else if (selected_player != state_.players.end()) {
                req.kind = ContractActionKind::submit_bonuses; req.row = selected_player->row; req.player_id = selected_player->player_id; req.quote_id = state_.quote_id;
            } else return false;
            if (!salary_offer_set_ && req.proposed_bonuses == (team_premium_mode_ ? state_.team_premiums : selected_player->team_premiums)) { page_ = Page::roster; return true; }
            emit(req); return true;
        }
        for (int index = 0; index < 7; ++index) {
            if (y >= top + 25 + index * 7 && y < top + 32 + index * 7 && x >= 5 && x <= 300 && (team_premium_mode_ || selected_player != state_.players.end())) {
                const uint32_t initial = proposed_bonuses_[size_t(index)];
                open_calculator(index, Page::team_premiums, initial); return true;
            }
        }
        return false;
    }
    if (y >= 215 && y <= 224) {
        const int which = x < 60 ? 0 : x < 120 ? 1 : x < 180 ? 2 : x < 250 ? 3 : -1;
        if (which < 0 || (!state_.mode_three && which==3)) return false;
        tab_ = uint8_t(which);
        page_ = which == 3 ? Page::team_premiums : Page::roster;
        if (which == 3) {
            team_premium_mode_ = true; proposed_bonuses_ = state_.team_premiums;
        }
        return true;
    }
    if(page_==Page::roster && x>=0 && x<61) {
        int baseline=36;
        for(size_t i=0;i<state_.players.size();++i) {
            const auto& p=state_.players[i];
            if(i&&(p.source.slot&0xc000)!=(state_.players[i-1].source.slot&0xc000))baseline+=2;
            if(y>=baseline-7 && y<baseline+2){player_info_row_=p.row;return true;}baseline+=7;
        }
    }
    return false;
}

bool ContractsScreen::process_timer_expired(int pointer_x, int pointer_y) {
    //InstallDefaultHooks(0x80) dispatches event8 against the actual name-column row:
    //outside the list it returns; over a player it enters the normal talk.
    if (pending_ || close_requested_) return false;
    if (page_ == Page::roster) return click(pointer_x, pointer_y, true);
    if (page_ == Page::calculator) {
        //BalanceFromGame event8 restores its backdrop and returns ffffffff;40c65
        //does not write the proposed bonus on that sentinel. No offer/insurance
        //command and no invented N-key delay belong to this local return.
        salary_input_ = calculator_initial_;
        page_ = calculator_return_;
        calculator_backdrop_.reset();
        calculator_bonus_index_ = -1;
        editing_salary_ = false;
        return true;
    }
    if (page_ == Page::player_offer || page_ == Page::team_premiums) {
        //41232 checks event8 after the offer button branch. A timer owns
        //neither that button nor its mutations: discard the local negotiation.
        page_ = Page::roster;
        return true;
    }
    return false; // result notices retain their separate physical input owner
}

bool ContractsScreen::move(int x,int y) {
    if (page_ != Page::roster || pending_) return false;
    int next=-1, baseline=36;
    for(size_t i=0;i<state_.players.size();++i) {
        const auto& p=state_.players[i];
        if(i&&(p.source.slot&0xc000)!=(state_.players[i-1].source.slot&0xc000))baseline+=2;
        if(x<61&&x>=0&&y>=baseline-7&&y<baseline+2){next=p.row;break;}baseline+=7;
    }
    if(next==hovered_row_)return false;
    hovered_row_=next;return true;
}

bool ContractsScreen::key(std::string_view key) {
    if (key == "ESC" || key == "ESCAPE") {
        if (page_ == Page::calculator) return false;
        if (page_ != Page::roster) { page_ = Page::roster; return true; }
        close_requested_ = true; pending_.reset(); return true;
    }
    if (pending_) return false;
    if (page_ == Page::calculator && key == "BACKSPACE") { salary_input_ /= 10; return true; }
    if (page_ == Page::calculator && key == "CLEAR") { salary_input_ = 0; return true; }
    if (page_ == Page::calculator && key.size() == 1 && key[0] >= '0' && key[0] <= '9') {
        const uint32_t digit = uint32_t(key[0] - '0');
        const uint32_t max = calculator_bonus_index_ >= 0 ? (calculator_bonus_index_ < 3 ? 20u : 1000u) : calculator_bonus_index_ == -2 ? 60u : 200000u;
        if (salary_input_ <= (max - digit) / 10) salary_input_ = salary_input_ * 10 + digit;
        return true;
    }
    return false;
}

void ContractsScreen::acknowledge_quote(const ContractsState& state) {
    if (!pending_ || pending_->kind != ContractActionKind::request_demands || state.revision <= state_.revision ||
        state.manager_slot != state_.manager_slot || state.runtime_club != state_.runtime_club || !state.demands ||
        state.quote_row != pending_->row || state.quote_id == 0)
        throw FormatError("contracts: quote acknowledgement does not match request");
    const auto old = std::find_if(state_.players.begin(), state_.players.end(), [&](const auto& p) { return p.row == pending_->row; });
    const auto fresh = std::find_if(state.players.begin(), state.players.end(), [&](const auto& p) { return p.row == pending_->row; });
    if (old == state_.players.end() || fresh == state.players.end() || old->player_id != pending_->player_id || fresh->player_id != old->player_id)
        throw FormatError("contracts: quote player identity changed");
    const uint64_t request_id_base = state_.request_id_base;
    proposed_bonuses_ = fresh->team_premiums;
    state_ = state;
    state_.selected_row = pending_->row;
    state_.request_id_base = request_id_base;
    salary_input_ = 0;
    selected_term_ = 0;
    salary_offer_set_ = false;
    offer_salary_ = 0;
    offer_term_ = 0;
    editing_salary_ = false;
    pending_.reset();
    page_ = Page::player_offer;
}

void ContractsScreen::acknowledge_offer(const ContractsState& state) {
    if (!pending_ || (pending_->kind != ContractActionKind::submit_offer && pending_->kind != ContractActionKind::submit_bonuses) || state.revision <= state_.revision ||
        state.manager_slot != state_.manager_slot || state.runtime_club != state_.runtime_club)
        throw FormatError("contracts: offer acknowledgement does not match request");
    result_backdrop_ = render();
    const uint64_t request_id_base = state_.request_id_base;
    state_ = state;
    state_.request_id_base = request_id_base;
    state_.selected_row = pending_->row;
    state_.demands.reset(); state_.quote_id = 0; state_.quote_row = -1;
    editing_salary_ = false;
    pending_.reset();
    result_return_=Page::roster;page_ = Page::result;
}

void ContractsScreen::acknowledge_bonus_offer(const ContractsState& state) {
    if (!pending_ || pending_->kind != ContractActionKind::submit_bonuses ||
        state.revision <= state_.revision || state.manager_slot != state_.manager_slot || state.runtime_club != state_.runtime_club)
        throw FormatError("contracts: bonus acknowledgement does not match request");
    result_backdrop_ = render();
    const uint64_t base = state_.request_id_base;
    state_ = state; state_.request_id_base = base; pending_.reset(); result_return_=Page::roster;page_ = state.feedback.empty() ? Page::roster : Page::result;
}

void ContractsScreen::acknowledge_insurance(const ContractsState& state) {
    if (!pending_ || pending_->kind != ContractActionKind::insure_player || state.revision <= state_.revision ||
        state.manager_slot != state_.manager_slot || state.runtime_club != state_.runtime_club ||
        state.quote_id != state_.quote_id || state.quote_row != pending_->row || !state.demands)
        throw FormatError("contracts: insurance acknowledgement does not match request");
    const auto before = std::find_if(state_.players.begin(), state_.players.end(), [&](const auto& p) { return p.row == pending_->row; });
    const auto after = std::find_if(state.players.begin(), state.players.end(), [&](const auto& p) { return p.row == pending_->row; });
    if (before == state_.players.end() || after == state.players.end() || before->player_id != pending_->player_id || after->player_id != before->player_id)
        throw FormatError("contracts: insurance player identity changed");
    const auto base = state_.request_id_base;
    state_ = state; state_.request_id_base = base; pending_.reset(); page_ = Page::player_offer;
    calculator_backdrop_.reset(); editing_salary_ = false;
}

void ContractsScreen::reject(std::string feedback) {
    if (!pending_) throw FormatError("contracts: no action to reject");
    const auto kind = pending_->kind;
    pending_.reset(); state_.feedback = std::move(feedback); editing_salary_ = false;
    if (kind == ContractActionKind::insure_player) page_ = Page::player_offer;
}

std::string ContractsScreen::state_json() const {
    auto out = contracts_state_json(state_);
    out.pop_back();
    out += ",\"monthly_salary_input\":" + std::to_string(salary_input_) +
           ",\"selected_term\":" + std::to_string(unsigned(selected_term_)) +
           ",\"editing_salary\":" + std::string(editing_salary_ ? "true" : "false") +
           ",\"page\":" + std::to_string(unsigned(page_)) + "}";
    return out;
}

} // namespace bmh::ui
