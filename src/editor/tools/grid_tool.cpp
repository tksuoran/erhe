#include "tools/grid_tool.hpp"
#include "renderers/line_renderer.hpp"
#include "erhe_tracy.hpp"

#include "imgui.h"

namespace editor
{

auto Grid_tool::state() const -> Tool::State
{
    return State::passive;
}

void Grid_tool::render(Render_context& render_context)
{
    ZoneScoped;

    if (render_context.line_renderer == nullptr)
    {
        return;
    }

    if (!m_enable)
    {
        return;
    }

    uint32_t cell_major_color = 0xff000000u;
    uint32_t cell_minor_color = 0xff333333u;

    float extent     = static_cast<float>(m_cell_count) * m_cell_size;
    float minor_step = m_cell_size / static_cast<float>(m_cell_div);
    int cell;
    Line_renderer& line_renderer = *render_context.line_renderer;
    for (cell = -m_cell_count; cell < m_cell_count; ++cell)
    {
        float xz = static_cast<float>(cell) * m_cell_size;
        line_renderer.set_line_color(cell_major_color);
        line_renderer.add_lines( { {glm::vec3(     xz, 0.0f, -extent), glm::vec3(    xz, 0.0f,  extent)},
                                   {glm::vec3(-extent, 0.0f,      xz), glm::vec3(extent, 0.0f,      xz)}  } );
        line_renderer.set_line_color(cell_minor_color);
        for (int i = 0; i < (m_cell_div - 1); ++i)
        {
            xz += minor_step;
            line_renderer.add_lines( { {glm::vec3(     xz, 0.0f, -extent), glm::vec3(    xz, 0.0f,  extent)},
                                       {glm::vec3(-extent, 0.0f,      xz), glm::vec3(extent, 0.0f,      xz)}  } );
        }
    }
    line_renderer.set_line_color(cell_major_color);
    float xz = static_cast<float>(cell) * m_cell_size;
    line_renderer.add_lines( { {glm::vec3(     xz, 0.0f, -extent), glm::vec3(    xz, 0.0f,  extent)},
                               {glm::vec3(-extent, 0.0f,      xz), glm::vec3(extent, 0.0f,      xz)}  } );
}

void Grid_tool::window(Pointer_context&)
{
    ZoneScoped;

    ImGui::Begin      ("Grid");
    ImGui::Checkbox   ("Enable",     &m_enable);
    ImGui::SliderFloat("Cell Size",  &m_cell_size,  0.0f, 10.0f);
    ImGui::SliderInt  ("Cell Div",   &m_cell_div,   0, 10);
    ImGui::SliderInt  ("Cell Count", &m_cell_count, 1, 100);
    ImGui::End        ();
}

auto Grid_tool::snap(glm::vec3 v) const -> glm::vec3
{
    v.x = std::floor((v.x + m_cell_size * 0.5f) / m_cell_size) * m_cell_size;
    v.y = std::floor((v.y + m_cell_size * 0.5f) / m_cell_size) * m_cell_size;
    v.z = std::floor((v.z + m_cell_size * 0.5f) / m_cell_size) * m_cell_size;
    return v;
}

} // namespace editor
