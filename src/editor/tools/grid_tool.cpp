#include "tools/grid_tool.hpp"
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

[[nodiscard]] auto Grid::node_attachment_type() const -> const char*
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
        erhe::scene::Node* node = get_node();
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
        erhe::scene::Node* node = get_node();
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

    const glm::vec3 camera_position = context.camera->position_in_world();
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
                const std::string label        = fmt::format("Node: {}", host_node->name());
                const std::string detach_label = fmt::format("Detach from {}", host_node->name());
                ImGui::TextUnformatted(label.c_str());
                if (ImGui::Button(detach_label.c_str()))
                {
                    host_node->detach(this);
                }
            }
        }
        const auto& selection = selection_tool->selection();
        if (!selection.empty())
        {
            const auto& host_node = selection.front();
            const std::string label = fmt::format("Attach to {}", host_node->name());
            if (ImGui::Button(label.c_str()))
            {
                host_node->attach(shared_from_this());
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

    //const auto& config = get<erhe::application::Configuration>()->grid;

    std::shared_ptr<Grid> grid = std::make_shared<Grid>();

    // TODO Move config to editor
    //
    // grid->name        = "Default Grid";
    // grid->enable      = config.enabled;
    // grid->cell_size   = config.cell_size;
    // grid->cell_div    = config.cell_div;
    // grid->cell_count  = config.cell_count;
    // grid->major_width = config.major_width;
    // grid->minor_width = config.minor_width;
    // grid->major_color = config.major_color;
    // grid->minor_color = config.minor_color;

    m_grids.push_back(grid);
}

void Grid_tool::post_initialize()
{
    m_line_renderer_set = get<erhe::application::Line_renderer_set>();
    m_selection_tool    = get<Selection_tool>();
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
        grid->render(m_line_renderer_set, context);
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
                ? "Toggle all grids on -> off"
                : "Toggle all grids off -> on"
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
                0.0, 0.0,-1.0, 0.0,
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
        default:
        {
            return glm::dmat4{1.0};
        }
    }
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
        grid_names.push_back(grid->get_name().c_str());
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
        m_grids[m_grid_index]->imgui(m_selection_tool);
    }

    ImGui::NewLine();

    constexpr ImVec2 button_size{110.0f, 0.0f};
    const bool add_pressed = ImGui::Button("Add Grid", button_size);
    if (add_pressed)
    {
        std::shared_ptr<Grid> new_grid = std::make_shared<Grid>();
        //new_grid->name = "new grid"; TODO
        m_grids.push_back(new_grid);
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
        const auto position_in_world_opt = grid->intersect_ray(ray_origin_in_world, ray_direction_in_world);
        if (!position_in_world_opt.has_value())
        {
            continue;
        }
        const glm::dvec3 position_in_world = position_in_world_opt.value();
        const double     distance          = glm::distance(ray_origin_in_world, position_in_world);
        if (distance < min_distance)
        {
            min_distance    = distance;
            result.position = position_in_world;
            result.grid     = grid.get();
        }
    }
    return result;
}

} // namespace editor
