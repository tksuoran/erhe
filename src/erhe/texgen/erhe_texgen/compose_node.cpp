#include "erhe_texgen/compose_node.hpp"

#include "erhe_verify/verify.hpp"

#include <algorithm>

namespace erhe::texgen {

Compose_node::Compose_node(const Node_descriptor& descriptor, const int id)
    : m_descriptor{&descriptor}
    , m_id        {id}
{
    m_parameters.reserve(descriptor.parameters.size());
    for (const Parameter_descriptor& parameter : descriptor.parameters) {
        if (parameter.kind == Parameter_kind::enum_parameter) {
            // Mirror set_enum_index(): the default must address an existing
            // enum value (a value-less enum parameter may only default to 0).
            ERHE_VERIFY(
                parameter.enum_values.empty()
                    ? (parameter.default_enum_index == 0)
                    : (parameter.default_enum_index < parameter.enum_values.size())
            );
        }
        Parameter_value value{};
        value.float_value            = parameter.default_float;
        value.color_value            = parameter.default_color;
        value.enum_index             = parameter.default_enum_index;
        value.bool_value             = parameter.default_bool;
        value.size_exponent          = parameter.default_size_exponent;
        value.gradient_stops         = parameter.default_gradient_stops;
        value.gradient_interpolation = parameter.default_gradient_interpolation;
        value.curve_points           = parameter.default_curve_points;
        m_parameters.push_back(value);
    }
    m_inputs.resize(descriptor.inputs.size());
}

Compose_node::Compose_node(const int id, const int sampler_binding, const Value_type type)
    : m_id                {id}
    , m_is_sampler_source {true}
    , m_sampler_binding   {sampler_binding}
    , m_sampler_type      {type}
{
}

auto Compose_node::is_sampler_source() const -> bool
{
    return m_is_sampler_source;
}

auto Compose_node::get_sampler_binding() const -> int
{
    return m_sampler_binding;
}

auto Compose_node::get_sampler_type() const -> Value_type
{
    return m_sampler_type;
}

auto Compose_node::get_descriptor() const -> const Node_descriptor&
{
    return *m_descriptor;
}

auto Compose_node::get_id() const -> int
{
    return m_id;
}

void Compose_node::set_input(const std::size_t input_index, const Compose_node* source, const std::size_t output_index)
{
    ERHE_VERIFY(input_index < m_inputs.size());
    // A sampler-source node has no descriptor and exposes a single output at
    // index 0; any other node is validated against its descriptor's outputs.
    ERHE_VERIFY(
        (source == nullptr) ||
        (source->is_sampler_source() ? (output_index == 0) : (output_index < source->get_descriptor().outputs.size()))
    );
    m_inputs[input_index].source       = source;
    m_inputs[input_index].output_index = output_index;
}

void Compose_node::clear_input(const std::size_t input_index)
{
    ERHE_VERIFY(input_index < m_inputs.size());
    m_inputs[input_index].source       = nullptr;
    m_inputs[input_index].output_index = 0;
}

auto Compose_node::get_input(const std::size_t input_index) const -> const Input_binding&
{
    ERHE_VERIFY(input_index < m_inputs.size());
    return m_inputs[input_index];
}

auto Compose_node::get_input_index(const std::string_view name) const -> std::size_t
{
    const std::vector<Input_descriptor>& inputs = m_descriptor->inputs;
    for (std::size_t i = 0, end = inputs.size(); i < end; ++i) {
        if (inputs[i].name == name) {
            return i;
        }
    }
    ERHE_FATAL("Compose_node: input '%s' not found", std::string{name}.c_str());
}

auto Compose_node::get_parameter_index(const std::string_view name) const -> std::size_t
{
    const std::vector<Parameter_descriptor>& parameters = m_descriptor->parameters;
    for (std::size_t i = 0, end = parameters.size(); i < end; ++i) {
        if (parameters[i].name == name) {
            return i;
        }
    }
    ERHE_FATAL("Compose_node: parameter '%s' not found", std::string{name}.c_str());
}

auto Compose_node::get_checked_parameter(const std::string_view name, const Parameter_kind kind) -> Parameter_value&
{
    const std::size_t index = get_parameter_index(name);
    ERHE_VERIFY(m_descriptor->parameters[index].kind == kind);
    return m_parameters[index];
}

void Compose_node::set_float(const std::string_view name, const float value)
{
    get_checked_parameter(name, Parameter_kind::float_parameter).float_value = value;
}

void Compose_node::set_color(const std::string_view name, const std::array<float, 4>& value)
{
    get_checked_parameter(name, Parameter_kind::color_parameter).color_value = value;
}

void Compose_node::set_enum_index(const std::string_view name, const std::size_t index)
{
    const std::size_t parameter_index = get_parameter_index(name);
    const Parameter_descriptor& parameter = m_descriptor->parameters[parameter_index];
    ERHE_VERIFY(parameter.kind == Parameter_kind::enum_parameter);
    // Material Maker exposes an enum as a discrete UI control whose choices are
    // the available values; an out-of-range index cannot be selected. A
    // programmatic caller (MCP set_parameter, a hand-edited graph file) can
    // still supply one, so clamp to the last valid value rather than asserting -
    // mirroring set_size_exponent() - so untrusted input degrades harmlessly
    // instead of aborting the editor. (A value-less enum may only address 0.)
    const std::size_t clamped =
        parameter.enum_values.empty()               ? std::size_t{0} :
        (index >= parameter.enum_values.size())     ? (parameter.enum_values.size() - 1) :
        index;
    m_parameters[parameter_index].enum_index = clamped;
}

void Compose_node::set_bool(const std::string_view name, const bool value)
{
    get_checked_parameter(name, Parameter_kind::bool_parameter).bool_value = value;
}

void Compose_node::set_size_exponent(const std::string_view name, const int exponent)
{
    const std::size_t parameter_index = get_parameter_index(name);
    const Parameter_descriptor& parameter = m_descriptor->parameters[parameter_index];
    ERHE_VERIFY(parameter.kind == Parameter_kind::size_parameter);
    // Material Maker exposes size as a discrete UI control whose choices are
    // bounded by [min, max]; out-of-range values cannot be selected. Mirror
    // that by clamping to the descriptor's exponent range rather than
    // asserting, so a programmatic caller cannot request an unrepresentable
    // resolution.
    const int clamped =
        (exponent < parameter.min_size_exponent) ? parameter.min_size_exponent :
        (exponent > parameter.max_size_exponent) ? parameter.max_size_exponent :
        exponent;
    m_parameters[parameter_index].size_exponent = clamped;
}

void Compose_node::set_gradient(const std::string_view name, const std::vector<Gradient_stop>& stops, const Gradient_interpolation interpolation)
{
    Parameter_value& value = get_checked_parameter(name, Parameter_kind::gradient_parameter);
    value.gradient_interpolation = interpolation;
    if (stops.empty()) {
        // A gradient needs at least one stop; degrade to opaque black rather
        // than emitting a stop-less function.
        value.gradient_stops = { Gradient_stop{} };
        return;
    }
    std::vector<Gradient_stop> sorted = stops;
    std::sort(
        sorted.begin(), sorted.end(),
        [](const Gradient_stop& a, const Gradient_stop& b) { return a.position < b.position; }
    );
    // Nudge equal-or-descending positions strictly increasing (Material Maker's
    // MMGradient.sort) so the emitted ladder's (pos[i+1]-pos[i]) is never zero.
    for (std::size_t i = 0; (i + 1) < sorted.size(); ++i) {
        if ((sorted[i].position + 0.0000005f) >= sorted[i + 1].position) {
            sorted[i + 1].position = sorted[i].position + 0.000001f;
        }
    }
    value.gradient_stops = std::move(sorted);
}

void Compose_node::set_curve(const std::string_view name, const std::vector<Curve_point>& points)
{
    Parameter_value& value = get_checked_parameter(name, Parameter_kind::curve_parameter);
    std::vector<Curve_point> sorted = points;
    std::sort(
        sorted.begin(), sorted.end(),
        [](const Curve_point& a, const Curve_point& b) { return a.x < b.x; }
    );
    // Nudge equal-or-descending x strictly increasing so the emitted Hermite
    // segments never divide by a zero span.
    for (std::size_t i = 0; (i + 1) < sorted.size(); ++i) {
        if ((sorted[i].x + 0.0000005f) >= sorted[i + 1].x) {
            sorted[i + 1].x = sorted[i].x + 0.000001f;
        }
    }
    value.curve_points = std::move(sorted);
}

auto Compose_node::get_parameter_value(const std::size_t parameter_index) const -> const Parameter_value&
{
    ERHE_VERIFY(parameter_index < m_parameters.size());
    return m_parameters[parameter_index];
}

void Compose_node::set_seed(const float seed)
{
    m_seed = seed;
}

auto Compose_node::get_seed() const -> float
{
    return m_seed;
}

} // namespace erhe::texgen
