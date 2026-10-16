// SPDX-License-Identifier: GPL-3.0-only
#include "cli/json.hpp"
#include <iostream>
#include <stdexcept>

using namespace bmh::cli;
int main() try {
    const auto value = parse_json(R"({"items":[1,-2,true,null],"text":"Unit\n\"case\"","nested":{"name":"test"}})");
    if (!value.object() || value.object()->at("items").array()->size() != 4 ||
        *value.object()->at("text").string() != "Unit\n\"case\"")
        throw std::runtime_error("JSON structure/string decode mismatch");
    const auto encoded = dump_json(value);
    if (dump_json(parse_json(encoded)) != encoded) throw std::runtime_error("JSON roundtrip mismatch");
    for (const char* bad : {"{", "[1,]", "{\"x\":}", "true false", "\"unterminated"}) {
        bool rejected = false;
        try { parse_json(bad); } catch (const JsonError&) { rejected = true; }
        if (!rejected) throw std::runtime_error("Malformed JSON accepted");
    }
    std::cout << "JSON synthetic structure, escapes, roundtrip and refusal checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
