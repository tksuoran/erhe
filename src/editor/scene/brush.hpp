#pragma once

#include "scene/collision_generator.hpp"

#include "erhe/geometry/types.hpp"
#include "erhe/primitive/enums.hpp"
#include "erhe/primitive/buffer_info.hpp"
#include "erhe/primitive/format_info.hpp"

#include "LinearMath/btVector3.h"

class btCollisionShape;
class btUniformScalingShape;

namespace erhe::geometry
{
    class Geometry;
}

namespace erhe::primitive
{
    struct Primitive_geometry;
    struct Material;
}

namespace erhe::scene
{
    class Mesh;
    class Node;
}

namespace editor
{

class Node_physics;
class Scene_manager;

class Reference_frame
{
public:
    Reference_frame() = default;

    Reference_frame(const erhe::geometry::Geometry& geometry,
                    erhe::geometry::Polygon_id      polygon_id);

    void transform_by(glm::mat4 m);

    auto transform() const -> glm::mat4;

    auto scale() const -> float;

    uint32_t                   corner_count{0};
    erhe::geometry::Polygon_id polygon_id{0};
    glm::vec3                  centroid{0.0f, 0.0f, 0.0f}; // polygon centroid
    glm::vec3                  position{1.0f, 0.0f, 0.0f}; // one of polygon corner points
    glm::vec3                  B{0.0f, 0.0f, 1.0f};
    glm::vec3                  T{1.0f, 0.0f, 0.0f};
    glm::vec3                  N{0.0f, 1.0f, 0.0f};
};

struct Brush_create_info
{
    Brush_create_info(std::shared_ptr<erhe::geometry::Geometry> geometry,
                      erhe::primitive::Format_info              primitive_format_info,
                      erhe::primitive::Buffer_info              primitive_buffer_info,
                      erhe::primitive::Normal_style             normal_style,
                      float                                     density,
                      float                                     volume,
                      std::shared_ptr<btCollisionShape>         collision_shape);

    Brush_create_info(std::shared_ptr<erhe::geometry::Geometry> geometry,
                      erhe::primitive::Format_info              primitive_format_info,
                      erhe::primitive::Buffer_info              primitive_buffer_info,
                      erhe::primitive::Normal_style             normal_style,
                      float                                     density,
                      Collision_volume_calculator               collision_volume_calculator,
                      Collision_shape_generator                 collision_shape_generator);

    ~Brush_create_info();

    std::shared_ptr<erhe::geometry::Geometry> geometry;
    erhe::primitive::Format_info              primitive_format_info;
    erhe::primitive::Buffer_info              primitive_buffer_info;
    erhe::primitive::Normal_style             normal_style{erhe::primitive::Normal_style::corner_normals};
    float                                     density{1.0f};
    float                                     volume{1.0f};
    std::shared_ptr<btCollisionShape>         collision_shape;
    Collision_volume_calculator               collision_volume_calculator;
    Collision_shape_generator                 collision_shape_generator;
};

struct Instance
{
    std::shared_ptr<erhe::scene::Node> node;
    std::shared_ptr<erhe::scene::Mesh> mesh;
    std::shared_ptr<Node_physics>      node_physics;
};

class Brush
{
public:
    using Create_info = Brush_create_info;

    Brush() = default;
    explicit Brush(Create_info& create_info);
    ~Brush();

    Brush(const Brush&) = delete;
    auto operator=(const Brush&) -> Brush& = delete;
    Brush(Brush&& other) noexcept;
    auto operator=(Brush&& other) noexcept -> Brush&;

    auto get_reference_frame(uint32_t corner_count) -> Reference_frame;

    static constexpr const float c_scale_factor = 65536.0f;

    struct Scaled
    {
        int                                                  scale_key;
        std::shared_ptr<erhe::primitive::Primitive_geometry> primitive_geometry;
        std::shared_ptr<btCollisionShape>                    collision_shape;
        float                                                volume;
        btVector3                                            local_inertia;
    };

    auto get_scaled(float scale, Scene_manager& scene_manager)
    -> const Scaled&;

    auto create_scaled(int scale_key, Scene_manager& scene_manager)
    -> Scaled;

    auto make_instance(Scene_manager&                                    scene_manager,
                       const std::shared_ptr<erhe::scene::Node>&         parent,
                       glm::mat4                                         local_to_parent,
                       const std::shared_ptr<erhe::primitive::Material>& material,
                       float                                             scale)
    -> Instance;

    std::shared_ptr<erhe::geometry::Geometry>            geometry;
    std::shared_ptr<erhe::primitive::Primitive_geometry> primitive_geometry;
    erhe::primitive::Normal_style                        normal_style{erhe::primitive::Normal_style::corner_normals};
    std::shared_ptr<btCollisionShape>                    collision_shape;
    Collision_volume_calculator                          collision_volume_calculator;
    Collision_shape_generator                            collision_shape_generator;
    float                                                volume{0.0f};
    float                                                density{1.0f};
    std::vector<Reference_frame>                         reference_frames;
    std::vector<Scaled>                                  scaled_entries;
};

}
