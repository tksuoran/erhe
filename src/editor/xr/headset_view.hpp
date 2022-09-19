#pragma once

#include "tools/tool.hpp"
#include "xr/hand_tracker.hpp"
#include "xr/headset_view_resources.hpp"
#include "scene/viewport_window.hpp"

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/toolkit/math_util.hpp"

#include <array>

namespace erhe::graphics
{
    class Framebuffer;
    class OpenGL_state_tracker;
    class Texture;
}
namespace erhe::scene
{
    class Camera;
    class Mesh;
    class Node;
}

namespace erhe::xr
{
    class Headset;
}

namespace erhe::application
{
    class Commands;
    class Configuration;
    class Line_renderer_set;
    class Text_renderer;
}

namespace editor
{

class Controller_visualization;
class Editor_rendering;
class Headset_view;
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

class Headset_view
    : public erhe::components::Component
    , public erhe::application::Imgui_window
    , public erhe::application::Rendergraph_node
    , public Scene_view
    , public Tool
{
public:
    static constexpr std::string_view c_name       {"Headset_view"};
    static constexpr std::string_view c_description{"Headset View"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Headset_view ();
    ~Headset_view() noexcept override;

    // Implements Rendergraph_node
    void execute_rendergraph_node() override;

    // Implements Component
    [[nodiscard]] auto get_type_hash                  () const -> uint32_t override { return c_type_hash; }
    [[nodiscard]] auto processing_requires_main_thread() const -> bool override { return true; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements Imgui_window
    void imgui() override;

    // Implements Tool
    [[nodiscard]] auto description() -> const char* override;
    void tool_render(const Render_context& context) override;

    // Public API
    void begin_frame         ();
    void connect             (const std::shared_ptr<Shadow_render_node>& shadow_render_node);
    void add_finger_input    (const Finger_point& finger_point);
    void add_controller_input(const Controller_input& controller_input);

    [[nodiscard]] auto finger_to_viewport_distance_threshold() const -> float;
    [[nodiscard]] auto get_hand_tracker      () const -> Hand_tracker*;
    [[nodiscard]] auto get_headset           () const -> erhe::xr::Headset*;

    // Implements Scene_view
    [[nodiscard]] auto get_scene_root        () const -> std::shared_ptr<Scene_root>          override;
    [[nodiscard]] auto get_camera            () const -> std::shared_ptr<erhe::scene::Camera> override;
    [[nodiscard]] auto get_shadow_render_node() const -> Shadow_render_node*                  override;

private:
    [[nodiscard]] auto get_headset_view_resources(
        erhe::xr::Render_view& render_view
    ) -> std::shared_ptr<Headset_view_resources>;

    void setup_root_camera();

    void update_pointer_context_from_controller();

    // Component dependencies
    std::shared_ptr<erhe::application::Commands>          m_commands;
    std::shared_ptr<erhe::application::Configuration>     m_configuration;
    std::shared_ptr<erhe::application::Line_renderer_set> m_line_renderer_set;
    std::shared_ptr<erhe::application::Text_renderer>     m_text_renderer;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<Editor_rendering>                     m_editor_rendering;
    std::shared_ptr<Hand_tracker>                         m_hand_tracker;
    std::shared_ptr<Tools>                                m_tools;

    std::shared_ptr<Shadow_render_node>                  m_shadow_render_node;
    std::shared_ptr<Scene_root>                          m_scene_root;
    std::unique_ptr<erhe::xr::Headset>                   m_headset;
    std::shared_ptr<erhe::scene::Camera>                 m_root_camera;
    std::vector<std::shared_ptr<Headset_view_resources>> m_view_resources;
    std::unique_ptr<Controller_visualization>            m_controller_visualization;
    std::array<float, 4>                                 m_clear_color{0.0f, 0.0f, 0.0f, 0.95f};
    std::vector<Finger_point>                            m_finger_inputs;
    std::vector<Controller_input>                        m_controller_inputs;
    float                                                m_finger_to_viewport_distance_threshold{0.1f};
    bool                                                 m_mouse_down{false};
};

} // namespace editor
