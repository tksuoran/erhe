#include "erhe_gltf/gltf_curve.hpp"
#include "erhe_scene/curve.hpp"

#include <fmt/format.h>
#include <simdjson.h>

#include <cmath>

namespace erhe::gltf {

namespace {

void append_float(std::string& out, const float value)
{
    // {} is the shortest decimal that reads back to the same float; "-0" would read as the integer 0.
    if ((value == 0.0f) && std::signbit(value)) {
        out += "-0.0";
        return;
    }
    out += fmt::format("{}", value);
}

void append_vec3(std::string& out, const glm::vec3& v)
{
    out += '[';
    append_float(out, v.x);
    out += ',';
    append_float(out, v.y);
    out += ',';
    append_float(out, v.z);
    out += ']';
}

[[nodiscard]] auto read_vec3(simdjson::dom::element element, glm::vec3& out_value) -> bool
{
    simdjson::dom::array array;
    if (element.get_array().get(array) != simdjson::SUCCESS) {
        return false;
    }
    if (array.size() != 3) {
        return false;
    }
    std::size_t i = 0;
    for (simdjson::dom::element component : array) {
        double value = 0.0;
        if (component.get_double().get(value) != simdjson::SUCCESS) {
            return false;
        }
        const float float_value = static_cast<float>(value);
        if (!std::isfinite(float_value)) {
            return false;
        }
        out_value[static_cast<glm::length_t>(i)] = float_value;
        ++i;
    }
    return true;
}

} // anonymous namespace

auto curve_path_to_json(const erhe::math::Curve_path& path) -> std::string
{
    std::string out = fmt::format("{{\"closed\":{},\"points\":[", (path.closure == erhe::math::Curve_closure::closed) ? "true" : "false");
    for (std::size_t i = 0, end = path.points.size(); i < end; ++i) {
        const erhe::math::Curve_point& point = path.points[i];
        if (i > 0) {
            out += ',';
        }
        out += "{\"position\":";
        append_vec3(out, point.position);
        out += ",\"in\":";
        append_vec3(out, point.in_handle);
        out += ",\"out\":";
        append_vec3(out, point.out_handle);
        out += fmt::format(",\"kind\":\"{}\"}}", erhe::scene::c_str(point.kind));
    }
    out += "]}";
    return out;
}

auto curve_path_from_json(const std::string_view json, erhe::math::Curve_path& out_path, std::string& out_error) -> bool
{
    simdjson::dom::parser  parser;
    simdjson::dom::element root;
    if (parser.parse(simdjson::padded_string{json}).get(root) != simdjson::SUCCESS) {
        out_error = "not JSON";
        return false;
    }
    simdjson::dom::object object;
    if (root.get_object().get(object) != simdjson::SUCCESS) {
        out_error = "not an object";
        return false;
    }
    erhe::math::Curve_path path{};
    for (const simdjson::dom::key_value_pair member : object) {
        if ((member.key != "closed") && (member.key != "points")) {
            out_error = fmt::format("unknown key '{}'", std::string_view{member.key});
            return false;
        }
    }
    bool closed = false;
    if (object.at_key("closed").get_bool().get(closed) != simdjson::SUCCESS) {
        out_error = "'closed' missing or not a boolean";
        return false;
    }
    path.closure = closed ? erhe::math::Curve_closure::closed : erhe::math::Curve_closure::open;
    simdjson::dom::array points;
    if (object.at_key("points").get_array().get(points) != simdjson::SUCCESS) {
        out_error = "'points' missing or not an array";
        return false;
    }
    std::size_t index = 0;
    for (simdjson::dom::element point_element : points) {
        simdjson::dom::object point_object;
        if (point_element.get_object().get(point_object) != simdjson::SUCCESS) {
            out_error = fmt::format("point {} is not an object", index);
            return false;
        }
        for (const simdjson::dom::key_value_pair member : point_object) {
            if ((member.key != "position") && (member.key != "in") && (member.key != "out") && (member.key != "kind")) {
                out_error = fmt::format("point {}: unknown key '{}'", index, std::string_view{member.key});
                return false;
            }
        }
        erhe::math::Curve_point point{};
        simdjson::dom::element position;
        simdjson::dom::element in_handle;
        simdjson::dom::element out_handle;
        std::string_view       kind_text;
        if (
            (point_object.at_key("position").get(position)   != simdjson::SUCCESS) || !read_vec3(position,   point.position)  ||
            (point_object.at_key("in"      ).get(in_handle)  != simdjson::SUCCESS) || !read_vec3(in_handle,  point.in_handle) ||
            (point_object.at_key("out"     ).get(out_handle) != simdjson::SUCCESS) || !read_vec3(out_handle, point.out_handle)
        ) {
            out_error = fmt::format("point {}: 'position', 'in' and 'out' must be arrays of 3 finite numbers", index);
            return false;
        }
        if (
            (point_object.at_key("kind").get_string().get(kind_text) != simdjson::SUCCESS) ||
            !erhe::scene::curve_point_kind_from_string(kind_text, point.kind)
        ) {
            out_error = fmt::format("point {}: 'kind' must be \"corner\", \"smooth\" or \"free\"", index);
            return false;
        }
        path.points.push_back(point);
        ++index;
    }
    out_path = std::move(path);
    return true;
}

} // namespace erhe::gltf
