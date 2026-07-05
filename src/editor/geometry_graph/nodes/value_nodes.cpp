#include "geometry_graph/nodes/value_nodes.hpp"

#include <imgui/imgui.h>
#include <nlohmann/json.hpp>

namespace editor {

Float_value_node::Float_value_node()
    : Geometry_graph_node{"Float"}
{
    make_output_pin(Geometry_pin_key::float_value, "value");
}

void Float_value_node::evaluate(Geometry_graph&)
{
    set_output(0, Geometry_payload{.value = m_value});
}

void Float_value_node::imgui()
{
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragFloat("##value", &m_value, 0.01f)) { mark_dirty(); }
}

void Float_value_node::write_parameters(nlohmann::json& out) const
{
    out["value"] = m_value;
}

void Float_value_node::read_parameters(const nlohmann::json& in)
{
    m_value = in.value("value", m_value);
    mark_dirty();
}

Integer_value_node::Integer_value_node()
    : Geometry_graph_node{"Integer"}
{
    make_output_pin(Geometry_pin_key::int_value, "value");
}

void Integer_value_node::evaluate(Geometry_graph&)
{
    set_output(0, Geometry_payload{.value = m_value});
}

void Integer_value_node::imgui()
{
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragInt("##value", &m_value, 0.1f)) { mark_dirty(); }
}

void Integer_value_node::write_parameters(nlohmann::json& out) const
{
    out["value"] = m_value;
}

void Integer_value_node::read_parameters(const nlohmann::json& in)
{
    m_value = in.value("value", m_value);
    mark_dirty();
}

Vector_value_node::Vector_value_node()
    : Geometry_graph_node{"Vector"}
{
    make_output_pin(Geometry_pin_key::vec3_value, "value");
}

void Vector_value_node::evaluate(Geometry_graph&)
{
    set_output(0, Geometry_payload{.value = m_value});
}

void Vector_value_node::imgui()
{
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragFloat3("##value", &m_value.x, 0.01f)) { mark_dirty(); }
}

void Vector_value_node::write_parameters(nlohmann::json& out) const
{
    write_vec3(out, "value", m_value);
}

void Vector_value_node::read_parameters(const nlohmann::json& in)
{
    m_value = read_vec3(in, "value", m_value);
    mark_dirty();
}

}
