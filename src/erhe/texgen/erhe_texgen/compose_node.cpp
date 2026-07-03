#include "erhe_texgen/compose_node.hpp"

#include "erhe_verify/verify.hpp"

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
        value.float_value   = parameter.default_float;
        value.color_value   = parameter.default_color;
        value.enum_index    = parameter.default_enum_index;
        value.bool_value    = parameter.default_bool;
        value.size_exponent = parameter.default_size_exponent;
        m_parameters.push_back(value);
    }
    m_inputs.resize(descriptor.inputs.size());
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
    ERHE_VERIFY((source == nullptr) || (output_index < source->get_descriptor().outputs.size()));
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
