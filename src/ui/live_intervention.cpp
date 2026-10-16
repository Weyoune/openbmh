// SPDX-License-Identifier: GPL-3.0-only
#include "ui/live_intervention.hpp"

#include "formats/binary.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

#include <sstream>

namespace bmh::ui {
namespace {
LiveInterventionResult reject(std::string why) {
    LiveInterventionResult result;
    result.reason = std::move(why);
    return result;
}

bool context_valid(const LiveInterventionContext& c) {
    return c.human_controlled && c.team >= 0 && c.minute > 2 &&
           c.substitutions_used >= 0 && c.substitutions_used <= 3 &&
           c.lineup.manager_slot >= 0 && c.lineup.runtime_club >= 0 &&
           c.lineup.runtime_club == c.team &&
           c.lineup.revision == c.revision;
}

bool request_matches(const LiveInterventionContext& c, int manager, int club,
                     uint64_t revision) {
    return manager == c.lineup.manager_slot && club == c.lineup.runtime_club &&
           revision == c.revision;
}

LiveInterventionResult accepted(LiveInterventionProposal proposal) {
    LiveInterventionResult result;
    result.accepted = true;
    result.proposal = proposal;
    return result;
}

LiveInterventionProposal base_proposal(const LiveInterventionContext& c) {
    LiveInterventionProposal p;
    p.team = c.team;
    p.manager_slot = c.lineup.manager_slot;
    p.runtime_club = c.lineup.runtime_club;
    p.revision = c.revision;
    p.minute = c.minute;
    return p;
}
} // namespace

LiveInterventionResult propose_live_lineup_change(
        const LiveInterventionContext& c, const LineupRequest& r) {
    if (!context_valid(c)) return reject("live_context_unavailable");
    if (!request_matches(c, r.manager_slot, r.runtime_club, r.revision))
        return reject("stale_or_wrong_manager");
    if (r.kind != LineupRequestKind::swap_players)
        return reject("lineup_request_not_a_live_change");
    if (c.substitutions_used >= 3) return reject("substitution_limit_reached");
    if (r.first_row < 0 || r.second_row < 0 || r.first_row == r.second_row ||
            size_t(r.first_row) >= c.lineup.players.size() ||
            size_t(r.second_row) >= c.lineup.players.size())
        return reject("invalid_squad_rows");

    const auto& first = c.lineup.players.at(size_t(r.first_row));
    const auto& second = c.lineup.players.at(size_t(r.second_row));
    if (first.row != r.first_row || second.row != r.second_row ||
            first.player_id < 0 || second.player_id < 0 ||
            first.player_id != r.first_player || second.player_id != r.second_player ||
            first.slot != r.first_slot || second.slot != r.second_slot)
        return reject("lineup_request_does_not_match_projection");

    const bool first_starter = (first.slot & 0x8000) != 0;
    const bool second_starter = (second.slot & 0x8000) != 0;
    const bool first_bench = (first.slot & 0x4000) != 0;
    const bool second_bench = (second.slot & 0x4000) != 0;
    if (first_starter == second_starter || first_bench == second_bench ||
            !(first_starter || second_starter) || !(first_bench || second_bench))
        return reject("choose_one_active_player_and_one_bench_player");

    LiveInterventionProposal p = base_proposal(c);
    p.kind = LiveProposalKind::substitution;
    if (first_starter) {
        p.outgoing_player = first.player_id; p.outgoing_row = first.row; p.outgoing_slot = first.slot;
        p.incoming_player = second.player_id; p.incoming_row = second.row; p.incoming_slot = second.slot;
    } else {
        p.outgoing_player = second.player_id; p.outgoing_row = second.row; p.outgoing_slot = second.slot;
        p.incoming_player = first.player_id; p.incoming_row = first.row; p.incoming_slot = first.slot;
    }
    return accepted(p);
}

LiveInterventionResult propose_live_pitch_change(
        const LiveInterventionContext& c, const PitchRequest& r) {
    if (!context_valid(c)) return reject("live_context_unavailable");
    if (!request_matches(c, r.manager_slot, r.runtime_club, r.revision))
        return reject("stale_or_wrong_manager");
    LiveInterventionProposal p = base_proposal(c);
    if (r.kind == PitchRequestKind::apply_formation) {
        if (r.formation < 0 || r.formation >= 8) return reject("invalid_formation");
        p.kind = LiveProposalKind::formation; p.formation = r.formation;
        return accepted(p);
    }
    if (r.kind == PitchRequestKind::move_player) {
        if (r.row < 0 || size_t(r.row) >= c.lineup.players.size()) return reject("invalid_squad_row");
        const auto& player = c.lineup.players.at(size_t(r.row));
        if (player.row != r.row || player.player_id != r.player_id || player.slot != r.slot)
            return reject("pitch_request_does_not_match_projection");
        if (r.new_depth < 0 || r.new_depth > 18 || r.new_lane < 0 || r.new_lane > 8)
            return reject("invalid_pitch_position");
        p.kind = LiveProposalKind::player_position;
        p.row = r.row; p.depth = r.new_depth; p.lane = r.new_lane;
        return accepted(p);
    }
    return reject("pitch_action_not_supported_during_live_match");
}

LiveInterventionResult propose_live_tactic_change(
        const LiveInterventionContext& c, const TacticsRequest& r) {
    if (!context_valid(c)) return reject("live_context_unavailable");
    if (!request_matches(c, r.manager_slot, r.runtime_club, r.revision))
        return reject("stale_or_wrong_manager");
    if (r.kind != TacticsRequestKind::set_tactic || r.index < 0 || r.index >= 8 ||
            r.desired_value < 0 || r.desired_value > 255)
        return reject("invalid_tactic_request");
    LiveInterventionProposal p = base_proposal(c);
    p.kind = LiveProposalKind::tactic;
    p.tactic_index = r.index; p.tactic_value = r.desired_value;
    return accepted(p);
}

std::string live_intervention_proposal_json(const LiveInterventionProposal& p) {
    const char* kind = "none";
    switch (p.kind) {
    case LiveProposalKind::none: kind = "none"; break;
    case LiveProposalKind::substitution: kind = "substitution"; break;
    case LiveProposalKind::formation: kind = "formation"; break;
    case LiveProposalKind::player_position: kind = "player_position"; break;
    case LiveProposalKind::tactic: kind = "tactic"; break;
    }
    std::ostringstream out;
    out << "{\"status\":\"proposal_only\",\"kind\":\"" << kind
        << "\",\"team\":" << p.team << ",\"manager_slot\":" << p.manager_slot
        << ",\"runtime_club\":" << p.runtime_club << ",\"revision\":" << p.revision
        << ",\"minute\":" << p.minute << ",\"outgoing_player\":" << p.outgoing_player
        << ",\"incoming_player\":" << p.incoming_player << ",\"outgoing_row\":" << p.outgoing_row
        << ",\"incoming_row\":" << p.incoming_row << ",\"outgoing_slot\":" << p.outgoing_slot
        << ",\"incoming_slot\":" << p.incoming_slot << ",\"formation\":" << p.formation
        << ",\"row\":" << p.row << ",\"depth\":" << p.depth << ",\"lane\":" << p.lane
        << ",\"tactic_index\":" << p.tactic_index << ",\"tactic_value\":" << p.tactic_value << "}";
    return out.str();
}

namespace {
void fill(Image& image, int x, int y, int w, int h, uint8_t color) {
    for (int py = std::max(0, y); py < std::min(image.height, y + h); ++py)
        for (int px = std::max(0, x); px < std::min(image.width, x + w); ++px)
            image.at(px, py) = color;
}

struct Glyph { char ch; std::array<uint8_t, 7> rows; };
constexpr Glyph kGlyphs[] = {
    {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}},
    {'C',{14,17,16,16,16,17,14}}, {'D',{30,17,17,17,17,17,30}},
    {'E',{31,16,16,30,16,16,31}}, {'F',{31,16,16,30,16,16,16}},
    {'G',{14,17,16,23,17,17,14}}, {'H',{17,17,17,31,17,17,17}},
    {'I',{14,4,4,4,4,4,14}}, {'J',{7,2,2,2,18,18,12}},
    {'K',{17,18,20,24,20,18,17}}, {'L',{16,16,16,16,16,16,31}},
    {'M',{17,27,21,21,17,17,17}}, {'N',{17,25,21,19,17,17,17}},
    {'O',{14,17,17,17,17,17,14}}, {'P',{30,17,17,30,16,16,16}},
    {'Q',{14,17,17,17,21,18,13}}, {'R',{30,17,17,30,20,18,17}},
    {'S',{15,16,16,14,1,1,30}}, {'T',{31,4,4,4,4,4,4}},
    {'U',{17,17,17,17,17,17,14}}, {'V',{17,17,17,17,17,10,4}},
    {'W',{17,17,17,21,21,21,10}}, {'X',{17,17,10,4,10,17,17}},
    {'Y',{17,17,10,4,4,4,4}}, {'Z',{31,1,2,4,8,16,31}},
    {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}},
    {'2',{14,17,1,2,4,8,31}}, {'3',{30,1,1,14,1,1,30}},
    {'4',{2,6,10,18,31,2,2}}, {'5',{31,16,16,30,1,1,30}},
    {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}},
    {'8',{14,17,17,14,17,17,14}}, {'9',{14,17,17,15,1,1,14}},
    {'\'',{4,4,2,0,0,0,0}}, {':',{0,4,4,0,4,4,0}},
    {'-',{0,0,0,31,0,0,0}}, {'+',{0,4,4,31,4,4,0}},
    {'[',{14,8,8,8,8,8,14}}, {']',{14,2,2,2,2,2,14}},
    {'>',{16,8,4,2,4,8,16}}, {'<',{1,2,4,8,4,2,1}},
    {'/',{1,2,2,4,8,8,16}}, {'?',{14,17,1,2,4,0,4}}, {' ',{0,0,0,0,0,0,0}}
};

