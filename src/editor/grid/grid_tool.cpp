#include "grid/grid_tool.hpp"

#include "app_context.hpp"
#include "app_settings.hpp"
#include "graphics/icon_set.hpp"
#include "grid/grid.hpp"
#include "tools/tools.hpp"

#include "erhe_configuration/configuration.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_profile/profile.hpp"

#include <imgui/imgui.h>

#include <fmt/format.h>

namespace editor {

using glm::vec3;

Grid_tool::Grid_tool(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 context,
    Icon_set&                    icon_set,
    Tools&                       tools
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Grid", "grid"}
    , Tool                     {context}
{
    ERHE_PROFILE_FUNCTION();

    set_description("Grid");
    set_flags      (Tool_flags::background);
    set_icon       (icon_set.custom_icons, icon_set.icons.grid);

    tools.register_tool(this);

    const auto& ini = erhe::configuration::get_ini_file_section(erhe::c_erhe_config_file_path, "grid");
    bool initial_snap_enable{true};
    bool initial_grid_visible{true};
    ini.get("snap_enabled", initial_snap_enable);
    ini.get("visible",      initial_grid_visible);
    ini.get("major_color",  config.major_color);
    ini.get("minor_color",  config.minor_color);
    ini.get("major_width",  config.major_width);
    ini.get("minor_width",  config.minor_width);
    ini.get("cell_size",    config.cell_size);
    ini.get("cell_div",     config.cell_div);
    ini.get("cell_count",   config.cell_count);

    std::shared_ptr<Grid> grid = std::make_shared<Grid>();
    // TODO Move config to editor ?
    // grid->name        = "Default Grid";
    grid->set_visible     (initial_grid_visible);
    grid->set_snap_enabled(initial_snap_enable);
    grid->set_cell_size   (config.cell_size);
    grid->set_cell_div    (config.cell_div);
    grid->set_cell_count  (config.cell_count);
    grid->set_major_color (config.major_color);
    grid->set_minor_color (config.minor_color);
    grid->set_major_width (config.major_width);
    grid->set_minor_width (config.minor_width);

    m_grids.push_back(grid);
}

void Grid_tool::tool_render(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION();

    for (const auto& grid : m_grids) {
        grid->render(context);
    }
}

//void Grid_tool::viewport_toolbar(bool& hovered)
//{
//    ImGui::SameLine();
//
//    const bool grid_pressed = erhe::imgui::make_button(
//        "G",
//        (m_enable)
//            ? erhe::imgui::Item_mode::active
//            : erhe::imgui::Item_mode::normal
//    );
//    if (ImGui::IsItemHovered()) {
//        hovered = true;
//        ImGui::SetTooltip(
//            m_enable
//                ? "Toggle all grids on -> off"
//                : "Toggle all grids off -> on"
//        );
//    };
//
//    if (grid_pressed) {
//        m_enable = !m_enable;
//    }
//}

auto get_plane_transform(const Grid_plane_type plane_type) -> glm::mat4
{
    switch (plane_type) {
        case Grid_plane_type::XY: {
            return glm::mat4{
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 0.0f,-1.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f
            };
        }
        case Grid_plane_type::XZ: {
            return glm::mat4{1.0f};
        }
        case Grid_plane_type::YZ: {
            return glm::mat4{
                0.0f, 1.0f, 0.0f, 0.0f,
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f
            };
        }
        default: {
            return glm::mat4{1.0f};
        }
    }
}

void Grid_tool::imgui()
{
    ERHE_PROFILE_FUNCTION();

    //ImGui::Checkbox("Enable All", &m_enable);

    ImGui::NewLine();

    std::vector<const char*> grid_names;
    for (auto& grid : m_grids) {
        grid_names.push_back(grid->get_name().c_str());
    }
    ImGui::Combo("Grid", &m_grid_index, grid_names.data(), static_cast<int>(grid_names.size()));
    ImGui::NewLine();

    if (!m_grids.empty()) {
        m_grid_index = std::min(m_grid_index, static_cast<int>(grid_names.size() - 1));
        m_grids[m_grid_index]->imgui(m_context);
    }

    ImGui::NewLine();

    const ImVec2 button_size{ImGui::GetContentRegionAvail().x, 0.0f};

    const bool add_pressed = ImGui::Button("Add Grid", button_size);
    if (add_pressed) {
        std::shared_ptr<Grid> new_grid = std::make_shared<Grid>();
        //new_grid->name = "new grid"; TODO
        m_grids.push_back(new_grid);
    }

    if (ImGui::Button("Remove Grid", button_size)) {
        m_grids.erase(m_grids.begin() + m_grid_index);
    }
}

auto Grid_tool::update_hover(const glm::vec3 ray_origin_in_world, const glm::vec3 ray_direction_in_world) const -> Grid_hover_position
{
    Grid_hover_position result{
        .grid = nullptr
    };
    float min_distance = std::numeric_limits<float>::max();

    //if (!m_enable) {
    //    return result;
    //}

    for (auto& grid : m_grids) {
        const auto position_in_world_opt = grid->intersect_ray(ray_origin_in_world, ray_direction_in_world);
        if (!position_in_world_opt.has_value()) {
            continue;
        }
        const glm::vec3 position_in_world = position_in_world_opt.value();
        const float     distance          = glm::distance(ray_origin_in_world, position_in_world);
        if (distance < min_distance) {
            min_distance    = distance;
            result.position = position_in_world;
            result.grid     = grid;
        }
    }
    return result;
}

}
