#pragma once

#include "brushes/brush_tool.hpp"
#include "create/create.hpp"
#include "tools/debug_visualizations.hpp"
#include "tools/fly_camera_tool.hpp"
#include "grid/grid_tool.hpp"
#include "tools/hotbar.hpp"
#include "tools/hover_tool.hpp"
#include "tools/hud.hpp"
#include "tools/material_paint_tool.hpp"
#include "tools/paint_tool.hpp"
#include "physics/physics_tool.hpp"
#include "tools/selection_tool.hpp"
#include "transform/transform_tool.hpp"
#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#endif

#include "erhe_profile/profile.hpp"
#include "erhe_renderer/pipeline_renderpass.hpp"

namespace erhe::commands {
    class Commands;
}
namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
}
namespace erhe::scene {
    class Scene_message_bus;
}

namespace editor {

class App_context;
class App_settings;
class Input_state;
class Item_tree_window;
class Operation_stack;
class Operations;
class Render_context;
class Scene_commands;
class Scene_message_bus;
class Scene_root;
class Tool;
class Scene_views;

class Tools_pipeline_renderpasses
{
public:
    Tools_pipeline_renderpasses(Mesh_memory& mesh_memory, Programs& programs);
    erhe::renderer::Pipeline_pass tool1_hidden_stencil;
    erhe::renderer::Pipeline_pass tool2_visible_stencil;
    erhe::renderer::Pipeline_pass tool3_depth_clear;
    erhe::renderer::Pipeline_pass tool4_depth;
    erhe::renderer::Pipeline_pass tool5_visible_color;
    erhe::renderer::Pipeline_pass tool6_hidden_color;
};

class Tools
{
public:
    Tools(
        erhe::imgui::Imgui_renderer&    imgui_renderer,
        erhe::imgui::Imgui_windows&     imgui_windows,
        erhe::scene::Scene_message_bus& scene_message_bus,
        App_context&                    context,
        App_rendering&                  app_rendering,
        App_settings&                   app_settings,
        Mesh_memory&                    mesh_memory,
        Programs&                       programs
    );

    // Public API
    void update_transforms    ();
    void render_viewport_tools(const Render_context& context);
    void register_tool        (Tool* tool);
    void set_priority_tool    (Tool* tool);
    [[nodiscard]] auto get_priority_tool  () const -> Tool*;
    [[nodiscard]] auto get_tools          () const -> const std::vector<Tool*>&;
    [[nodiscard]] auto get_tool_scene_root() -> std::shared_ptr<Scene_root>;

private:
    App_context&                      m_context;
    Tools_pipeline_renderpasses       m_pipeline_renderpasses;
    Tool*                             m_priority_tool{nullptr};
    ERHE_PROFILE_MUTEX(std::mutex,    m_mutex);
    std::vector<Tool*>                m_tools;
    std::vector<Tool*>                m_background_tools;
    std::shared_ptr<Scene_root>       m_scene_root;

    std::shared_ptr<Item_tree_window> m_content_library_tree_window;
    std::shared_ptr<Item_tree_window> m_tool_scene_browser;
};

}
