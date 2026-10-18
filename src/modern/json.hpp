// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "content/package.hpp"
namespace bmh::modern::detail {
using J = cli::Json;
inline const J &get(const J &value, const std::string &key) {
    const auto *object = value.object();
    if (!object || !object->contains(key))
        throw FormatError("Missing content field: " + key);
    return object->at(key);
}
inline std::string str(const J &value) {
    if (!value.string())
        throw FormatError("Content field is not text");
    return *value.string();
}
inline int64_t num(const J &value) {
    if (!value.integer())
        throw FormatError("Content field is not an integer");
    return *value.integer();
}
inline const J::Array &arr(const J &value) {
    if (!value.array())
        throw FormatError("Content field is not a list");
    return *value.array();
}
inline J parse(const content::Documents &docs, const std::string &name) {
    return cli::parse_json(docs.at(name));
}
inline J::Array items(const content::Documents &docs, const std::string &table) {
    return arr(get(parse(docs, "world/" + table + ".json"), "items"));
}
} // namespace bmh::modern::detail
