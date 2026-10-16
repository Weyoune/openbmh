// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace bmh::cli {

struct Json {
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json, std::less<>>;
    using Value = std::variant<std::nullptr_t, bool, std::int64_t, std::string, Array, Object>;
    Value value = nullptr;

    Json() = default;
    Json(std::nullptr_t) : value(nullptr) {}
    Json(bool v) : value(v) {}
    Json(std::int64_t v) : value(v) {}
    Json(int v) : value(std::int64_t(v)) {}
    Json(std::string v) : value(std::move(v)) {}
    Json(const char* v) : value(std::string(v)) {}
    Json(Array v) : value(std::move(v)) {}
    Json(Object v) : value(std::move(v)) {}

    const Object* object() const { return std::get_if<Object>(&value); }
    Object* object() { return std::get_if<Object>(&value); }
    const Array* array() const { return std::get_if<Array>(&value); }
    const std::string* string() const { return std::get_if<std::string>(&value); }
    const std::int64_t* integer() const { return std::get_if<std::int64_t>(&value); }
    const bool* boolean() const { return std::get_if<bool>(&value); }
};

class JsonError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

Json parse_json(std::string_view source);
std::string dump_json(const Json& value);

} // namespace bmh::cli