const std::array<uint8_t, 7>& glyph(char c) {
    c = char(std::toupper(static_cast<unsigned char>(c)));
    for (const auto& g : kGlyphs) if (g.ch == c) return g.rows;
    for (const auto& g : kGlyphs) if (g.ch == '?') return g.rows;
    return kGlyphs[sizeof(kGlyphs) / sizeof(kGlyphs[0]) - 1].rows;
}
void text(Image& out, int x, int y, std::string_view value, uint8_t color) {
    for (unsigned char ch : value) {
        const auto& shape = glyph(char(ch));
        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < 5; ++col)
                if (shape[size_t(row)] & (1u << (4 - col)))
                    if (x + col >= 0 && x + col < out.width && y + row >= 0 && y + row < out.height)
                        out.at(x + col, y + row) = color;
        x += 6;
        if (x >= out.width) break;
    }
}
std::string number(int value) { return std::to_string(value); }
std::string user_feedback(std::string_view reason) {
    if (reason == "substitution_limit_reached") return "KEINE AUSWECHSLUNG MEHR MOEGLICH";
    if (reason == "choose_one_active_player_and_one_bench_player")
        return "SPIELER AUF DEM FELD UND ERSATZ WAEHLEN";
    if (reason == "stale_or_wrong_manager" || reason == "lineup_request_does_not_match_projection" ||
        reason == "pitch_request_does_not_match_projection") return "AUFSTELLUNG HAT SICH GEAENDERT";
    return "AKTION DERZEIT NICHT MOEGLICH";
}
} // namespace

