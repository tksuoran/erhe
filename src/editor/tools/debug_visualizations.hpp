#pragma once

#include "renderable.hpp"
#include "windows/property_editor.hpp"

#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_imgui/imgui_window.hpp"
#include "erhe_math/math_util.hpp"

#include <memory>

namespace erhe::imgui {
    class Imgui_windows;
}
namespace erhe::scene {
    class Camera;
    class Light;
    class Mesh;
    class Skin;
}
namespace erhe::scene_renderer {
    class Program_interface;
    class Texel_renderer;
}

namespace editor {

class App_context;
class App_message_bus;
class App_rendering;
class Programs;
class Scene_root;
class Scene_view;

enum class Visualization_mode {
    None     = 0,
    Selected = 1,
    Hovered  = 2,
    All      = 3
};

static constexpr const char* c_visualization_mode_strings[] = {
    "None",
    "Selected",
    "Hovered",
    "All"
};

class Debug_visualizations
    : public erhe::imgui::Imgui_window
    , public Renderable
{
public:
    Debug_visualizations(
        erhe::graphics::Device&                  graphics_device,
        erhe::imgui::Imgui_renderer&             imgui_renderer,
        erhe::imgui::Imgui_windows&              imgui_windows,
        erhe::scene_renderer::Program_interface& program_interface,
        App_context&                             context,
        App_message_bus&                         app_message_bus,
        App_rendering&                           app_rendering,
        Programs&                                programs
    );

    // Implements Renderable
    void render(const Render_context& context) override;

    // Implements Imgui_window
    void imgui() override;

private:
    [[nodiscard]] auto get_selected_camera(const Render_context& render_context) -> std::shared_ptr<erhe::scene::Camera>;

    class Light_visualization_context
    {
    public:
        const Render_context&                render_context;
        std::shared_ptr<erhe::scene::Camera> selected_camera;
        const erhe::scene::Light*            light           {nullptr};
        glm::vec4                            light_color     {0};
        glm::vec4                            half_light_color{0};
    };

    void shadow_debug            (const Render_context& render_context);
    void world_axes_visualization(const Render_context& render_context);

    void mesh_visualization(const Render_context& render_context, erhe::scene::Mesh* mesh);
    void skin_visualization(const Render_context& render_context, erhe::scene::Skin& skin);

    void light_visualization(
        const Render_context&                render_context,
        std::shared_ptr<erhe::scene::Camera> selected_camera,
        const erhe::scene::Light*            light
    );

    void directional_light_visualization(const Light_visualization_context& context);
    void point_light_visualization      (const Light_visualization_context& context);
    void spot_light_visualization       (const Light_visualization_context& context);
    void camera_visualization           (const Render_context& render_context, const erhe::scene::Camera* camera);
    void selection_visualization        (const Render_context& context);
    void physics_nodes_visualization    (const Render_context& context);
    void raytrace_nodes_visualization   (const Render_context& context);
    void mesh_labels                    (const Render_context& context, erhe::scene::Mesh* mesh);

    void label(
        const Render_context&  context,
        const glm::mat4&       clip_from_world,
        const glm::mat4&       world_from_node,
        const glm::vec3&       position_in_world,
        const uint32_t         text_color,
        const std::string_view label_text
    );

    void make_combo(const char* label, Visualization_mode& visualization);

    App_context&                         m_context;
    Scene_view*                          m_hover_scene_view{nullptr};
    erhe::math::Bounding_volume_combiner m_selection_bounding_volume;

    Visualization_mode m_lights                 {Visualization_mode::All};
    Visualization_mode m_cameras                {Visualization_mode::Selected};
    Visualization_mode m_skins                  {Visualization_mode::All};
    Visualization_mode m_node_axis_visualization{Visualization_mode::None};
    Visualization_mode m_physics_visualization  {Visualization_mode::None};
    Visualization_mode m_vertex_labels          {Visualization_mode::None};
    Visualization_mode m_facet_labels           {Visualization_mode::None};
    Visualization_mode m_edge_labels            {Visualization_mode::None};
    Visualization_mode m_corner_labels          {Visualization_mode::None};
    Visualization_mode m_raytrace_visualization {Visualization_mode::None};

    Property_editor m_property_editor;

    bool m_vertex_positions{false};

    float     m_gap                              {0.003f};
    bool      m_tool_hide                        {false};
    bool      m_world_axes                       {false};
    bool      m_shadow_debug                     {false};
    bool      m_selection                        {true};
    bool      m_selection_bounding_points_visible{false};
    bool      m_selection_box                    {false};
    bool      m_selection_sphere                 {false};
    bool      m_selection_convex_hull            {false};
    bool      m_selection_convex_hull_projected  {false};
    bool      m_selection_parts                  {false};
    float     m_selection_node_axis_width        {2.0f};
    glm::vec4 m_selection_major_color            {2.0f, 1.6f, 0.1f, 1.0f};
    glm::vec4 m_selection_minor_color            {2.0f, 1.6f, 0.1f, 0.5f};
    glm::vec4 m_group_selection_major_color      {2.0f, 1.2f, 0.1f, 1.0f};
    glm::vec4 m_group_selection_minor_color      {2.0f, 1.2f, 0.1f, 0.5f};
    float     m_selection_major_width            {4.0f};
    float     m_selection_minor_width            {2.0f};

    float     m_camera_visualization_width       {8.0f};
    glm::vec4 m_camera_line_color                {1.0f, 1.0f, 1.0f, 1.0f};
    bool      m_camera_cull_test                 {false};

    bool      m_debug_convex_hull                {false};
    int       m_convex_hull_edge                 {0};

    float     m_light_visualization_width        {8.0f};

    bool      m_frustum_box                      {false};
    bool      m_frustum_planes                   {false};

    int       m_sphere_step_count                {80};
    int       m_max_labels                       {400};
    glm::vec4 m_vertex_label_text_color          {0.3f, 1.0f, 0.3f, 1.0f};
    glm::vec4 m_vertex_label_line_color          {0.0f, 0.8f, 0.0f, 1.0f};
    float     m_vertex_label_line_width          {1.0f};
    float     m_vertex_label_line_length         {0.1f};
    glm::vec4 m_edge_label_text_color            {1.0f, 0.3f, 0.3f, 1.0f};
    float     m_edge_label_text_offset           {0.1f};
    glm::vec4 m_edge_label_line_color            {0.8f, 0.0f, 0.0f, 1.0f};
    float     m_edge_label_line_width            {1.0f};
    float     m_edge_label_line_length           {0.5f};
    glm::vec4 m_facet_label_text_color           {1.0f, 1.0f, 5.0f, 1.0f};
    glm::vec4 m_facet_label_line_color           {1.0f, 0.5f, 0.0f, 1.0f};
    float     m_facet_label_line_width           {1.0f};
    float     m_facet_label_line_length          {0.1f};
    glm::vec4 m_corner_label_text_color          {0.5f, 1.0f, 1.0f, 1.0f};
    glm::vec4 m_corner_label_line_color          {0.0f, 0.5f, 0.5f, 1.0f};
    float     m_corner_label_line_length         {0.05f};
    float     m_corner_label_line_width          {1.0f};

    erhe::graphics::Vertex_input_state                    m_empty_vertex_input;
    std::unique_ptr<erhe::scene_renderer::Texel_renderer> m_shadow_texel_renderer;
    erhe::graphics::Render_pipeline_state                 m_shadow_texel_pipeline;
};

}
