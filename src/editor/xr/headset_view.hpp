#pragma once

#if defined(ERHE_XR_LIBRARY_OPENXR)
#include "xr/hand_tracker.hpp"
#include "xr/controller_visualization.hpp"
#include "xr/headset_view_resources.hpp"
#include "scene/scene_view.hpp"

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
//namespace erhe::xr {
//    class Headset;
//}

namespace editor {

class Editor_context;
class Editor_message_bus;
class Editor_rendering;
class Editor_settings;
class Hud;
class Mesh_memory;
class Scene_builder;
class Scene_root;
class Tools;

class Controller_input
{;
public:
    glm::vec3 position     {0.0f, 0.0f, 0.0f};
    glm::vec3 direction    {0.0f, 0.0f, 0.0f};
    float     trigger_value{0.0f};
};

class Headset_view_node
    : public erhe::rendergraph::Rendergraph_node
{
public:
    Headset_view_node(erhe::rendergraph::Rendergraph& rendergraph, Headset_view& headset_view);

    // Implements Rendergraph_node
    void execute_rendergraph_node() override;

private:
    Headset_view& m_headset_view;
};

class Headset_view
    : public std::enable_shared_from_this<Headset_view>
    , public Scene_view
    , public Renderable
{
public:
    class Config
    {
    public:
        bool openxr           {false};
        bool quad_view        {false};
        bool debug            {false};
        bool depth            {false};
        bool visibility_mask  {false};
        bool hand_tracking    {false};
        bool composition_alpha{false};
    };
    Config config;

    Headset_view(
        erhe::graphics::Instance&       graphics_instance,
        erhe::rendergraph::Rendergraph& rendergraph,
        erhe::window::Context_window&   context_window,
        Editor_context&                 editor_context,
        Editor_rendering&               editor_rendering,
        Editor_settings&                editor_settings,
        Mesh_memory&                    mesh_memory,
        Scene_builder&                  scene_builder
    );

    // Public API
    void render_headset           ();
    void begin_frame              ();
    void end_frame                ();
    void request_renderdoc_capture();

    void add_finger_input(const Finger_point& finger_point);

    [[nodiscard]] auto finger_to_viewport_distance_threshold() const -> float;
    [[nodiscard]] auto get_headset                          () const -> erhe::xr::Headset*;
    [[nodiscard]] auto get_root_node                        () const -> std::shared_ptr<erhe::scene::Node>;

    void render(const Render_context&);

    // Implements Scene_view
    auto get_scene_root        () const -> std::shared_ptr<Scene_root>          override;
    auto get_camera            () const -> std::shared_ptr<erhe::scene::Camera> override;
    auto get_shadow_render_node() const -> Shadow_render_node*                  override;
    auto get_rendergraph_node  () -> erhe::rendergraph::Rendergraph_node*       override;

private:
    [[nodiscard]] auto get_headset_view_resources(erhe::xr::Render_view& render_view) -> std::shared_ptr<Headset_view_resources>;

    void setup_root_camera();

    void update_pointer_context_from_controller();

    erhe::window::Context_window&                        m_context_window;
    std::shared_ptr<Headset_view_node>                   m_rendergraph_node;
    std::shared_ptr<Shadow_render_node>                  m_shadow_render_node;
    std::shared_ptr<Scene_root>                          m_scene_root;
    std::unique_ptr<erhe::xr::Headset>                   m_headset;
    std::shared_ptr<erhe::scene::Node>                   m_root_node;
    std::shared_ptr<erhe::scene::Camera>                 m_root_camera;
    std::vector<std::shared_ptr<Headset_view_resources>> m_view_resources;
    std::unique_ptr<Controller_visualization>            m_controller_visualization;
    std::vector<Finger_point>                            m_finger_inputs;
    float                                                m_finger_to_viewport_distance_threshold{0.1f};
    bool                                                 m_head_tracking_enabled{true};
    bool                                                 m_mouse_down{false};
    bool                                                 m_menu_down {false};

    bool                                                 m_request_renderdoc_capture{false};
    bool                                                 m_renderdoc_capture_started{false};
};

} // namespace editor
#else
#   include "xr/null_headset_view.hpp"
#endif
