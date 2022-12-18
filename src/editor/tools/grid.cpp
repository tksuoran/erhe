#include "tools/grid.hpp"
#include "editor_rendering.hpp"
#include "renderers/render_context.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui/imgui_helpers.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/renderers/line_renderer.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#   include <imgui/misc/cpp/imgui_stdlib.h>
#endif

#include <fmt/format.h>

namespace editor
{

using glm::vec3;

Grid::Grid()
{
}

[[nodiscard]] auto Grid::get_type () const -> uint64_t
{
    return
        erhe::scene::Item_type::node_attachment |
        erhe::scene::Item_type::grid;
}

[[nodiscard]] auto Grid::type_name() const -> const char*
{
    return "Grid";
}

auto Grid::get_name() const -> const std::string&
{
    return m_name;
}

auto Grid::snap_world_position(const glm::dvec3& position_in_world) const -> glm::dvec3
{
    const double snap_size = static_cast<double>(m_cell_size) / static_cast<double>(m_cell_div);
    const glm::dvec3 position_in_grid = glm::dvec3{grid_from_world() * glm::dvec4{position_in_world, 1.0}};
    const glm::dvec3 snapped_position_in_grid{
        std::floor((position_in_grid.x + snap_size * 0.5) / snap_size) * snap_size,
        std::floor((position_in_grid.y + snap_size * 0.5) / snap_size) * snap_size,
        std::floor((position_in_grid.z + snap_size * 0.5) / snap_size) * snap_size
    };

    return glm::dvec3{
        world_from_grid() * glm::dvec4{snapped_position_in_grid, 1.0}
    };
}

auto Grid::snap_grid_position(const glm::dvec3& position_in_grid) const -> glm::dvec3
{
    const double snap_size = static_cast<double>(m_cell_size) / static_cast<double>(m_cell_div);
    const glm::dvec3 snapped_position_in_grid{
        std::floor((position_in_grid.x + snap_size * 0.5) / snap_size) * snap_size,
        std::floor((position_in_grid.y + snap_size * 0.5) / snap_size) * snap_size,
        std::floor((position_in_grid.z + snap_size * 0.5) / snap_size) * snap_size
    };

    return snapped_position_in_grid;
}

auto Grid::world_from_grid() const -> glm::dmat4
{
    if (m_plane_type == Grid_plane_type::Node)
    {
        const erhe::scene::Node* node = get_node();
        if (node != nullptr)
        {
            return glm::dmat4{node->world_from_node()};
        }
    }
    return m_world_from_grid;
}

auto Grid::grid_from_world() const -> glm::dmat4
{
    if (m_plane_type == Grid_plane_type::Node)
    {
        const erhe::scene::Node* node = get_node();
        if (node != nullptr)
        {
            return glm::dmat4{node->node_from_world()};
        }
    }
    return m_grid_from_world;
}

void Grid::render(
    const std::shared_ptr<erhe::application::Line_renderer_set>& line_renderer_set,
    const Render_context&                                        context
)
{
    if (!m_enable)
    {
        return;
    }

    const erhe::scene::Node* camera_node = context.camera->get_node();
    ERHE_VERIFY(camera_node != nullptr);
    const glm::vec3 camera_position = camera_node->position_in_world();
    const glm::mat4 m = world_from_grid();

    const float extent     = static_cast<float>(m_cell_count) * m_cell_size;
    const float minor_step = m_cell_size / static_cast<float>(m_cell_div);
    int cell;
    auto& major_renderer = m_see_hidden_major
        ? *line_renderer_set->visible.at(1).get()
        : *line_renderer_set->hidden .at(1).get();
    auto& minor_renderer = m_see_hidden_minor
        ? *line_renderer_set->visible.at(0).get()
        : *line_renderer_set->hidden .at(0).get();
    major_renderer.set_thickness(m_major_width);
    minor_renderer.set_thickness(m_minor_width);
    major_renderer.set_line_color(m_major_color);
    minor_renderer.set_line_color(m_minor_color);

    for (cell = -m_cell_count; cell < m_cell_count; ++cell)
    {
        float xz = static_cast<float>(cell) * m_cell_size;

        major_renderer.add_lines(
            m,
            {
                {
                    vec3{     xz, 0.0f,  -extent},
                    vec3{     xz, 0.0f,   extent}
                },
                {
                    vec3{-extent, 0.0f,      xz},
                    vec3{ extent, 0.0f,      xz}
                }
            }
        );
        for (int i = 0; i < (m_cell_div - 1); ++i)
        {
            xz += minor_step;
            minor_renderer.add_lines(
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
                }
            );
        }
    }
    float xz = static_cast<float>(cell) * m_cell_size;
    major_renderer.add_lines(
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
        }
    );
}

