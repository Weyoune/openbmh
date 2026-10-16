// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <filesystem>
#include <string_view>
#include <optional>

#include "formats/font.hpp"
#include "formats/le.hpp"
#include "formats/textd.hpp"
#include "ui/mailbox_state.hpp"

namespace bmh::ui {

// Owned message pages. Article actions are requests; only the app adapter writes
// recipient/keep bits. Navigation leaves the notification flag and RNG untouched.
class MailboxScreen {
public:
    MailboxScreen(const std::filesystem::path& data, MailboxState state,
                  std::string manager_name, Image desktop_background);
    Image render() const;
    bool click(int x, int y, bool right = false);
    bool key(std::string_view value);
    bool close_requested() const { return close_requested_; }
    void cancel_close_request() { close_requested_ = false; }
    size_t page() const { return page_; }
    size_t page_count() const;
    const MailboxState& state() const { return state_; }
    const std::optional<MailboxActionRequest>& pending() const { return pending_; }
    void acknowledge(MailboxState state);
    void reject(std::string reason);
    std::string state_json() const;
private:
    MailboxState state_;
    std::string manager_name_; // owned UTF-8
    Image desktop_background_, atlas_;
    LeImage exe_;
    TextDb texts_;
    Font font_;
    size_t page_ = 0;
    bool close_requested_ = false;
    std::string feedback_;
    MailboxActionKind action_mode_ = MailboxActionKind::none;
    std::optional<MailboxActionRequest> pending_;
    void validate_state(const MailboxState&) const;
    void text(Image&, int x, int baseline, std::string_view original_bytes,
              uint8_t color, int right) const;
    void body(Image&, int x, int baseline, int right, int bottom,
              std::string_view utf8, uint8_t color) const;
};

} // namespace bmh::ui
