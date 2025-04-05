#include "tools/grid.hpp"

#include "editor_context.hpp"
#include "renderers/render_context.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_renderer/scoped_line_renderer.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_verify/verify.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui/imgui.h>
#   include <imgui/misc/cpp/imgui_stdlib.h>
#endif

#include <fmt/format.h>

namespace editor {

using glm::vec3;

auto Grid::get_static_type() -> uint64_t
{
    return erhe::Item_type::node_attachment | erhe::Item_type::grid;
}

auto Grid::get_type() const -> uint64_t
{
    return get_static_type();
}

auto Grid::get_type_name() const -> std::string_view
{
    return static_type_name;
}

auto Grid::snap_world_position(const glm::vec3& position_in_world) const -> glm::vec3
{
    if (!m_snap_enabled) {
        return position_in_world;
    }
    const float     snap_size        = m_cell_size / m_cell_div;
    const glm::vec3 position_in_grid = glm::vec3{grid_from_world() * glm::vec4{position_in_world, 1.0}};
    const glm::vec3 snapped_position_in_grid{
        std::floor((position_in_grid.x + snap_size * 0.5) / snap_size) * snap_size,
        std::floor((position_in_grid.y + snap_size * 0.5) / snap_size) * snap_size,
        std::floor((position_in_grid.z + snap_size * 0.5) / snap_size) * snap_size
    };

    return glm::vec3{
        world_from_grid() * glm::vec4{snapped_position_in_grid, 1.0}
    };
}

auto Grid::snap_grid_position(const glm::vec3& position_in_grid) const -> glm::vec3
{
    if (!m_snap_enabled) {
        return position_in_grid;
    }

    const float     snap_size        = m_cell_size / m_cell_div;
    const glm::vec3 snapped_position_in_grid{
        std::floor((position_in_grid.x + snap_size * 0.5f) / snap_size) * snap_size,
        std::floor((position_in_grid.y + snap_size * 0.5f) / snap_size) * snap_size,
        std::floor((position_in_grid.z + snap_size * 0.5f) / snap_size) * snap_size
    };

    return snapped_position_in_grid;
}

auto Grid::world_from_grid() const -> glm::mat4
{
    if (m_plane_type == Grid_plane_type::Node) {
        const erhe::scene::Node* node = get_node();
        if (node != nullptr) {
            return node->world_from_node();
        }
    }
    return m_world_from_grid;
}

auto Grid::grid_from_world() const -> glm::mat4
{
    if (m_plane_type == Grid_plane_type::Node) {
        const erhe::scene::Node* node = get_node();
        if (node != nullptr) {
            return node->node_from_world();
        }
    }
    return m_grid_from_world;
}

void Grid::render(const Render_context& context)
{
    if (!is_visible()) {
        return;
    }
    if (context.camera == nullptr) {
        return;
    }

    //render(context, true); // major
    //render(context, false); // minor
}

void Grid::render(const Render_context& context, bool major)
{
    const erhe::scene::Node* camera_node = context.camera->get_node();
    ERHE_VERIFY(camera_node != nullptr);
    const glm::mat4 m = world_from_grid();

    const float extent     = static_cast<float>(m_cell_count) * m_cell_size;
    const float minor_step = m_cell_size / static_cast<float>(m_cell_div);
    int cell;

    erhe::renderer::Scoped_line_renderer renderer = 
        major ? context.get_line_renderer(1, true, m_see_hidden_major) :
                context.get_line_renderer(0, true, m_see_hidden_minor);

    renderer.set_thickness (major ? m_major_width : m_minor_width);
    renderer.set_line_color(major ? m_major_color : m_minor_color);

    bool minor = !major;
    for (cell = -m_cell_count; cell < m_cell_count; ++cell) {
        float xz = static_cast<float>(cell) * m_cell_size;

        if (major) {
            renderer.add_lines(
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
        }
        if (minor) {
            for (int i = 0; i < (m_cell_div - 1); ++i) {
                xz += minor_step;
                renderer.add_lines(
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
    }
    if (major) {
        float xz = static_cast<float>(cell) * m_cell_size;
        renderer.add_lines(
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

void Grid::imgui(Editor_context& context)
{
    ImGui::InputText  ("Name",             &m_name);
    ImGui::Separator();
    bool visible = is_visible();
    if (ImGui::Checkbox("Visible", &visible)) {
        set_visible(visible);
    }
    
    ImGui::Checkbox   ("Intersect enable", &m_intersect_enable);
    ImGui::Checkbox   ("Snap enable",      &m_snap_enabled);
    ImGui::Checkbox   ("See Major Hidden", &m_see_hidden_major);
    ImGui::Checkbox   ("See Minor Hidden", &m_see_hidden_minor);
    ImGui::SliderFloat("Cell Size",        &m_cell_size,       0.0f, 10.0f);
    ImGui::SliderInt  ("Cell Div",         &m_cell_div,        0,    10);
    ImGui::SliderInt  ("Cell Count",       &m_cell_count,      1,    100);
    ImGui::SliderFloat("Major Width",      &m_major_width,  -100.0f, 100.0f);
    ImGui::SliderFloat("Minor Width",      &m_minor_width,  -100.0f, 100.0f);
    ImGui::ColorEdit4 ("Major Color",      &m_major_color.x, ImGuiColorEditFlags_Float);
    ImGui::ColorEdit4 ("Minor Color",      &m_minor_color.x, ImGuiColorEditFlags_Float);

    erhe::imgui::make_combo("Plane", m_plane_type, grid_plane_type_strings, IM_ARRAYSIZE(grid_plane_type_strings));
    if (m_plane_type != Grid_plane_type::Node) {
        ImGui::DragScalarN("Offset", ImGuiDataType_Float, &m_center.x, 3, 0.01f);
        const float min_rotation = -180.0;
        const float max_rotation =  180.0;
        ImGui::DragScalarN("Rotation", ImGuiDataType_Float, &m_rotation, 1, 0.05f, &min_rotation, &max_rotation);

        const float     radians      = glm::radians(m_rotation);
        const glm::mat4 orientation  = get_plane_transform(m_plane_type);
        const glm::vec3 plane_normal = glm::vec3{0.0, 1.0, 0.0};
        const glm::mat4 offset       = erhe::math::create_translation<float>(m_center);
        const glm::mat4 rotation     = erhe::math::create_rotation<float>( radians, plane_normal);
        m_world_from_grid = orientation * rotation * offset;
        m_grid_from_world = glm::inverse(m_world_from_grid); // orientation * inverse_rotation * inverse_offset;
    } else {
        {
            erhe::scene::Node* host_node = get_node();
            if (host_node != nullptr) {
                const std::string label        = fmt::format("Node: {}", host_node->get_name());
                const std::string detach_label = fmt::format("Detach from {}", host_node->get_name());
                ImGui::TextUnformatted(label.c_str());
                if (ImGui::Button(detach_label.c_str())) {
                    host_node->detach(this);
                }
            }
        }
        const auto& host_node = context.selection->get<erhe::scene::Node>();
        if (host_node) {
            const std::string label = fmt::format("Attach to {}", host_node->get_name());
            if (ImGui::Button(label.c_str())) {
                host_node->attach(
                    std::static_pointer_cast<Grid>(shared_from_this())
                );
            }
        }
    }
}

auto Grid::intersect_ray(const glm::vec3& ray_origin_in_world, const glm::vec3& ray_direction_in_world) -> std::optional<glm::vec3>
{
    if (!m_intersect_enable) {
        return {};
    }

    const glm::vec3 ray_origin_in_grid    = glm::vec3{grid_from_world() * glm::vec4{ray_origin_in_world,    1.0f}};
    const glm::vec3 ray_direction_in_grid = glm::vec3{grid_from_world() * glm::vec4{ray_direction_in_world, 0.0f}};
    const auto intersection = erhe::math::intersect_plane<float>(
        glm::vec3{0.0f, 1.0f, 0.0f},
        glm::vec3{0.0f, 0.0f, 0.0f},
        ray_origin_in_grid,
        ray_direction_in_grid
    );
    if (!intersection.has_value() || intersection.value() < 0.0f) {
        return {};
    }
    const glm::vec3 position_in_grid = ray_origin_in_grid + intersection.value() * ray_direction_in_grid;

    if (
        (position_in_grid.x < -m_cell_size * static_cast<float>(m_cell_count)) ||
        (position_in_grid.x >  m_cell_size * static_cast<float>(m_cell_count)) ||
        (position_in_grid.z < -m_cell_size * static_cast<float>(m_cell_count)) ||
        (position_in_grid.z >  m_cell_size * static_cast<float>(m_cell_count))
    ) {
        return {};
    }

    return glm::vec3{
        world_from_grid() * glm::vec4{position_in_grid, 1.0}
    };
}

} // namespace editor
