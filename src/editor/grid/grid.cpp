#include "grid/grid.hpp"

#include "app_context.hpp"
#include "app_rendering.hpp"
#include "config/generated/grid_config.hpp"
#include "items.hpp"
#include "renderers/render_context.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_scene/node.hpp"

#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <fmt/format.h>

#include <glm/gtx/matrix_operation.hpp>

#include <algorithm>
#include <cmath>

namespace editor {

Grid::Grid()
{
    update();
}

// Clone constructor for node duplication. Copies all grid data while the base
// (Item / Node_attachment for_clone) gives the clone a fresh id and leaves it
// detached (m_node reset). Keep this in sync with the data members below.
Grid::Grid(const Grid& src, erhe::for_clone)
    : Item                 {src, erhe::for_clone{}}
    , m_plane_type         {src.m_plane_type         }
    , m_intersect_enable   {src.m_intersect_enable   }
    , m_snap_enabled       {src.m_snap_enabled       }
    , m_rotation           {src.m_rotation           }
    , m_center             {src.m_center             }
    , m_cell_size          {src.m_cell_size          }
    , m_cell_div           {src.m_cell_div           }
    , m_cell_count         {src.m_cell_count         }
    , m_label_enable       {src.m_label_enable       }
    , m_label_text_fraction{src.m_label_text_fraction}
    , m_label_spacing      {src.m_label_spacing      }
    , m_label_fade         {src.m_label_fade         }
    , m_level_colors       {src.m_level_colors       }
    , m_level_widths       {src.m_level_widths       }
    , m_label_color        {src.m_label_color        }
    , m_world_from_grid    {src.m_world_from_grid    }
    , m_grid_from_world    {src.m_grid_from_world    }
{
}

void Grid::read_config(const Grid_config& config)
{
    set_visible(config.visible);
    m_snap_enabled        = config.snap_enabled;
    m_cell_size           = config.cell_size;
    m_cell_div            = config.cell_div;
    m_cell_count          = config.cell_count;
    m_label_enable        = config.label_enable;
    m_label_text_fraction = config.label_text_fraction;
    m_label_spacing       = config.label_spacing;
    m_label_fade          = config.label_fade;
    m_label_color         = config.label_color;
    m_level_colors[0]     = config.level0_color;
    m_level_colors[1]     = config.level1_color;
    m_level_colors[2]     = config.level2_color;
    m_level_colors[3]     = config.level3_color;
    m_level_widths[0]     = config.level0_width;
    m_level_widths[1]     = config.level1_width;
    m_level_widths[2]     = config.level2_width;
    m_level_widths[3]     = config.level3_width;
}

void Grid::write_config(Grid_config& config) const
{
    config.visible             = is_visible();
    config.snap_enabled        = m_snap_enabled;
    config.cell_size           = m_cell_size;
    config.cell_div            = m_cell_div;
    config.cell_count          = m_cell_count;
    config.label_enable        = m_label_enable;
    config.label_text_fraction = m_label_text_fraction;
    config.label_spacing       = m_label_spacing;
    config.label_fade          = m_label_fade;
    config.label_color         = m_label_color;
    config.level0_color        = m_level_colors[0];
    config.level1_color        = m_level_colors[1];
    config.level2_color        = m_level_colors[2];
    config.level3_color        = m_level_colors[3];
    config.level0_width        = m_level_widths[0];
    config.level1_width        = m_level_widths[1];
    config.level2_width        = m_level_widths[2];
    config.level3_width        = m_level_widths[3];
}

auto Grid::snap_world_position(const glm::vec3& position_in_world) const -> glm::vec3
{
    if (!m_snap_enabled) {
        return position_in_world;
    }
    const float     snap_size        = m_cell_size / static_cast<float>(std::max(1, m_cell_div));
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

    const float     snap_size        = m_cell_size / static_cast<float>(std::max(1, m_cell_div));
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
    // TODO Handling visibility here may add latency, but I guess that is okay.
    //      If Gr\id overrided handle_flag_bits_update(), it could handle visibility
    //      changes more directly, but it would then need access to App_rendering.
    context.app_context.app_rendering->set_grid_visibility(is_visible());
    context.app_context.app_rendering->set_grid_label(
        glm::vec4{
            m_label_enable ? 1.0f : 0.0f,
            m_label_text_fraction,
            std::max(1.0f, std::round(m_label_spacing)), // shader formats integer values only
            m_label_fade
        }
    );
    context.app_context.app_rendering->set_grid_colors(m_level_colors, m_label_color);
    context.app_context.app_rendering->set_grid_line_widths(
        glm::vec4{m_level_widths[0], m_level_widths[1], m_level_widths[2], m_level_widths[3]}
    );

    // Derive the 4 grid LOD level cell sizes from cell size and cell div,
    // preserving the old CPU grid semantics: level1 = cell size (major
    // lines), level2 = cell size / cell div (minor lines = snap step),
    // extended geometrically to level0 (super-major) and level3
    // (sub-minor). Defaults (size 1, div 10) reproduce the historical
    // shader level sizes {10, 1, 0.1, 0.01}.
    const float div  = static_cast<float>(std::max(1, m_cell_div));
    const float size = std::max(0.001f, m_cell_size);
    context.app_context.app_rendering->set_grid_sizes(
        glm::vec4{size * div, size, size / div, size / (div * div)}
    );
}

void Grid::update()
{
    if (m_plane_type != Grid_plane_type::Node) {
        const float     radians      = glm::radians(m_rotation);
        const glm::mat4 orientation  = get_plane_transform(m_plane_type);
        const glm::vec3 plane_normal = glm::vec3{0.0f, 1.0f, 0.0f};
        const glm::mat4 offset       = erhe::math::create_translation<float>(m_center);
        const glm::mat4 rotation     = erhe::math::create_rotation<float>(radians, plane_normal);
        m_world_from_grid = orientation * rotation * offset;
        m_grid_from_world = glm::inverse(m_world_from_grid); // orientation * inverse_rotation * inverse_offset;
    }
}

void Grid::imgui(App_context& context)
{
    ImGui::InputText  ("Name",     &m_name);
    ImGui::Separator();
    bool visible = is_visible();
    if (ImGui::Checkbox("Visible", &visible)) {
        set_visible(visible);
    }
    
    ImGui::Checkbox   ("Intersect enable", &m_intersect_enable);
    ImGui::Checkbox   ("Snap enable",      &m_snap_enabled);
    ImGui::SliderFloat("Cell Size",        &m_cell_size,       0.01f, 10.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderInt  ("Cell Div",         &m_cell_div,        1,     10);
    ImGui::ColorEdit4 ("Level 0 Color",    &m_level_colors[0].x, ImGuiColorEditFlags_Float);
    ImGui::ColorEdit4 ("Level 1 Color",    &m_level_colors[1].x, ImGuiColorEditFlags_Float);
    ImGui::ColorEdit4 ("Level 2 Color",    &m_level_colors[2].x, ImGuiColorEditFlags_Float);
    ImGui::ColorEdit4 ("Level 3 Color",    &m_level_colors[3].x, ImGuiColorEditFlags_Float);
    // Line width is a fraction of the level cell size (grid.frag
    // PristineGrid line width).
    ImGui::SliderFloat("Level 0 Width",    &m_level_widths[0], 0.0f, 0.5f, "%.4f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Level 1 Width",    &m_level_widths[1], 0.0f, 0.5f, "%.4f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Level 2 Width",    &m_level_widths[2], 0.0f, 0.5f, "%.4f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Level 3 Width",    &m_level_widths[3], 0.0f, 0.5f, "%.4f", ImGuiSliderFlags_Logarithmic);
    ImGui::Checkbox   ("Axis Labels",      &m_label_enable);
    ImGui::SliderFloat("Label Size",       &m_label_text_fraction, 0.05f, 0.5f);
    ImGui::SliderFloat("Label Spacing",    &m_label_spacing,       1.0f,  100.0f, "%.0f");
    ImGui::SliderFloat("Label Fade",       &m_label_fade,          0.5f,  24.0f);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Glyph size in pixels at which labels are fully visible.\nSmaller values keep labels visible further away.");
    }
    ImGui::ColorEdit4 ("Label Color",      &m_label_color.x, ImGuiColorEditFlags_Float);

    erhe::imgui::make_combo("Plane", m_plane_type, grid_plane_type_strings, IM_ARRAYSIZE(grid_plane_type_strings));

    if (m_plane_type != Grid_plane_type::Node) {
        ImGui::DragScalarN("Offset", ImGuiDataType_Float, &m_center.x, 3, 0.01f);
        const float min_rotation = -180.0;
        const float max_rotation =  180.0;
        ImGui::DragScalarN("Rotation", ImGuiDataType_Float, &m_rotation, 1, 0.05f, &min_rotation, &max_rotation);
        update();
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
        const auto& host_node = get<erhe::scene::Node>(context.selection->get_selected_items());
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

auto Grid::normal_in_world() const -> glm::vec3
{
    const glm::mat4 world_from_grid_       = world_from_grid();
    const glm::mat4 normal_world_from_grid = glm::transpose(glm::adjugate(world_from_grid_));
    const glm::vec3 normal_in_world        = glm::vec3{normal_world_from_grid * glm::vec4{0.0f, 1.0f, 0.0f, 0.0f}};
    const glm::vec3 unit_normal_in_world   = glm::normalize(normal_in_world);
    return unit_normal_in_world;
}

auto Grid::tangent_in_world() const -> glm::vec3
{
    const glm::mat4 world_from_grid_      = world_from_grid();
    const glm::vec3 tangent_in_world      = glm::vec3{world_from_grid_ * glm::vec4{0.0f, 0.0f, 1.0f, 0.0f}};
    const glm::vec3 unit_tangent_in_world = glm::normalize(tangent_in_world);
    return unit_tangent_in_world;
}

auto Grid::bitangent_in_world() const -> glm::vec3
{
    const glm::mat4 world_from_grid_        = world_from_grid();
    const glm::vec3 bitangent_in_world      = glm::vec3{world_from_grid_ * glm::vec4{1.0f, 0.0f, 0.0f, 0.0f}};
    const glm::vec3 unit_bitangent_in_world = glm::normalize(bitangent_in_world);
    return unit_bitangent_in_world;
}

auto Grid::get_cell_size() const -> float
{
    return m_cell_size;
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

}
