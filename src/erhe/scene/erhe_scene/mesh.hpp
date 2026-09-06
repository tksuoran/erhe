#pragma once

#include "erhe_item/item.hpp"
#include "erhe_scene/gprim.hpp"
#include "erhe_primitive/primitive.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <memory>
#include <vector>

namespace erhe::raytrace {
    class IGeometry;
    class IInstance;
    class IScene;
    class Hit;
    class Ray;
}

namespace erhe::scene {

using Layer_id = uint64_t;

class Raytrace_primitive;
class Scene_host;
class Skin;

class Mesh;

// One primitive of a Mesh with its material. A property sub-object of its
// mesh (doc/property-system.md D29): a Dependency_object with its own owner
// type, whose `material` is a member-backed object property
// (Mesh_primitive::material_property), so the generic rows, undo and MCP
// reach it as (mesh, primitive index); Mesh::set_primitive_material stays
// the one writer. It registers member-backed properties only and nothing
// observes a primitive: the mesh holds primitives by value in a vector,
// and a reallocation copy-constructs the Dependency_object base, which
// keeps neither observers nor expression dependents.
class Mesh_primitive : public erhe::property::Dependency_object
{
public:
    Mesh_primitive();
    Mesh_primitive(
        const std::shared_ptr<erhe::primitive::Primitive>& primitive,
        const std::shared_ptr<erhe::primitive::Material>&  material                 = {},
        const glm::vec4&                                   lightmap_uv_scale_offset = glm::vec4{0.0f}
    );
    // Copies the three fields; the owner link is not copied (Mesh stamps it).
    Mesh_primitive(const Mesh_primitive& other);
    Mesh_primitive& operator=(const Mesh_primitive& other);
    ~Mesh_primitive() noexcept override;

    [[nodiscard]] static auto property_owner_type() -> erhe::property::Owner_type;
    [[nodiscard]] auto get_property_owner_type() const -> erhe::property::Owner_type override { return property_owner_type(); }

    // Object reference to an erhe::primitive::Material, member-backed over
    // `material`; a write notifies the owning mesh's scene host.
    static const erhe::property::Property<erhe::property::Object_reference> material_property;

    // The mesh whose primitive list holds this object, and the index there;
    // null / 0 for a primitive value outside a mesh.
    [[nodiscard]] auto get_owner() const -> Mesh*       { return m_owner; }
    [[nodiscard]] auto get_index() const -> std::size_t { return m_index; }

    std::shared_ptr<erhe::primitive::Primitive> primitive;
    std::shared_ptr<erhe::primitive::Material>  material;

    // Baked-lightmap atlas region of this primitive: lightmap UV =
    // channel-2 UV * xy + zw. All-zero (the default) means "no baked
    // lightmap"; the fragment shader gates on xy > 0. Written by the
    // lightmap baker (doc/lightmap_baking_plan.md), uploaded per draw by
    // Primitive_buffer.
    glm::vec4                                   lightmap_uv_scale_offset{0.0f};

private:
    friend class Mesh;
    static void on_material_set(Mesh_primitive& primitive); // material_property after_set

    Mesh*       m_owner{nullptr};
    std::size_t m_index{0};
};

// A geometric prim (doc/usd-compatibility-plan.md C5, USD `Mesh`): an
// `Xformable` with its own transform, name and children, and a child prim of
// its parent. A parent holds any number of `Mesh` children.
class Mesh : public erhe::Item<Item_base, Gprim, Mesh, erhe::Item_kind::clone_using_custom_clone_constructor>
{
public:
    Mesh(); // default
    ~Mesh() noexcept override;

    explicit Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    explicit Mesh(std::string_view name);
    Mesh(
        std::string_view                                   name,
        const std::shared_ptr<erhe::primitive::Primitive>& primitive
    );
    Mesh(const Mesh&, erhe::for_clone);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Mesh"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return Gprim::get_static_type() | erhe::Item_type::mesh; }
    void handle_flag_bits_update(uint64_t old_flag_bits, uint64_t new_flag_bits) override;

    // Overrides Typed: the class fixes the token.
    [[nodiscard]] auto get_class_type_name() const -> std::string_view override { return "Mesh"; }

    // Overrides Xformable: registers / unregisters the mesh with the scene's
    // mesh layers on top of the node registration the base does, and mirrors
    // the world transform into the raytrace instances, the negative
    // determinant flag and the computed world bounds.
    void handle_item_host_update(erhe::Item_host* old_item_host, erhe::Item_host* new_item_host) override;
    void handle_transform_update(uint64_t serial)                                                override;

