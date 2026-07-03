#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace erhe::texgen {

// Value types that can flow between texture graph nodes.
//
// Ported from Material Maker's io_types.mmt (MIT license,
// https://github.com/RodZill4/material-maker).
//
// All three MVP types share the canonical coordinate signature "vec2 uv" and
// the same Material Maker slot class, so they freely interconnect through the
// conversion expression templates below. More types (e.g. sdf2d) can be added
// later by extending the enum, the name tables and the conversion table.
enum class Value_type : unsigned int {
    grayscale = 0, // GLSL float - Material Maker type name "f"
    rgb       = 1, // GLSL vec3  - color
    rgba      = 2  // GLSL vec4  - color + alpha
};

constexpr std::size_t value_type_count = 3;

// Short Material Maker style type key: "f" / "rgb" / "rgba"
[[nodiscard]] auto value_type_name(Value_type type) -> const char*;

// GLSL type name: "float" / "vec3" / "vec4"
[[nodiscard]] auto glsl_type_name(Value_type type) -> const char*;

// Returns the GLSL conversion expression template converting a value of type
// `from` to type `to`, with "$(value)" as the placeholder for the source
// expression. Returns nullptr when from == to (no conversion needed).
[[nodiscard]] auto conversion_template(Value_type from, Value_type to) -> const char*;

// Wraps `expression` in the from -> to conversion expression.
// Returns the expression unchanged when from == to.
[[nodiscard]] auto convert(std::string_view expression, Value_type from, Value_type to) -> std::string;

} // namespace erhe::texgen
