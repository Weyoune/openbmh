// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "launch/mods.hpp"
#include <memory>
#include <optional>
namespace bmh::launch {
enum class LauncherAction { start, add, remove, rename, end };
struct LauncherChoice {
    LauncherAction action = LauncherAction::end;
    int selection = -1;
};
class LauncherUI {
  public:
    virtual ~LauncherUI() = default;
    virtual LauncherChoice next(const std::vector<ModEntry> &) = 0;
    virtual std::optional<std::filesystem::path> choose_folder(const std::string &title,
                                                               const std::filesystem::path &initial = {}) = 0;
    virtual std::optional<AddMod> configure_add(AddMod defaults) = 0;
    virtual std::optional<std::string> rename(const std::string &) = 0;
    virtual bool confirm_delete(const std::string &) = 0;
    virtual void message(const std::string &, bool error) = 0;
    virtual void progress(const std::string &, bool cancellable) = 0;
    virtual bool pump() = 0; // cancellation requested
    virtual void finish_progress() = 0;
};
std::unique_ptr<LauncherUI> make_launcher_ui();
} // namespace bmh::launch
