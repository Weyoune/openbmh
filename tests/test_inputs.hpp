// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>

namespace bmh::test {
inline std::filesystem::path external_input(const char* name) {
    const char* root = std::getenv("OPENBMH_TEST_INPUTS");
    if (!root || !*root) throw std::runtime_error("External integration-test input directory not configured");
    const auto path = std::filesystem::path(root) / name;
    if (!std::filesystem::exists(path)) throw std::runtime_error("External integration-test input missing");
    return path;
}
inline const std::string& expected_text(const std::string& key) {
    static const auto entries = [] {
        std::map<std::string, std::string> result;
        std::ifstream input(external_input("expected-text.tsv"), std::ios::binary);
        if (!input) throw std::runtime_error("Cannot read external text expectations");
        for (std::string line; std::getline(input, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const auto split = line.find('\t');
            if (split == std::string::npos) throw std::runtime_error("Malformed external text expectation");
            result.emplace(line.substr(0, split), line.substr(split + 1));
        }
        return result;
    }();
    const auto found = entries.find(key);
    if (found == entries.end()) throw std::runtime_error("External text expectation key missing");
    return found->second;
}
}
