#pragma once

#include "tools/tool.hpp"

#include "erhe/application/windows/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/scene/node.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace erhe::scene
{
    class Camera;
    class Light;
    class Mesh;
    class Scene;
}

namespace erhe::application
{
    class Line_renderer_set;
}

namespace editor
{

class Scene_root;
class Selection_tool;
class Viewport_config;

class Debug_visualizations
    : public erhe::components::Component
    , public Tool
{
public:
    static constexpr int              c_priority{3};
    static constexpr std::string_view c_label   {"Debug_visualizations"};
    static constexpr std::string_view c_title   {"Debug visualizations"};
    static constexpr uint32_t         hash = compiletime_xxhash::xxh32(c_label.data(), c_label.size(), {});

    Debug_visualizations ();
    ~Debug_visualizations() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements Tool
    [[nodiscard]] auto tool_priority() const -> int   override { return c_priority; }
    [[nodiscard]] auto description  () -> const char* override;
    void tool_render(const Render_context& context) override;

private:

    class Light_visualization_context
    {
    public:
        const Render_context&                render_context;
        std::shared_ptr<erhe::scene::Camera> selected_camera;
        const erhe::scene::Light*            light           {nullptr};
        uint32_t                             light_color     {0};
        uint32_t                             half_light_color{0};
    };

    void mesh_selection_visualization(
        erhe::scene::Mesh* mesh
    );

    void light_visualization(
        const Render_context&                render_context,
        std::shared_ptr<erhe::scene::Camera> selected_camera,
        const erhe::scene::Light*            light
    );

    void directional_light_visualization(Light_visualization_context& context);
    void point_light_visualization      (Light_visualization_context& context);
    void spot_light_visualization       (Light_visualization_context& context);

    void camera_visualization(
        const Render_context&       render_context,
        const erhe::scene::Camera*  camera
    );

    // Component dependencies
    std::shared_ptr<erhe::application::Line_renderer_set> m_line_renderer_set;
    std::shared_ptr<Viewport_config>   m_viewport_config;
    std::shared_ptr<Scene_root>        m_scene_root;
    std::shared_ptr<Selection_tool>    m_selection_tool;
};

} // namespace editor
