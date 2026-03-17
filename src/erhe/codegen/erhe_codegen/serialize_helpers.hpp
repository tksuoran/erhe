#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>
#include <simdjson.h>

namespace erhe::codegen {

// Serialization helpers (append to output string)
void serialize_string(std::string& out, std::string_view value);
void serialize_bool  (std::string& out, bool value);
void serialize_int   (std::string& out, int64_t value);
void serialize_uint  (std::string& out, uint64_t value);
void serialize_float (std::string& out, float value);
void serialize_double(std::string& out, double value);

// Deserialization helpers (scalar)
void deserialize_field(simdjson::ondemand::value val, bool&        out);
void deserialize_field(simdjson::ondemand::value val, int8_t&      out);
void deserialize_field(simdjson::ondemand::value val, uint8_t&     out);
void deserialize_field(simdjson::ondemand::value val, int16_t&     out);
void deserialize_field(simdjson::ondemand::value val, uint16_t&    out);
void deserialize_field(simdjson::ondemand::value val, int32_t&     out);
void deserialize_field(simdjson::ondemand::value val, uint32_t&    out);
void deserialize_field(simdjson::ondemand::value val, int64_t&     out);
void deserialize_field(simdjson::ondemand::value val, uint64_t&    out);
void deserialize_field(simdjson::ondemand::value val, float&       out);
void deserialize_field(simdjson::ondemand::value val, double&      out);
void deserialize_field(simdjson::ondemand::value val, std::string& out);

// Deserialization helpers (glm)
void deserialize_field(simdjson::ondemand::value val, glm::vec2& out);
void deserialize_field(simdjson::ondemand::value val, glm::vec3& out);
void deserialize_field(simdjson::ondemand::value val, glm::vec4& out);
void deserialize_field(simdjson::ondemand::value val, glm::mat4& out);

// Serialization helpers (glm)
void serialize_vec2(std::string& out, const glm::vec2& value);
void serialize_vec3(std::string& out, const glm::vec3& value);
void serialize_vec4(std::string& out, const glm::vec4& value);
void serialize_mat4(std::string& out, const glm::mat4& value);

// Serialization helpers (vector/array) — templates in header
template <typename T>
void serialize_array_elements(std::string& out, const T* data, std::size_t count);

template <typename T, std::size_t N>
void serialize_array(std::string& out, const std::array<T, N>& value)
{
    out += '[';
    serialize_array_elements(out, value.data(), N);
    out += ']';
}

template <typename T>
void serialize_vector(std::string& out, const std::vector<T>& value)
{
    out += '[';
    serialize_array_elements(out, value.data(), value.size());
    out += ']';
}

// Deserialization helpers (vector/array) — templates in header
template <typename T>
void deserialize_array_elements(simdjson::ondemand::array arr, T* data, std::size_t count);

template <typename T, std::size_t N>
void deserialize_field(simdjson::ondemand::value val, std::array<T, N>& out)
{
    simdjson::ondemand::array arr;
    if (!val.get_array().get(arr)) {
        deserialize_array_elements(arr, out.data(), N);
    }
}

template <typename T>
void deserialize_field(simdjson::ondemand::value val, std::vector<T>& out)
{
    simdjson::ondemand::array arr;
    if (!val.get_array().get(arr)) {
        out.clear();
        for (auto element : arr) {
            T item{};
            simdjson::ondemand::value elem_val;
            if (!element.get(elem_val)) {
                deserialize_field(elem_val, item);
            }
            out.push_back(std::move(item));
        }
    }
}

} // namespace erhe::codegen
