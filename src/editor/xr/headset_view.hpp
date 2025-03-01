#pragma once

#if defined(ERHE_XR_LIBRARY_OPENXR)
#include "scene/scene_view.hpp"
#include "xr/hand_tracker.hpp"
#include "xr/controller_visualization.hpp"
#include "xr/headset_view_resources.hpp"

#include "erhe_imgui/imgui_window.hpp"
#include "erhe_math/input_axis.hpp"
#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_xr/headset.hpp"

namespace erhe::graphics {
    class Instance;
}
namespace erhe::imgui {
    class Imgui_windows;
}
namespace erhe::renderer {
    class Line_renderer_set;
    class Text_renderer;
}
namespace erhe::scene {
    class Camera;
    class Node;
}
namespace erhe::scene_renderer {
    class Forward_renderer;
    class Shadow_renderer;
}
namespace erhe::window {
    class Context_window;
}

namespace editor {

class Editor_context;
class Editor_message_bus;
class Editor_rendering;
class Editor_settings;
class Fly_camera_tool;
class Hud;
class Mesh_memory;
class Scene_builder;
class Scene_root;
class Time;
class Tools;

class Controller_input
{;
public:
    glm::vec3 position     {0.0f, 0.0f, 0.0f};
    glm::vec3 direction    {0.0f, 0.0f, 0.0f};
    float     trigger_value{0.0f};
};

class Headset_view_node : public erhe::rendergraph::Rendergraph_node
{
public:
    Headset_view_node(erhe::rendergraph::Rendergraph& rendergraph, Headset_view& headset_view);

    // Implements Rendergraph_node
    void execute_rendergraph_node() override;

private:
    Headset_view& m_headset_view;
};

class Headset_camera_offset_move_command : public erhe::commands::Command
{
public:
    Headset_camera_offset_move_command(erhe::commands::Commands& commands, erhe::math::Input_axis& variable, char axis);

    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;

private:
    erhe::math::Input_axis& m_variable;
    char                    m_axis;
};

class Headset_view
    : public std::enable_shared_from_this<Headset_view>
    , public Scene_view
    , public Renderable
    , public erhe::imgui::Imgui_window
{
public:
    class Config
    {
    public:
        bool quad_view        {false};
        bool debug            {false};
        bool depth            {false};
        bool visibility_mask  {false};
        bool hand_tracking    {false};
        bool composition_alpha{false};
    };
    Config config;

    Headset_view(
        erhe::commands::Commands&       commands,
        erhe::graphics::Instance&       graphics_instance,
        erhe::imgui::Imgui_renderer&    imgui_renderer,
        erhe::imgui::Imgui_windows&     imgui_windows,
        erhe::rendergraph::Rendergraph& rendergraph,
        erhe::window::Context_window&   context_window,
        Editor_context&                 editor_context,
        Editor_rendering&               editor_rendering,
        Editor_settings&                editor_settings
    );

    void attach_to_scene(std::shared_ptr<Scene_root> scene_root, Mesh_memory& mesh_memory);

    // Public API
    [[nodiscard]] auto update_events() -> bool;

    void render_headset           ();
    void end_frame                ();
    void request_renderdoc_capture();

    void update_fixed_step();

    // Implements Scene_view
    auto get_scene_root        () const -> std::shared_ptr<Scene_root>          override;
    auto get_camera            () const -> std::shared_ptr<erhe::scene::Camera> override;
    auto get_shadow_render_node() const -> Shadow_render_node*                  override;
    auto get_rendergraph_node  () -> erhe::rendergraph::Rendergraph_node*       override;

    // Implements Imgui_window
    void imgui() override;

    void add_finger_input(const Finger_point& finger_point);

    [[nodiscard]] auto finger_to_viewport_distance_threshold() const -> float;
    [[nodiscard]] auto get_headset                          () const -> erhe::xr::Headset*;
    [[nodiscard]] auto get_root_node                        () const -> std::shared_ptr<erhe::scene::Node>;
    [[nodiscard]] auto is_active                            () const -> bool;
    [[nodiscard]] auto get_camera_offset                    () const -> glm::vec3;

    void render(const Render_context&);

private:
    [[nodiscard]] auto get_headset_view_resources(erhe::xr::Render_view& render_view) -> std::shared_ptr<Headset_view_resources>;

    void setup_root_camera();
    void update_camera_node();
    void update_pointer_context_from_controller();

    erhe::math::Input_axis             m_translate_x;
    erhe::math::Input_axis             m_translate_y;
    erhe::math::Input_axis             m_translate_z;
    glm::vec3                          m_camera_offset{0.0f, 0.0f, 0.0f};
    Headset_camera_offset_move_command m_offset_x_command;
    Headset_camera_offset_move_command m_offset_y_command;
    Headset_camera_offset_move_command m_offset_z_command;

    Editor_context&                                      m_editor_context;
    erhe::window::Context_window&                        m_context_window;
    std::shared_ptr<Headset_view_node>                   m_rendergraph_node;
    std::shared_ptr<Shadow_render_node>                  m_shadow_render_node;
    std::shared_ptr<Scene_root>                          m_scene_root;
    std::unique_ptr<erhe::xr::Headset>                   m_headset;
    std::shared_ptr<erhe::scene::Node>                   m_root_node; // scene root node
    std::shared_ptr<erhe::scene::Node>                   m_headset_node; // transform set by headset
    std::shared_ptr<erhe::scene::Camera>                 m_root_camera;
    std::vector<std::shared_ptr<Headset_view_resources>> m_view_resources;
    std::unique_ptr<Controller_visualization>            m_controller_visualization;
    std::vector<Finger_point>                            m_finger_inputs;
    Shader_stages_variant                                m_shader_stages_variant{Shader_stages_variant::not_set};    

    float                                                m_finger_to_viewport_distance_threshold{0.1f};

    bool                                                 m_request_renderdoc_capture{false};
    bool                                                 m_renderdoc_capture_started{false};
    uint64_t                                             m_frame_number{0};
};

} // namespace editor
#else
#   include "xr/null_headset_view.hpp"
#endif
