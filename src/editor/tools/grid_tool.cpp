#include "tools/grid_tool.hpp"
#include "tools/tools.hpp"
#include "editor_rendering.hpp"
#include "renderers/render_context.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui/imgui_helpers.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/renderers/line_renderer.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

using glm::vec3;

Grid_tool::Grid_tool()
    : erhe::application::Imgui_window{c_title}
    , erhe::components::Component    {c_type_name}
{
}

Grid_tool::~Grid_tool() noexcept
{
}

void Grid_tool::declare_required_components()
{
    require<erhe::application::Configuration>();
    require<erhe::application::Imgui_windows>();
    require<Tools>();
}

void Grid_tool::initialize_component()
{
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
    get<Tools                           >()->register_background_tool(this);

    const auto& config = get<erhe::application::Configuration>()->grid;
    m_enable      = config.enabled;
    m_major_color = config.major_color;
    m_minor_color = config.minor_color;
    m_major_width = config.major_width;
    m_minor_width = config.minor_width;
    m_cell_size   = config.cell_size;
    m_cell_div    = config.cell_div;
    m_cell_count  = config.cell_count;
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
    const Render_context& context
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

    if (context.camera == nullptr)
    {
        return;
    }

    const glm::vec3 camera_position = context.camera->position_in_world();

    const uint32_t  cell_major_color = erhe::toolkit::convert_float4_to_uint32(m_major_color);
    const uint32_t  cell_minor_color = erhe::toolkit::convert_float4_to_uint32(m_minor_color);
    const glm::mat4 m = erhe::toolkit::create_translation<float>(m_center);

    const float extent     = static_cast<float>(m_cell_count) * m_cell_size;
    const float minor_step = m_cell_size / static_cast<float>(m_cell_div);
    int cell;
    auto& major_renderer = m_see_hidden_major
        ? *m_line_renderer_set->visible.at(1).get()
        : *m_line_renderer_set->hidden .at(1).get();
    auto& minor_renderer = m_see_hidden_minor
        ? *m_line_renderer_set->visible.at(0).get()
        : *m_line_renderer_set->hidden .at(0).get();
    major_renderer.set_thickness(m_major_width);
    minor_renderer.set_thickness(m_minor_width);
    major_renderer.set_line_color(cell_major_color);
    minor_renderer.set_line_color(cell_minor_color);

    //const float x_split  = (camera_position.x > -extent) && (camera_position.x < extent) ? camera_position.x : 0.0f;
    //const float x_split0 = -extent * 0.2f + x_split * 0.8f;
    const float x_split1 = -extent; //  extent * 0.2f + x_split * 0.8f;
    //const float z_split = (camera_position.z > -extent) && (camera_position.z < extent) ? camera_position.z : 0.0f;
    //const float z_split0 = -extent * 0.2f + z_split * 0.8f;
    const float z_split1 = -extent; // extent * 0.2f + z_split * 0.8f;

    for (cell = -m_cell_count; cell < m_cell_count; ++cell)
    {
        float xz = static_cast<float>(cell) * m_cell_size;

        major_renderer.add_lines(
            m,
            {
                //{
                //    vec3{     xz, 0.0f,  -extent},
                //    vec3{     xz, 0.0f,  z_split0}
                //},
                //{
                //    vec3{     xz, 0.0f,  z_split0},
                //    vec3{     xz, 0.0f,  z_split1}
                //},
                {
                    vec3{     xz, 0.0f,  z_split1},
                    vec3{     xz, 0.0f,    extent}
                },
                //{
                //    vec3{ -extent, 0.0f,      xz},
                //    vec3{x_split0, 0.0f,      xz}
                //},
                //{
                //    vec3{x_split0, 0.0f,      xz},
                //    vec3{x_split1, 0.0f,      xz}
                //},
                {
                    vec3{x_split1, 0.0f,      xz},
                    vec3{  extent, 0.0f,      xz}
                }
            }
        );
        for (int i = 0; i < (m_cell_div - 1); ++i)
        {
            xz += minor_step;
            minor_renderer.add_lines(
                m,
                {
                    //{
                    //    vec3{     xz, 0.0f,  -extent},
                    //    vec3{     xz, 0.0f,  z_split0}
                    //},
                    //{
                    //    vec3{     xz, 0.0f,  z_split0},
                    //    vec3{     xz, 0.0f,  z_split1}
                    //},
                    {
                        vec3{     xz, 0.0f,  z_split1},
                        vec3{     xz, 0.0f,    extent}
                    },
                    //{
                    //    vec3{ -extent, 0.0f,      xz},
                    //    vec3{x_split0, 0.0f,      xz}
                    //},
                    //{
                    //    vec3{x_split0, 0.0f,      xz},
                    //    vec3{x_split1, 0.0f,      xz}
                    //},
                    {
                        vec3{x_split1, 0.0f,      xz},
                        vec3{  extent, 0.0f,      xz}
                    }
                }
            );
        }
    }
    float xz = static_cast<float>(cell) * m_cell_size;
    major_renderer.add_lines(
        m,
        {
            //{
            //    vec3{     xz, 0.0f,  -extent},
            //    vec3{     xz, 0.0f,  z_split0}
            //},
            //{
            //    vec3{     xz, 0.0f,  z_split0},
            //    vec3{     xz, 0.0f,  z_split1}
            //},
            {
                vec3{     xz, 0.0f,  z_split1},
                vec3{     xz, 0.0f,    extent}
            },
            //{
            //    vec3{ -extent, 0.0f,      xz},
            //    vec3{x_split0, 0.0f,      xz}
            //},
            //{
            //    vec3{x_split0, 0.0f,      xz},
            //    vec3{x_split1, 0.0f,      xz}
            //},
            {
                vec3{x_split1, 0.0f,      xz},
                vec3{  extent, 0.0f,      xz}
            }
        }
    );
}


void Grid_tool::viewport_toolbar()
{
    ImGui::SameLine();
    const bool grid_pressed = erhe::application::make_button(
        "G",
        (m_enable)
            ? erhe::application::Item_mode::active
            : erhe::application::Item_mode::normal
    );
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            m_enable
                ? "Toggle grid on -> off"
                : "Toggle grid off -> on"
        );
    };

    if (grid_pressed)
    {
        m_enable = !m_enable;
    }
}

void Grid_tool::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION

    ImGui::Checkbox   ("Enable",           &m_enable);
    ImGui::Checkbox   ("See Major Hidden", &m_see_hidden_major);
    ImGui::Checkbox   ("See Minor Hidden", &m_see_hidden_minor);
    ImGui::SliderFloat("Cell Size",    &m_cell_size,  0.0f,    10.0f);
    ImGui::SliderInt  ("Cell Div",     &m_cell_div,   0,       10);
    ImGui::SliderInt  ("Cell Count",   &m_cell_count, 1,       100);
    ImGui::SliderFloat("Major Width",  &m_major_width,  -100.0f, 100.0f);
    ImGui::SliderFloat("Minor Width",  &m_minor_width,  -100.0f, 100.0f);
    ImGui::DragFloat3 ("Center",       &m_center.x);
    ImGui::ColorEdit4 ("Major Color",  &m_major_color.x, ImGuiColorEditFlags_Float);
    ImGui::ColorEdit4 ("Minor Color",  &m_minor_color.x, ImGuiColorEditFlags_Float);
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
