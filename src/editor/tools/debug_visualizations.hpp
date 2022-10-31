#pragma once

#include "tools/tool.hpp"

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/toolkit/math_util.hpp"

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
class Trs_tool;
class Viewport_config;

class Debug_visualizations
    : public erhe::application::Imgui_window
    , public erhe::components::Component
    , public Tool
{
public:
    static constexpr int              c_priority {3};
    static constexpr std::string_view c_type_name{"Debug_visualizations"};
    static constexpr std::string_view c_title    {"Debug visualizations"};
    static constexpr uint32_t         c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Debug_visualizations ();
    ~Debug_visualizations() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements Tool
    [[nodiscard]] auto tool_priority() const -> int   override { return c_priority; }
    [[nodiscard]] auto description  () -> const char* override;
    void tool_render(const Render_context& context) override;

    // Implements Imgui_window
    void imgui() override;

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
        const Render_context& render_context,
        erhe::scene::Mesh*    mesh
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
        const Render_context&      render_context,
        const erhe::scene::Camera* camera
    );

    // Component dependencies
    std::shared_ptr<erhe::application::Line_renderer_set> m_line_renderer_set;
    std::shared_ptr<Viewport_config>                      m_viewport_config;
    std::shared_ptr<Selection_tool>                       m_selection_tool;
    std::shared_ptr<Trs_tool>                             m_trs_tool;
    erhe::toolkit::Bounding_volume_combiner               m_selection_bounding_volume;

    float m_gap{0.005f};
    bool  m_tool_hide{false};
    bool  m_physics  {false};
    bool  m_raytrace {false};
    bool  m_selection{true};
    bool  m_lights   {false};
    bool  m_cameras  {false};

    bool      m_selection_node_axis_visible{false};
    float     m_selection_node_axis_width  {5.0f};
    glm::vec4 m_selection_major_color      {1.0f, 1.0f, 0.0f, 1.0f};
    glm::vec4 m_selection_minor_color      {1.0f, 1.0f, 0.0f, 0.5f};
    glm::vec4 m_group_selection_major_color{1.0f, 0.8f, 0.0f, 1.0f};
    glm::vec4 m_group_selection_minor_color{1.0f, 0.8f, 0.0f, 0.5f};
    float     m_selection_major_width      {10.0f};
    float     m_selection_minor_width      {5.0f};
    float     m_camera_visualization_width {5.0f};
    float     m_light_visualization_width  {5.0f};
    int       m_sphere_step_count          {80};
};

} // namespace editor