LiveInterventionDialog::LiveInterventionDialog(LiveInterventionContext context)
    : context_(std::move(context)) {
    if (!context_valid(context_)) throw FormatError("live dialog: human side and minute > 2 required");
    if (context_.lineup.players.empty()) throw FormatError("live dialog: lineup projection is empty");
    if (context_.current_formation < -1 || context_.current_formation >= 8)
        throw FormatError("live dialog: invalid formation projection");
}

Image LiveInterventionDialog::render() const {
    Image out(320, 240, 0x13);
    fill(out, 0, 0, 320, 18, 0x39);
    fill(out, 1, 1, 318, 16, 0x24);
    text(out, 7, 5, "MINUTE " + number(context_.minute) + " - PAUSE", 0xf3);
    constexpr int tab_x[] = {2, 108, 214};
    constexpr const char* labels[] = {"AUFSTELLUNG", "FORMATION", "TAKTIK"};
    const LiveInterventionPage pages[] = {LiveInterventionPage::lineup,
        LiveInterventionPage::formation, LiveInterventionPage::tactics};
    for (size_t i = 0; i < 3; ++i) {
        const bool active = pages[i] == page_;
        fill(out, tab_x[i], 21, 102, 14, active ? 0xf3 : 0x39);
        fill(out, tab_x[i] + 1, 22, 100, 12, active ? 0x24 : 0x13);
        text(out, tab_x[i] + 8, 24, labels[i], active ? 0xf3 : 0x51);
    }

    if (page_ == LiveInterventionPage::lineup) {
        text(out, 5, 40, "SPIELER AUF DEM FELD UND ERSATZ WAEHLEN", 0xe2);
        const size_t visible = std::min<size_t>(context_.lineup.players.size(), 24);
        for (size_t i = 0; i < visible; ++i) {
            const auto& player = context_.lineup.players[i];
            const int y = 51 + int(i) * 7;
            const bool selected = selected_row_ && *selected_row_ == int(i);
            if (selected) fill(out, 3, y - 1, 314, 7, 0x39);
            const char* state = (player.slot & 0x8000) ? "F" : (player.slot & 0x4000) ? "E" : "-";
            text(out, 6, y, number(int(i + 1)), selected ? 0xf3 : 0x51);
            text(out, 24, y, state, selected ? 0xf3 : 0xe2);
            const std::string name = player.name.empty() ? "SPIELER" : player.name.substr(0, 36);
            text(out, 38, y, name, selected ? 0xf3 : 0xe2);
            text(out, 270, y, number(player.player_id), selected ? 0xf3 : 0x51);
        }
    } else if (page_ == LiveInterventionPage::formation) {
        text(out, 6, 42, "FORMATION WAEHLEN", 0xe2);
        for (int i = 0; i < 8; ++i) {
            const int x = 10 + (i % 2) * 153;
            const int y = 58 + (i / 2) * 38;
            const bool active = context_.current_formation == i;
            fill(out, x, y, 140, 30, active ? 0x39 : 0x24);
            fill(out, x + 1, y + 1, 138, 28, 0x13);
            text(out, x + 12, y + 11, "FORMATION", 0xe2);
            text(out, x + 112, y + 11, number(i + 1), 0xf3);
        }
    } else {
        text(out, 6, 42, "TAKTIK AENDERN MIT < ODER >", 0xe2);
        for (int i = 0; i < 8; ++i) {
            const int y = 55 + i * 20;
            fill(out, 8, y, 304, 17, i == selected_tactic_ ? 0x39 : 0x24);
            text(out, 14, y + 5, "TAKTIK", 0xe2);
            text(out, 76, y + 5, number(i + 1), 0xf3);
            text(out, 236, y + 5, "<", 0xf3);
            text(out, 267, y + 5, number(context_.tactics[size_t(i)]), 0xe2);
            text(out, 296, y + 5, ">", 0xf3);
        }
    }
    fill(out, 0, 226, 320, 14, 0x39);
    fill(out, 1, 227, 318, 12, 0x24);
    text(out, 6, 230, feedback_.empty() ? "ESC: WEITERSPIELEN" : feedback_, 0xf3);
    return out;
}

