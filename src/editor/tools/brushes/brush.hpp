#pragma once

#include "scene/collision_generator.hpp"
#include "scene/scene_root.hpp"
#include "tools/brushes/reference_frame.hpp"

#include "erhe_physics/irigid_body.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_primitive/primitive.hpp"
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
    class Renderable_mesh;
    class Mesh_primitive;
}
namespace erhe::scene {
    class Mesh;
    class Node;
    class Scene;
}

namespace editor {

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
    double                                     scale          {1.0f};
    bool                                       physics_enabled{true};
    erhe::physics::Motion_mode                 motion_mode    {erhe::physics::Motion_mode::e_dynamic};
};

class Brush : public erhe::Item<erhe::Item_base, erhe::Item_base, Brush, erhe::Item_kind::not_clonable>
{
public:
    static constexpr float c_scale_factor = 65536.0;

    class Scaled
    {
    public:
        int                                              scale_key;
        erhe::primitive::Primitive                       primitive;
        std::shared_ptr<erhe::physics::ICollision_shape> collision_shape;
        float                                            volume;
        glm::mat4                                        local_inertia;
    };

    explicit Brush(const Brush_data& create_info);
    Brush           (const Brush&) = delete;
    Brush& operator=(const Brush&) = delete;
    Brush           (Brush&& other) noexcept;
    Brush& operator=(Brush&&) = delete;

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Brush"};
    [[nodiscard]] static auto get_static_type() -> uint64_t;
    auto get_type     () const -> uint64_t         override;
    auto get_type_name() const -> std::string_view override;

    auto clone() const -> std::shared_ptr<Item_base> override
    {
        return std::shared_ptr<Item_base>{}; // No clone() for brush
    }

    // Public API
    void late_initialize();

    [[nodiscard]] auto get_reference_frame       (GEO::index_t corner_count, GEO::index_t face_offset, GEO::index_t corner_offset) -> Reference_frame;
    [[nodiscard]] auto get_scaled                (double scale) -> const Scaled&;
    [[nodiscard]] auto create_scaled             (int scale_key) -> Scaled;
    [[nodiscard]] auto make_instance             (const Instance_create_info& instance_create_info) -> std::shared_ptr<erhe::scene::Node>;
    [[nodiscard]] auto get_bounding_box          () -> erhe::math::Bounding_box;
    [[nodiscard]] auto get_geometry              () -> std::shared_ptr<erhe::geometry::Geometry>;
    [[nodiscard]] auto get_corner_count_to_facets() -> const std::map<GEO::index_t, std::vector<GEO::index_t>>&;
    [[nodiscard]] auto get_max_corner_count      () const -> GEO::index_t;

private:
    void update_facet_statistics();

    Brush_data                   m_data;
    erhe::primitive::Primitive   m_primitive;
    std::vector<Reference_frame> m_reference_frames;
    std::vector<Scaled>          m_scaled_entries;
    std::map<GEO::index_t, std::vector<GEO::index_t>> m_corner_count_to_facets;
    GEO::index_t                 m_max_corner_count{0};
};

}
