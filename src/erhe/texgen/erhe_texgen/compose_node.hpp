#pragma once

#include "erhe_texgen/node_descriptor.hpp"

#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

namespace erhe::texgen {

class Compose_node;

// Per-instance value storage for one parameter; the field matching the
// descriptor's Parameter_kind is the meaningful one.
class Parameter_value
{
public:
    float                float_value  {0.0f};
    std::array<float, 4> color_value  {0.0f, 0.0f, 0.0f, 1.0f};
    std::size_t          enum_index   {0};
    bool                 bool_value   {false};
    int                  size_exponent{8};

    // gradient_parameter: control-point data (live-editable). The editor node
    // reuses this same storage for its live gradient widget.
    std::vector<Gradient_stop> gradient_stops        {};
    Gradient_interpolation     gradient_interpolation{Gradient_interpolation::linear};

    // curve_parameter: control-point data (live-editable).
    std::vector<Curve_point>   curve_points{};
};

// Binding of one input to an upstream node output.
// source == nullptr means unconnected (the input's default expression is used).
class Input_binding
{
public:
    const Compose_node* source      {nullptr};
    std::size_t         output_index{0};
};

// One node in the compose-time DAG. Lightweight and non-owning: the caller
// owns the nodes and the referenced Node_descriptor, and both must outlive
// composition. The id must be unique within one composition; it feeds the
// "o{id}" name generation that keeps per-node locals, uniforms and helper
// functions collision-free.
class Compose_node
{
public:
    Compose_node(const Node_descriptor& descriptor, int id);

    // Sampler-source constructor (Phase 5 buffer cut point): this node has no
    // descriptor; composing it contributes a sampler2D binding named
    // "tex_<binding>" and its output is "texture(tex_<binding>, uv)" converted
    // to `type`. Used when the compose DAG walk reaches a buffer node - instead
    // of recursing into the buffer's input subtree, the composer samples the
    // buffer's already-rendered texture (which the caller binds at <binding>).
    Compose_node(int id, int sampler_binding, Value_type type);

    // True for a sampler-source node (no descriptor; get_descriptor() must not
    // be called).
    [[nodiscard]] auto is_sampler_source () const -> bool;
    [[nodiscard]] auto get_sampler_binding() const -> int;
    [[nodiscard]] auto get_sampler_type   () const -> Value_type;

    [[nodiscard]] auto get_descriptor() const -> const Node_descriptor&;
    [[nodiscard]] auto get_id        () const -> int;

    // Input bindings
    void set_input  (std::size_t input_index, const Compose_node* source, std::size_t output_index);
    void clear_input(std::size_t input_index);
    [[nodiscard]] auto get_input      (std::size_t input_index) const -> const Input_binding&;
    [[nodiscard]] auto get_input_index(std::string_view name) const -> std::size_t;

    // Parameter values (name must exist in the descriptor with the matching kind)
    void set_float        (std::string_view name, float value);
    void set_color        (std::string_view name, const std::array<float, 4>& value);
    void set_enum_index   (std::string_view name, std::size_t index);
    void set_bool         (std::string_view name, bool value);
    void set_size_exponent(std::string_view name, int exponent);

    // Sets a gradient parameter's control points and interpolation. Stops are
    // copied and sorted by position; positions equal-or-out-of-order are nudged
    // strictly increasing (mirrors Material Maker's MMGradient.sort) so the
    // emitted ladder never divides by zero. An empty stop list degrades to a
    // single opaque-black stop rather than emitting a stop-less function.
    void set_gradient(std::string_view name, const std::vector<Gradient_stop>& stops, Gradient_interpolation interpolation);

    // Sets a curve parameter's control points. Points are copied and sorted by
    // x; an empty / single-point list is accepted (the emitted function then
    // returns a constant). Mirrors set_size_exponent's tolerant validation.
    void set_curve(std::string_view name, const std::vector<Curve_point>& points);
    [[nodiscard]] auto get_parameter_value(std::size_t parameter_index) const -> const Parameter_value&;
    [[nodiscard]] auto get_parameter_index(std::string_view name) const -> std::size_t;

    // Per-node seed feeding the $seed uniform
    void set_seed(float seed);
    [[nodiscard]] auto get_seed() const -> float;

private:
    [[nodiscard]] auto get_checked_parameter(std::string_view name, Parameter_kind kind) -> Parameter_value&;

    const Node_descriptor*       m_descriptor{nullptr};
    int                          m_id        {0};
    float                        m_seed      {0.0f};
    std::vector<Parameter_value> m_parameters; // parallel to descriptor parameters
    std::vector<Input_binding>   m_inputs;     // parallel to descriptor inputs

    // Sampler-source state (m_descriptor is nullptr when this is set).
    bool       m_is_sampler_source{false};
    int        m_sampler_binding  {0};
    Value_type m_sampler_type     {Value_type::rgba};
};

} // namespace erhe::texgen
