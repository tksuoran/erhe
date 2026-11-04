#pragma once

#include "renderable.hpp"
#include "scene/scene_view.hpp"
#include "scene/viewport_scene_view.hpp"

#include "erhe_math/math_util.hpp"
#include "erhe_imgui/imgui_window.hpp"
#include "erhe_rendergraph/rendergraph_node.hpp"

#include <array>

namespace erhe::commands {
    class Commands;
}
namespace erhe::graphics {
    class Device;
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
    class Xformable; using Node = Xformable;
}
namespace erhe::scene_renderer {
    class Forward_renderer;
    class Mesh_memory;
    class Shadow_renderer;
}
namespace erhe::window {
    class Context_window;
}

namespace editor {

class App_context;
class Time_context;
class App_message_bus;
class App_rendering;
class App_settings;
class Hud;
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

class Headset_view
    : public Scene_view
    , public std::enable_shared_from_this<Headset_view>
    , public Renderable
{
public:
    Headset_view(
        erhe::commands::Commands&       commands,
        erhe::graphics::Device&         graphics_device,
        erhe::rendergraph::Rendergraph& rendergraph,
        erhe::window::Context_window&   context_window,
#if defined(ERHE_XR_LIBRARY_OPENXR)
        erhe::xr::Headset*              headset,
#endif
        App_context&                    context,
        App_rendering&                  app_rendering,
        App_settings&                   app_settings
    );

    // Public API
    void update_fixed_step(const Time_context& time_context);
    void render_headset   ();
    void wait_frame       ();
    void begin_frame      ();
    void end_frame        ();

    void scene_control_imgui(erhe::imgui::Imgui_host*) {}
    [[nodiscard]] auto get_headlamp_cast_shadow() const -> bool { return false; }

    [[nodiscard]] auto finger_to_viewport_distance_threshold() const -> float;
    [[nodiscard]] auto get_headset                          () const -> void*;
    [[nodiscard]] auto get_root_node                        () const -> std::shared_ptr<erhe::scene::Node>;
    [[nodiscard]] auto is_active                            () const -> bool { return false; }
    [[nodiscard]] auto get_world_from_stage                 () const -> glm::mat4 { return glm::mat4{1.0f}; }
    [[nodiscard]] auto get_stage_from_world                 () const -> glm::mat4 { return glm::mat4{1.0f}; }

    // Implements Renderable
    void render(const Render_context& render_context) override;

    // Implements Scene_view
    auto get_camera            () const -> std::shared_ptr<erhe::scene::Camera> override;
    auto get_projection_scale (float view_distance) const -> float                                override;
    auto get_shadow_render_node() const -> Shadow_render_node*                  override;
    auto get_rendergraph_node  () -> erhe::rendergraph::Rendergraph_node*       override;

private:
};

}
