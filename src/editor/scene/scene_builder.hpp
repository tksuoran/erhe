#pragma once

#include "brushes/brush_tool.hpp"
#include "physics/collision_generator.hpp"

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
    class Device;
}
namespace erhe::imgui {
    class Imgui_windows;
    class Imgui_renderer;
}
namespace erhe::primitive {
    class Build_info;
}
namespace erhe::rendergraph {
    class Rendergraph;
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
class Depth_visualization_window;
class App_context;
class App_message_bus;
class App_rendering;
class App_scenes;
class App_settings;
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
    float                                      instance_gap  {0.5f};
    float                                      object_scale  {1.0f};
    int                                        detail        {4};
};

class Scene_builder final
{
public:
    Scene_builder(
        std::shared_ptr<Scene_root>     scene,
        tf::Executor&                   executor,
        erhe::graphics::Device&         graphics_device,
        erhe::imgui::Imgui_renderer&    imgui_renderer,
        erhe::imgui::Imgui_windows&     imgui_windows,
        erhe::rendergraph::Rendergraph& rendergraph,
        App_context&                    app_context,
        App_message_bus&                app_message_bus,
        App_rendering&                  app_rendering,
        App_settings&                   app_settings,
        Mesh_memory&                    mesh_memory,
        Post_processing&                post_processing,
        Tools&                          tools,
        Scene_views&                    scene_views
    );
    ~Scene_builder() noexcept;

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
    ////     App_settings&         app_settings,
    ////     Mesh_memory&          mesh_memory,
    ////     GEO::Mesh&&           geo_mesh
    //// ) -> std::shared_ptr<Brush>;

    auto make_brush(
        Content_library_node&                            folder,
        App_settings&                                    app_settings,
        Mesh_memory&                                     mesh_memory,
        const std::shared_ptr<erhe::geometry::Geometry>& geometry
    ) -> std::shared_ptr<Brush>;

    [[nodiscard]] auto build_info(Mesh_memory& mesh_memory) -> erhe::primitive::Build_info;

    void setup_cameras(
        erhe::graphics::Device&         graphics_device,
        erhe::imgui::Imgui_renderer&    imgui_renderer,
        erhe::imgui::Imgui_windows&     imgui_windows,
        erhe::rendergraph::Rendergraph& rendergraph,
        bool                            openxr,
        App_message_bus&                app_message_bus,
        App_rendering&                  app_rendering,
        App_settings&                   app_settings,
        Post_processing&                post_processing,
        Tools&                          tools,
        Scene_views&                    scene_views
    );
    void animate_lights     (const double time_d);
    void add_room           ();

    auto get_brushes() -> Content_library_node&;

    void make_brushes               (App_settings& app_settings, Mesh_memory& mesh_memory, tf::Executor& executor);
    void make_platonic_solid_brushes(App_settings& app_settings, Mesh_memory& mesh_memory);
    void make_sphere_brushes        (App_settings& app_settings, Mesh_memory& mesh_memory);
    void make_torus_brushes         (App_settings& app_settings, Mesh_memory& mesh_memory);
    void make_cylinder_brushes      (App_settings& app_settings, Mesh_memory& mesh_memory);
    void make_cone_brushes          (App_settings& app_settings, Mesh_memory& mesh_memory);
    void make_json_brushes          (App_settings& app_settings, Mesh_memory& mesh_memory, tf::Taskflow* tf, Json_library& library);
    void make_mesh_nodes            (const Make_mesh_config& config, std::vector<std::shared_ptr<Brush>>& brushes);
    void setup_lights               ();

    App_context& m_context;

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

    std::shared_ptr<Content_library_node> m_platonic_solids_folder;
    std::shared_ptr<Content_library_node> m_johnson_solids_folder;

    // Config
    float m_mass_scale{1.0f};
    int   m_detail    {4};

    // Output
    std::shared_ptr<Viewport_scene_view> m_primary_viewport_window;
    std::shared_ptr<Scene_root>          m_scene_root;
};

}
