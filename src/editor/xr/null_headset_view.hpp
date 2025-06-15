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

class Headset_view
    : public Scene_view
    , public std::enable_shared_from_this<Headset_view>
    , public Renderable
    , public erhe::imgui::Imgui_window
{
public:
    Headset_view(
        erhe::commands::Commands&       commands,
        erhe::graphics::Device&         graphics_device,
        erhe::imgui::Imgui_renderer&    imgui_renderer,
        erhe::imgui::Imgui_windows&     imgui_windows,
        erhe::rendergraph::Rendergraph& rendergraph,
        erhe::window::Context_window&   context_window,
        Editor_context&                 editor_context,
        Editor_rendering&               editor_rendering,
        Editor_settings&                editor_settings
    );

    // Public API
    void update_fixed_step();
    void render_headset   ();
    void begin_frame      ();
    void end_frame        ();

    // Implements Imgui_window
    void imgui() override {}

    [[nodiscard]] auto finger_to_viewport_distance_threshold() const -> float;
    [[nodiscard]] auto get_headset                          () const -> void*;
    [[nodiscard]] auto get_root_node                        () const -> std::shared_ptr<erhe::scene::Node>;
    [[nodiscard]] auto is_active                            () const -> bool { return false; }
    [[nodiscard]] auto get_camera_offset                    () const -> glm::vec3 { return glm::vec3{}; }

    // Implements Renderable
    void render(const Render_context& render_context) override;

    // Implements Scene_view
    [[nodiscard]] auto get_scene_root        () const -> std::shared_ptr<Scene_root>          override;
    [[nodiscard]] auto get_camera            () const -> std::shared_ptr<erhe::scene::Camera> override;
    [[nodiscard]] auto get_shadow_render_node() const -> Shadow_render_node*                  override;
    [[nodiscard]] auto get_rendergraph_node  () -> erhe::rendergraph::Rendergraph_node*       override;

private:
};

} // namespace editor