    // The mesh itself: the transitional accessor every consumer that reads
    // "the node of this mesh" still spells, kept while the U steps of
    // doc/usd-compatibility-plan.md retire it.
    [[nodiscard]] auto get_node()       -> Node*       { return this; }
    [[nodiscard]] auto get_node() const -> const Node* { return this; }

    // Implements Item_base (D29): the primitives are the sub-objects.
    [[nodiscard]] auto get_property_sub_object_count() const -> std::size_t override;
    [[nodiscard]] auto get_property_sub_object      (std::size_t index) -> erhe::property::Dependency_object* override;
    [[nodiscard]] auto get_property_sub_object      (std::size_t index) const -> const erhe::property::Dependency_object* override;
    [[nodiscard]] auto get_property_sub_object_label(std::size_t index) const -> std::string override;

    // Public API
    void clear_primitives    ();
    void update_rt_primitives();
    // Drops one primitive's optimized mesh variant and re-registers every mesh
    // sharing that Primitive. Call before the FIRST GPU write of a live edit.
    //
    // A live edit - a vertex drag, paint, weight paint - writes the ORIGINAL
    // buffer mesh, because Element_mappings describe only that build. The
    // optimized build is welded, so a per-corner edit is not even expressible
    // in it: two corners the weld merged share one output slot. A mesh still
    // drawing the optimized variant would show nothing change until the edit
    // commits and rebuilds the primitive. Dropping the variant is what makes
    // the edit visible; writing through it is not an option.
    //
    // Re-registration is not optional, and not limited to this mesh. Draw list
    // records BAKE the drawn variant's base_vertex and index ranges, and a
    // Primitive is shared - glTF instances and brush instances hold the same
    // one. A sharer left unregistered keeps drawing from ranges this drop
    // retires and the pool later hands to another mesh.
    //
    // Idempotent, and free when there is no variant to drop - which is every
    // call while mesh optimization is off.
    void invalidate_optimized_primitive_variant(std::size_t primitive_index);
    // Live-edit bracket around one primitive's optimized variant
    // (doc/meshoptimizer-integration.md, requirement 11). Called when the edit
    // STARTS (drag begin, stroke begin - before its first GPU write): takes an
    // optimization hold on the Primitive and drops the live variant exactly
    // like invalidate_optimized_primitive_variant(). While the hold is active
    // Primitive::publish_optimized_render_shape() refuses, so a variant built
    // from pre-edit data (a deferred finalize landing mid-stroke) can never
    // appear beside the in-progress edit.
    //
    // Returns the held Primitive (null if nothing was held). The caller keeps
    // it for the edit's duration and, when the edit ENDS, calls
    // release_optimization_hold() ON THAT OBJECT - never through a
    // (mesh, index) lookup, because an operation executing mid-edit (undo
    // during a stroke) can swap the mesh's slot to a different Primitive.
    [[nodiscard]] auto begin_optimized_variant_edit(std::size_t primitive_index) -> std::shared_ptr<erhe::primitive::Primitive>;
    void add_primitive       (const std::shared_ptr<erhe::primitive::Primitive>& primitive, const std::shared_ptr<erhe::primitive::Material>& material = {});
    void set_primitives      (const std::vector<Mesh_primitive>& primitives);
    // Reassign the material of one primitive: a write of the primitive's
    // material_property, whose after_set notifies the scene host
    // (Scene_host::on_mesh_material_changed). get_primitives() is const, so
    // every writer - this, the generic rows, undo, MCP - goes through the
    // property and the draw lists always see the change.
    void set_primitive_material(std::size_t primitive_index, const std::shared_ptr<erhe::primitive::Material>& material);
    // Set the baked-lightmap atlas region of one primitive. Likewise the only
    // way to change it, so the scene host (draw list primitive records) always
    // sees the change (Scene_host::on_mesh_primitive_data_changed).
    void set_primitive_lightmap_uv_scale_offset(std::size_t primitive_index, const glm::vec4& lightmap_uv_scale_offset);
    void set_rt_mask         (uint32_t rt_mask);
    void attach_rt_to_scene  (erhe::raytrace::IScene* rt_scene);
    void detach_rt_from_scene();
    [[nodiscard]] auto get_primitives        () const -> const std::vector<Mesh_primitive>&;
    [[nodiscard]] auto get_rt_scene          () const -> erhe::raytrace::IScene*;
    [[nodiscard]] auto get_rt_primitives     () const -> const std::vector<std::unique_ptr<Raytrace_primitive>>&;
    // World-space bounds. For a skinned mesh these are the POSED bounds, derived
    // from the joint transforms and the primitives' per-joint rest boxes; the
    // mesh node's own transform is not applied, because skinning ignores it.
    [[nodiscard]] auto get_aabb_world        () const -> erhe::math::Aabb;
    // Computed (doc/property-system.md D26): the corners of
    // get_aabb_world(), 0 0 0 for an invalid box; pushed to expressions from
    // handle_transform_update and the primitive changes.
    static const erhe::property::Property<glm::vec3> world_bounds_min_property;
    static const erhe::property::Property<glm::vec3> world_bounds_max_property;
    // Inherited flags (doc/property-system.md D23): the closest ancestor
    // with a local value wins (a node above or a style holds
    // Mesh.shadow_cast for the meshes below it, D30); the effective value
    // is mirrored into Item_flags::shadow_cast / lightmapped so the
    // renderers stay bit tests.
    static const erhe::property::Property<bool> shadow_cast_property;
    static const erhe::property::Property<bool> lightmapped_property;
    // Posed bounds from the skin alone. Returns an invalid Aabb when the mesh is
    // not skinned, or when the primitives carry no per-joint rest bounds.
    [[nodiscard]] auto get_skinned_aabb_world() const -> erhe::math::Aabb;
    // Posed world bounds of a single primitive of this mesh: the union of the
    // primitive's per-joint rest boxes transformed by the joints' current
    // world-from-bind matrices - the boxes GPU skinning is bounded by. Returns
    // an invalid Aabb when the mesh is not skinned or the primitive carries no
    // joint bounds. Shared by everything that needs posed skinned bounds
    // (shadow caster culling via get_aabb_world, debug visualizations).
    [[nodiscard]] auto get_skinned_primitive_aabb_world(const erhe::primitive::Primitive& primitive) const -> erhe::math::Aabb;

