#include "texture_graph/nodes/texture_descriptor_node.hpp"
#include "texture_graph/texture_payload.hpp"

#include "erhe_texgen/node_descriptor.hpp"

#include <imgui/imgui.h>
#include <nlohmann/json.hpp>

#include <vector>

namespace editor {

Texture_descriptor_node::Texture_descriptor_node(const erhe::texgen::Node_descriptor& descriptor)
    : Texture_graph_node{descriptor.label.c_str()}
    , m_descriptor      {descriptor}
{
    build_pins_from_descriptor(descriptor);

    // Initialize the live parameter values from the descriptor defaults. The
    // field of Parameter_value that matters is the one matching the parameter
    // kind; the others stay at their harmless zero defaults.
    m_parameter_values.resize(descriptor.parameters.size());
    for (std::size_t i = 0, end = descriptor.parameters.size(); i < end; ++i) {
        const erhe::texgen::Parameter_descriptor& parameter = descriptor.parameters[i];
        erhe::texgen::Parameter_value&            value     = m_parameter_values[i];
        switch (parameter.kind) {
            case erhe::texgen::Parameter_kind::float_parameter: value.float_value   = parameter.default_float;         break;
            case erhe::texgen::Parameter_kind::color_parameter: value.color_value   = parameter.default_color;         break;
            case erhe::texgen::Parameter_kind::enum_parameter:  value.enum_index    = parameter.default_enum_index;    break;
            case erhe::texgen::Parameter_kind::bool_parameter:  value.bool_value    = parameter.default_bool;          break;
            case erhe::texgen::Parameter_kind::size_parameter:  value.size_exponent = parameter.default_size_exponent; break;
            default: break;
        }
    }
}

void Texture_descriptor_node::evaluate(Texture_graph&)
{
    // Step 2: composition happens at sinks in Step 3; here evaluate() only
    // records how this node's outputs are typed and that this live node is
    // their producer, so downstream nodes and (Step 3) sinks can walk upstream
    // via the payload references with correct types.
    pull_inputs();
    for (std::size_t i = 0, end = m_descriptor.outputs.size(); i < end; ++i) {
        Texture_payload payload{};
        payload.source_node  = this;
        payload.output_index = i;
        payload.value_type   = m_descriptor.outputs[i].type;
        set_output(i, payload);
    }
}

void Texture_descriptor_node::imgui()
{
    for (std::size_t i = 0, end = m_descriptor.parameters.size(); i < end; ++i) {
        const erhe::texgen::Parameter_descriptor& parameter = m_descriptor.parameters[i];
        erhe::texgen::Parameter_value&            value     = m_parameter_values[i];
        ImGui::PushID(static_cast<int>(i));
        const char* label = parameter.label.empty() ? parameter.name.c_str() : parameter.label.c_str();
        switch (parameter.kind) {
            case erhe::texgen::Parameter_kind::float_parameter: {
                ImGui::TextUnformatted(label);
                ImGui::SetNextItemWidth(140.0f);
                if (ImGui::DragFloat("##float", &value.float_value, parameter.step, parameter.min_value, parameter.max_value)) {
                    mark_dirty();
                }
                break;
            }
            case erhe::texgen::Parameter_kind::color_parameter: {
                ImGui::TextUnformatted(label);
                // ColorButton is a display-only swatch (it does not open a
                // picker popup, which is forbidden inside the ax::NodeEditor
                // canvas); DragFloat4 does the editing.
                const ImVec4 preview{value.color_value[0], value.color_value[1], value.color_value[2], value.color_value[3]};
                ImGui::ColorButton("##swatch", preview, ImGuiColorEditFlags_NoTooltip);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(140.0f);
                if (ImGui::DragFloat4("##color", value.color_value.data(), 0.005f, 0.0f, 1.0f)) {
                    mark_dirty();
                }
                break;
            }
            case erhe::texgen::Parameter_kind::enum_parameter: {
                std::vector<const char*> names;
                names.reserve(parameter.enum_values.size());
                for (const erhe::texgen::Enum_value& enum_value : parameter.enum_values) {
                    names.push_back(enum_value.label.c_str());
                }
                int index = static_cast<int>(value.enum_index);
                if (texture_enum_stepper(label, index, names.data(), static_cast<int>(names.size()))) {
                    value.enum_index = static_cast<std::size_t>(index);
                    mark_dirty();
                }
                break;
            }
            case erhe::texgen::Parameter_kind::bool_parameter: {
                if (ImGui::Checkbox(label, &value.bool_value)) {
                    mark_dirty();
                }
                break;
            }
            case erhe::texgen::Parameter_kind::size_parameter: {
                ImGui::TextUnformatted(label);
                ImGui::SetNextItemWidth(140.0f);
                if (ImGui::DragInt("##size", &value.size_exponent, 0.1f, parameter.min_size_exponent, parameter.max_size_exponent)) {
                    mark_dirty();
                }
                ImGui::SameLine();
                ImGui::Text("= %d", 1 << value.size_exponent);
                break;
            }
            default: break;
        }
        ImGui::PopID();
    }

    if (m_descriptor.uses_seed()) {
        ImGui::TextUnformatted("Seed");
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::DragFloat("##seed", &m_seed, 1.0f)) {
            mark_dirty();
        }
    }
}