LiveInterventionDialogEvent LiveInterventionDialog::propose(LiveInterventionResult result) {
    LiveInterventionDialogEvent event;
    if (!result.accepted) {
        feedback_ = user_feedback(result.reason);
        return event;
    }
    feedback_ = "ANFRAGE GESENDET";
    event.kind = LiveInterventionDialogEvent::Kind::proposal;
    event.proposal = result.proposal;
    return event;
}

LiveInterventionDialogEvent LiveInterventionDialog::choose_player(int row) {
    LiveInterventionDialogEvent event;
    if (row < 0 || size_t(row) >= context_.lineup.players.size()) return event;
    if (!selected_row_) { selected_row_ = row; return event; }
    if (*selected_row_ == row) { selected_row_.reset(); return event; }
    LineupRequest request;
    request.kind = LineupRequestKind::swap_players;
    request.manager_slot = context_.lineup.manager_slot;
    request.runtime_club = context_.lineup.runtime_club;
    request.revision = context_.revision;
    request.first_row = *selected_row_; request.second_row = row;
    const auto& first = context_.lineup.players[size_t(*selected_row_)];
    const auto& second = context_.lineup.players[size_t(row)];
    request.first_player = first.player_id; request.first_slot = first.slot;
    request.second_player = second.player_id; request.second_slot = second.slot;
    selected_row_.reset();
    return propose(propose_live_lineup_change(context_, request));
}