    Layer_id              layer_id{0xff};
    std::shared_ptr<Skin> skin; // TODO Make this a separate node attachment
    float                 point_size{3.0f};
    float                 line_width{1.0f};

private:
    static void on_render_flag_property_changed(erhe::property::Dependency_object& object, const erhe::property::Property_changed_args& args);
    void        rederive_render_flag_bits();
    // The state that mirrors this mesh's world transform: the raytrace
    // instance transforms, the negative-determinant flag, the computed world
    // bounds and the scene host's draw list records. Run from
    // handle_transform_update() and after a primitive-list change.
    void        update_transform_dependent_state();

private:
    friend class Mesh_primitive;

    // Scene_host of the node this mesh is attached to, or nullptr.
    [[nodiscard]] auto get_scene_host() const -> Scene_host*;
    void notify_primitives_changed();
    // Mesh_primitive::material_property after_set: the scene host sees the
    // reassignment (the body set_primitive_material ran inline before D29).
    void notify_primitive_material_changed();
    // Sets every primitive's owner link to (this, index); after any change
    // of m_primitives (element writes included: a reallocation copies).
    void stamp_primitive_owners();

    std::vector<Mesh_primitive>                      m_primitives;
    erhe::raytrace::IScene*                          m_rt_scene{nullptr};
    std::vector<std::unique_ptr<Raytrace_primitive>> m_rt_primitives;
    bool                                             m_rt_primitives_dirty{false};
};

[[nodiscard]] auto operator<(const Mesh& lhs, const Mesh& rhs) -> bool;

// The one mesh of a prim: the prim itself when it is a Mesh, else its first
// Mesh child. The convenience for the one-mesh case; a consumer that needs
// every mesh of a prim uses for_each_mesh_child().
[[nodiscard]] auto get_mesh(const std::shared_ptr<erhe::Item_base>& item) -> std::shared_ptr<Mesh>;
[[nodiscard]] auto get_mesh(const erhe::Hierarchy* item) -> std::shared_ptr<Mesh>;

// Make `mesh` a child prim of `parent` (a null parent detaches it), keeping
// the mesh's LOCAL transform. Xformable::set_parent preserves the WORLD
// transform instead, which would give a mesh created at the origin a local
// transform that cancels its new parent's; a mesh that carries no transform
// of its own belongs at its parent's place.
void set_mesh_parent(const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<erhe::Hierarchy>& parent);

// Every Mesh child of a prim, in child order. Takes a callback and allocates
// nothing, so it is usable from per-frame code.
void for_each_mesh_child(const erhe::Hierarchy& item, const std::function<void(const std::shared_ptr<Mesh>&)>& callback);

} // namespace erhe::scene
