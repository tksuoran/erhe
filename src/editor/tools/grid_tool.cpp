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
#   include <imgui/misc/cpp/imgui_stdlib.h>
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

    m_grids.push_back(
        Grid
        {
            .name        = "Default Grid",
            .enable      = config.enabled,
            .cell_size   = config.cell_size,
            .cell_div    = config.cell_div,
            .cell_count  = config.cell_count,
            .major_width = config.major_width,
            .minor_width = config.minor_width,
            .major_color = config.major_color,
            .minor_color = config.minor_color
        }
    );
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

    if (context.camera == nullptr)
    {
        return;
    }

    if (!m_enable)
    {
        return;
    }

    for (const auto& grid : m_grids)
    {
        if (!grid.enable)
        {
            continue;
        }

        const glm::vec3 camera_position = context.camera->position_in_world();
        const glm::mat4 m = grid.world_from_grid;

        const float extent     = static_cast<float>(grid.cell_count) * grid.cell_size;
        const float minor_step = grid.cell_size / static_cast<float>(grid.cell_div);
        int cell;
        auto& major_renderer = grid.see_hidden_major
            ? *m_line_renderer_set->visible.at(1).get()
            : *m_line_renderer_set->hidden .at(1).get();
        auto& minor_renderer = grid.see_hidden_minor
            ? *m_line_renderer_set->visible.at(0).get()
            : *m_line_renderer_set->hidden .at(0).get();
        major_renderer.set_thickness(grid.major_width);
        minor_renderer.set_thickness(grid.minor_width);
        major_renderer.set_line_color(grid.major_color);
        minor_renderer.set_line_color(grid.minor_color);

        for (cell = -grid.cell_count; cell < grid.cell_count; ++cell)
        {
            float xz = static_cast<float>(cell) * grid.cell_size;

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
            for (int i = 0; i < (grid.cell_div - 1); ++i)
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
        float xz = static_cast<float>(cell) * grid.cell_size;
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

auto get_plane_transform(Grid_plane_type plane_type) -> glm::dmat4
{
    switch (plane_type)
    {
        case Grid_plane_type::XY:
        {
            return glm::dmat4{
                1.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 1.0, 0.0,
                0.0, 1.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 1.0
            };
        }
        case Grid_plane_type::XZ:
        {
            return glm::dmat4{1.0};
        }
        case Grid_plane_type::YZ:
        {
            return glm::dmat4{
                0.0, 1.0, 0.0, 0.0,
                1.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 1.0, 0.0,
                0.0, 0.0, 0.0, 1.0
            };
        }
    }
    return glm::dmat4{1.0};
}

void Grid_tool::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION

    ImGui::Checkbox("Enable All", &m_enable);

    ImGui::NewLine();

    std::vector<const char*> grid_names;
    for (auto& grid : m_grids)
    {
        grid_names.push_back(grid.name.c_str());
    }
    ImGui::SetNextItemWidth(300);
    ImGui::Combo(
        "Grid",
        &m_grid_index,
        grid_names.data(),
        static_cast<int>(grid_names.size())
    );
    ImGui::NewLine();

    if (!m_grids.empty())
    {
        m_grid_index = std::min(m_grid_index, static_cast<int>(grid_names.size() - 1));

        auto& grid = m_grids.at(m_grid_index);
        ImGui::InputText  ("Name",             &grid.name);
        ImGui::Separator();
        ImGui::Checkbox   ("Enable",           &grid.enable);
        ImGui::Checkbox   ("See Major Hidden", &grid.see_hidden_major);
        ImGui::Checkbox   ("See Minor Hidden", &grid.see_hidden_minor);
        ImGui::SliderFloat("Cell Size",        &grid.cell_size,       0.0f, 10.0f);
        ImGui::SliderInt  ("Cell Div",         &grid.cell_div,        0,    10);
        ImGui::SliderInt  ("Cell Count",       &grid.cell_count,      1,    100);
        ImGui::SliderFloat("Major Width",      &grid.major_width,  -100.0f, 100.0f);
        ImGui::SliderFloat("Minor Width",      &grid.minor_width,  -100.0f, 100.0f);
        ImGui::ColorEdit4 ("Major Color",      &grid.major_color.x, ImGuiColorEditFlags_Float);
        ImGui::ColorEdit4 ("Minor Color",      &grid.minor_color.x, ImGuiColorEditFlags_Float);

        erhe::application::make_combo(
            "Plane",
            grid.plane_type,
            grid_plane_type_strings,
            IM_ARRAYSIZE(grid_plane_type_strings)
        );
        ImGui::DragScalarN(
            "Offset",
            ImGuiDataType_Double,
            &grid.center.x,
            3,
            0.01f
        );
        const double min_rotation = -180.0;
        const double max_rotation =  180.0;
        ImGui::DragScalarN(
            "Rotation",
            ImGuiDataType_Double,
            &grid.rotation,
            1,
            0.01f,
            &min_rotation,
            &max_rotation
        );

        if (grid.plane_type != Grid_plane_type::Custom)
        {
            const double radians = glm::radians(grid.rotation);
            const glm::dmat4 orientation  = get_plane_transform(grid.plane_type);
            const glm::dvec3 plane_normal = glm::dvec3{orientation * glm::dvec4{0.0, 1.0, 0.0, 0.0}};
            const glm::dmat4 offset       = erhe::toolkit::create_translation<double>(grid.center);
            const glm::dmat4 rotation     = erhe::toolkit::create_rotation<double>( radians, plane_normal);
            //const glm::dmat4 inverse_rotation = erhe::toolkit::create_rotation<double>(-radians, plane_normal);
            //const glm::dmat4 inverse_offset   = erhe::toolkit::create_translation<double>(-grid.center);
            grid.world_from_grid = rotation * offset * orientation;
            grid.grid_from_world = glm::inverse(grid.world_from_grid); // orientation * inverse_offset * inverse_rotation;
        }
    }

    ImGui::NewLine();

    constexpr ImVec2 button_size{110.0f, 0.0f};
    const bool add_pressed = ImGui::Button("Add Grid", button_size);
    if (add_pressed)
    {
        m_grids.push_back(Grid{.name = "new grid"});
    }

    ImGui::SameLine();
    if (ImGui::Button("Remove Grid", button_size))
    {
        m_grids.erase(m_grids.begin() + m_grid_index);
    }
#endif
}

auto Grid_tool::update_hover(
    const glm::dvec3 ray_origin_in_world,
    const glm::dvec3 ray_direction_in_world
) const -> Grid_hover_position
{
    Grid_hover_position result
    {
        .grid = nullptr
    };
    double min_distance = std::numeric_limits<double>::max();

    for (auto& grid : m_grids)
    {
        const glm::dvec3 ray_origin_in_grid    = glm::dvec3{grid.grid_from_world * glm::dvec4{ray_origin_in_world, 1.0}};
        const glm::dvec3 ray_direction_in_grid = glm::dvec3{grid.grid_from_world * glm::dvec4{ray_direction_in_world, 0.0}};
        const auto intersection = erhe::toolkit::intersect_plane<double>(
            glm::dvec3{0.0, 1.0, 0.0},
            glm::dvec3{0.0, 0.0, 0.0},
            ray_origin_in_grid,
            ray_direction_in_grid
        );
        if (!intersection.has_value())
        {
            continue;
        }
        const glm::dvec3 position_in_grid  = ray_origin_in_grid + intersection.value() * ray_direction_in_grid;
        const glm::dvec3 position_in_world = glm::dvec3{grid.world_from_grid * glm::dvec4{position_in_grid, 1.0}};
        const double     distance          = glm::distance(ray_origin_in_world, position_in_world);
        if (distance < min_distance)
        {
            min_distance = distance;
            //const glm::dvec3 snapped_position_in_grid  = grid.snap_grid_position(position_in_grid);
            //const glm::dvec3 snapped_position_in_world = glm::dvec3{grid.world_from_grid * glm::dvec4{snapped_position_in_grid, 1.0}};
            //result.valid              = true;
            result.position           = position_in_world;
            //result.position_with_snap = snapped_position_in_world;
            result.grid               = &grid;
        }
    }
    return result;
}

auto Grid::snap_world_position(const glm::dvec3& position_in_world) const -> glm::dvec3
{
    const glm::dvec3 position_in_grid = glm::dvec3{grid_from_world * glm::dvec4{position_in_world, 1.0}};
    const glm::dvec3 snapped_position_in_grid{
        std::floor((position_in_grid.x + cell_size * 0.5) / cell_size) * cell_size,
        std::floor((position_in_grid.y + cell_size * 0.5) / cell_size) * cell_size,
        std::floor((position_in_grid.z + cell_size * 0.5) / cell_size) * cell_size
    };

    return glm::dvec3{
        world_from_grid * glm::dvec4{snapped_position_in_grid, 1.0}
    };
}

auto Grid::snap_grid_position(const glm::dvec3& position_in_grid) const -> glm::dvec3
{
    const glm::dvec3 snapped_position_in_grid{
        std::floor((position_in_grid.x + cell_size * 0.5) / cell_size) * cell_size,
        std::floor((position_in_grid.y + cell_size * 0.5) / cell_size) * cell_size,
        std::floor((position_in_grid.z + cell_size * 0.5) / cell_size) * cell_size
    };

    return glm::dvec3{
        world_from_grid * glm::dvec4{snapped_position_in_grid, 1.0}
    };
}

} // namespace editor
