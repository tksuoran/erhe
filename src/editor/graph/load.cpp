#include "graph/load.hpp"
#include "graph/shader_graph.hpp"

#include "erhe_graph/pin.hpp"
#include "erhe_verify/verify.hpp"

#include <imgui/imgui.h>

namespace editor {

Load::Load()
    : Shader_graph_node{"Load"}
    , m_payload{
        .format      = erhe::dataformat::Format::format_32_scalar_sint,
        .int_value   = { 0, 0, 0, 0 },
        .float_value = { 0.0f, 0.0f, 0.0f, 0.0f }
    }
{
    make_output_pin(pin_key_todo, "out");
}

void Load::evaluate(Shader_graph& graph)
{
    ERHE_VERIFY(get_output_pins().size() == 1);
    double double_value = graph.load(m_row, m_col);
    m_payload = Payload{
        .format      = erhe::dataformat::Format::format_32_scalar_sint,
        .int_value   = { static_cast<int>(double_value), static_cast<int>(double_value), static_cast<int>(double_value), static_cast<int>(double_value) },
        .float_value = { static_cast<float>(double_value), static_cast<float>(double_value), static_cast<float>(double_value), static_cast<float>(double_value) }
    };
    set_output(0, m_payload);
}

void Load::imgui()
{
    ImGui::Text("%d", m_payload.int_value[0]); // TODO Handle format
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputInt("Row##", &m_row);
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputInt("Col##", &m_col);
}

} // namespace editor