auto Texture_descriptor_node::descriptor() const -> const erhe::texgen::Node_descriptor*
{
    return &m_descriptor;
}

void Texture_descriptor_node::configure(erhe::texgen::Compose_node& compose_node) const
{
    for (std::size_t i = 0, end = m_descriptor.parameters.size(); i < end; ++i) {
        const erhe::texgen::Parameter_descriptor& parameter = m_descriptor.parameters[i];
        const erhe::texgen::Parameter_value&      value     = m_parameter_values[i];
        switch (parameter.kind) {
            case erhe::texgen::Parameter_kind::float_parameter: compose_node.set_float        (parameter.name, value.float_value);   break;
            case erhe::texgen::Parameter_kind::color_parameter: compose_node.set_color        (parameter.name, value.color_value);   break;
            case erhe::texgen::Parameter_kind::enum_parameter:  compose_node.set_enum_index   (parameter.name, value.enum_index);    break;
            case erhe::texgen::Parameter_kind::bool_parameter:  compose_node.set_bool         (parameter.name, value.bool_value);    break;
            case erhe::texgen::Parameter_kind::size_parameter:  compose_node.set_size_exponent(parameter.name, value.size_exponent); break;
            default: break;
        }
    }
    if (m_descriptor.uses_seed()) {
        compose_node.set_seed(m_seed);
    }
}

void Texture_descriptor_node::write_parameters(nlohmann::json& out) const
{
    for (std::size_t i = 0, end = m_descriptor.parameters.size(); i < end; ++i) {
        const erhe::texgen::Parameter_descriptor& parameter = m_descriptor.parameters[i];
        const erhe::texgen::Parameter_value&      value     = m_parameter_values[i];
        const std::string&                        name      = parameter.name;
        switch (parameter.kind) {
            case erhe::texgen::Parameter_kind::float_parameter: out[name] = value.float_value; break;
            case erhe::texgen::Parameter_kind::color_parameter: out[name] = { value.color_value[0], value.color_value[1], value.color_value[2], value.color_value[3] }; break;
            case erhe::texgen::Parameter_kind::enum_parameter:  out[name] = static_cast<int>(value.enum_index); break;
            case erhe::texgen::Parameter_kind::bool_parameter:  out[name] = value.bool_value; break;
            case erhe::texgen::Parameter_kind::size_parameter:  out[name] = value.size_exponent; break;
            default: break;
        }
    }
    if (m_descriptor.uses_seed()) {
        out["seed"] = m_seed;
    }
}

void Texture_descriptor_node::read_parameters(const nlohmann::json& in)
{
    for (std::size_t i = 0, end = m_descriptor.parameters.size(); i < end; ++i) {
        const erhe::texgen::Parameter_descriptor& parameter = m_descriptor.parameters[i];
        erhe::texgen::Parameter_value&            value     = m_parameter_values[i];
        const std::string&                        name      = parameter.name;
        if (!in.contains(name)) {
            continue;
        }
        const nlohmann::json& entry = in[name];
        switch (parameter.kind) {
            case erhe::texgen::Parameter_kind::float_parameter: {
                value.float_value = entry.get<float>();
                break;
            }
            case erhe::texgen::Parameter_kind::color_parameter: {
                if (entry.is_array() && (entry.size() == 4)) {
                    for (std::size_t c = 0; c < 4; ++c) {
                        value.color_value[c] = entry[c].get<float>();
                    }
                }
                break;
            }
            case erhe::texgen::Parameter_kind::enum_parameter: {
                value.enum_index = static_cast<std::size_t>(entry.get<int>());
                break;
            }
            case erhe::texgen::Parameter_kind::bool_parameter: {
                value.bool_value = entry.get<bool>();
                break;
            }
            case erhe::texgen::Parameter_kind::size_parameter: {
                value.size_exponent = entry.get<int>();
                break;
            }
            default: break;
        }
    }
    if (m_descriptor.uses_seed() && in.contains("seed")) {
        m_seed = in["seed"].get<float>();
    }
    mark_dirty();
}

} // namespace editor
