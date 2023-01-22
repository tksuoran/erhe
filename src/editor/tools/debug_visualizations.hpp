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
}

namespace editor
{

class Scene_root;

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

    // Implements Tool
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
        glm::vec4                            light_color     {0};
        glm::vec4                            half_light_color{0};
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

    void directional_light_visualization(const Light_visualization_context& context);
    void point_light_visualization      (const Light_visualization_context& context);
    void spot_light_visualization       (const Light_visualization_context& context);
    void camera_visualization(
        const Render_context&      render_context,
        const erhe::scene::Camera* camera
    );
    void selection_visualization     (const Render_context& context);
    void physics_nodes_visualization (const std::shared_ptr<Scene_root>& scene_root);
    void raytrace_nodes_visualization(const std::shared_ptr<Scene_root>& scene_root);
    void mesh_labels(
        const Render_context& context,
        erhe::scene::Mesh*    mesh
    );

    void label(
        const Render_context&  context,
        const glm::mat4&       clip_from_world,
        const glm::mat4&       world_from_node,
        const glm::vec3&       position_in_world,
        const uint32_t         text_color,
        const std::string_view label_text
    );

    erhe::toolkit::Bounding_volume_combiner m_selection_bounding_volume;

    float m_gap      {0.003f};
    bool  m_tool_hide{false};
    bool  m_physics  {false};
    bool  m_raytrace {false};
    bool  m_selection{true};
    bool  m_lights   {false};
    bool  m_cameras  {false};

    bool      m_selection_bounding_points_visible{false};
    bool      m_selection_node_axis_visible      {false};
    bool      m_selection_box                    {false};
    bool      m_selection_sphere                 {true};
    float     m_selection_node_axis_width        {4.0f};
    glm::vec4 m_selection_major_color            {2.0f, 1.6f, 0.1f, 1.0f};
    glm::vec4 m_selection_minor_color            {2.0f, 1.6f, 0.1f, 0.5f};
    glm::vec4 m_group_selection_major_color      {2.0f, 1.2f, 0.1f, 1.0f};
    glm::vec4 m_group_selection_minor_color      {2.0f, 1.2f, 0.1f, 0.5f};
    float     m_selection_major_width            {4.0f};
    float     m_selection_minor_width            {2.0f};
    float     m_camera_visualization_width       {4.0f};
    float     m_light_visualization_width        {4.0f};
    int       m_sphere_step_count                {80};

    int       m_max_labels               {400};
    bool      m_show_only_selection      {true};
    bool      m_show_points              {false};
    bool      m_show_polygons            {false};
    bool      m_show_edges               {false};
    bool      m_show_corners             {false};
    glm::vec4 m_point_label_text_color   {0.3f, 1.0f, 0.3f, 1.0f};
    glm::vec4 m_point_label_line_color   {0.0f, 0.8f, 0.0f, 1.0f};
    float     m_point_label_line_width   {1.5f};
    float     m_point_label_line_length  {0.1f};
    glm::vec4 m_edge_label_text_color    {1.0f, 0.3f, 0.3f, 1.0f};
    float     m_edge_label_text_offset   {0.1f};
    glm::vec4 m_edge_label_line_color    {0.8f, 0.0f, 0.0f, 1.0f};
    float     m_edge_label_line_width    {1.5f};
    float     m_edge_label_line_length   {0.5f};
    glm::vec4 m_polygon_label_text_color {1.0f, 1.0f, 5.0f, 1.0f};
    glm::vec4 m_polygon_label_line_color {1.0f, 0.5f, 0.0f, 1.0f};
    float     m_polygon_label_line_width {1.5f};
    float     m_polygon_label_line_length{0.1f};
    glm::vec4 m_corner_label_text_color  {0.5f, 1.0f, 1.0f, 1.0f};
    glm::vec4 m_corner_label_line_color  {0.0f, 0.5f, 0.5f, 1.0f};
    float     m_corner_label_line_length {0.05f};
    float     m_corner_label_line_width  {1.5f};
    //// std::vector<std::string> m_lines;
};

extern Debug_visualizations* g_debug_visualizations;

} // namespace editor
