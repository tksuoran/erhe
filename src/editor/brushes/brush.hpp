#pragma once

#include "brushes/brush_geometry_slot.hpp"
#include "brushes/reference_frame.hpp"
#include "physics/collision_generator.hpp"
#include "scene/scene_root.hpp"

#include "erhe_item/item.hpp"
#include "erhe_item/typed.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/primitive_builder.hpp"
#include "erhe_primitive/build_info.hpp"

namespace erhe {
    class Hierarchy;
}
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
    class Xformable; using Node = Xformable;
    class Scene;
}

namespace editor {

class Brush_geometry_queue_state;

class Brush_data
{
public:
    [[nodiscard]] auto get_name() const -> const std::string&;

    App_context&                                     context;
    App_settings&                                    app_settings;
    std::string                                      name                       {};
    erhe::primitive::Build_info                      build_info;
    erhe::primitive::Normal_style                    normal_style               {erhe::primitive::Normal_style::corner_normals};
    std::shared_ptr<erhe::geometry::Geometry>        geometry                   {};
    Geometry_generator                               geometry_generator         {};
    // The preparation queue a tier 2 request goes to
    // (doc/plans/deferred_brush_geometry.md D4, D8). Weak: the queue is owned
    // by Scene_builder and a brush may outlive it, in which case tier 1
    // preparation on the calling thread is all that is left.
    std::weak_ptr<Brush_geometry_queue_state>        geometry_queue             {};
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
    bool                                       mesh_shadow_cast{true};  // local value of Mesh::shadow_cast_property on the mesh
    bool                                       mesh_lightmapped{false}; // likewise lightmapped_property; false leaves it inherited
    Scene_root*                                scene_root     {nullptr};
    glm::mat4                                  world_from_node{1.0f};
    std::shared_ptr<erhe::primitive::Material> material;
    double                                     scale          {1.0f};
    bool                                       physics_enabled{true};
    erhe::physics::Motion_mode                 motion_mode    {erhe::physics::Motion_mode::e_dynamic};
    std::optional<float>                       mass_override  {};   // rigid body mass; inertia is rescaled to match
};

class Brush : public erhe::Item<erhe::Item_base, erhe::Typed, Brush, erhe::Item_kind::not_clonable>
{
public:
    static constexpr float c_scale_factor = 65536.0;

    class Scaled
    {
    public:
        int                                              scale_key;
        std::shared_ptr<erhe::primitive::Primitive>      primitive;
        std::shared_ptr<erhe::physics::ICollision_shape> collision_shape;
        float                                            volume;
        glm::mat4                                        local_inertia;
    };

    explicit Brush(const Brush_data& create_info);
    Brush           (const Brush&) = delete;
    Brush& operator=(const Brush&) = delete;
    // A brush owns the mutex and the condition variable of its geometry slot,
    // so it does not move.
    Brush           (Brush&&) = delete;
    Brush& operator=(Brush&&) = delete;

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Brush"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Typed::get_static_type() | erhe::Item_type::brush; }

    // Overrides erhe::Typed: the class fixes the token. USD has no prim type
    // for this kind, so the token is the erhe class name, written as a custom
    // typeName (doc/erhe/usd_compatibility.md).
    [[nodiscard]] auto get_class_type_name() const -> std::string_view override { return "Brush"; }

    auto clone() const -> std::shared_ptr<Item_base> override
    {
        return std::shared_ptr<Item_base>{}; // No clone() for brush
    }

    // Public API
    void late_initialize();

