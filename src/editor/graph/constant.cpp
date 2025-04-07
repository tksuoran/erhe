#include "graph/constant.hpp"
#include "erhe_graph/pin.hpp"

#include <imgui/imgui.h>

namespace editor {

Constant::Constant()
    : Shader_graph_node{"Constant"}
    , m_payload{
        .format      = erhe::dataformat::Format::format_32_scalar_sint,
        .int_value   = { 0, 0, 0, 0 },
        .float_value = { 0.0f, 0.0f, 0.0f, 0.0f }
    }
{
    make_output_pin(pin_key_todo, "out");
}

void Constant::evaluate(Shader_graph&)
{
    ERHE_VERIFY(get_output_pins().size() == 1);
    set_output(0, m_payload);
}

void Constant::imgui()
{
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputInt("##", &m_payload.int_value[0]);
}

} // namespace editor
