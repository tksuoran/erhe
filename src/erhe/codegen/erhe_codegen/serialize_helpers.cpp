#include "erhe_codegen/serialize_helpers.hpp"

#include <charconv>
#include <cmath>

namespace erhe::codegen {

void serialize_string(std::string& out, std::string_view value)
{
    out += '"';
    for (const char c : value) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(static_cast<unsigned char>(c)));
                    out += buf;
                } else {
                    out += c;
                }
                break;
        }
    }
    out += '"';
}

void serialize_bool(std::string& out, bool value)
{
    out += value ? "true" : "false";
}

void serialize_int(std::string& out, int64_t value)
{
    char buf[32];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), value);
    out.append(buf, static_cast<std::size_t>(ptr - buf));
}

void serialize_uint(std::string& out, uint64_t value)
{
    char buf[32];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), value);
    out.append(buf, static_cast<std::size_t>(ptr - buf));
}

void serialize_float(std::string& out, float value)
{
    if (std::isnan(value) || std::isinf(value)) {
        out += "null";
        return;
    }
    char buf[32];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), value);
    out.append(buf, static_cast<std::size_t>(ptr - buf));
}

void serialize_double(std::string& out, double value)
{
    if (std::isnan(value) || std::isinf(value)) {
        out += "null";
        return;
    }
    char buf[32];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), value);
    out.append(buf, static_cast<std::size_t>(ptr - buf));
}

// Scalar deserialization

void deserialize_field(simdjson::ondemand::value val, bool& out)
{
    bool v;
    if (!val.get_bool().get(v)) {
        out = v;
    }
}

void deserialize_field(simdjson::ondemand::value val, int8_t& out)
{
    int64_t v;
    if (!val.get_int64().get(v)) {
        out = static_cast<int8_t>(v);
    }
}

void deserialize_field(simdjson::ondemand::value val, uint8_t& out)
{
    uint64_t v;
    if (!val.get_uint64().get(v)) {
        out = static_cast<uint8_t>(v);
    }
}

void deserialize_field(simdjson::ondemand::value val, int16_t& out)
{
    int64_t v;
    if (!val.get_int64().get(v)) {
        out = static_cast<int16_t>(v);
    }
}

void deserialize_field(simdjson::ondemand::value val, uint16_t& out)
{
    uint64_t v;
    if (!val.get_uint64().get(v)) {
        out = static_cast<uint16_t>(v);
    }
}

void deserialize_field(simdjson::ondemand::value val, int32_t& out)
{
    int64_t v;
    if (!val.get_int64().get(v)) {
        out = static_cast<int32_t>(v);
    }
}

void deserialize_field(simdjson::ondemand::value val, uint32_t& out)
{
    uint64_t v;
    if (!val.get_uint64().get(v)) {
        out = static_cast<uint32_t>(v);
    }
}

void deserialize_field(simdjson::ondemand::value val, int64_t& out)
{
    int64_t v;
    if (!val.get_int64().get(v)) {
        out = v;
    }
}

void deserialize_field(simdjson::ondemand::value val, uint64_t& out)
{
    uint64_t v;
    if (!val.get_uint64().get(v)) {
        out = v;
    }
}

void deserialize_field(simdjson::ondemand::value val, float& out)
{
    double v;
    if (!val.get_double().get(v)) {
        out = static_cast<float>(v);
    }
}

void deserialize_field(simdjson::ondemand::value val, double& out)
{
    double v;
    if (!val.get_double().get(v)) {
        out = v;
    }
}

void deserialize_field(simdjson::ondemand::value val, std::string& out)
{
    std::string_view v;
    if (!val.get_string().get(v)) {
        out = v;
    }
}

// glm deserialization

void deserialize_field(simdjson::ondemand::value val, glm::vec2& out)
{
    simdjson::ondemand::array arr;
    if (!val.get_array().get(arr)) {
        std::size_t i = 0;
        for (auto element : arr) {
            if (i >= 2) break;
            double v;
            if (!element.get_double().get(v)) {
                out[static_cast<glm::length_t>(i)] = static_cast<float>(v);
            }
            ++i;
        }
    }
}

void deserialize_field(simdjson::ondemand::value val, glm::vec3& out)
{
    simdjson::ondemand::array arr;
    if (!val.get_array().get(arr)) {
        std::size_t i = 0;
        for (auto element : arr) {
            if (i >= 3) break;
            double v;
            if (!element.get_double().get(v)) {
                out[static_cast<glm::length_t>(i)] = static_cast<float>(v);
            }
            ++i;
        }
    }
}

void deserialize_field(simdjson::ondemand::value val, glm::vec4& out)
{
    simdjson::ondemand::array arr;
    if (!val.get_array().get(arr)) {
        std::size_t i = 0;
        for (auto element : arr) {
            if (i >= 4) break;
            double v;
            if (!element.get_double().get(v)) {
                out[static_cast<glm::length_t>(i)] = static_cast<float>(v);
            }
            ++i;
        }
    }
}

void deserialize_field(simdjson::ondemand::value val, glm::mat4& out)
{
    simdjson::ondemand::array arr;
    if (!val.get_array().get(arr)) {
        std::size_t i = 0;
        for (auto element : arr) {
            if (i >= 16) break;
            double v;
            if (!element.get_double().get(v)) {
                out[static_cast<glm::length_t>(i / 4)][static_cast<glm::length_t>(i % 4)] = static_cast<float>(v);
            }
            ++i;
        }
    }
}

// glm serialization

void serialize_vec2(std::string& out, const glm::vec2& value)
{
    out += '[';
    serialize_float(out, value.x); out += ',';
    serialize_float(out, value.y);
    out += ']';
}

void serialize_vec3(std::string& out, const glm::vec3& value)
{
    out += '[';
    serialize_float(out, value.x); out += ',';
    serialize_float(out, value.y); out += ',';
    serialize_float(out, value.z);
    out += ']';
}

void serialize_vec4(std::string& out, const glm::vec4& value)
{
    out += '[';
    serialize_float(out, value.x); out += ',';
    serialize_float(out, value.y); out += ',';
    serialize_float(out, value.z); out += ',';
    serialize_float(out, value.w);
    out += ']';
}

void serialize_mat4(std::string& out, const glm::mat4& value)
{
    out += '[';
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            if (col != 0 || row != 0) {
                out += ',';
            }
            serialize_float(out, value[col][row]);
        }
    }
    out += ']';
}

} // namespace erhe::codegen
