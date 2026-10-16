// SPDX-License-Identifier: GPL-3.0-only
#include "cli/protocol.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>

#include "app/career_session.hpp"
#include "game/save.hpp"

using namespace bmh;
using cli::Json;
using Object = Json::Object;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
Json request(Object fields) { return Json(std::move(fields)); }
bool ok(const Json& response) { return *response.object()->at("ok").boolean(); }
std::string error(const Json& response) {
    const auto found=response.object()->find("error");
    return found==response.object()->end()?"":*found->second.string();
}
}
int main(int argc,char** argv) {
    try {
        if (argc!=3) throw std::runtime_error("usage: game_end_cli_tests <BMH data> <Native GAMEND.FBM>");
        if (!*argv[1]) { std::cout << "GameEnd CLI tests skipped: BMH_DATA not configured\n"; return 77; }
        const std::filesystem::path data(argv[1]),fixture(argv[2]);
        cli::Protocol protocol(data,589388519u);
        const auto loaded=protocol.handle(request({{"cmd","load"},{"path",fixture.string()}}));
        check(ok(loaded) && protocol.active(),"Native GAMEND CLI load");
        const auto before_revision=protocol.revision();
        const auto completed=protocol.handle(request({{"cmd","kickoff"},{"manager",0},
            {"revision",int64_t(before_revision)}}));
        check(ok(completed) && !protocol.active() && protocol.revision()==before_revision+1,
              "terminal kickoff succeeds and consumes exactly one revision");
        const auto& fields=*completed.object();
        check(!fields.contains("state") && !fields.contains("results"),"terminal response has no desk/results projection");
        const auto& events=*fields.at("events").array();
        check(events.size()==1 && *events[0].object()->at("event").string()=="career_complete",
              "one explicit terminal event, no next_desk or audio event");
        const auto& status=*fields.at("status").object();
        check(*status.at("phase").string()=="career_complete" && !*status.at("active").boolean() &&
              *status.at("date").integer()==365 && *status.at("season").integer()==1 &&
              *status.at("duration").integer()==0,"Source endpoint cursor/duration");
        check(cli::text_response(completed).find("career complete | date365")==std::string::npos &&
              cli::text_response(completed).find("career complete | date 365")!=std::string::npos &&
              cli::text_response(completed).find("club ?")==std::string::npos,"terminal text output has no invented desk");
        const auto terminal_revision=protocol.revision();
        const auto terminal_status=cli::dump_json(protocol.handle(request({{"cmd","status"}})));
        check(terminal_status==cli::dump_json(protocol.handle(request({{"cmd","status"}}))),"owned repeated terminal status");
        for (auto command:{"desk","results","tactics","training","kickoff","save","bank.deposit","camp.book"})
            check(error(protocol.handle(request({{"cmd",command}})))=="career_complete" &&
                  protocol.revision()==terminal_revision,"terminal gate precedes missing envelopes and projections");
        check(ok(protocol.handle(request({{"cmd","help"}}))) && ok(protocol.handle(request({{"cmd","quit"}}))),
              "help/quit remain available after completion");
        check(terminal_status==cli::dump_json(protocol.handle(request({{"cmd","status"}}))),"rejected commands leave terminal status unchanged");
        check(ok(protocol.handle(request({{"cmd","load"},{"path",fixture.string()}}))) && protocol.active() &&
              protocol.revision()==terminal_revision+1,"explicit reload starts a new active transport career");
        std::cout<<"Native GAMEND CLI terminal event/status/revision and inert commands OK\n";

        app::CareerSession session(data,589388519u,fixture);
        uint64_t revision=1;
        ui::DeskRequest kickoff; kickoff.kind=ui::DeskRequestKind::advance_turn;
        kickoff.manager_slot=0;kickoff.revision=revision;
        std::string detail;
        check(session.dispatch_kickoff(kickoff,revision,detail).kind==TurnDeskKind::CareerComplete &&
              session.career_complete(),"direct Native session completes");
        const auto stream=save_stream(session.state());
        const auto game=session.game();const auto rand=session.rand();const auto saved_revision=revision;
        kickoff.revision=revision;
        check(session.dispatch_kickoff(kickoff,revision,detail).kind==TurnDeskKind::Failed && detail=="career_complete",
              "completed direct kickoff is rejected before cursor/revision adoption");
        const auto target=std::filesystem::temp_directory_path()/"openbmh-game-end-inert-save.FBM";
        const bool existed=std::filesystem::exists(target);
        check(!session.save_game(target,0,true,detail) && detail=="career_complete" &&
              std::filesystem::exists(target)==existed,"completed session cannot write a save");
        ui::DeskRequest widget;widget.kind=ui::DeskRequestKind::change_option;
        check(!session.dispatch_desk_widget(widget,revision,detail) && detail=="career_complete",
              "completed session cannot mutate desk options");
        check(save_stream(session.state())==stream && session.game().seed()==game.seed() &&
              session.game().calls()==game.calls() && session.rand().seed()==rand.seed() &&
              session.rand().calls()==rand.calls() && revision==saved_revision,"direct terminal requests preserve all native bytes/RNG/revision");
        std::cout<<"Direct session completion/kickoff/save/desk guards preserve native stream and both RNGs OK\n";
        return 0;
    } catch(const std::exception& exception) { std::cerr<<exception.what()<<'\n';return 1; }
}
