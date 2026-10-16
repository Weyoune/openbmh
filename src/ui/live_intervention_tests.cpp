// SPDX-License-Identifier: GPL-3.0-only
#include "ui/live_intervention.hpp"

#include <cstdlib>
#include <iostream>

using namespace bmh::ui;

static void require(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
static LiveInterventionContext context() {
    LiveInterventionContext c;
    c.team = 17; c.minute = 43; c.substitutions_used = 1; c.human_controlled = true;
    c.revision = 12; c.lineup.manager_slot = 0; c.lineup.runtime_club = 17;
    c.lineup.revision = 12;
    c.lineup.players.resize(24);
    for (int row = 0; row < 24; ++row) c.lineup.players[size_t(row)].row = row;
    c.lineup.players[0].player_id = 100; c.lineup.players[0].slot = uint16_t(0x8000 | 100);
    c.lineup.players[1].player_id = 200; c.lineup.players[1].slot = uint16_t(0x4000 | 200);
    return c;
}

int main() {
    auto c = context();
    LineupRequest r;
    r.kind = LineupRequestKind::swap_players; r.manager_slot = 0; r.runtime_club = 17;
    r.revision = 12; r.first_row = 1; r.second_row = 0;
    r.first_player = 200; r.first_slot = c.lineup.players[1].slot;
    r.second_player = 100; r.second_slot = c.lineup.players[0].slot;
    auto valid = propose_live_lineup_change(c, r);
    require(valid.accepted && valid.proposal.kind == LiveProposalKind::substitution,
            "starter/bench selection should propose a substitution");
    require(valid.proposal.outgoing_player == 100 && valid.proposal.incoming_player == 200,
            "selection order must not change substitution direction");
    require(live_intervention_proposal_json(valid.proposal).find("proposal_only") != std::string::npos,
            "serialized request must say proposal_only");

    auto stale = r; stale.revision = 11;
    require(!propose_live_lineup_change(c, stale).accepted, "stale request must be rejected");
    auto wrong_player = r; wrong_player.first_player = 201;
    require(!propose_live_lineup_change(c, wrong_player).accepted, "unprojected player must be rejected");
    auto no_subs = c; no_subs.substitutions_used = 3;
    require(!propose_live_lineup_change(no_subs, r).accepted, "fourth substitution must be rejected");
    auto early = c; early.minute = 2;
    require(!propose_live_lineup_change(early, r).accepted, "intervention before minute 3 must be rejected");
    auto not_human = c; not_human.human_controlled = false;
    require(!propose_live_lineup_change(not_human, r).accepted, "AI side must be rejected");
    auto wrong_team = c; wrong_team.team = 18;
    require(!propose_live_lineup_change(wrong_team, r).accepted, "different runtime team must be rejected");

    PitchRequest formation;
    formation.kind = PitchRequestKind::apply_formation; formation.manager_slot = 0;
    formation.runtime_club = 17; formation.revision = 12; formation.formation = 7;
    auto form = propose_live_pitch_change(c, formation);
    require(form.accepted && form.proposal.kind == LiveProposalKind::formation &&
            form.proposal.formation == 7, "formation should produce a proposal");
    formation.formation = 8;
    require(!propose_live_pitch_change(c, formation).accepted, "invalid formation must be rejected");

    TacticsRequest tactic;
    tactic.kind = TacticsRequestKind::set_tactic; tactic.manager_slot = 0;
    tactic.runtime_club = 17; tactic.revision = 12; tactic.index = 3; tactic.desired_value = 2;
    auto tactic_result = propose_live_tactic_change(c, tactic);
    require(tactic_result.accepted && tactic_result.proposal.tactic_index == 3,
            "valid tactic request should produce a proposal");
    tactic.index = 8;
    require(!propose_live_tactic_change(c, tactic).accepted, "invalid tactic index must be rejected");
    LiveInterventionDialog dialog(c);
    const auto frame = dialog.render();
    require(frame.width == 320 && frame.height == 240 && frame.pixels.size() == 320u * 240u,
            "live pause renders a complete modal frame");
    require(dialog.click(-1, 60).kind == LiveInterventionDialogEvent::Kind::none &&
            dialog.click(400, 60).kind == LiveInterventionDialogEvent::Kind::none,
            "out-of-canvas clicks are ignored");
    require(dialog.key("F2").kind == LiveInterventionDialogEvent::Kind::none &&
            dialog.page() == LiveInterventionPage::formation,
            "F2 opens the formation page without changing match state");
    auto formation_click = dialog.click(20, 60);
    require(formation_click.kind == LiveInterventionDialogEvent::Kind::proposal &&
            formation_click.proposal.kind == LiveProposalKind::formation &&
            formation_click.proposal.formation == 0,
            "formation page emits a checked proposal");
    dialog.key("F3");
    auto tactic_click = dialog.click(300, 57);
    require(tactic_click.kind == LiveInterventionDialogEvent::Kind::proposal &&
            tactic_click.proposal.kind == LiveProposalKind::tactic &&
            tactic_click.proposal.tactic_value == 1,
            "tactic page emits a checked proposal");
    dialog.key("F1");
    dialog.click(10, 51);
    auto sub_click = dialog.click(10, 58);
    require(sub_click.kind == LiveInterventionDialogEvent::Kind::proposal &&
            sub_click.proposal.outgoing_player == 100 && sub_click.proposal.incoming_player == 200,
            "lineup page emits a correctly directed substitution proposal");
    require(c.lineup.players[0].player_id == 100 && c.lineup.revision == 12,
            "dialog rendering and proposals do not mutate the frozen match projection");
    auto large_squad = c;
    large_squad.lineup.players.resize(30);
    large_squad.lineup.players[25].row = 25;
    large_squad.lineup.players[25].player_id = 300;
    large_squad.lineup.players[25].slot = uint16_t(0x4000 | 300);
    LiveInterventionDialog bounded(large_squad);
    bounded.click(10, 51);
    require(bounded.click(10, 226).kind == LiveInterventionDialogEvent::Kind::none &&
            bounded.click(10, 219).kind == LiveInterventionDialogEvent::Kind::none,
            "footer and undisplayed squad rows cannot emit substitutions");
    const auto visible_sub = bounded.click(10, 58);
    require(visible_sub.kind == LiveInterventionDialogEvent::Kind::proposal &&
            visible_sub.proposal.outgoing_player == 100 && visible_sub.proposal.incoming_player == 200,
            "ignored footer clicks preserve the visible player selection");
    bounded.key("F2");
    require(bounded.click(155, 60).kind == LiveInterventionDialogEvent::Kind::none,
            "empty space between formation buttons cannot submit a formation");
    bounded.key("F3");
    require(bounded.click(300, 74).kind == LiveInterventionDialogEvent::Kind::none,
            "empty space between tactic rows cannot submit a tactic change");
    LiveInterventionDialog exhausted(no_subs);
    exhausted.click(10, 51); exhausted.click(10, 58);
    require(exhausted.feedback() == "KEINE AUSWECHSLUNG MEHR MOEGLICH",
            "user feedback explains the substitution limit in German");
    auto resumed = dialog.key("ESC");
    require(resumed.kind == LiveInterventionDialogEvent::Kind::resume && dialog.closed(),
            "escape closes the modal and resumes the minute loop");
    std::cout << "live intervention proposal checks passed\n";
}
