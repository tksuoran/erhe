#include "graph/add.hpp"
#include <imgui/imgui.h>

namespace editor {

Add::Add()
    : Shader_graph_node{"Add"}
{
    make_input_pin(pin_key_todo, "A");
    make_input_pin(pin_key_todo, "B");
    make_output_pin(pin_key_todo, "out");
}

void Add::evaluate(Shader_graph&)
{
    const Payload& lhs    = accumulate_input_from_links(0);
    const Payload& rhs    = accumulate_input_from_links(1);
    const Payload  result = lhs + rhs;
    set_output(0, result);
}

void Add::imgui()
{
    const Payload lhs = accumulate_input_from_links(0);
    const Payload rhs = accumulate_input_from_links(1);
    const Payload out = get_output(0);
    ImGui::Text("%d + %d = %d", lhs.int_value[0], rhs.int_value[0], out.int_value[0]); // TODO Handle format
}

}
