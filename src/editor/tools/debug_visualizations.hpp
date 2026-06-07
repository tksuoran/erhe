#pragma once

#include "renderable.hpp"
#include "windows/property_editor.hpp"

#include "config/generated/debug_visualizations_settings.hpp"

#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_imgui/imgui_window.hpp"
#include "erhe_renderer/generated/visualization_mode.hpp"
#include "app_message.hpp"
#include "erhe_message_bus/message_bus.hpp"
#include "erhe_math/math_util.hpp"

#include <memory>

namespace erhe::graphics { class Command_buffer; }
namespace erhe::imgui    { class Imgui_windows; }
namespace erhe::scene {
    class Camera;
    class Layout;
    class Light;
    class Mesh;
    class Node;
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

// Visualization_mode is the codegen enum from
// erhe_renderer/generated/visualization_mode.hpp (off, selected, hovered,
// all) so the modes can be serialized in editor settings.
static constexpr const char* c_visualization_mode_strings[] = {
    "Off",
    "Selected",
    "Hovered",
    "All"
};

class Debug_visualizations
    : public Renderable
{
public:
    // Implements Renderable
    void render(const Render_context& context) override;

    void imgui(Scene_view& scene_view);

    // Whole-struct copy in / out of m_settings. Persistence is owned by
    // Scene_view, whose collect callback (registered with
    // Editor_settings_store) calls write_config.
    void read_config (const Debug_visualizations_settings& settings);
    void write_config(Debug_visualizations_settings& settings) const;

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

#if 0
    // TODO Fix
    void shadow_debug            (const Render_context& render_context);
#endif
    void world_axes_visualization(const Render_context& render_context);

    void mesh_visualization(const Render_context& render_context, erhe::scene::Mesh* mesh);
    void skin_visualization(const Render_context& render_context, erhe::scene::Skin& skin);

    void light_visualization(
        const Render_context&                render_context,
        std::shared_ptr<erhe::scene::Camera> selected_camera,
        const erhe::scene::Light*            light
    );

    void directional_light_visualization (const Light_visualization_context& context);
    void shadow_frustum_fit_visualization(const Render_context& context);
    void point_light_visualization       (const Light_visualization_context& context);
    void spot_light_visualization        (const Light_visualization_context& context);
    void camera_visualization            (const Render_context& render_context, const erhe::scene::Camera* camera);
    void layout_visualization            (const Render_context& render_context, const erhe::scene::Node& node, const erhe::scene::Layout& layout);
    void selection_visualization         (const Render_context& context);
    void physics_nodes_visualization     (const Render_context& context);
    void raytrace_nodes_visualization    (const Render_context& context);
    void mesh_labels                     (const Render_context& context, erhe::scene::Mesh* mesh);

    void label(
        const Render_context& context,
        const glm::mat4&      clip_from_world,
        const glm::mat4&      world_from_node,
        const glm::vec3&      position_in_world,
        uint32_t              text_color,
        std::string_view      label_text
    );

    void make_combo(const char* label, Visualization_mode& visualization);

    erhe::math::Bounding_volume_combiner m_selection_bounding_volume;

    // Persisted settings, used in place (rendering reads them, imgui()
    // edits them). Defaults come from the generated struct, which mirrors
    // debug_visualizations_settings.py. Scene_view owns persistence via
    // read_config() / write_config().
    Debug_visualizations_settings m_settings;

    // Live-only tuning knob, not persisted.
    float m_selection_node_axis_width{2.0f};

    Property_editor m_property_editor;

#if 0 // Shadow debug / Texel_renderer is currently broken
    erhe::graphics::Vertex_input_state                    m_empty_vertex_input;
    std::unique_ptr<erhe::scene_renderer::Texel_renderer> m_shadow_texel_renderer;
    erhe::graphics::Base_render_pipeline                  m_shadow_texel_pipeline;
#endif
};

}
