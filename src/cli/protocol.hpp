// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "cli/json.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace bmh::app { class CareerSession; }
namespace bmh::cli {

class Protocol {
public:
    Protocol(std::filesystem::path data, std::uint32_t seed);
    ~Protocol();
    Json handle(const Json& request, bool replay = false);
    bool active() const noexcept;
    std::uint64_t revision() const noexcept { return revision_; }
    static Json help();

private:
    std::filesystem::path data_;
    std::uint32_t seed_;
    std::uint64_t revision_ = 0;
    std::unique_ptr<app::CareerSession> session_;
};

Json text_request(std::string_view line, const Protocol* protocol);
std::string text_response(const Json& response);

} // namespace bmh::cli
