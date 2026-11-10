// SPDX-License-Identifier: GPL-3.0-only
// App: notices and questions over a screen (construction, season tickets, advertising renewal, national friendly, environment).
// (moved out of main.cpp unchanged; the class is declared in app/app.hpp)
#include "app/app.hpp"

// Next callback: stadium construction payment notice, shown over the investment detail.
auto App::present_construction_notice(const GameState& staged, const ConstructionNotice& notice) -> void {
        if (!flow.stadium()) throw std::runtime_error("construction notice has no stadium owner");
        // The Original payment notices restore the investment detail,
        // after the tip question has returned, before the outer ACK.
        const Image backdrop = flow.stadium()->construction_notice_backdrop();
        auto view = ui::construction_notice_dialog(staged, season_exe, notice);
        std::optional<ui::GameEndRenderOptions> effective_options;
        if (flow.stadium()->state().block_expansion || flow.stadium()->state().quoted_object) {
            const auto& owner = flow.stadium()->state();
            view.compact_dialog_frame = owner.compact_dialog_frame;
            effective_options = ui::GameEndRenderOptions{owner.plain_background, owner.gradient_text};
        }
        const char* title = notice.kind == ConstructionNoticeKind::InsufficientFunds
            ? "OpenBMH - stadium_funds_notice" : "OpenBMH - stadium_sponsor_notice";
        if (present_pending_dialog(staged, view, backdrop, title, effective_options) == PendingOfferAnswer::unsupported)
            throw std::runtime_error("construction notice presenter stopped");
}

// Next callback: season ticket compensation notice.
auto App::present_ticket_compensation_notice(const GameState& staged, const SeasonTicketCompensationNotice& notice) -> void {
        if (!flow.stadium()) throw std::runtime_error("compensation notice has no stadium owner");
        const Image backdrop = flow.stadium()->construction_notice_backdrop();
        auto view = ui::season_ticket_compensation_notice_dialog(staged, season_exe, notice);
        const auto& owner = flow.stadium()->state();
        view.compact_dialog_frame = owner.compact_dialog_frame;
        if (present_pending_dialog(staged, view, backdrop,
                "OpenBMH - stadium_ticket_compensation_notice",
                ui::GameEndRenderOptions{owner.plain_background, owner.gradient_text}) == PendingOfferAnswer::unsupported)
            throw std::runtime_error("compensation notice presenter stopped");
}

// Callback: advertising contract renewal. A question gives renew/decline; a sponsor's
// refusal is a notice that gives acknowledged; anything else aborts.
auto App::ask_advertising_renewal(const GameState& staged, const AdvertisingRenewal& renewal, const Image& backdrop) -> app::CareerSession::AdvertisingRenewalPresentation {
        if (!running) return app::CareerSession::AdvertisingRenewalPresentation::abort;
        const int manager = int(staged.memory.u8(::bmh::layout::ActiveManager));
        if (renewal.status == AdvertisingRenewal::Status::offered) {
            const auto view = ui::advertising_renewal_question_dialog(staged, manager, renewal);
            const auto answer = present_pending_dialog(staged, view, backdrop,
                "OpenBMH - advertising_renewal_question");
            if (answer == PendingOfferAnswer::yes) return app::CareerSession::AdvertisingRenewalPresentation::renew;
            if (answer == PendingOfferAnswer::no) return app::CareerSession::AdvertisingRenewalPresentation::decline;
            return app::CareerSession::AdvertisingRenewalPresentation::abort;
        }
        if (renewal.status == AdvertisingRenewal::Status::declined_by_sponsor) {
            const auto view = ui::advertising_renewal_notice_dialog(staged, manager, renewal);
            if (present_pending_dialog(staged, view, backdrop,
                    "OpenBMH - advertising_renewal_notice") == PendingOfferAnswer::yes)
                return app::CareerSession::AdvertisingRenewalPresentation::acknowledged;
        }
        return app::CareerSession::AdvertisingRenewalPresentation::abort;
}

// Callback: national friendly question. Returns yes/no, or std::nullopt when unsupported
// or the program is quitting.
auto App::ask_national_friendly(const GameState& staged,ui::TaxDialogProjection view,const Image& backdrop) -> std::optional<bool> {
        if(!running)return {};
        const auto answer=present_pending_dialog(staged,std::move(view),backdrop,
            "OpenBMH - national_friendly_question");
        flow.acknowledge_national_friendly_modal_release();
        // The synchronous questions already consumed their UP;
        // do not retain the picker DOWN as a held parent press.
        press.reset();
        if(answer==PendingOfferAnswer::unsupported)return {};
        return answer==PendingOfferAnswer::yes;
}

// Next callback: environment/settings notice dialog. Returns false when the program is
// quitting or the dialog could not be shown.
auto App::present_environment_notice(const GameState& staged, ui::TaxDialogProjection view, const Image& backdrop) -> bool {
        if (!running) return false;
        const bool settings_notice = flow.in_settings() && flow.pending_settings();
        const bool shown = present_pending_dialog(staged, std::move(view), backdrop,
            settings_notice ? "OpenBMH - settings_notice" : "OpenBMH - environment_notice")
            != PendingOfferAnswer::unsupported;
        if (settings_notice) {
            flow.acknowledge_settings_notice_release();
            press.reset();
            SDL_FlushEvents(SDL_MOUSEMOTION, SDL_MOUSEBUTTONUP);
        }
        return shown;
}
