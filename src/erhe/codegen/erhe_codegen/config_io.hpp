#pragma once

#include "erhe_file/file.hpp"

#include <simdjson.h>

#include <optional>
#include <string>
#include <string_view>

namespace erhe::codegen {

// Load a codegen-generated config struct from a JSON file.
// Returns default-constructed T if the file is missing or malformed.
template <typename T>
auto load_config(std::string_view file_path) -> T
{
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
    deserialize(obj, config);
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
