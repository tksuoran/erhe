#include "tools/grid_tool.hpp"
#include "editor_rendering.hpp"
#include "graphics/icon_set.hpp"
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
    require<Icon_set>();
    require<Tools   >();
}

void Grid_tool::initialize_component()
{
    set_description(c_title);
    set_flags      (Tool_flags::background);
    set_icon       (get<Icon_set>()->icons.grid);
    get<Tools>()->register_tool(this);
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);

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


void Grid_tool::viewport_toolbar(bool& hovered)
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
        hovered = true;
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

    if (m_enable)
    {
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
    }
    return result;
}

} // namespace editor