    [[nodiscard]] auto get_reference_frame       (GEO::index_t corner_count, GEO::index_t face_offset, GEO::index_t corner_offset) -> Reference_frame;
    // Null when the brush has no geometry (Brush_geometry_state::failed): the
    // caller refuses the placement instead of dereferencing it.
    [[nodiscard]] auto get_scaled                (double scale) -> const Scaled*;
    [[nodiscard]] auto create_scaled             (int scale_key) -> Scaled;
    [[nodiscard]] auto make_instance             (const Instance_create_info& instance_create_info) -> std::shared_ptr<erhe::scene::Node>;
    [[nodiscard]] auto get_bounding_box          () -> erhe::math::Aabb;
    // Tier 1 (doc/plans/deferred_brush_geometry.md R3): prepares or waits as
    // needed and returns the ready geometry, or null when preparation failed.
    [[nodiscard]] auto get_geometry              () -> std::shared_ptr<erhe::geometry::Geometry>;
    // Tier 2: asks for preparation and returns at once, after putting the
    // brush at the front of the preparation queue.
    auto               request_geometry          () -> Brush_geometry_request_outcome;
    // The preparation queue's worker entry point: prepares the geometry only
    // while the brush is still `queued`. `name` is the copy the requesting
    // thread made of the brush's name.
    auto               prepare_geometry_if_queued(std::string_view name) -> Brush_geometry_worker_outcome;
    [[nodiscard]] auto get_geometry_state        () const -> Brush_geometry_state;
    // The geometry only while the brush is `ready`; never prepares, never
    // waits. A tier 2 consumer that wants to report the geometry's statistics
    // reads it through this, so that reporting never becomes a tier 1 wait.
    [[nodiscard]] auto get_geometry_if_ready     () const -> std::shared_ptr<erhe::geometry::Geometry>;
    [[nodiscard]] auto get_corner_count_to_facets() -> const std::map<GEO::index_t, std::vector<GEO::index_t>>&;
    [[nodiscard]] auto get_max_corner_count      () -> GEO::index_t;
    // The material a placed instance gets (member-backed object property,
    // doc/erhe/property_system.md D18 / D28; a brush keeps a material, no clear).
    static const erhe::property::Property<erhe::property::Object_reference> material_property;
    [[nodiscard]] auto get_material              () const -> const std::shared_ptr<erhe::primitive::Material>&;
    [[nodiscard]] auto get_density               () const -> float;
    [[nodiscard]] auto get_normal_style          () const -> erhe::primitive::Normal_style;
    void               set_material              (const std::shared_ptr<erhe::primitive::Material>& material);
    [[nodiscard]] auto make_with_material        (const std::shared_ptr<erhe::primitive::Material>& material) const -> std::shared_ptr<Brush>;
    [[nodiscard]] auto make_shared_payload_copy  () const -> std::shared_ptr<Brush>;

private:
    // Called by the geometry slot under the brush mutex, right after the
    // geometry is stored and before the state becomes ready (D3), so a thread
    // that has observed `ready` also sees the statistics.
    void update_facet_statistics(const erhe::geometry::Geometry& geometry);

    // The create info as given, minus the geometry and the generator, which
    // the slot owns: the slot is the single source of truth for both.
    Brush_data                                         m_data;
    Brush_geometry_slot                                m_geometry_slot;
    std::shared_ptr<erhe::primitive::Material>         m_material;
    std::shared_ptr<erhe::primitive::Primitive>        m_primitive;
    std::vector<Reference_frame>                      m_reference_frames;
    std::vector<Scaled>                               m_scaled_entries;
    std::map<GEO::index_t, std::vector<GEO::index_t>> m_corner_count_to_facets;
    GEO::index_t                                      m_max_corner_count{0};
};

// Place a brush in a scene with undo support. Usable from both
// interactive UI (Brush_tool) and programmatic paths (MCP, Scene_builder).
// The instance node is inserted under `parent`, any prim (the scene root node
// when null), at world transform `world_from_node`.
auto place_brush_in_scene(
    App_context&                                      context,
    Brush&                                            brush,
    Scene_root&                                       scene_root,
    const glm::mat4&                                  world_from_node,
    const std::shared_ptr<erhe::primitive::Material>& material,
    double                                            scale           = 1.0,
    erhe::physics::Motion_mode                        motion_mode     = erhe::physics::Motion_mode::e_dynamic,
    std::shared_ptr<erhe::Hierarchy>                  parent          = {},
    std::size_t                                       index_in_parent = 0,
    std::optional<float>                              mass_override   = {}
) -> std::shared_ptr<erhe::scene::Node>;

}
