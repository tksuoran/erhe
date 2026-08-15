#pragma once

#include "erhe_item/item.hpp"
#include "erhe_scene/node_attachment.hpp"
#include "erhe_primitive/primitive.hpp"

#include <glm/glm.hpp>

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

class Mesh_primitive
{
public:
    std::shared_ptr<erhe::primitive::Primitive> primitive;
    std::shared_ptr<erhe::primitive::Material>  material;

    // Baked-lightmap atlas region of this primitive: lightmap UV =
    // channel-2 UV * xy + zw. All-zero (the default) means "no baked
    // lightmap"; the fragment shader gates on xy > 0. Written by the
    // lightmap baker (doc/lightmap_baking_plan.md), uploaded per draw by
    // Primitive_buffer.
    glm::vec4                                   lightmap_uv_scale_offset{0.0f};
};

class Mesh;

class Mesh : public erhe::Item<Item_base, Node_attachment, Mesh, erhe::Item_kind::clone_using_custom_clone_constructor>
{
public:
    Mesh(); // default
    explicit Mesh(Mesh&&) noexcept;
    Mesh& operator=(Mesh&&) noexcept;
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
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return Item_type::node_attachment | erhe::Item_type::mesh; }
    void handle_flag_bits_update(uint64_t old_flag_bits, uint64_t new_flag_bits) override;

    // Implements Node_attachment
    void handle_item_host_update     (erhe::Item_host* old_item_host, erhe::Item_host* new_item_host) override;
    void handle_node_transform_update()                                                               override;

    // Public API
    void clear_primitives    ();
    void update_rt_primitives();
    void add_primitive       (const std::shared_ptr<erhe::primitive::Primitive>& primitive, const std::shared_ptr<erhe::primitive::Material>& material = {});
    void set_primitives      (const std::vector<Mesh_primitive>& primitives);
    // Reassign the material of one primitive. Use this instead of writing
    // through get_mutable_primitives() so the scene host (draw lists) sees
    // the change (Scene_host::on_mesh_material_changed).
    void set_primitive_material(std::size_t primitive_index, const std::shared_ptr<erhe::primitive::Material>& material);
    void set_rt_mask         (uint32_t rt_mask);
    void attach_rt_to_scene  (erhe::raytrace::IScene* rt_scene);
    void detach_rt_from_scene();
    [[nodiscard]] auto get_mutable_primitives()       ->       std::vector<Mesh_primitive>&;
    [[nodiscard]] auto get_primitives        () const -> const std::vector<Mesh_primitive>&;
    [[nodiscard]] auto get_rt_scene          () const -> erhe::raytrace::IScene*;
    [[nodiscard]] auto get_rt_primitives     () const -> const std::vector<std::unique_ptr<Raytrace_primitive>>&;
    // World-space bounds. For a skinned mesh these are the POSED bounds, derived
    // from the joint transforms and the primitives' per-joint rest boxes; the
    // mesh node's own transform is not applied, because skinning ignores it.
    [[nodiscard]] auto get_aabb_world        () const -> erhe::math::Aabb;
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
    // Scene_host of the node this mesh is attached to, or nullptr.
    [[nodiscard]] auto get_scene_host() const -> Scene_host*;
    void notify_primitives_changed();

    std::vector<Mesh_primitive>                      m_primitives;
    erhe::raytrace::IScene*                          m_rt_scene{nullptr};
    std::vector<std::unique_ptr<Raytrace_primitive>> m_rt_primitives;
    bool                                             m_rt_primitives_dirty{false};
};

[[nodiscard]] auto operator<(const Mesh& lhs, const Mesh& rhs) -> bool;

[[nodiscard]] auto get_mesh(const std::shared_ptr<erhe::Item_base>& item) -> std::shared_ptr<Mesh>;

} // namespace erhe::scene
