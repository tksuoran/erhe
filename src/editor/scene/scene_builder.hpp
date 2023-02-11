#pragma once

#include "tools/brushes/brush_tool.hpp"
#include "scene/collision_generator.hpp"
#include "scene/frame_controller.hpp"

#include "erhe/components/components.hpp"

#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

class btCollisionShape;

namespace erhe::geometry
{
    class Geometry;
}

namespace erhe::graphics
{
    class Buffer_transfer_queue;
}

namespace erhe::primitive
{
    class Build_info;
}

namespace erhe::scene
{
    class Camera;
    class Light;
}

namespace editor
{

class Brush;
class Brush_data;
class Scene_root;
class Viewport_window;

class Scene_builder
    : public erhe::components::Component
{
public:
    class Config
    {
    public:
        float directional_light_intensity{20.0f};
        float directional_light_radius   {6.0f};
        float directional_light_height   {10.0f};
        int   directional_light_count    {4};
        float spot_light_intensity       {150.0f};
        float spot_light_radius          {20.0f};
        float spot_light_height          {10.0f};
        int   spot_light_count           {3};
        float floor_size                 {40.0f};
        int   instance_count             {1};
        float instance_gap               {0.4f};
        float object_scale               {1.0f};
        float mass_scale                 {1.0f};
        int   detail                     {2};
        bool  floor                      {true};
        bool  gltf_files                 {false};
        bool  obj_files                  {false};
        bool  sphere                     {false};
        bool  torus                      {false};
        bool  cylinder                   {false};
        bool  cone                       {false};
        bool  platonic_solids            {true};
        bool  johnson_solids             {false};
    };
    Config config;

    static constexpr std::string_view c_type_name{"Scene_builder"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Scene_builder ();
    ~Scene_builder() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void deinitialize_component     () override;

    // Public API
    void add_rendertarget_viewports(int count);
    [[nodiscard]] auto get_scene_root             () const -> std::shared_ptr<Scene_root>;
    [[nodiscard]] auto get_primary_viewport_window() const -> std::shared_ptr<Viewport_window>;

    // Can discard return value
    auto make_camera(
        const std::string_view name,
        const glm::vec3        position,
        const glm::vec3        look_at = glm::vec3{0.0f, 0.0f, 0.0f}
    ) -> std::shared_ptr<erhe::scene::Camera>;

    // TODO Something nicer, do not expose here
    [[nodiscard]] auto buffer_transfer_queue() -> erhe::graphics::Buffer_transfer_queue&;

private:
    void setup_scene();

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

    auto make_brush(
        Brush_data&& brush_create_info,
        const bool   instantiate_to_scene
    ) -> std::shared_ptr<Brush>;

    auto make_brush(
        erhe::geometry::Geometry&& geometry,
        const bool                 instantiate_to_scene
    ) -> std::shared_ptr<Brush>;

    auto make_brush(
        const std::shared_ptr<erhe::geometry::Geometry>& geometry,
        const bool                                       instantiate_to_scene
    ) -> std::shared_ptr<Brush>;

    [[nodiscard]] auto build_info() -> erhe::primitive::Build_info&;

    void setup_cameras      ();
    void animate_lights     (const double time_d);
    void add_room           ();
    void make_brushes       ();
    void make_mesh_nodes    ();
    void make_cube_benchmark();
    void setup_lights       ();

    // Self owned parts
    std::mutex                          m_brush_mutex;
    std::unique_ptr<Brush>              m_floor_brush;
    std::unique_ptr<Brush>              m_table_brush;
    std::mutex                          m_scene_brushes_mutex;
    std::vector<std::shared_ptr<Brush>> m_scene_brushes;

    std::vector<std::shared_ptr<erhe::physics::ICollision_shape>> m_collision_shapes;

    // Output
    std::shared_ptr<Viewport_window> m_primary_viewport_window;
    std::shared_ptr<Scene_root>      m_scene_root;
};

extern Scene_builder* g_scene_builder;

} // namespace editor
