#include "graph/store.hpp"
#include "graph/shader_graph.hpp"

#include "erhe_graph/pin.hpp"

#include <imgui/imgui.h>

namespace editor {

Store::Store()
    : Shader_graph_node{"Store"}
    , m_payload{
        .format      = erhe::dataformat::Format::format_32_scalar_sint,
        .int_value   = { 0, 0, 0, 0 },
        .float_value = { 0.0f, 0.0f, 0.0f, 0.0f }
    }
{
    make_input_pin(pin_key_todo, "A");
}

void Store::evaluate(Shader_graph& graph)
{
    ERHE_VERIFY(get_input_pins().size() == 1);
    m_payload = accumulate_input_from_links(0); // TODO Handle format
    graph.store(m_row, m_col, static_cast<double>(m_payload.int_value[0]));
}

void Store::imgui()
{
    ImGui::Text("%d", m_payload.int_value[0]); // TODO Handle format
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputInt("Row##", &m_row);
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputInt("Col##", &m_col);
}

} // namespace editor
