#pragma once

#include "scene/collision_generator.hpp"
#include "scene/scene_root.hpp"

#include "erhe_geometry/types.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_primitive/primitive_builder.hpp"
#include "erhe_primitive/build_info.hpp"

namespace erhe::geometry {
    class Geometry;
}
namespace erhe::physics {
    class ICollision_shape;
    class IWorld;
}
namespace erhe::primitive {
    class Material;
    class Geometry_mesh;
    class Geometry_primitive;
}
namespace erhe::scene {
    class Mesh;
    class Mesh_layer;
    class Node;
    class Scene;
}

namespace editor
{

class Editor_context;
class Editor_settings;
class Node_physics;
class Node_raytrace;
class Raytrace_primitive;

class Reference_frame
{
public:
    Reference_frame();

    Reference_frame(
        const erhe::geometry::Geometry& geometry,
        erhe::geometry::Polygon_id      polygon_id,
        uint32_t                        face_offset,
        uint32_t                        corner_offset
    );

    void transform_by(const glm::mat4& m);

    [[nodiscard]] auto transform() const -> glm::mat4;
    [[nodiscard]] auto scale    () const -> float;

    uint32_t                   corner_count {0};
    uint32_t                   face_offset  {0};
    uint32_t                   corner_offset{0};
    erhe::geometry::Polygon_id polygon_id   {0};
    glm::vec3                  centroid     {0.0f, 0.0f, 0.0f}; // polygon centroid
    glm::vec3                  position     {1.0f, 0.0f, 0.0f}; // one of polygon corner points
    glm::vec3                  B            {0.0f, 0.0f, 1.0f};
    glm::vec3                  T            {1.0f, 0.0f, 0.0f};
    glm::vec3                  N            {0.0f, 1.0f, 0.0f};
};

using Geometry_generator = std::function<std::shared_ptr<erhe::geometry::Geometry>()>;

class Brush_data
{
public:
    [[nodiscard]] auto get_name() const -> const std::string&;

    Editor_context&                                  context;
    Editor_settings&                                 editor_settings;
    std::string                                      name                       {};
    erhe::primitive::Build_info                      build_info;
    erhe::primitive::Normal_style                    normal_style               {erhe::primitive::Normal_style::corner_normals};
    std::shared_ptr<erhe::geometry::Geometry>        geometry                   {};
    Geometry_generator                               geometry_generator         {};
    float                                            density                    {1.0f};
    float                                            volume                     {1.0f};
    Collision_volume_calculator                      collision_volume_calculator{};
    std::shared_ptr<erhe::physics::ICollision_shape> collision_shape            {};
    Collision_shape_generator                        collision_shape_generator  {};
};

class Instance_create_info final
{
public:
    uint64_t                                   node_flags     {0};
    uint64_t                                   mesh_flags     {0};
    Scene_root*                                scene_root     {nullptr};
    glm::mat4                                  world_from_node{1.0f};
    std::shared_ptr<erhe::primitive::Material> material;
    float                                      scale          {1.0f};
    bool                                       physics_enabled{true};
    erhe::physics::Motion_mode                 motion_mode    {erhe::physics::Motion_mode::e_dynamic};
};

class Brush
    : public erhe::Item
{
public:
    static constexpr float c_scale_factor = 65536.0f;

    class Scaled
    {
    public:
        int                                                  scale_key;
        std::shared_ptr<erhe::geometry::Geometry>            geometry;
        std::shared_ptr<erhe::primitive::Geometry_primitive> geometry_primitive;
        std::shared_ptr<erhe::physics::ICollision_shape>     collision_shape;
        float                                                volume;
        glm::mat4                                            local_inertia;
    };

    explicit Brush(const Brush_data& create_info);
    Brush         (const Brush&) = delete;
    void operator=(const Brush&) = delete;
    Brush         (Brush&& other) noexcept;
    void operator=(Brush&&)      = delete;

    // Implements Item
    static constexpr std::string_view static_type_name{"Brush"};
    [[nodiscard]] static auto get_static_type() -> uint64_t;
    auto get_type     () const -> uint64_t         override;
    auto get_type_name() const -> std::string_view override;

    // Public API
    void late_initialize();

    [[nodiscard]] auto get_reference_frame(
        uint32_t corner_count,
        uint32_t face_offset,
        uint32_t corner_offset
    ) -> Reference_frame;

    [[nodiscard]] auto get_reference_frame(
        uint32_t face_offset,
        uint32_t corner_offset
    ) -> Reference_frame;

    [[nodiscard]] auto get_scaled      (float scale) -> const Scaled&;
    [[nodiscard]] auto create_scaled   (int scale_key) -> Scaled;
    [[nodiscard]] auto make_instance   (const Instance_create_info& instance_create_info) -> std::shared_ptr<erhe::scene::Node>;
    [[nodiscard]] auto get_bounding_box() -> erhe::math::Bounding_box;
    [[nodiscard]] auto get_geometry    () -> std::shared_ptr<erhe::geometry::Geometry>;

    Brush_data                                           data;
    std::shared_ptr<erhe::primitive::Geometry_primitive> geometry_primitive;
    std::vector<Reference_frame>                         reference_frames;
    std::vector<Scaled>                                  scaled_entries;
};

}
