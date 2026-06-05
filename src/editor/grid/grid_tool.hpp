#pragma once

#include "tools/tool.hpp"
#include "tools/tool_window.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace erhe::imgui { class Imgui_windows; }

struct Grid_config;

namespace editor {

class Grid;
class Icon_set;
class Tools;

class Grid_hover_position
{
public:
    glm::vec3             position{0.0f};
    std::shared_ptr<Grid> grid    {};
};

class Grid_tool : public Tool
{
public:
    Grid_tool(
        const Grid_config&           grid_config,
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 context,
        Icon_set&                    icon_set,
        Tools&                       tools
    );

    // Implements Tool
    void tool_render(const Render_context& context)  override;

    // Public API
    //void viewport_toolbar(bool& hovered);

    auto update_hover(glm::vec3 ray_origin, glm::vec3 ray_direction) const -> Grid_hover_position;

    // Copies the currently selected grid's settings into config.
    // Persistence is owned by Editor_settings_store, which calls this
    // through a registered collect callback.
    void write_config(Grid_config& config) const;

private:
    void window_imgui();

    Tool_window                        m_window;
    std::vector<std::shared_ptr<Grid>> m_grids;
    int                                m_grid_index{0};
};

}
