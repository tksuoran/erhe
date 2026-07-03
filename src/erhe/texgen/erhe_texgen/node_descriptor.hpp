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
    float_parameter = 0, // becomes a float uniform - live editable
    color_parameter = 1, // becomes a vec4 uniform - live editable
    enum_parameter  = 2, // substitutes its code fragment - edit recompiles
    bool_parameter  = 3, // substitutes "true"/"false" literal - edit recompiles
    size_parameter  = 4  // power-of-two exponent, substitutes the float literal pow(2, e) - edit recompiles
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
};

class Node_descriptor
{
public:
    std::string                       name      {}; // factory / type name
    std::string                       label     {}; // UI label
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
