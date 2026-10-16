// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "ui/lineup.hpp"
#include "ui/pitch.hpp"
#include "ui/tactics.hpp"

namespace bmh::ui {

// A copied view used while a watched match is paused for manager input. It is
// descriptive only: proposal helpers below never write a GameState or draw RNG.
struct LiveInterventionContext {
    int team = -1;
    int minute = 0;
    int substitutions_used = 0;
    bool human_controlled = false;
    uint64_t revision = 0;
    LineupState lineup;
    int current_formation = -1;
    std::array<uint8_t, 8> tactics{};
};

enum class LiveProposalKind { none, substitution, formation, player_position, tactic };
struct LiveInterventionProposal {
    LiveProposalKind kind = LiveProposalKind::none;
    int team = -1;
    int manager_slot = -1;
    int runtime_club = -1;
    uint64_t revision = 0;
    int minute = 0;
    int outgoing_player = -1, incoming_player = -1;
    int outgoing_row = -1, incoming_row = -1;
    uint16_t outgoing_slot = 0, incoming_slot = 0;
    int formation = -1;
    int row = -1, depth = -1, lane = -1;
    int tactic_index = -1, tactic_value = -1;
};

struct LiveInterventionResult {
    bool accepted = false;
    LiveInterventionProposal proposal;
    std::string reason;
};

// Translate an existing drawless preparation request into a live-match
// proposal. The controller must still validate and apply the proposal through
// an authoritative live-match API; acceptance here means only well-formed UI.
LiveInterventionResult propose_live_lineup_change(
    const LiveInterventionContext&, const LineupRequest&);
LiveInterventionResult propose_live_pitch_change(
    const LiveInterventionContext&, const PitchRequest&);
LiveInterventionResult propose_live_tactic_change(
    const LiveInterventionContext&, const TacticsRequest&);
std::string live_intervention_proposal_json(const LiveInterventionProposal&);


enum class LiveInterventionPage { lineup, formation, tactics };
struct LiveInterventionDialogEvent {
    enum class Kind { none, resume, proposal } kind = Kind::none;
    LiveInterventionProposal proposal;
};

// A self-contained modal view for a live_minute pause. It only returns proposals;
// an authoritative controller must apply/reject them and send a fresh projection.
class LiveInterventionDialog {
public:
    explicit LiveInterventionDialog(LiveInterventionContext context);
    Image render() const;
    LiveInterventionDialogEvent click(int x, int y, bool right = false);
    LiveInterventionDialogEvent key(std::string_view key);
    LiveInterventionPage page() const { return page_; }
    bool closed() const { return closed_; }
    const LiveInterventionContext& context() const { return context_; }
    const std::string& feedback() const { return feedback_; }
private:
    LiveInterventionContext context_;
    LiveInterventionPage page_ = LiveInterventionPage::lineup;
    std::optional<int> selected_row_;
    int selected_tactic_ = 0;
    std::string feedback_;
    bool closed_ = false;
    LiveInterventionDialogEvent choose_player(int row);
    LiveInterventionDialogEvent propose(LiveInterventionResult result);
};

} // namespace bmh::ui
