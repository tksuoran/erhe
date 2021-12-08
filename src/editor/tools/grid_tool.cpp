#include "tools/grid_tool.hpp"
#include "editor_tools.hpp"
#include "rendering.hpp"

#include "renderers/line_renderer.hpp"

#include "erhe/toolkit/profile.hpp"

#include <imgui.h>

namespace editor
{

Grid_tool::Grid_tool()
    : erhe::components::Component{c_name}
    , Imgui_window               {c_description}
{
}

Grid_tool::~Grid_tool() = default;

void Grid_tool::connect()
{
    m_line_renderer = get<Line_renderer>();
}

void Grid_tool::initialize_component()
{
    get<Editor_tools>()->register_background_tool(this);
}

auto Grid_tool::description() -> const char*
{
   return c_description.data();
}

void Grid_tool::tool_render(const Render_context& /*context*/)
{
    ERHE_PROFILE_FUNCTION

    if (m_line_renderer == nullptr)
    {
        return;
    }

    if (!m_enable)
    {
        return;
    }

    const ImVec4 im_major_color{m_major_color.x, m_major_color.y, m_major_color.z, m_major_color.w};
    const ImVec4 im_minor_color{m_minor_color.x, m_minor_color.y, m_minor_color.z, m_minor_color.w};
    const uint32_t cell_major_color = ImGui::ColorConvertFloat4ToU32(im_major_color);
    const uint32_t cell_minor_color = ImGui::ColorConvertFloat4ToU32(im_minor_color);

    const float extent     = static_cast<float>(m_cell_count) * m_cell_size;
    const float minor_step = m_cell_size / static_cast<float>(m_cell_div);
    int cell;
    auto& line_renderer = m_line_renderer->visible;
    for (cell = -m_cell_count; cell < m_cell_count; ++cell)
    {
        float xz = static_cast<float>(cell) * m_cell_size;
        line_renderer.set_line_color(cell_major_color);
        line_renderer.add_lines(
            {
                {
                    glm::vec3{     xz, 0.0f, -extent},
                    glm::vec3{     xz, 0.0f,  extent}
                },
                {
                    glm::vec3{-extent, 0.0f,      xz},
                    glm::vec3{ extent, 0.0f,      xz}
                }
            },
            m_thickness
        );
        line_renderer.set_line_color(cell_minor_color);
        for (int i = 0; i < (m_cell_div - 1); ++i)
        {
            xz += minor_step;
            line_renderer.add_lines(
                {
                    {
                        glm::vec3{     xz, 0.0f, -extent},
                        glm::vec3{     xz, 0.0f,  extent}
                    },
                    {
                        glm::vec3{-extent, 0.0f,      xz},
                        glm::vec3{ extent, 0.0f,      xz}
                    }
                },
                m_thickness
            );
        }
    }
    line_renderer.set_line_color(cell_major_color);
    float xz = static_cast<float>(cell) * m_cell_size;
    line_renderer.add_lines(
        {
            {
                glm::vec3{    xz, 0.0f, -extent},
                glm::vec3{    xz, 0.0f,  extent}
            },
            {
                glm::vec3{-extent, 0.0f,     xz},
                glm::vec3{ extent, 0.0f,     xz}
            }
        },
        m_thickness
    );
}

void Grid_tool::imgui()
{
    ERHE_PROFILE_FUNCTION

    ImGui::Checkbox   ("Enable",      &m_enable);
    ImGui::SliderFloat("Cell Size",   &m_cell_size,  0.0f,    10.0f);
    ImGui::SliderInt  ("Cell Div",    &m_cell_div,   0,       10);
    ImGui::SliderInt  ("Cell Count",  &m_cell_count, 1,       100);
    ImGui::SliderFloat("Thickness",   &m_thickness,  -100.0f, 100.0f);
    ImGui::ColorEdit4 ("Major Color", &m_major_color.x, ImGuiColorEditFlags_Float);
    ImGui::ColorEdit4 ("Minor Color", &m_minor_color.x, ImGuiColorEditFlags_Float);
}

auto Grid_tool::snap(const glm::vec3 v) const -> glm::vec3
{
    return glm::vec3{
        std::floor((v.x + m_cell_size * 0.5f) / m_cell_size) * m_cell_size,
        std::floor((v.y + m_cell_size * 0.5f) / m_cell_size) * m_cell_size,
        std::floor((v.z + m_cell_size * 0.5f) / m_cell_size) * m_cell_size
    };
}

} // namespace editor
