#pragma once

#include "tools/brushes/brush_tool.hpp"
#include "scene/collision_generator.hpp"
#include "scene/frame_controller.hpp"

#include "erhe_profile/profile.hpp"

#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

class btCollisionShape;

namespace GEO {
    class Mesh;
}
namespace erhe::graphics {
    class Buffer_transfer_queue;
    class Instance;
}
namespace erhe::imgui {
    class Imgui_windows;
}
namespace erhe::primitive {
    class Build_info;
}
namespace erhe::scene {
    class Camera;
    class Light;
    class Scene_message_bus;
}
namespace erhe::scene_renderer {
    class Shadow_renderer;
}

namespace tf {
    class Executor;
    class Taskflow;
}

namespace editor {

class Brush;
class Brush_data;
class Content_library_node;
class Debug_view_window;
class Editor_context;
class Editor_message_bus;
class Editor_rendering;
class Editor_scenes;
class Editor_settings;
class Fly_camera_tool;
class Json_library;
class Mesh_memory;
class Post_processing;
class Scene_root;
class Settings_window;
class Tools;
class Viewport_config_window;
class Viewport_scene_view;
class Scene_views;

class Make_mesh_config
{
public:
    std::shared_ptr<erhe::primitive::Material> material      {};
    int                                        instance_count{1};
    float                                      instance_gap  {0.2f};
    float                                      object_scale  {0.4f};
    int                                        detail        {4};
};

class Scene_builder
{
public:
    Scene_builder(
        std::shared_ptr<Scene_root>     scene,
        tf::Executor&                   executor,
        erhe::graphics::Instance&       graphics_instance,
        erhe::imgui::Imgui_renderer&    imgui_renderer,
        erhe::imgui::Imgui_windows&     imgui_windows,
        erhe::rendergraph::Rendergraph& rendergraph,
        Editor_context&                 editor_context,
        Editor_rendering&               editor_rendering,
        Editor_settings&                editor_settings,
        Mesh_memory&                    mesh_memory,
        Post_processing&                post_processing,
        Tools&                          tools,
        Scene_views&                    scene_views
    );

    // Public API
    [[nodiscard]] auto get_scene_root() const -> std::shared_ptr<Scene_root>;

    // Can discard return value
    auto make_camera(
        const std::string_view name,
        const glm::vec3        position,
        const glm::vec3        look_at = glm::vec3{0.0f, 0.0f, 0.0f}
    ) -> std::shared_ptr<erhe::scene::Camera>;

    void add_platonic_solids(const Make_mesh_config& config);
    void add_johnson_solids (const Make_mesh_config& config);
    void add_curved_shapes  (const Make_mesh_config& config);
    void add_torus_chain    (const Make_mesh_config& config, bool connected);
    void add_cubes          (glm::ivec3 shape, float scale, float gap);

private:
    auto make_directional_light(
        const std::string_view name,
        const glm::vec3        position,
        const glm::vec3        color,
        const float            intensity
    ) -> std::shared_ptr<erhe::scene::Light>;

    auto make_spot_light(
        const std::string_view name,
        const glm::vec3        position,
        const glm::vec3        direction,
        const glm::vec3        color,
        const float            intensity,
        const glm::vec2        spot_cone_angle
    ) -> std::shared_ptr<erhe::scene::Light>;

    auto make_brush(Content_library_node& folder, Brush_data&& brush_create_info) -> std::shared_ptr<Brush>;

    //// auto make_brush(
    ////     Content_library_node& folder,
    ////     Editor_settings&      editor_settings,
    ////     Mesh_memory&          mesh_memory,
    ////     GEO::Mesh&&           geo_mesh
    //// ) -> std::shared_ptr<Brush>;

    auto make_brush(
        Content_library_node&                            folder,
        Editor_settings&                                 editor_settings,
        Mesh_memory&                                     mesh_memory,
        const std::shared_ptr<erhe::geometry::Geometry>& geometry
    ) -> std::shared_ptr<Brush>;

    [[nodiscard]] auto build_info(Mesh_memory& mesh_memory) -> erhe::primitive::Build_info;

    void setup_cameras(
        erhe::graphics::Instance&       graphics_instance,
        erhe::imgui::Imgui_renderer&    imgui_renderer,
        erhe::imgui::Imgui_windows&     imgui_windows,
        erhe::rendergraph::Rendergraph& rendergraph,
        Editor_rendering&               editor_rendering,
        Editor_settings&                editor_settings,
        Post_processing&                post_processing,
        Tools&                          tools,
        Scene_views&                    scene_views
    );
    void animate_lights     (const double time_d);
    void add_room           ();

    auto get_brushes() -> Content_library_node&;

    void make_brushes               (Editor_settings& editor_settings, Mesh_memory& mesh_memory, tf::Executor& executor);
    void make_platonic_solid_brushes(Editor_settings& editor_settings, Mesh_memory& mesh_memory);
    void make_sphere_brushes        (Editor_settings& editor_settings, Mesh_memory& mesh_memory);
    void make_torus_brushes         (Editor_settings& editor_settings, Mesh_memory& mesh_memory);
    void make_cylinder_brushes      (Editor_settings& editor_settings, Mesh_memory& mesh_memory);
    void make_cone_brushes          (Editor_settings& editor_settings, Mesh_memory& mesh_memory);
    void make_json_brushes          (Editor_settings& editor_settings, Mesh_memory& mesh_memory, tf::Taskflow* tf, Json_library& library);
    void make_mesh_nodes            (const Make_mesh_config& config, std::vector<std::shared_ptr<Brush>>& brushes);
    void setup_lights               ();

    Editor_context& m_context;

    class Config
    {
    public:
        Config();
        float camera_exposure            {1.0f};
        float shadow_range               {22.0f};
        float directional_light_intensity{20.0f};
        float directional_light_radius   {6.0f};
        float directional_light_height   {10.0f};
        int   directional_light_count    {4};
        float spot_light_intensity       {150.0f};
        float spot_light_radius          {20.0f};
        float spot_light_height          {10.0f};
        int   spot_light_count           {3};
        float floor_size                 {40.0f};
        float mass_scale                 {1.0f};
        int   detail                     {1};
        bool  floor                      {true};
    };
    Config m_config;

    // Self owned parts
    ERHE_PROFILE_MUTEX(std::mutex,      m_brush_mutex);
    std::unique_ptr<Brush>              m_floor_brush;
    std::unique_ptr<Brush>              m_table_brush;
    std::vector<std::shared_ptr<Brush>> m_platonic_solids;
    std::vector<std::shared_ptr<Brush>> m_johnson_solids;
    std::shared_ptr<Brush>              m_sphere_brush;
    std::shared_ptr<Brush>              m_torus_brush;
    std::shared_ptr<Brush>              m_cylinder_brush[2];
    std::shared_ptr<Brush>              m_cone_brush;

    std::vector<std::shared_ptr<erhe::physics::ICollision_shape>> m_collision_shapes;

    // Output
    std::shared_ptr<Viewport_scene_view> m_primary_viewport_window;
    std::shared_ptr<Scene_root>          m_scene_root;
};

} // namespace editor
