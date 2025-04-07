#include "graph/multiply.hpp"

#include <imgui/imgui.h>

namespace editor {

Multiply::Multiply()
    : Shader_graph_node{"Mul"}
{
    make_input_pin(pin_key_todo, "A");
    make_input_pin(pin_key_todo, "B");
    make_output_pin(pin_key_todo, "out");
}

void Multiply::evaluate(Shader_graph&)
{
    const Payload lhs    = accumulate_input_from_links(0);
    const Payload rhs    = accumulate_input_from_links(1);
    const Payload result = lhs * rhs;
    set_output(0, result);
}

void Multiply::imgui()
{
    const Payload lhs = accumulate_input_from_links(0);
    const Payload rhs = accumulate_input_from_links(1);
    const Payload out = get_output(0);
    ImGui::Text("%d * %d = %d", lhs.int_value[0], rhs.int_value[0], out.int_value[0]); // TODO Handle format
}

} // namespace editor