void Grid::imgui(
    const std::shared_ptr<Selection_tool>& selection_tool
)
{
    ImGui::InputText  ("Name",             &m_name);
    ImGui::Separator();
    ImGui::Checkbox   ("Enable",           &m_enable);
    ImGui::Checkbox   ("See Major Hidden", &m_see_hidden_major);
    ImGui::Checkbox   ("See Minor Hidden", &m_see_hidden_minor);
    ImGui::SliderFloat("Cell Size",        &m_cell_size,       0.0f, 10.0f);
    ImGui::SliderInt  ("Cell Div",         &m_cell_div,        0,    10);
    ImGui::SliderInt  ("Cell Count",       &m_cell_count,      1,    100);
    ImGui::SliderFloat("Major Width",      &m_major_width,  -100.0f, 100.0f);
    ImGui::SliderFloat("Minor Width",      &m_minor_width,  -100.0f, 100.0f);
    ImGui::ColorEdit4 ("Major Color",      &m_major_color.x, ImGuiColorEditFlags_Float);
    ImGui::ColorEdit4 ("Minor Color",      &m_minor_color.x, ImGuiColorEditFlags_Float);

    erhe::application::make_combo(
        "Plane",
        m_plane_type,
        grid_plane_type_strings,
        IM_ARRAYSIZE(grid_plane_type_strings)
    );
    if (m_plane_type != Grid_plane_type::Node)
    {
        ImGui::DragScalarN(
            "Offset",
            ImGuiDataType_Double,
            &m_center.x,
            3,
            0.01f
        );
        const double min_rotation = -180.0;
        const double max_rotation =  180.0;
        ImGui::DragScalarN(
            "Rotation",
            ImGuiDataType_Double,
            &m_rotation,
            1,
            0.05f,
            &min_rotation,
            &max_rotation
        );

        const double radians = glm::radians(m_rotation);
        const glm::dmat4 orientation  = get_plane_transform(m_plane_type);
        const glm::dvec3 plane_normal = glm::dvec3{0.0, 1.0, 0.0};
        const glm::dmat4 offset       = erhe::toolkit::create_translation<double>(m_center);
        const glm::dmat4 rotation     = erhe::toolkit::create_rotation<double>( radians, plane_normal);
        m_world_from_grid = orientation * rotation * offset;
        m_grid_from_world = glm::inverse(m_world_from_grid); // orientation * inverse_rotation * inverse_offset;
    }
    else
    {
        {
            erhe::scene::Node* host_node = get_node();
            if (host_node != nullptr)
            {
                const std::string label        = fmt::format("Node: {}", host_node->get_name());
                const std::string detach_label = fmt::format("Detach from {}", host_node->get_name());
                ImGui::TextUnformatted(label.c_str());
                if (ImGui::Button(detach_label.c_str()))
                {
                    host_node->detach(this);
                }
            }
        }
        const auto& host_node = selection_tool->get_first_selected_node();
        if (host_node)
        {
            const std::string label = fmt::format("Attach to {}", host_node->get_name());
            if (ImGui::Button(label.c_str()))
            {
                host_node->attach(
                    std::static_pointer_cast<Grid>(shared_from_this())
                );
            }
        }
    }
}

auto Grid::intersect_ray(
    const glm::dvec3& ray_origin_in_world,
    const glm::dvec3& ray_direction_in_world
) -> std::optional<glm::dvec3>
{
    if (!m_enable)
    {
        return {};
    }
    const glm::dvec3 ray_origin_in_grid    = glm::dvec3{grid_from_world() * glm::dvec4{ray_origin_in_world, 1.0}};
    const glm::dvec3 ray_direction_in_grid = glm::dvec3{grid_from_world() * glm::dvec4{ray_direction_in_world, 0.0}};
    const auto intersection = erhe::toolkit::intersect_plane<double>(
        glm::dvec3{0.0, 1.0, 0.0},
        glm::dvec3{0.0, 0.0, 0.0},
        ray_origin_in_grid,
        ray_direction_in_grid
    );
    if (!intersection.has_value())
    {
        return {};
    }
    const glm::dvec3 position_in_grid = ray_origin_in_grid + intersection.value() * ray_direction_in_grid;

    if (
        (position_in_grid.x < -static_cast<double>(m_cell_size) * static_cast<double>(m_cell_count)) ||
        (position_in_grid.x >  static_cast<double>(m_cell_size) * static_cast<double>(m_cell_count)) ||
        (position_in_grid.z < -static_cast<double>(m_cell_size) * static_cast<double>(m_cell_count)) ||
        (position_in_grid.z >  static_cast<double>(m_cell_size) * static_cast<double>(m_cell_count))
    )
    {
        return {};
    }

    return glm::dvec3{world_from_grid() * glm::dvec4{position_in_grid, 1.0}};
}


} // namespace editor
