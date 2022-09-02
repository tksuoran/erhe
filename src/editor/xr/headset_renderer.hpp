#pragma once

#include "xr/headset_view_resources.hpp"

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"

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
    class Configuration;
    class Line_renderer_set;
    class Text_renderer;
}

namespace editor
{

class Controller_visualization;
class Editor_rendering;
class Hand_tracker;
class Headset_renderer;
class Mesh_memory;
class Scene_builder;
class Scene_root;
class Tools;

class Headset_renderer
    : public erhe::components::Component
    , public erhe::application::Imgui_window
    , public erhe::application::Rendergraph_node
{
public:
    static constexpr std::string_view c_name       {"Headset_renderer"};
    static constexpr std::string_view c_description{"Headset Renderer"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Headset_renderer ();
    ~Headset_renderer() noexcept override;

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

    // Public API
    void begin_frame();
    auto scene_root () -> std::shared_ptr<Scene_root>;
    auto root_camera() -> std::shared_ptr<erhe::scene::Camera>;

private:
    [[nodiscard]] auto get_headset_view_resources(
        erhe::xr::Render_view& render_view
    ) -> std::shared_ptr<Headset_view_resources>;

    void setup_root_camera();

    // Component dependencies
    std::shared_ptr<erhe::application::Configuration>     m_configuration;
    std::shared_ptr<erhe::application::Line_renderer_set> m_line_renderer_set;
    std::shared_ptr<erhe::application::Text_renderer>     m_text_renderer;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<Editor_rendering>                     m_editor_rendering;
    std::shared_ptr<Hand_tracker>                         m_hand_tracker;
    std::shared_ptr<Tools>                                m_tools;

    std::shared_ptr<Scene_root>                          m_scene_root;
    std::unique_ptr<erhe::xr::Headset>                   m_headset;
    std::shared_ptr<erhe::scene::Camera>                 m_root_camera;
    std::vector<std::shared_ptr<Headset_view_resources>> m_view_resources;
    std::unique_ptr<Controller_visualization>            m_controller_visualization;
    std::array<float, 4>                                 m_clear_color{0.0f, 0.0f, 0.0f, 0.95f};
};

} // namespace editor
