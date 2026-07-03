#pragma once

#include "erhe_texgen/value_type.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace erhe::texgen {

// Node_descriptor and its sub-descriptors mirror Material Maker's
// shader_model schema (.mmg node files / gen_shader.gd, MIT license).
// Descriptors are immutable data tables; Compose_node instances reference
// them and carry the per-instance parameter values and input bindings.

class Input_descriptor
{
public:
    std::string name              {};                    // referenced in templates as $name(coord)
    Value_type  type              {Value_type::rgba};    // expected value type at this input
    std::string default_expression{};                    // GLSL expression used when unconnected; may use $uv
    bool        function          {false};               // true: emit an input helper function (multiply-sampled inputs)
};

class Output_descriptor
{
public:
    Value_type  type      {Value_type::rgba};
    std::string expression{}; // GLSL expression template, e.g. "$color" or "perlin($uv, ...)"
};

enum class Parameter_kind : unsigned int {
    float_parameter    = 0, // becomes a float uniform - live editable
    color_parameter    = 1, // becomes a vec4 uniform - live editable
    enum_parameter     = 2, // substitutes its code fragment - edit recompiles
    bool_parameter     = 3, // substitutes "true"/"false" literal - edit recompiles
    size_parameter     = 4, // power-of-two exponent, substitutes the float literal pow(2, e) - edit recompiles
    gradient_parameter = 5, // emits a per-node "vec4 <name>(float x)" gradient function; $name resolves to it
    curve_parameter    = 6  // emits a per-node "float <name>(float x)" curve function; $name resolves to it
};

// Interpolation between adjacent gradient stops. Ported from Material Maker's
// MMGradient.Interpolation (types/gradient.gd, MIT license).
enum class Gradient_interpolation : unsigned int {
    constant   = 0,
    linear     = 1,
    smoothstep = 2,
    cubic      = 3
};

// One gradient control point: a position on [0, 1] and its rgba color.
class Gradient_stop
{
public:
    float                position{0.0f};
    std::array<float, 4> color   {0.0f, 0.0f, 0.0f, 1.0f};
};

// One tone-curve control point: an (x, y) knot plus left / right tangent
// slopes (Hermite handles). Ported from Material Maker's MMCurve.Point
// (types/curve.gd, MIT license).
class Curve_point
{
public:
    float x          {0.0f};
    float y          {0.0f};
    float left_slope {0.0f};
    float right_slope{0.0f};
};

class Enum_value
{
public:
    std::string label{}; // UI label
    std::string code {}; // GLSL code fragment substituted for the parameter
};

class Parameter_descriptor
{
public:
    std::string    name {};
    std::string    label{};
    Parameter_kind kind {Parameter_kind::float_parameter};

    // float_parameter
    float default_float{0.0f};
    float min_value    {0.0f};
    float max_value    {1.0f};
    float step         {0.01f};

    // color_parameter
    std::array<float, 4> default_color{0.0f, 0.0f, 0.0f, 1.0f};

    // enum_parameter
    std::vector<Enum_value> enum_values       {};
    std::size_t             default_enum_index{0};

    // bool_parameter
    bool default_bool{false};

    // size_parameter (power-of-two exponent range)
    int min_size_exponent    {4};
    int max_size_exponent    {12};
    int default_size_exponent{8};

    // gradient_parameter (default control points + interpolation). Defaults to
    // a black@0 -> white@1 linear ramp when left empty (see Compose_node ctor).
    std::vector<Gradient_stop> default_gradient_stops        {};
    Gradient_interpolation     default_gradient_interpolation{Gradient_interpolation::linear};

    // curve_parameter (default control points). Defaults to the identity
    // 0,0 -> 1,1 curve when left empty (see Compose_node ctor).
    std::vector<Curve_point> default_curve_points{};
};

class Node_descriptor
{
public:
    std::string                       name      {}; // factory / type name
    std::string                       label     {}; // UI label
    std::string                       category  {}; // UI palette grouping (editor-only; empty for library test descriptors)
    std::vector<Input_descriptor>     inputs    {};
    std::vector<Output_descriptor>    outputs   {};
    std::vector<Parameter_descriptor> parameters{};
    std::string                       global    {}; // GLSL function library injected once (deduplicated)
    std::string                       code      {}; // inline statements run before the output expressions

    // True when any template of this descriptor references $seed / $(seed)
    // or uses $rnd(...) - such nodes get a per-node seed uniform.
    [[nodiscard]] auto uses_seed() const -> bool;
};

} // namespace erhe::texgen
