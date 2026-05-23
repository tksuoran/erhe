#pragma once

#include "brushes/brush_tool.hpp"
#include "physics/collision_generator.hpp"
#include "scene/make_mesh_config.hpp"

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
namespace erhe::physics {
    class ICollision_shape;
}
namespace erhe::primitive {
    class Build_info;
    class Material;
}
namespace erhe::rendergraph {
    class Rendergraph;
}
namespace erhe::scene {
    class Camera;
    class Light;
}
namespace erhe::scene_renderer {
    class Mesh_memory;
    class Shadow_renderer;
}

namespace tf {
    class Executor;
    class Taskflow;
}

struct Scene_config;
struct Add_cameras_args;
struct Add_lights_args;
struct Add_room_args;

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
class Post_processing;
class Scene_root;
class Settings_window;
class Tools;
class Viewport_config_window;
class Viewport_scene_view;
class Scene_views;

class Scene_builder final
{
public:
    Scene_builder(
        const Scene_config&                scene_config,
        bool                               enable_post_processing,
        std::shared_ptr<Scene_root>        scene,
        tf::Executor&                      executor,
        App_context&                       app_context,
        App_settings&                      app_settings,
        erhe::scene_renderer::Mesh_memory& mesh_memory
    );
    ~Scene_builder() noexcept;

    // Public API
    [[nodiscard]] auto get_scene_root() const -> std::shared_ptr<Scene_root>;

    void add_cameras        (const Add_cameras_args& args);
    void add_room           (const Add_room_args&    args);
    void add_lights         (const Add_lights_args&  args);
    void add_platonic_solids(const Make_mesh_config& config);
    void add_johnson_solids (const Make_mesh_config& config);
    void add_curved_shapes  (const Make_mesh_config& config);
    void add_torus_chain    (const Make_mesh_config& config, bool connected);
    void add_cubes          (glm::ivec3 shape, float scale, float gap);

    // Resource registration / unregistration hooks driven by
    // Scene_builder_floor_resources_operation so undo of add_room()
    // tears down the floor brush, collision shape and material that
    // were created eagerly. Each call mutates m_floor_brushes /
    // m_collision_shapes / the material library under m_brush_mutex
    // and the content-library's own mutex. The Operation captures
    // the resource shared_ptrs at construction; remove_floor_resources
    // is idempotent if the resources are not present (returns false).
    void register_floor_resources(
        const std::shared_ptr<Brush>&                          brush,
        const std::shared_ptr<erhe::physics::ICollision_shape>& collision_shape,
        const std::shared_ptr<erhe::primitive::Material>&      material
    );
    void unregister_floor_resources(
        const std::shared_ptr<Brush>&                          brush,
        const std::shared_ptr<erhe::physics::ICollision_shape>& collision_shape,
        const std::shared_ptr<erhe::primitive::Material>&      material
    );

    // Test hooks: read-only size accessors used by the regression
    // test asserting that undo + redo of add_room does not leak.
    [[nodiscard]] auto floor_brushes_size   () const -> std::size_t;
    [[nodiscard]] auto collision_shapes_size() const -> std::size_t;

private:
    auto make_camera(
        std::string_view name,
        glm::vec3        position,
        glm::vec3        look_at,
        float            z_near,
        float            z_far,
        float            exposure,
        float            shadow_range
    ) -> std::shared_ptr<erhe::scene::Node>;

    auto make_directional_light(
        std::string_view name,
        glm::vec3        position,
        glm::vec3        color,
        float            intensity,
        bool             cast_shadow
    ) -> std::shared_ptr<erhe::scene::Node>;

    auto make_spot_light(
        std::string_view name,
        glm::vec3        position,
        glm::vec3        direction,
        glm::vec3        color,
        float            intensity,
        glm::vec2        spot_cone_angle
    ) -> std::shared_ptr<erhe::scene::Node>;

    auto make_brush(Content_library_node& folder, Brush_data&& brush_create_info) -> std::shared_ptr<Brush>;

    //// auto make_brush(
    ////     Content_library_node&              folder,
    ////     App_settings&                      app_settings,
    ////     erhe::scene_renderer::Mesh_memory& mesh_memory,
    ////     GEO::Mesh&&                        geo_mesh
    //// ) -> std::shared_ptr<Brush>;

    auto make_brush(
        Content_library_node&                            folder,
        App_settings&                                    app_settings,
        erhe::scene_renderer::Mesh_memory&               mesh_memory,
        const std::shared_ptr<erhe::geometry::Geometry>& geometry
    ) -> std::shared_ptr<Brush>;

    [[nodiscard]] auto build_info(erhe::scene_renderer::Mesh_memory& mesh_memory) -> erhe::primitive::Build_info;

    void animate_lights     (const double time_d);

    auto get_brushes() -> Content_library_node&;

    void ensure_brushes             (float mass_scale, int detail);
    void make_brushes               (App_settings& app_settings, erhe::scene_renderer::Mesh_memory& mesh_memory, tf::Executor& executor);
    void make_platonic_solid_brushes(App_settings& app_settings, erhe::scene_renderer::Mesh_memory& mesh_memory);
    void make_sphere_brushes        (App_settings& app_settings, erhe::scene_renderer::Mesh_memory& mesh_memory);
    void make_torus_brushes         (App_settings& app_settings, erhe::scene_renderer::Mesh_memory& mesh_memory);
    void make_cylinder_brushes      (App_settings& app_settings, erhe::scene_renderer::Mesh_memory& mesh_memory);
    void make_cone_brushes          (App_settings& app_settings, erhe::scene_renderer::Mesh_memory& mesh_memory);
    void make_json_brushes          (App_settings& app_settings, erhe::scene_renderer::Mesh_memory& mesh_memory, tf::Taskflow* tf, Json_library& library);
    void make_mesh_nodes            (const Make_mesh_config& config, std::vector<std::shared_ptr<Brush>>& brushes);

    App_context&          m_context;
    const Scene_config&   m_scene_config;
    bool                  m_enable_post_processing;

    // Self owned parts
    ERHE_PROFILE_MUTEX(std::mutex,      m_brush_mutex);
    // shared_ptr so Scene_builder_floor_resources_operation can hold a
    // co-owning reference for undo-time re-registration.
    std::vector<std::shared_ptr<Brush>> m_floor_brushes;
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

    // Brushes are built eagerly in the Scene_builder constructor using
    // these defaults. ensure_brushes() guards on m_brushes_built and is
    // therefore a no-op for any later scene.add_* invocation -- the args
    // it passes for mass_scale / detail are ignored.
    float m_mass_scale    {1.0f};
    int   m_detail        {4};
    bool  m_brushes_built {false};

    // Output
    std::shared_ptr<Scene_root>          m_scene_root;
};

}
