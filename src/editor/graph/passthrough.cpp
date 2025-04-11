#include "graph/passthrough.hpp"
#include <imgui/imgui.h>

namespace editor {

Passthrough::Passthrough()
    : Shader_graph_node{"Passthrough"}
{
    make_input_pin(pin_key_todo, "in");
    make_output_pin(pin_key_todo, "out");
}

void Passthrough::evaluate(Shader_graph&)
{
    const Payload& in = accumulate_input_from_links(0);
    set_output(0, in);
}

void Passthrough::imgui()
{
    const Payload in  = accumulate_input_from_links(0);
    const Payload out = get_output(0);
    ImGui::Text("%d", in.int_value[0]); // TODO Handle format
}

} // namespace editor
