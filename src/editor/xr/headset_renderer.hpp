#pragma once

#include "windows/imgui_window.hpp"

#include "xr/headset_view_resources.hpp"

#include "erhe/components/components.hpp"

#include <array>

namespace erhe::graphics
{
    class Framebuffer;
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

namespace editor
{

class Application;
class Editor_rendering;
class Editor_tools;
class Hand_tracker;
class Headset_renderer;
class Line_renderer_set;
class Mesh_memory;
class Scene_builder;
class Scene_root;

class Controller_visualization;

class Headset_renderer
    : public erhe::components::Component
    , public Imgui_window
{
public:
    static constexpr std::string_view c_name       {"Headset_renderer"};
    static constexpr std::string_view c_description{"Headset Renderer"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Headset_renderer ();
    ~Headset_renderer() override;

    // Implements Component
    [[nodiscard]] auto get_type_hash                  () const -> uint32_t override { return hash; }
    [[nodiscard]] auto processing_requires_main_thread() const -> bool override { return true; }
    void connect             () override;
    void initialize_component() override;

    // Implements Imgui_window
    void imgui() override;

    // Public API
    void begin_frame();
    void render     ();
    auto root_camera() -> std::shared_ptr<erhe::scene::Camera>;

private:
    [[nodiscard]] auto get_headset_view_resources(
        erhe::xr::Render_view& render_view
    ) -> Headset_view_resources&;

    void setup_root_camera();

    // Component dependencies
    std::shared_ptr<Application>       m_application;
    std::shared_ptr<Editor_rendering>  m_editor_rendering;
    std::shared_ptr<Editor_tools>      m_editor_tools;
    std::shared_ptr<Hand_tracker>      m_hand_tracker;
    std::shared_ptr<Line_renderer_set> m_line_renderer_set;
    std::shared_ptr<Scene_root>        m_scene_root;

    std::unique_ptr<erhe::xr::Headset>        m_headset;
    std::shared_ptr<erhe::scene::Camera>      m_root_camera;
    std::vector<Headset_view_resources>       m_view_resources;
    std::unique_ptr<Controller_visualization> m_controller_visualization;
    std::array<float, 4>                      m_clear_color{0.0f, 0.0f, 0.0f, 0.95f};
};

}
