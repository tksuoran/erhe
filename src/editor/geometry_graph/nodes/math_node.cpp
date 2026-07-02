#include "geometry_graph/nodes/math_node.hpp"

#include <imgui/imgui.h>

#include <cmath>

namespace editor {

Math_node::Math_node()
    : Geometry_graph_node{"Math"}
{
    make_input_pin(Geometry_pin_key::float_value, "A");
    make_input_pin(Geometry_pin_key::float_value, "B");
    make_output_pin(Geometry_pin_key::float_value, "out");
}

void Math_node::evaluate(Geometry_graph&)
{
    pull_inputs();
    const float a = get_input(0).get_float(m_a);
    const float b = get_input(1).get_float(m_b);

    float result = 0.0f;
    switch (m_operation) {
        case Math_operation::add:         result = a + b; break;
        case Math_operation::subtract:    result = a - b; break;
        case Math_operation::multiply:    result = a * b; break;
        case Math_operation::divide:      result = (b != 0.0f) ? (a / b) : 0.0f; break;
        case Math_operation::power:       result = std::pow(a, b); break;
        case Math_operation::minimum:     result = std::min(a, b); break;
        case Math_operation::maximum:     result = std::max(a, b); break;
        case Math_operation::absolute:    result = std::abs(a); break;
        case Math_operation::square_root: result = (a >= 0.0f) ? std::sqrt(a) : 0.0f; break;
        case Math_operation::sine:        result = std::sin(a); break;
        case Math_operation::cosine:      result = std::cos(a); break;
    }
    set_output(0, Geometry_payload{.value = result});
}

void Math_node::imgui()
{
    const char* operation_names[] = { "Add", "Subtract", "Multiply", "Divide", "Power", "Min", "Max", "Abs", "Sqrt", "Sin", "Cos" };
    int operation = static_cast<int>(m_operation);
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::Combo("##operation", &operation, operation_names, IM_ARRAYSIZE(operation_names))) {
        m_operation = static_cast<Math_operation>(operation);
        mark_dirty();
    }
    ImGui::TextUnformatted("A");
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::DragFloat("##a", &m_a, 0.01f)) { mark_dirty(); }
    ImGui::TextUnformatted("B");
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::DragFloat("##b", &m_b, 0.01f)) { mark_dirty(); }
    ImGui::Text("= %f", static_cast<double>(get_output(0).get_float()));
}

}
