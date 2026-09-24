#pragma once

#include "grid/grid_frame.hpp"

#include "tools/tool.hpp"
#include "tools/tool_window.hpp"
#include "windows/dependency_property_rows.hpp"
#include "windows/property_editor.hpp"

#include "app_message.hpp"

#include "erhe_message_bus/message_bus.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace erhe { class Item_host; }
namespace erhe::imgui { class Imgui_windows; }
namespace erhe::scene { class Camera; }

struct Grid_config;

namespace editor {

class App_message_bus;
class Editor_settings_store;
class Grid;
class Icon_set;
class Tools;

class Grid_hover_position
{
public:
    glm::vec3             position{0.0f};
    std::shared_ptr<Grid> grid    {};
    Grid_frame            frame   {}; // the plane that was hit: the grid as the hovering camera sees it
};

class Grid_tool : public Tool
{
public:
    Grid_tool(
        const Grid_config&           grid_config,
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 context,
        App_message_bus&             app_message_bus,
        Editor_settings_store&       settings_store,
        Icon_set&                    icon_set,
        Tools&                       tools
    );

    // Implements Tool
    void tool_render(const Render_context& context)  override;

    // Public API
    //void viewport_toolbar(bool& hovered);

    auto update_hover(const erhe::scene::Camera* camera, glm::vec3 ray_origin, glm::vec3 ray_direction) const -> Grid_hover_position;

    // Copies the currently selected grid's settings into config.
    // Persistence is owned by Editor_settings_store, which calls this
    // through a registered collect callback.
    void write_config(Grid_config& config) const;

    // The grids the tool owns, in the order the Grid window lists them. The
    // MCP item resolver addresses a grid through this (a grid is in no scene).
    [[nodiscard]] auto get_grids() const -> const std::vector<std::shared_ptr<Grid>>& { return m_grids; }

private:
    void window_imgui();
    // doc/editor/coding_rules.md "Scene-hosted references in editor parts": a grid's frame node
    // is scene content, so the tool drops it when that content leaves.
    void on_close_scene  (erhe::Item_host* closing_host);
    void on_items_removed(const Removed_items& removed);

    Tool_window                        m_window;
    Editor_settings_store&             m_settings_store;
    Property_editor                    m_property_editor;
    Dependency_property_rows           m_property_rows;
    std::vector<std::shared_ptr<Grid>> m_grids;
    int                                m_grid_index{0};

    erhe::message_bus::Subscription<Close_scene_message>   m_close_scene_subscription;
    erhe::message_bus::Subscription<Items_removed_message> m_items_removed_subscription;
};

}
