#pragma once

#include "tools/tool.hpp"
#include "xr/hand_tracker.hpp"
#include "xr/headset_view_resources.hpp"
#include "scene/viewport_window.hpp"

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/toolkit/math_util.hpp"

#include <array>

namespace erhe::scene
{
    class Camera;
    class Node;
}

namespace erhe::xr
{
    class Headset;
}

namespace editor
{

class Controller_visualization;
class Scene_root;

class Controller_input
{;
public:
    glm::vec3 position     {0.0f, 0.0f, 0.0f};
    glm::vec3 direction    {0.0f, 0.0f, 0.0f};
    float     trigger_value{0.0f};
};

class Headset_view_node
    : public erhe::application::Rendergraph_node
{
public:
    Headset_view_node();

    // Implements Rendergraph_node
    void execute_rendergraph_node() override;
};

class Headset_view
    : public erhe::components::Component
    , public erhe::application::Imgui_window
    , public Scene_view
    , public Tool
    , public std::enable_shared_from_this<Headset_view>
{
public:
    static constexpr std::string_view c_name       {"Headset_view"};
    static constexpr std::string_view c_description{"Headset View"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Headset_view ();
    ~Headset_view() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash                  () const -> uint32_t override { return c_type_hash; }
    [[nodiscard]] auto processing_requires_main_thread() const -> bool override { return true; }
    void declare_required_components() override;
    void initialize_component       () override;
    void deinitialize_component     () override;

    // Implements Imgui_window
    void imgui() override;

    // Implements Tool
    void tool_render(const Render_context& context) override;

    // Public API
    void render_headset  ();

    void begin_frame     ();
    void add_finger_input(const Finger_point& finger_point);
    void end_frame       ();

    [[nodiscard]] auto finger_to_viewport_distance_threshold() const -> float;
    [[nodiscard]] auto get_headset           () const -> erhe::xr::Headset*;

    [[nodiscard]] auto get_root_node() const -> std::shared_ptr<erhe::scene::Node>;

    // Implements Scene_view
    [[nodiscard]] auto get_scene_root        () const -> std::shared_ptr<Scene_root>                    override;
    [[nodiscard]] auto get_camera            () const -> std::shared_ptr<erhe::scene::Camera>           override;
    [[nodiscard]] auto get_shadow_render_node() const -> Shadow_render_node*                            override;
    [[nodiscard]] auto get_rendergraph_node  () -> std::shared_ptr<erhe::application::Rendergraph_node> override;

private:
    [[nodiscard]] auto get_headset_view_resources(
        erhe::xr::Render_view& render_view
    ) -> std::shared_ptr<Headset_view_resources>;

    void setup_root_camera();

    void update_pointer_context_from_controller();

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
};

extern Headset_view* g_headset_view;

} // namespace editor
