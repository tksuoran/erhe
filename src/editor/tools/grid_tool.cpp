#include "tools/grid_tool.hpp"
#include "tools/tools.hpp"
#include "editor_rendering.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui_windows.hpp"
#include "erhe/application/renderers/line_renderer.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

using glm::vec3;

Grid_tool::Grid_tool()
    : erhe::components::Component    {c_label}
    , erhe::application::Imgui_window{c_title, c_label}
{
}

Grid_tool::~Grid_tool() noexcept
{
}

void Grid_tool::declare_required_components()
{
    require<Tools>();
    require<erhe::application::Configuration>();
    require<erhe::application::Imgui_windows>();
}

void Grid_tool::initialize_component()
{
    get<Tools                           >()->register_background_tool(this);
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);

    const auto& config = get<erhe::application::Configuration>()->grid;
    m_enable     = config.enabled;
    m_cell_size  = config.cell_size;
    m_cell_div   = config.cell_div;
    m_cell_count = config.cell_count;
}

void Grid_tool::post_initialize()
{
    m_line_renderer_set = get<erhe::application::Line_renderer_set>();
}

auto Grid_tool::description() -> const char*
{
   return c_title.data();
}

void Grid_tool::tool_render(
    const Render_context& /*context*/
)
{
    ERHE_PROFILE_FUNCTION

    if (m_line_renderer_set == nullptr)
    {
        return;
    }

    if (!m_enable)
    {
        return;
    }

    const uint32_t  cell_major_color = erhe::toolkit::convert_float4_to_uint32(m_major_color);
    const uint32_t  cell_minor_color = erhe::toolkit::convert_float4_to_uint32(m_minor_color);
    const glm::mat4 m = erhe::toolkit::create_translation<float>(m_center);

    const float extent     = static_cast<float>(m_cell_count) * m_cell_size;
    const float minor_step = m_cell_size / static_cast<float>(m_cell_div);
    int cell;
    auto& line_renderer = m_line_renderer_set->visible;
    for (cell = -m_cell_count; cell < m_cell_count; ++cell)
    {
        float xz = static_cast<float>(cell) * m_cell_size;
        line_renderer.set_line_color(cell_major_color);
        line_renderer.add_lines(
            m,
            {
                {
                    vec3{     xz, 0.0f, -extent},
                    vec3{     xz, 0.0f,  extent}
                },
                {
                    vec3{-extent, 0.0f,      xz},
                    vec3{ extent, 0.0f,      xz}
                }
            },
            m_thickness
        );
        line_renderer.set_line_color(cell_minor_color);
        for (int i = 0; i < (m_cell_div - 1); ++i)
        {
            xz += minor_step;
            line_renderer.add_lines(
                m,
                {
                    {
                        vec3{     xz, 0.0f, -extent},
                        vec3{     xz, 0.0f,  extent}
                    },
                    {
                        vec3{-extent, 0.0f,      xz},
                        vec3{ extent, 0.0f,      xz}
                    }
                },
                m_thickness
            );
        }
    }
    line_renderer.set_line_color(cell_major_color);
    float xz = static_cast<float>(cell) * m_cell_size;
    line_renderer.add_lines(
        m,
        {
            {
                vec3{    xz, 0.0f, -extent},
                vec3{    xz, 0.0f,  extent}
            },
            {
                vec3{-extent, 0.0f,     xz},
                vec3{ extent, 0.0f,     xz}
            }
        },
        m_thickness
    );
}

void Grid_tool::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION

    ImGui::Checkbox   ("Enable",      &m_enable);
    ImGui::SliderFloat("Cell Size",   &m_cell_size,  0.0f,    10.0f);
    ImGui::SliderInt  ("Cell Div",    &m_cell_div,   0,       10);
    ImGui::SliderInt  ("Cell Count",  &m_cell_count, 1,       100);
    ImGui::SliderFloat("Thickness",   &m_thickness,  -100.0f, 100.0f);
    ImGui::DragFloat3 ("Center",      &m_center.x);
    ImGui::ColorEdit4 ("Major Color", &m_major_color.x, ImGuiColorEditFlags_Float);
    ImGui::ColorEdit4 ("Minor Color", &m_minor_color.x, ImGuiColorEditFlags_Float);
#endif
}

auto Grid_tool::snap(const vec3 v) const -> vec3
{
    return vec3{
        std::floor((v.x + m_cell_size * 0.5f) / m_cell_size) * m_cell_size,
        std::floor((v.y + m_cell_size * 0.5f) / m_cell_size) * m_cell_size,
        std::floor((v.z + m_cell_size * 0.5f) / m_cell_size) * m_cell_size
    };
}

void Grid_tool::set_major_color(const glm::vec4 color)
{
    m_major_color = color;
}
void Grid_tool::set_minor_color(const glm::vec4 color)
{
    m_minor_color = color;
}

} // namespace editor
