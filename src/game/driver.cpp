// SPDX-License-Identifier: GPL-3.0-only
#include "game/driver.hpp"

#include <algorithm>
#include <array>
#include <vector>

#include "game/matchday.hpp"
#include "game/matchday_setup.hpp"
#include "game/season.hpp"

namespace bmh {

int cup_tie(const MatchGroup& grp, int m) {
    const uint16_t r = grp.cup_results.at(size_t(m));
    int home = r >> 5 & 0x1F, away = r & 0x1F;
    int other_home = 0, other_away = 0;
    if (!grp.cup_other_leg.empty()) {
        other_home = grp.cup_other_leg.at(size_t(m)) >> 5 & 0x1F;
        other_away = grp.cup_other_leg.at(size_t(m)) & 0x1F;
    }
    // this match's home team was the away team of the other leg
    if ((grp.flags() & 0x10) && other_home + away == home + other_away && (r >> 11) == 0) {
        away *= 2;        // away goals of this match ...
        other_away *= 2;  // ... and of the other leg count double
    }
    const int home_total = home + other_away, away_total = other_home + away;
    return (unsigned(home_total) < unsigned(away_total) ? 1 : 0) + (home_total == away_total ? 2 : 0);
}

bool cup_screen_shown(const std::vector<uint8_t>& buttons, int cup_id, int minute) {
    if (buttons.empty()) return true;
    const int kind = (minute > 45) + 1;
    const int row = cup_id + 5;
    if (row == 0x12) return buttons.at(size_t(0x12 + kind)) == 0 || buttons.at(size_t(0x1b + kind)) == 0;
    return buttons.at(size_t(kind + row * 9)) == 0;
}

void play_phase(MatchEngine& engine, std::vector<MatchGroup>& groups, int first, int last, int stoppage, int phase,
                MinuteReport& report, const std::function<void(int)>& after_minute) {
    for (MatchGroup& grp : groups) grp.ended = 0, grp.phase = phase;
    for (int minute = first; minute <= last + stoppage && !report.unsupported; minute++) {
        bool running = false;
        for (const MatchGroup& grp : groups)
            for (int m = 0; m < grp.matches(); m++)
                if (!((grp.void_mask() | grp.ended) >> m & 1)) running = true;
        if (!running) break;  // the original leaves the phase after a minute without matches
        report = engine.minute(minute, last, stoppage);
        if (after_minute) after_minute(minute);
    }
}

namespace {

void set_void(MatchGroup& grp, int m) {
    const uint32_t v = grp.void_mask() | 1u << m;
    for (int i = 0; i < 4; i++) grp.desc.at(0xb8 + size_t(i)) = uint8_t(v >> (8 * i));
}

}  // namespace

namespace {

bool group_played(const MatchGroup& grp) {
    for (int m = 0; m < grp.matches(); m++)
        if (grp.ended >> m & 1) return true;
    return false;
}

bool result_visible(GameState& g, const MatchGroup& grp, int last,
                    const std::vector<uint8_t>& buttons, PhaseResultKind& kind,
                    int& competition_id) {
    const uint32_t id = le32(grp.desc, 0xcc);
    if (id < 0x15) {
        kind = PhaseResultKind::League;
        competition_id = int(id);
        return league_screen_shown(buttons, 2, last, int(id));
    }
    kind = PhaseResultKind::Cup;
    competition_id = int16_t(g.memory.u16(id + 0x18));
    return cup_screen_shown(buttons, competition_id, last);
}

PhaseResultRequest make_result_request(const MatchGroup& grp, uint64_t phase_id,
                                       uint32_t group_index, uint64_t revision,
                                       PhaseResultKind kind, int competition_id, int last) {
    PhaseResultRequest req;
    req.phase_id = phase_id;
    req.group_index = group_index;
    req.revision = revision;
    req.kind = kind;
    req.competition_id = competition_id;
    req.phase_end_minute = last;
    req.matches.reserve(size_t(grp.matches()));
    for (int m = 0; m < grp.matches(); ++m) {
        PhaseResultMatch score;
        score.home_team = grp.fixtures.at(size_t(m * 2));
        score.away_team = grp.fixtures.at(size_t(m * 2 + 1));
        score.score_known = match_score(grp, m, score.home_score, score.away_score);
        req.matches.push_back(score);
    }
    return req;
}

void finish_group_phase(GameState& g, MatchGroup& grp, int last, GameRandom& game) {
    const bool full_time = last == 90 || last == 120 || last == 40;
    if (!full_time) return;
    const bool final_phase = last == 40 || last == 120;
    for (int m = 0; m < grp.matches(); m++) {
        if ((grp.flags() & 8) && cup_tie(grp, m) == 2) {
            uint16_t& r = grp.cup_results.at(size_t(m));
            if (r != 0xCCCC) r = uint16_t((final_phase ? 2 : 1) * 0x400 + (r & 0x3FF));
            if (final_phase) finish_match(g, grp, m, last, game);
            continue;
        }
        if (grp.void_mask() >> m & 1) continue;
        finish_match(g, grp, m, last, game);
        set_void(grp, m);
    }
}

PhaseEndOutput rejected(const char* why) {
    PhaseEndOutput out;
    out.status = PhaseEndStatus::Rejected;
    out.reason = why;
    return out;
}

PhaseEndOutput advance_phase_end(PhaseEndCursor& c) {
    while (c.next_group < c.groups->size()) {
        MatchGroup& grp = c.groups->at(c.next_group);
        if (c.phase_played) {
            PhaseResultKind kind;
            int competition_id = 0;
            if (result_visible(*c.state, grp, c.last, c.buttons, kind, competition_id)) {
                c.pending = make_result_request(grp, c.phase_id, c.next_group, c.revision,
                                                kind, competition_id, c.last);
                // CupGroupOverviewEffects initializes the cup viewer before presenting it.
                // Keep this once at entry, so closing cannot erase viewer edits.
                if (kind == PhaseResultKind::Cup) cup_overview_effects(*c.state);
                PhaseEndOutput out;
                out.status = PhaseEndStatus::Waiting;
                out.request = c.pending;
                return out;
            }
        }
        finish_group_phase(*c.state, grp, c.last, *c.game);
        ++c.next_group;
    }
    c.complete = true;
    c.pending.reset();
    PhaseEndOutput out;
    out.status = PhaseEndStatus::Complete;
    return out;
}

}  // namespace

PhaseEndOutput begin_phase_end(PhaseEndCursor& c, uint64_t phase_id, GameState& g,
                               std::vector<MatchGroup>& groups, int last,
                               const std::vector<uint8_t>& buttons, GameRandom& game,
                               WatcomRandom& rand, MinuteReport& report) {
    if (c.initialized) {
        const bool same = c.phase_id == phase_id && c.state == &g && c.groups == &groups && c.last == last &&
                          c.buttons_source == &buttons && c.game == &game && c.rand == &rand && c.report == &report;
        if (!c.complete) {
            if (!same) return rejected("phase already active with different identity or borrowed objects");
            if (c.pending) {
                PhaseEndOutput out;
                out.status = PhaseEndStatus::Waiting;
                out.request = c.pending;
                return out;
            }
            return rejected("active phase has no pending result");
        }
        if (same) {
            PhaseEndOutput out;
            out.status = PhaseEndStatus::Complete;
            return out;
        }
    }
    c.initialized = true;
    c.complete = false;
    // Original local_48 belongs to EndPhase, not to a group. Preserve this
    // gate throughout the barrier even as each acknowledged group settles.
    c.phase_played = std::any_of(groups.begin(), groups.end(), group_played);
    c.phase_id = phase_id;
    c.revision = 1;
    c.next_group = 0;
    c.last = last;
    c.buttons = buttons;
    c.buttons_source = &buttons;
    c.state = &g;
    c.groups = &groups;
    c.game = &game;
    c.rand = &rand;
    c.report = &report;
    c.pending.reset();
    c.last_ack.reset();
    c.after_last_ack.reset();
    return advance_phase_end(c);
}

PhaseEndOutput continue_phase_end(PhaseEndCursor& c, const std::optional<PhaseResultReply>& reply) {
    if (!c.initialized) return rejected("phase not started");
    if (!reply) {
        if (c.complete) return rejected("phase already complete");
        if (!c.pending) return rejected("no result is pending");
        PhaseEndOutput out;
        out.status = PhaseEndStatus::Waiting;
        out.request = c.pending;
        return out;
    }
    if (c.last_ack && reply->phase_id == c.last_ack->phase_id &&
        reply->group_index == c.last_ack->group_index && reply->revision == c.last_ack->revision) {
        return *c.after_last_ack;
    }
    if (c.complete) return rejected("phase already complete");
    if (!c.pending || reply->phase_id != c.pending->phase_id ||
        reply->group_index != c.pending->group_index || reply->revision != c.pending->revision)
        return rejected("stale or mismatched result acknowledgement");

    MatchGroup& grp = c.groups->at(c.pending->group_index);
    if (c.pending->kind == PhaseResultKind::League) {
        stir_random(*c.game, *c.rand);
    }
    finish_group_phase(*c.state, grp, c.last, *c.game);
    c.next_group = c.pending->group_index + 1;
    c.last_ack = *reply;
    ++c.revision;
    c.pending.reset();
    PhaseEndOutput out = advance_phase_end(c);
    c.after_last_ack = out;
    return out;
}

void end_phase(GameState& g, std::vector<MatchGroup>& groups, int last, const std::vector<uint8_t>& buttons,
               GameRandom& game, WatcomRandom& rand, MinuteReport& report,
               const std::function<void(const PhaseResultRequest&, const std::vector<MatchGroup>&,
                                        const GameState&)>& presenter) {
    PhaseEndCursor cursor;
    PhaseEndOutput out = begin_phase_end(cursor, 1, g, groups, last, buttons, game, rand, report);
    while (out.status == PhaseEndStatus::Waiting && out.request) {
        const PhaseResultRequest& req = *out.request;
        if (presenter) presenter(req, groups, g);
        out = continue_phase_end(cursor, PhaseResultReply{req.phase_id, req.group_index, req.revision});
    }
}

void play_matchday(MatchEngine& engine, GameState& g, std::vector<MatchGroup>& groups,
                   const std::vector<uint8_t>& buttons, GameRandom& game, WatcomRandom& rand, MinuteReport& report,
                   const std::function<void(int)>& after_minute, int stop_at) {
    // EndPhase calls of the matchday: outdoors two halves (4 minutes of stoppage in the
    // second) and extra time; the indoor tournament (CareerSessionRuntimeField) plays 2 x 20 minutes
    const int outdoor[4][4] = {{1, 45, 0, 0}, {46, 90, 4, 0}, {91, 105, 0, 1}, {106, 120, 1, 1}};
    const int indoor[2][4] = {{1, 20, 0, 0}, {21, 40, 0, 0}};
    engine.indoor = g.indoor;
    std::vector<std::array<int, 4>> phases;
    if (g.indoor)
        for (const auto& p : indoor) phases.push_back({p[0], p[1], p[2], p[3]});
    else
        for (const auto& p : outdoor) phases.push_back({p[0], p[1], p[2], p[3]});
    for (const auto& p : phases) {
        play_phase(engine, groups, p[0], p[1], p[2], p[3], report, after_minute);
        if (report.unsupported) return;
        if (p[1] == stop_at) return;
        end_phase(g, groups, p[1], buttons, game, rand, report, engine.phase_results);
        if (report.unsupported) return;
        // FinishEvent / BuildSideRecords: EvaluatePoolsCoupon(0) after the second half (outdoors)
        if (!g.indoor && p[1] == 90) evaluate_pools_coupon(g, groups);
    }
    // CupTie2: penalty shoot-outs
    for (MatchGroup& grp : groups)
        for (int m = 0; m < grp.matches() && !report.unsupported; m++)
            if ((grp.flags() & 8) && !grp.cup_results.empty() && (grp.cup_results[size_t(m)] >> 11) != 0)
                engine.penalty_shootout(grp, m, report);
}

}  // namespace bmh
