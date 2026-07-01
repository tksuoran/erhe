#pragma once

#include "erhe_file/file.hpp"
#include "migration.hpp"

#include <simdjson.h>

#include <optional>
#include <string>
#include <string_view>

namespace erhe::codegen {

// Load a codegen-generated config struct from a JSON file.
// Returns default-constructed T if the file is missing or malformed.
//
// If out_upgraded is non-null, it is set to true when deserialization detected any
// struct (top-level or nested) whose stored _version was older than the code's current
// version -- i.e. the file was written by an older schema. Callers can use this to
// rewrite the file immediately in the current format.
template <typename T>
auto load_config(std::string_view file_path, bool* out_upgraded = nullptr) -> T
{
    if (out_upgraded != nullptr) {
        *out_upgraded = false;
    }
    T config{};
    std::optional<std::string> contents = erhe::file::read("load_config", file_path);
    if (!contents.has_value()) {
        return config;
    }
    simdjson::ondemand::parser parser;
    simdjson::padded_string padded{contents.value()};
    simdjson::ondemand::document doc;
    simdjson::error_code error = parser.iterate(padded).get(doc);
    if (error) {
        return config;
    }
    simdjson::ondemand::object obj;
    error = doc.get_object().get(obj);
    if (error) {
        return config;
    }
    clear_deserialized_old_version_flag();
    deserialize(obj, config);
    if (out_upgraded != nullptr) {
        *out_upgraded = deserialized_old_version();
    }
    return config;
}

// Save a codegen-generated config struct to a JSON file.
template <typename T>
auto save_config(const T& config, std::string_view file_path) -> bool
{
    std::string json = serialize(config, 0);
    json += '\n';
    return erhe::file::write_file(file_path, json);
}

} // namespace erhe::codegen