LiveInterventionDialogEvent LiveInterventionDialog::click(int x, int y, bool right) {
    LiveInterventionDialogEvent event;
    if (closed_) return event;
    if (right) return key("ESC");
    if (x < 0 || x >= 320 || y < 0 || y >= 240) return event;
    if (y >= 21 && y <= 35) {
        page_ = x < 108 ? LiveInterventionPage::lineup : x < 214 ? LiveInterventionPage::formation : LiveInterventionPage::tactics;
        selected_row_.reset();
        return event;
    }
    if (page_ == LiveInterventionPage::lineup) {
        const int row = (y - 51) / 7;
        const size_t visible = std::min<size_t>(context_.lineup.players.size(), 24);
        if (y >= 51 && y < 51 + int(visible) * 7 && row >= 0 && size_t(row) < visible)
            return choose_player(row);
    } else if (page_ == LiveInterventionPage::formation && y >= 58 && x >= 10 && x < 303) {
        const int col = x < 163 ? 0 : 1;
        const int row = (y - 58) / 38;
        const int form = row * 2 + col;
        if (row >= 0 && row < 4 && y < 58 + row * 38 + 30 &&
            x >= 10 + col * 153 && x < 150 + col * 153) {
            PitchRequest request;
            request.kind = PitchRequestKind::apply_formation;
            request.manager_slot = context_.lineup.manager_slot;
            request.runtime_club = context_.lineup.runtime_club;
            request.revision = context_.revision; request.formation = form;
            return propose(propose_live_pitch_change(context_, request));
        }
    } else if (page_ == LiveInterventionPage::tactics && y >= 55 && y < 215) {
        const int index = (y - 55) / 20;
        if (index >= 0 && index < 8 && x >= 8 && x < 312 && y < 55 + index * 20 + 17) {
            selected_tactic_ = index;
            const int current = context_.tactics[size_t(index)];
            const int next = x >= 234 && x < 258 ? std::max(0, current - 1) :
                             x >= 291 && x < 313 ? std::min(255, current + 1) : current;
            if (next != current) {
                TacticsRequest request;
                request.kind = TacticsRequestKind::set_tactic;
                request.manager_slot = context_.lineup.manager_slot;
                request.runtime_club = context_.lineup.runtime_club;
                request.revision = context_.revision; request.index = index;
                request.desired_value = next;
                return propose(propose_live_tactic_change(context_, request));
            }
        }
    }
    return event;
}

LiveInterventionDialogEvent LiveInterventionDialog::key(std::string_view key_value) {
    LiveInterventionDialogEvent event;
    if (closed_) return event;
    if (key_value == "ESC" || key_value == "ESCAPE") {
        closed_ = true;
        event.kind = LiveInterventionDialogEvent::Kind::resume;
    } else if (key_value == "F1") page_ = LiveInterventionPage::lineup;
    else if (key_value == "F2") page_ = LiveInterventionPage::formation;
    else if (key_value == "F3") page_ = LiveInterventionPage::tactics;
    return event;
}

} // namespace bmh::ui
