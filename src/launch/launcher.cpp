// SPDX-License-Identifier: GPL-3.0-only
#include "launch/launcher.hpp"
#include "launch/launcher_ui.hpp"
#include "launch/process.hpp"
#include "launch/game_data.hpp"
#include "launch/archive.hpp"
#include <atomic>
#include <future>
#include <mutex>
namespace bmh::launch {
int run_launcher(const std::filesystem::path &executable, const SaveMigrator &migrate) {
    auto ui = make_launcher_ui();
    ModLibrary library(executable.parent_path());
    for (;;)
        try {
            const auto mods = library.list();
            const auto action = ui->next(mods);
            if (action.action == LauncherAction::end)
                return 0;
            if (action.action == LauncherAction::add) {
                const auto source = ui->choose_folder("BMH-/Football-Limited-Installation wählen");
                if (!source)
                    continue;
                const auto check = check_game_directory(*source);
                if (!check.playable)
                    throw FormatError("Installation wird nicht unterstützt: " + check.unsupported);
                AddMod initial;
                initial.source = check.directory;
                initial.saves = check.directory;
                initial.name = check.family == GameFamily::english ? "FTDL-classic" : "BMH-classic";
                const auto request = ui->configure_add(initial);
                if (!request)
                    continue;
                std::mutex mutex;
                std::string text = "Import wird vorbereitet …";
                std::atomic_bool cancelled = false;
                ui->progress(text, true);
                auto job = std::async(std::launch::async, [&] {
                    return library.add(*request, migrate, [&](const std::string &s) {
                        if (cancelled.load())
                            throw FormatError("Import abgebrochen");
                        std::lock_guard lock(mutex);
                        text = s;
                    });
                });
                while (job.wait_for(std::chrono::milliseconds(20)) != std::future_status::ready) {
                    if (ui->pump())
                        cancelled = true;
                    std::lock_guard lock(mutex);
                    ui->progress(text, true);
                }
                ui->finish_progress();
                const auto report = job.get();
                std::string summary =
                    "Mod importiert: " + report.mod.name +
                    "\nÜbernommene Spielstände/Einstellungen: " + std::to_string(report.migrated);
                for (const auto &error : report.errors)
                    summary += "\n" + error;
                ui->message(summary, false);
                continue;
            }
            if (action.selection < 0 || size_t(action.selection) >= mods.size())
                continue;
            const auto &mod = mods[size_t(action.selection)];
            if (action.action == LauncherAction::remove) {
                if (ui->confirm_delete(mod.name))
                    library.remove(mod);
            } else if (action.action == LauncherAction::rename) {
                if (const auto name = ui->rename(mod.name))
                    library.rename(mod, *name);
            } else if (action.action == LauncherAction::start) {
                if (!mod.error.empty())
                    throw FormatError(mod.error);
                (void)content::open(mod.directory / "content.obw");
                const auto userdata = library.user_directory(mod);
                ui->progress("Spiel läuft: " + mod.name, false);
                const int code = run_child(executable,
                                           {"--run-mod", path_to_utf8(mod.directory / "content.obw"),
                                            "--save-directory", path_to_utf8(userdata)},
                                           [&] { ui->pump(); });
                ui->finish_progress();
                if (code)
                    ui->message("Das Spiel wurde mit Fehlercode " + std::to_string(code) + " beendet.", true);
            }
        } catch (const std::exception &e) {
            ui->finish_progress();
            ui->message(e.what(), true);
        }
}
} // namespace bmh::launch
