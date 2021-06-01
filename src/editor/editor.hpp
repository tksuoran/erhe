#pragma once

#include "tools/pointer_context.hpp"
#include "tools/selection_tool.hpp"
#include "erhe/toolkit/window.hpp"
#include "erhe/toolkit/view.hpp"
#include "erhe/components/component.hpp"

#include "imgui.h"
#include "renderdoc_app.h"

#include <chrono>
#include <optional>
#include <thread>

namespace erhe::graphics
{
    class Framebuffer;
    class OpenGL_state_tracker;
    class Renderbuffer;
    class Shader_monitor;
    class Texture;
}

namespace erhe::scene
{
    class ICamera;
}

namespace editor {

class Application;
class Brushes;
class Camera_properties;
class Fly_camera_tool;
class Forward_renderer;
class Grid_tool;
class Hover_tool;
class Id_renderer;
class Light_properties;
class Line_renderer;
class Material_properties;
class Mesh_properties;
class Node_properties;
class Operations;
class Operation_stack;
class Physics_tool;
class Physics_window;
class Scene_manager;
class Scene_root;
class Selection_tool;
class Shadow_renderer;
class Text_renderer;
class Tool;
class Trs_tool;
class Viewport_config;
class Viewport_window;
class Window;

class Editor
    : public erhe::components::Component
    , public erhe::toolkit::View
{
public:
    static constexpr const char* c_name = "Editor";
    Editor();
    virtual ~Editor();

    // Implements Component
    void connect() override;
    void initialize_component() override;

    // Implements View
    void update() override;
    void on_enter() override;
    void on_mouse_move( double x, double y) override;
    void on_mouse_click(erhe::toolkit::Mouse_button button, int count) override;
    void on_key_press(erhe::toolkit::Keycode code, uint32_t modifier_mask) override;
    void on_key_release(erhe::toolkit::Keycode code, uint32_t modifier_mask) override;

    void render();
    auto get_priority_action() const -> Action;
    void set_priority_action(Action action);
 
    void register_tool(Tool* tool);
    void register_background_tool(Tool* tool);
    void register_window(Window* window);
    void delete_selected_meshes();

    void set_view_camera(std::shared_ptr<erhe::scene::ICamera> camera);
    auto get_view_camera() const -> std::shared_ptr<erhe::scene::ICamera>;

private:
    Action m_priority_action{Action::select};

    void initialize_camera();
    auto get_action_tool(Action action) -> Tool*;
    void render_shadowmaps();
    void render_id();
    void render_clear_primary();
    void render_content();
    void render_selection();
    void render_tool_meshes();
    void render_update_tools();
    void update_and_render_tools();
    void on_pointer();
    void cancel_ready_tools(Tool* keep);
    void update_fixed_step(double dt);
    void update_once_per_frame();
    void update_pointer();
    void on_key(bool pressed, erhe::toolkit::Keycode code, uint32_t modifier_mask);
    void begin_frame();
    void gui_begin_frame();
    void gui_render();
    auto is_primary_scene_output_framebuffer_ready() -> bool;
    void bind_primary_scene_output_framebuffer();
    void end_primary_framebuffer();
    auto to_scene_content(glm::vec2 position_in_root) -> glm::vec2;

    std::shared_ptr<Brushes>             m_brushes;
    std::shared_ptr<Camera_properties>   m_camera_properties;
    std::shared_ptr<Fly_camera_tool>     m_fly_camera_tool;
    std::shared_ptr<Grid_tool>           m_grid_tool;
    std::shared_ptr<Hover_tool>          m_hover_tool;
    std::shared_ptr<Light_properties>    m_light_properties;
    std::shared_ptr<Material_properties> m_material_properties;
    std::shared_ptr<Mesh_properties>     m_mesh_properties;
    std::shared_ptr<Node_properties>     m_node_properties;
    std::shared_ptr<Operation_stack>     m_operation_stack;
    std::shared_ptr<Operations>          m_operations;
    std::shared_ptr<Physics_tool>        m_physics_tool;
    std::shared_ptr<Scene_root>          m_scene_root;
    std::shared_ptr<Selection_tool>      m_selection_tool;
    std::shared_ptr<Trs_tool>            m_trs_tool;
    std::shared_ptr<Viewport_config>     m_viewport_config;
    std::shared_ptr<Viewport_window>     m_viewport_window;
    std::shared_ptr<Physics_window>      m_physics_window;
    std::vector<Tool*>                   m_tools;
    std::vector<Tool*>                   m_background_tools;
    std::vector<Window*>                 m_windows;

    std::optional<Selection_tool::Subcription>            m_selection_layer_update_subscription;

    std::shared_ptr<Application>                          m_application;
    std::shared_ptr<Forward_renderer>                     m_forward_renderer;
    std::shared_ptr<Id_renderer>                          m_id_renderer;
    std::shared_ptr<Scene_manager>                        m_scene_manager;
    std::shared_ptr<Shadow_renderer>                      m_shadow_renderer;
    std::shared_ptr<Text_renderer>                        m_text_renderer;
    std::shared_ptr<Line_renderer>                        m_line_renderer;
    std::shared_ptr<erhe::graphics::Shader_monitor>       m_shader_monitor;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    bool                                                  m_trigger_capture{false};

    erhe::scene::Viewport                 m_scene_viewport  {0, 0, 0, 0};
    std::shared_ptr<erhe::scene::ICamera> m_view_camera;

    bool                                  m_enable_gui      {true};
    ImGuiContext*                         m_imgui_context   {nullptr};
    std::chrono::steady_clock::time_point m_current_time;
    double                                m_time_accumulator{0.0};
    double                                m_time            {0.0};
    Pointer_context                       m_pointer_context;
};

}
